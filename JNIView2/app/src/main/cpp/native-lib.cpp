#include <cmath>
#include <tuple>
#include <vector>
#include <string>
#include <memory>
#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include "vulkan_wrapper.h"

#define LOG_TAG "native"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

namespace std {
#if __cplusplus <= 201103L
    // note: this implementation does not disable this overload for array types
  template<typename T, typename... Args>
  std::unique_ptr<T> make_unique(Args&&... args)
  {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
  }
#endif
}

namespace vk {

    class Buffer {
    public:
        Buffer() = delete;

        Buffer(const VkPhysicalDevice &physicalDevice,
               const VkDevice &device,
               uint32_t size,
               VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties) : m_physicalDevice(physicalDevice), m_device(device) {
            // Buffer
            VkBufferCreateInfo bufferCreateInfo = {};
            bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.size = size;
            bufferCreateInfo.usage = usage; // buffer is used as a storage buffer.
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // buffer is exclusive to a single queue family at a time.

            if (vkCreateBuffer(m_device, &bufferCreateInfo, VK_NULL_HANDLE, &m_buffer) != VK_SUCCESS) {
                throw std::runtime_error("failed to create buffers!");
            }

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(m_device, m_buffer, &memoryRequirements);

            // Memory
            VkMemoryAllocateInfo allocateInfo = {};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize = memoryRequirements.size;
            allocateInfo.memoryTypeIndex = findMemoryType(memoryRequirements.memoryTypeBits, properties);

            if (vkAllocateMemory(m_device, &allocateInfo, VK_NULL_HANDLE, &m_bufferMemory) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate buffer memory!");
            }

            // Bind
            vkBindBufferMemory(m_device, m_buffer, m_bufferMemory, 0);
        }

        ~Buffer() {
            vkFreeMemory(m_device, m_bufferMemory, VK_NULL_HANDLE);
            vkDestroyBuffer(m_device, m_buffer, VK_NULL_HANDLE);
        }

    public:
        const VkBuffer &buf() const {
            return m_buffer;
        }

        const VkDeviceMemory &mem() const {
            return m_bufferMemory;
        }

        void dump(void *out, size_t size) const {
            void *data;
            VkMemoryRequirements memoryRequirements = { };
            vkGetBufferMemoryRequirements(m_device, m_buffer, &memoryRequirements);
            //
            vkMapMemory(m_device, m_bufferMemory, 0, memoryRequirements.size, 0, reinterpret_cast< void **>(&data));
            for (size_t i = 0; i < std::min(size_t(memoryRequirements.size), size); i += 1) {
                reinterpret_cast<uint8_t *>(out)[i] = reinterpret_cast<uint8_t *>(data)[i];
            }
            //
            vkUnmapMemory(m_device, m_bufferMemory);
        }

    private:
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
            VkPhysicalDeviceMemoryProperties memProperties;
            vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

            for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
                if ((typeFilter & (1 << i)) &&
                    (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }

            throw std::runtime_error("failed to find suitable memory type!");
        }

    private:
        const VkPhysicalDevice &m_physicalDevice;
        const VkDevice &m_device;
        VkBuffer m_buffer;
        VkDeviceMemory m_bufferMemory;
    };

    class Shader {
    public:
        Shader() = delete;

        Shader(const VkDevice &device,
               const std::vector<uint8_t> &compShaderCode,
               VkShaderStageFlagBits shaderStage) : m_device(device), m_shaderStage(shaderStage) {
            // Shader module
            auto createShaderModule = [&](const std::vector<uint8_t> &code) -> VkShaderModule {
                VkShaderModuleCreateInfo createInfo = {};
                createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                createInfo.codeSize = code.size();
                createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

                VkShaderModule shaderModule;
                if (vkCreateShaderModule(m_device, &createInfo, VK_NULL_HANDLE, &shaderModule) != VK_SUCCESS) {
                    throw std::runtime_error("failed to create shader module!");
                }
                return shaderModule;
            };
            m_compShaderModule = createShaderModule(compShaderCode);
        }

        ~Shader() {
            vkDestroyShaderModule(m_device, m_compShaderModule, VK_NULL_HANDLE);
        }

    public:
        VkShaderStageFlagBits stage() const {
            return m_shaderStage;
        }

        const VkShaderModule &module() const {
            return m_compShaderModule;
        }

    private:
        const VkDevice &m_device;
        VkShaderStageFlagBits m_shaderStage;
        VkShaderModule m_compShaderModule;
    };

    class Fence {
    public:
        Fence() = delete;

        Fence(const VkDevice &device) : m_device(device) {
            VkFenceCreateInfo fenceCreateInfo = {};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceCreateInfo.flags = 0;
            vkCreateFence(m_device, &fenceCreateInfo, VK_NULL_HANDLE, &m_fence);
        }

        ~Fence() {
            vkDestroyFence(m_device, m_fence, VK_NULL_HANDLE);
        }

    public:
        const VkFence &get() const {
            return m_fence;
        }

        void wait() const {
            // wait finish
            vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, 0xFFFFFFFF);
        }

    private:
        const VkDevice &m_device;
        VkFence m_fence;
    };

    class Command {
    public:
        Command() = delete;

        Command(const VkDevice &device,
                const VkQueue &graphicsQueue,
                const VkPipelineLayout &pipelineLayout,
                const VkPipeline &computePipeline,
                const std::vector<VkDescriptorSet> &descriptorSets,
                const VkCommandPool &commandPool)
                : m_device(device), m_graphicsQueue(graphicsQueue), m_commandPool(commandPool) {
            // Create
            VkCommandBufferAllocateInfo allocateInfo = {};
            allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocateInfo.commandPool = m_commandPool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1;

            if (vkAllocateCommandBuffers(device, &allocateInfo, &m_commandBuffer) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate command buffers!");
            }

            // Record
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

            if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) {
                throw std::runtime_error("failed to begin recording command buffer!");
            }

            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0,
                                    descriptorSets.size(), descriptorSets.data(), 0, VK_NULL_HANDLE);
            vkCmdDispatch(m_commandBuffer, 1024, 1024, 1);

            if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) {
                throw std::runtime_error("failed to record command buffer!");
            }
        }

        ~Command() {
            vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_commandBuffer);
        }

    public:
        std::unique_ptr<Fence> submit() {
            auto fence = std::make_unique<Fence>(m_device);

            // submit
            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &m_commandBuffer;
            vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence->get());

            return fence;
        }

    private:
        const VkDevice &m_device;
        const VkQueue &m_graphicsQueue;
        VkCommandBuffer m_commandBuffer;
        const VkCommandPool &m_commandPool;
    };

    class ComputePipeline {
    public:
        ComputePipeline() = delete;

        ComputePipeline(const VkDevice &device,
                        uint32_t queueFamilyIndex,
                        const VkQueue &graphicsQueue,
                        const std::unique_ptr<Shader> &shader,
                        const std::vector<std::vector<std::tuple<uint32_t, VkDescriptorType>>> &setsBindings)
                : m_device(device),
                  m_queueFamilyIndex(queueFamilyIndex),
                  m_graphicsQueue(graphicsQueue) {
            initDescriptor(setsBindings);
            initPipeline(shader);
            initCommandPool();
        }

        ~ComputePipeline() {
            vkDestroyCommandPool(m_device, m_commandPool, VK_NULL_HANDLE);
            //
            vkDestroyPipeline(m_device, m_computePipeline, VK_NULL_HANDLE);
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, VK_NULL_HANDLE);
            //
            vkFreeDescriptorSets(m_device, m_descriptorPool, m_descriptorSets.size(), m_descriptorSets.data());
            for (auto &setLayout : m_descriptorSetLayouts) {
                vkDestroyDescriptorSetLayout(m_device, setLayout, VK_NULL_HANDLE);
            }
            vkDestroyDescriptorPool(m_device, m_descriptorPool, VK_NULL_HANDLE);
        }

    public:
        void feedBuffer(uint32_t set,
                        uint32_t binding,
                        const std::unique_ptr<Buffer> &buffer,
                        uint32_t offset,
                        uint32_t range) {
            // Attach our buffer to this set
            VkDescriptorBufferInfo descriptorBufferInfo = {};
            descriptorBufferInfo.buffer = buffer->buf();
            descriptorBufferInfo.offset = offset;
            descriptorBufferInfo.range = range;

            VkWriteDescriptorSet writeDescriptorSet = {};
            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.dstSet = m_descriptorSets[set];
            writeDescriptorSet.dstBinding = binding;
            writeDescriptorSet.descriptorCount = 1;
            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
            vkUpdateDescriptorSets(m_device, 1, &writeDescriptorSet, 0, VK_NULL_HANDLE);
        }

        std::unique_ptr<Command> createCommand() {
            return std::make_unique<Command>(m_device,
                                             m_graphicsQueue,
                                             m_pipelineLayout,
                                             m_computePipeline,
                                             m_descriptorSets,
                                             m_commandPool);
        }

    private:
        void initDescriptor(const std::vector<std::vector<std::tuple<uint32_t, VkDescriptorType>>> &setsBindings) {
            // ----------
            // n_sets = 2
            // setsBindings = {[(0, SSBO)], [(1, UBO), (1, SSBO)]}
            // ----------

            // Check order, set

            // Pool
            VkDescriptorPoolSize descriptorPoolSize = {};
            descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorPoolSize.descriptorCount = 1;

            VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
            descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolCreateInfo.maxSets = setsBindings.size();
            descriptorPoolCreateInfo.poolSizeCount = 1;
            descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
            descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

            if (vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, VK_NULL_HANDLE, &m_descriptorPool) !=
                VK_SUCCESS) {
                throw std::runtime_error("failed to create descriptor pool!");
            }

            // Sets binding layout
            for (const auto &bindings : setsBindings) {
                std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;

                for (auto const &bind : bindings) {
                    VkDescriptorSetLayoutBinding setLayoutBinding = {};
                    std::tie(setLayoutBinding.binding, setLayoutBinding.descriptorType) = bind;
                    setLayoutBinding.descriptorCount = 1;
                    setLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                    setLayoutBindings.push_back(setLayoutBinding);
                }

                VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
                descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                descriptorSetLayoutCreateInfo.bindingCount = setLayoutBindings.size();
                descriptorSetLayoutCreateInfo.pBindings = setLayoutBindings.data();

                VkDescriptorSetLayout descriptorSetLayout;
                if (vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCreateInfo, VK_NULL_HANDLE,
                                                &descriptorSetLayout) != VK_SUCCESS) {
                    throw std::runtime_error("failed to create descriptor!");
                }
                m_descriptorSetLayouts.push_back(descriptorSetLayout);
            }

            // Descriptor
            VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
            descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocateInfo.descriptorPool = m_descriptorPool;
            descriptorSetAllocateInfo.descriptorSetCount = m_descriptorSetLayouts.size();
            descriptorSetAllocateInfo.pSetLayouts = m_descriptorSetLayouts.data();

            m_descriptorSets.resize(m_descriptorSetLayouts.size());
            if (vkAllocateDescriptorSets(m_device, &descriptorSetAllocateInfo, m_descriptorSets.data()) != VK_SUCCESS) {
                throw std::runtime_error("failed to create descriptor pool!");
            }
        }

        void initPipeline(const std::unique_ptr<Shader> &shader) {
            // Shader stages
            VkPipelineShaderStageCreateInfo compShaderStageInfo = {};
            compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            compShaderStageInfo.module = shader->module();
            compShaderStageInfo.pName = "main";

            // Pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutCreateInfo.setLayoutCount = m_descriptorSetLayouts.size();
            pipelineLayoutCreateInfo.pSetLayouts = m_descriptorSetLayouts.data();

            if (vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, VK_NULL_HANDLE, &m_pipelineLayout) !=
                VK_SUCCESS) {
                throw std::runtime_error("failed to create pipeline layout!");
            }

            // pipeline
            VkComputePipelineCreateInfo pipelineCreateInfo = {};
            pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineCreateInfo.stage = compShaderStageInfo;
            pipelineCreateInfo.layout = m_pipelineLayout;

            if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, VK_NULL_HANDLE,
                                         &m_computePipeline) != VK_SUCCESS) {
                throw std::runtime_error("failed to create compute pipeline!");
            }
        }

        void initCommandPool() {
            VkCommandPoolCreateInfo poolInfo = {};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = m_queueFamilyIndex;

            if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
                throw std::runtime_error("failed to create command pool!");
            }
        }

    private:
        const VkDevice &m_device;
        uint32_t m_queueFamilyIndex;
        const VkQueue &m_graphicsQueue;
        //
        VkDescriptorPool m_descriptorPool;
        std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
        std::vector<VkDescriptorSet> m_descriptorSets;
        //
        VkPipelineLayout m_pipelineLayout;
        VkPipeline m_computePipeline;
        //
        VkCommandPool m_commandPool;
    };

    class Device {
    public:
        Device() = delete;

        Device(VkPhysicalDevice physicalDevice,
               uint32_t queueFamilyIndex)
                : m_physicalDevice(physicalDevice),
                  m_queueFamilyIndex(queueFamilyIndex) {
            // Specifying the queues to be created
            VkDeviceQueueCreateInfo queueCreateInfo = {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = m_queueFamilyIndex;
            float queuePriority = 1.0f;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;

            // Specifying used device features
            VkPhysicalDeviceFeatures deviceFeatures = {};

            // Infomation of layers and extensions
            VkDeviceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.pQueueCreateInfos = &queueCreateInfo;
            createInfo.queueCreateInfoCount = 1;
            createInfo.pEnabledFeatures = &deviceFeatures;
            createInfo.enabledExtensionCount = 0;
            createInfo.enabledLayerCount = 0;

            // create device
            if (vkCreateDevice(m_physicalDevice, &createInfo, VK_NULL_HANDLE, &m_device) != VK_SUCCESS) {
                throw std::runtime_error("failed to create logical device!");
            }

            // get graphic queue
            vkGetDeviceQueue(m_device, 0, 0, &m_graphicsQueue);
        }

        ~Device() {
            vkDestroyDevice(m_device, VK_NULL_HANDLE);
        }

    public:
        std::unique_ptr<Buffer>
        createBuffer(uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) const {
            return std::make_unique<Buffer>(m_physicalDevice, m_device, size, usage, properties);
        }

        std::unique_ptr<Shader> createShader(const std::vector<uint8_t> &compShaderCode,
                                             VkShaderStageFlagBits shaderStage) const {
            return std::make_unique<Shader>(m_device, compShaderCode, shaderStage);
        }

        std::unique_ptr<ComputePipeline> createComputePipeline(const std::unique_ptr<Shader> &shader,
                                                               const std::vector<std::vector<std::tuple<uint32_t, VkDescriptorType>>> &setsBindings) const {
            return std::make_unique<ComputePipeline>(m_device, m_queueFamilyIndex, m_graphicsQueue, shader,
                                                     setsBindings);
        }

    private:
        VkPhysicalDevice m_physicalDevice;
        uint32_t m_queueFamilyIndex;
        VkDevice m_device;
        VkQueue m_graphicsQueue;
    };

    struct Config {
        const bool enableValidationLayers = false;

        /*
        const std::vector<const char*> deviceExtensions = {
          VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };
        */

        std::vector<const char *> getRequiredExtensions() {
            std::vector<const char *> extensions;

            if (enableValidationLayers) {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }

            return extensions;
        };

        std::vector<const char *> getValidationLayers() {
            std::vector<const char *> layers;

            if (enableValidationLayers) {
                layers.push_back("VK_LAYER_LUNARG_standard_validation");
            }

            return layers;
        };
    } config;

    class Instance {
    public:
        Instance() = delete;

        Instance(const std::string &appName,
                 uint32_t appVersion,
                 const std::string &engineName,
                 uint32_t engineVersion) {
            // Information about our application.
            // Optional, driver could use this to optimize for specific app.
            VkApplicationInfo appInfo = {};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = appName.data();
            appInfo.applicationVersion = appVersion;
            appInfo.pEngineName = engineName.data();
            appInfo.engineVersion = engineVersion;
            appInfo.apiVersion = VK_API_VERSION_1_0;

            // Information of extensions
            VkInstanceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;

            // extensions
            auto extensions = config.getRequiredExtensions();
            createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
            createInfo.ppEnabledExtensionNames = extensions.data();

            // layers
            auto layers = config.getValidationLayers();
            createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
            createInfo.ppEnabledLayerNames = layers.data();

            if (vkCreateInstance(&createInfo, VK_NULL_HANDLE, &m_instance) != VK_SUCCESS) {
                throw std::runtime_error("failed to create instance!");
            }
        }

        ~Instance() {
            vkDestroyInstance(m_instance, VK_NULL_HANDLE);
        }

    public:
        std::unique_ptr<Device> getDevice(VkQueueFlagBits queueFlag) const {
            //
            uint32_t queueFamilyIndex;
            VkPhysicalDevice physicalDevice;
            std::tie(queueFamilyIndex, physicalDevice) = initPhyscalDevice(queueFlag);

            //
            return std::make_unique<Device>(physicalDevice, queueFamilyIndex);
        }

        std::unique_ptr<Device> getGraphicDevice() const {
            return getDevice(VK_QUEUE_GRAPHICS_BIT);
        }

        std::unique_ptr<Device> getComputeDevice() const {
            return getDevice(VK_QUEUE_COMPUTE_BIT);
        }

    private:
        std::tuple<uint32_t, VkPhysicalDevice> initPhyscalDevice(const VkQueueFlagBits &queueFlag) const {
            // Count devices
            uint32_t deviceCount = 0;
            vkEnumeratePhysicalDevices(m_instance, &deviceCount, VK_NULL_HANDLE);
            if (deviceCount == 0) {
                throw std::runtime_error("failed to find GPUs with Vulkan support!");
            }

            // Enum all physical devices
            std::vector<VkPhysicalDevice> devices(deviceCount);
            vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

            auto isDeviceSuitable = [&](const VkPhysicalDevice &device) -> std::tuple<bool, uint32_t> {
                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, VK_NULL_HANDLE);

                std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

                uint32_t index = 0;
                for (const auto &queueFamily : queueFamilies) {
                    if (queueFamily.queueCount > 0 && queueFamily.queueFlags & queueFlag) {
                        break;
                    }
                    index += 1;
                }
                if (index == queueFamilies.size()) {
                    throw std::runtime_error("failed to find a suitable device!");
                }

                return std::make_tuple(true, index);
            };

            // Pick one with graphics and compute pipeline support
            uint32_t queueFamilyIndex;
            VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
            for (const auto &device : devices) {
                bool foundDevice;
                std::tie(foundDevice, queueFamilyIndex) = isDeviceSuitable(device);
                if (foundDevice) {
                    physicalDevice = device;
                    break;
                }
            }
            if (physicalDevice == VK_NULL_HANDLE) {
                throw std::runtime_error("failed to find a suitable device!");
            }

            return std::make_tuple(queueFamilyIndex, physicalDevice);
        }

    private:
        VkInstance m_instance;
    };

    std::unique_ptr<Instance> createInstance(const std::string &appName = "Demo",
                                             uint32_t appVersion = VK_MAKE_VERSION(1, 0, 0),
                                             const std::string &engineName = "No Engine",
                                             uint32_t engineVersion = VK_MAKE_VERSION(1, 0, 0)) {
        return std::make_unique<Instance>(appName, appVersion, engineName, engineVersion);
    }

}


class Engine {
public:
    Engine() {
        wrapper_init();
        LOGI("0. Vulkan env ready");

        m_instance = vk::createInstance();
        LOGI("1. Instance ready");

        m_device = m_instance->getComputeDevice();
        LOGI("2. Device ready");
    }

    ~Engine() {
        LOGI("8. Finish");
    }

    void init(const uint8_t *shader, size_t size) {
        std::vector<uint8_t> code;
        for (size_t i = 0; i < size; i += 1) {
            code.push_back(shader[i]);
        }
        m_shader = m_device->createShader(code, VK_SHADER_STAGE_COMPUTE_BIT);
        LOGI("3. Shader ready");

        m_pipeline = m_device->createComputePipeline(m_shader, {{std::make_tuple(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)}});
        LOGI("4. Pipeline ready");

        m_buffer = m_device->createBuffer(1024 * 1024 * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        m_pipeline->feedBuffer(0, 0, m_buffer, 0, 1024 * 1024 * 4);
        LOGI("5. Buffer ready");

        m_command = m_pipeline->createCommand();
        LOGI("6. Command ready");
    }

    void render(void *out, size_t size) {
        auto fence = m_command->submit();
        fence->wait();
        LOGI("7. execute ready");

        m_buffer->dump(out, size);
    }

private:
    std::unique_ptr<vk::Instance> m_instance;
    std::unique_ptr<vk::Device> m_device;
    std::unique_ptr<vk::Shader> m_shader;
    std::unique_ptr<vk::ComputePipeline> m_pipeline;
    std::unique_ptr<vk::Buffer> m_buffer;
    std::unique_ptr<vk::Command> m_command;
};
std::unique_ptr<Engine> engine;


extern "C" JNIEXPORT void JNICALL
Java_com_example_jniview2_CustomImageView_initNative(JNIEnv *env, jobject obj, jbyteArray bytes) {
    assert(obj);

    jsize size = env->GetArrayLength(bytes);
    if (size < 0) {
        LOGE("env->GetArrayLength() failed!, size = %d", size);
        return;
    }

    engine = std::make_unique<Engine>();

    auto *shader = reinterpret_cast<uint8_t *>(env->GetByteArrayElements(bytes, nullptr));
    engine->init(shader, static_cast<size_t >(size));
    env->ReleaseByteArrayElements(bytes, reinterpret_cast<jbyte *>(shader), 0);
}


extern "C" JNIEXPORT void JNICALL
Java_com_example_jniview2_CustomImageView_renderNative(JNIEnv *env, jobject obj, jobject bitmap) {
    assert(obj);

    int ret;
    AndroidBitmapInfo info;

    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("AndroidBitmap_getInfo() failed! error=%d", ret);
        return;
    }

    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("Bitmap format is not RGBA_8888!");
        return;
    }

    void *pixels;
    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed! error=%d", ret);
        return;
    }

    engine->render(pixels, 4 * 1024 * 1024);

    uint32_t x = 0;
    for (size_t i = 0; i < 1024 * 1024; i += 1) {
        x ^= reinterpret_cast<uint32_t *>(pixels)[i];
    }
    LOGE("check %d", x);

    if ((ret = AndroidBitmap_unlockPixels(env, bitmap)) < 0) {
        LOGE("AndroidBitmap_unlockPixels() failed! error=%d", ret);
        return;
    }
}