#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "graphics.hpp"
#include "assets.hpp"
#include "sprite_shaders.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace bb::gfx {
namespace {
#define INSTANCE_FUNCTIONS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) \
    X(vkDestroySurfaceKHR)
#define DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueWaitIdle) \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkCreateImageView) \
    X(vkDestroyImageView) X(vkCreateRenderPass) X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkFreeCommandBuffers) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer) \
    X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) \
    X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdPushConstants) \
    X(vkCmdBindDescriptorSets) X(vkCmdDraw) X(vkCmdPipelineBarrier) \
    X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkQueueSubmit) X(vkCreateSemaphore) \
    X(vkDestroySemaphore) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) \
    X(vkResetFences) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) \
    X(vkUnmapMemory) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) \
    X(vkBindImageMemory) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) \
    X(vkFreeDescriptorSets) X(vkUpdateDescriptorSets)
#define DECLARE(name) PFN_##name name{};
    INSTANCE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE

struct Texture {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{};
    VkDescriptorSet descriptor{};
};
struct PushConstants {
    std::array<float, 4> destination, source, transform, tint, effects, dimensions;
};
static_assert(sizeof(PushConstants) == 96);

struct State {
    GLFWwindow* window{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t family{}, imageIndex{};
    VkSwapchainKHR swapchain{};
    VkFormat format{};
    VkExtent2D extent{};
    std::vector<VkImageView> views;
    std::vector<VkImage> swapImages;
    bool captureSupported{};
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkCommandPool commandPool{};
    VkCommandBuffer commands{};
    VkSemaphore acquired{}, rendered{};
    VkFence completed{};
    VkDescriptorSetLayout descriptors{};
    VkDescriptorPool descriptorPool{};
    VkSampler sampler{};
    std::map<unsigned int, Texture> textures;
    unsigned int nextTexture{1};
    Texture2D white{}, font{};
    std::vector<int> advances;
    std::array<bool, 512> keys{}, previous{};
    std::deque<int> characters;
    float wheel{};
    bool recording{}, captureWanted{};
    VkBuffer readback{};
    VkDeviceMemory readbackMemory{};
    std::vector<unsigned char> screenshot;
    int fps{30};
    std::chrono::steady_clock::time_point frameStart;
} state;

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("Vulkan ") + operation + " failed (" +
                                 std::to_string(result) + ")");
    }
}

std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(state.physical, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required) return i;
    }
    throw std::runtime_error("Vulkan device has no compatible memory type");
}

VkDeviceMemory allocate(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags) {
    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = requirements.size;
    info.memoryTypeIndex = memoryType(requirements.memoryTypeBits, flags);
    VkDeviceMemory memory{};
    check(vkAllocateMemory(state.device, &info, nullptr, &memory), "allocate memory");
    return memory;
}

VkImageView imageView(VkImage image, VkFormat format) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view{};
    check(vkCreateImageView(state.device, &info, nullptr, &view), "create image view");
    return view;
}

VkCommandBuffer allocateCommands() {
    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = state.commandPool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;
    VkCommandBuffer commands{};
    check(vkAllocateCommandBuffers(state.device, &info, &commands), "allocate command buffer");
    return commands;
}

void destroySwapchain() {
    for (auto framebuffer : state.framebuffers) vkDestroyFramebuffer(state.device, framebuffer, nullptr);
    for (auto view : state.views) vkDestroyImageView(state.device, view, nullptr);
    state.framebuffers.clear(); state.views.clear();
    if (state.swapchain) vkDestroySwapchainKHR(state.device, state.swapchain, nullptr);
    state.swapchain = VK_NULL_HANDLE;
}

void createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state.physical, state.surface, &capabilities),
          "query surface capabilities");
    std::uint32_t count{};
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(state.physical, state.surface, &count, nullptr),
          "query surface formats");
    if (!count) throw std::runtime_error("Vulkan surface has no supported formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(state.physical, state.surface, &count, formats.data()),
          "read surface formats");
    auto chosen = formats.front();
    for (auto format : formats) if (format.format == VK_FORMAT_B8G8R8A8_UNORM) chosen = format;
    if (chosen.format == VK_FORMAT_UNDEFINED) chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
    if (state.renderPass && chosen.format != state.format) {
        throw std::runtime_error("Vulkan surface format changed; restart the application");
    }
    state.format = chosen.format;
    state.extent = capabilities.currentExtent;
    if (state.extent.width == UINT32_MAX) {
        int w{}, h{}; glfwGetFramebufferSize(state.window, &w, &h);
        state.extent.width = std::clamp(static_cast<std::uint32_t>(w),
                                       capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        state.extent.height = std::clamp(static_cast<std::uint32_t>(h),
                                        capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    std::uint32_t images = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount) images = std::min(images, capabilities.maxImageCount);
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = state.surface;
    info.minImageCount = images;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = state.extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    state.captureSupported = capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (state.captureSupported) info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(capabilities.supportedCompositeAlpha & info.compositeAlpha)) {
        for (auto alpha : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
            if (capabilities.supportedCompositeAlpha & alpha) { info.compositeAlpha = alpha; break; }
        }
    }
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(state.device, &info, nullptr, &state.swapchain), "create swapchain");
    check(vkGetSwapchainImagesKHR(state.device, state.swapchain, &images, nullptr), "query swap images");
    state.swapImages.resize(images);
    check(vkGetSwapchainImagesKHR(state.device, state.swapchain, &images, state.swapImages.data()),
          "read swap images");
    for (auto image : state.swapImages) state.views.push_back(imageView(image, state.format));
}

void createFramebuffers() {
    for (auto view : state.views) {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = state.renderPass;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = state.extent.width; info.height = state.extent.height; info.layers = 1;
        VkFramebuffer framebuffer{};
        check(vkCreateFramebuffer(state.device, &info, nullptr, &framebuffer), "create framebuffer");
        state.framebuffers.push_back(framebuffer);
    }
}

void createPipeline() {
    VkAttachmentDescription color{};
    color.format = state.format; color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 1; pass.pAttachments = &color;
    pass.subpassCount = 1; pass.pSubpasses = &subpass;
    pass.dependencyCount = 1; pass.pDependencies = &dependency;
    check(vkCreateRenderPass(state.device, &pass, nullptr, &state.renderPass), "create render pass");

    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                              sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1; layout.pSetLayouts = &state.descriptors;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &range;
    check(vkCreatePipelineLayout(state.device, &layout, nullptr, &state.layout), "create pipeline layout");
    auto module = [](const auto& words) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = sizeof(words); info.pCode = words;
        VkShaderModule shader{};
        check(vkCreateShaderModule(state.device, &info, nullptr, &shader), "create SPIR-V shader");
        return shader;
    };
    VkShaderModule vertex = module(vertexShader), fragment = module(fragmentShader);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment;
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1;
    raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1; blending.pAttachments = &blend;
    std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = dynamicStates.size(); dynamic.pDynamicStates = dynamicStates.data();
    VkGraphicsPipelineCreateInfo pipeline{};
    pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline.stageCount = stages.size(); pipeline.pStages = stages.data();
    pipeline.pVertexInputState = &input; pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport; pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample; pipeline.pColorBlendState = &blending;
    pipeline.pDynamicState = &dynamic; pipeline.layout = state.layout;
    pipeline.renderPass = state.renderPass;
    auto result = vkCreateGraphicsPipelines(state.device, VK_NULL_HANDLE, 1, &pipeline, nullptr,
                                            &state.pipeline);
    vkDestroyShaderModule(state.device, fragment, nullptr);
    vkDestroyShaderModule(state.device, vertex, nullptr);
    check(result, "create sprite pipeline");
}
}

void recordCapture();
void finishCapture();

void initialize(int w, int h, const char* title, bool hidden) {
    try {
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW");
        if (!glfwVulkanSupported()) throw std::runtime_error("Vulkan is unavailable. Install a Vulkan-capable graphics driver.");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        state.window = glfwCreateWindow(w, h, title, nullptr, nullptr);
        if (!state.window) throw std::runtime_error("Cannot create Vulkan window");
        glfwSetScrollCallback(state.window, [](GLFWwindow*, double, double y) { state.wheel += static_cast<float>(y); });
        glfwSetCharCallback(state.window, [](GLFWwindow*, unsigned int c) { state.characters.push_back(static_cast<int>(c)); });
        glfwSetKeyCallback(state.window, [](GLFWwindow*, int key, int, int action, int) {
            if (key >= 0 && key < static_cast<int>(state.keys.size())) {
                state.keys[key] = action != GLFW_RELEASE;
            }
        });
        auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(glfwGetInstanceProcAddress(nullptr, "vkCreateInstance"));
        if (!createInstance) throw std::runtime_error("Cannot load Vulkan entry point");
        std::uint32_t count{};
        auto extensions = glfwGetRequiredInstanceExtensions(&count);
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "Battle Bound DX"; application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo instance{};
        instance.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance.pApplicationInfo = &application;
        instance.enabledExtensionCount = count; instance.ppEnabledExtensionNames = extensions;
        check(createInstance(&instance, nullptr, &state.instance), "create instance");
#define LOAD_INSTANCE(name) \
        name = reinterpret_cast<PFN_##name>(glfwGetInstanceProcAddress(state.instance, #name)); \
        if (!name) throw std::runtime_error("Missing Vulkan entry point: " #name);
        INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
        check(glfwCreateWindowSurface(state.instance, state.window, nullptr, &state.surface), "create surface");
        check(vkEnumeratePhysicalDevices(state.instance, &count, nullptr), "query GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(state.instance, &count, devices.data()), "read GPUs");
        for (auto physical : devices) {
            std::uint32_t queues{};
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &queues, nullptr);
            std::vector<VkQueueFamilyProperties> families(queues);
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &queues, families.data());
            for (std::uint32_t i = 0; i < queues; ++i) {
                VkBool32 present{};
                check(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, state.surface, &present), "query presentation support");
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    state.physical = physical; state.family = i; break;
                }
            }
            if (state.physical) break;
        }
        if (!state.physical) throw std::runtime_error("No Vulkan graphics/present queue is available");
        float priority = 1;
        VkDeviceQueueCreateInfo queue{};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = state.family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo device{};
        device.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
        device.enabledExtensionCount = 1; device.ppEnabledExtensionNames = &swapchainExtension;
        check(vkCreateDevice(state.physical, &device, nullptr, &state.device), "create device");
#define LOAD_DEVICE(name) \
        name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(state.device, #name)); \
        if (!name) throw std::runtime_error("Missing Vulkan device entry point: " #name);
        DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
        vkGetDeviceQueue(state.device, state.family, 0, &state.queue);
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = state.family;
        check(vkCreateCommandPool(state.device, &pool, nullptr, &state.commandPool), "create command pool");
        state.commands = allocateCommands();
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0; binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1; binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo descriptor{};
        descriptor.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor.bindingCount = 1; descriptor.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(state.device, &descriptor, nullptr, &state.descriptors), "create descriptor layout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048};
        VkDescriptorPoolCreateInfo descriptors{};
        descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptors.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        descriptors.maxSets = 2048; descriptors.poolSizeCount = 1; descriptors.pPoolSizes = &size;
        check(vkCreateDescriptorPool(state.device, &descriptors, nullptr, &state.descriptorPool), "create descriptor pool");
        VkSamplerCreateInfo sampler{};
        sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(state.device, &sampler, nullptr, &state.sampler), "create sampler");
        VkSemaphoreCreateInfo semaphore{}; semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(state.device, &semaphore, nullptr, &state.acquired), "create acquire semaphore");
        check(vkCreateSemaphore(state.device, &semaphore, nullptr, &state.rendered), "create render semaphore");
        VkFenceCreateInfo fence{}; fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(state.device, &fence, nullptr, &state.completed), "create frame fence");
        createSwapchain(); createPipeline(); createFramebuffers();
        Color white = WHITE;
        state.white = upload(Image{&white, 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8});
        auto atlas = makeFontAtlas();
        state.font = upload(Image{atlas.pixels.data(), atlas.width, atlas.height, 1,
                                   PIXELFORMAT_UNCOMPRESSED_R8G8B8A8});
        state.advances = std::move(atlas.advances);
    } catch (...) {
        shutdown();
        throw;
    }
}

Texture2D upload(Image image) {
    if (!image.data || image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        throw std::runtime_error("Vulkan upload requires RGBA8 image data");
    }
    VkDeviceSize bytes = static_cast<VkDeviceSize>(image.width) * image.height * 4;
    VkBufferCreateInfo buffer{};
    buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer.size = bytes; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging{};
    check(vkCreateBuffer(state.device, &buffer, nullptr, &staging), "create upload buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(state.device, staging, &requirements);
    auto stagingMemory = allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkBindBufferMemory(state.device, staging, stagingMemory, 0), "bind upload buffer");
    void* mapped{};
    check(vkMapMemory(state.device, stagingMemory, 0, bytes, 0, &mapped), "map upload memory");
    std::memcpy(mapped, image.data, static_cast<std::size_t>(bytes));
    vkUnmapMemory(state.device, stagingMemory);
    Texture texture;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {static_cast<std::uint32_t>(image.width), static_cast<std::uint32_t>(image.height), 1};
    info.mipLevels = info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    check(vkCreateImage(state.device, &info, nullptr, &texture.image), "create costume image");
    vkGetImageMemoryRequirements(state.device, texture.image, &requirements);
    texture.memory = allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkBindImageMemory(state.device, texture.image, texture.memory, 0), "bind costume memory");
    auto command = allocateCommands();
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "begin upload");
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = info.extent;
    vkCmdCopyBufferToImage(command, staging, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    check(vkEndCommandBuffer(command), "end upload");
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    check(vkQueueSubmit(state.queue, 1, &submit, VK_NULL_HANDLE), "submit upload");
    check(vkQueueWaitIdle(state.queue), "wait for upload");
    vkFreeCommandBuffers(state.device, state.commandPool, 1, &command);
    vkDestroyBuffer(state.device, staging, nullptr); vkFreeMemory(state.device, stagingMemory, nullptr);
    texture.view = imageView(texture.image, info.format);
    VkDescriptorSetAllocateInfo descriptors{};
    descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptors.descriptorPool = state.descriptorPool;
    descriptors.descriptorSetCount = 1; descriptors.pSetLayouts = &state.descriptors;
    check(vkAllocateDescriptorSets(state.device, &descriptors, &texture.descriptor), "allocate costume descriptor");
    VkDescriptorImageInfo imageInfo{state.sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = texture.descriptor; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(state.device, 1, &write, 0, nullptr);
    unsigned int id = state.nextTexture++;
    state.textures.emplace(id, texture);
    return {id, image.width, image.height, 1, image.format};
}

void release(Texture2D texture) {
    auto found = state.textures.find(texture.id);
    if (found == state.textures.end()) return;
    vkDeviceWaitIdle(state.device);
    auto& item = found->second;
    vkFreeDescriptorSets(state.device, state.descriptorPool, 1, &item.descriptor);
    vkDestroyImageView(state.device, item.view, nullptr);
    vkDestroyImage(state.device, item.image, nullptr);
    vkFreeMemory(state.device, item.memory, nullptr);
    state.textures.erase(found);
}

void shutdown() {
    if (state.device && vkDeviceWaitIdle) vkDeviceWaitIdle(state.device);
    while (!state.textures.empty()) release(Texture2D{state.textures.begin()->first, 0, 0, 0, 0});
    if (state.device) {
        if (state.readback) vkDestroyBuffer(state.device, state.readback, nullptr);
        if (state.readbackMemory) vkFreeMemory(state.device, state.readbackMemory, nullptr);
        destroySwapchain();
        if (state.pipeline) vkDestroyPipeline(state.device, state.pipeline, nullptr);
        if (state.layout) vkDestroyPipelineLayout(state.device, state.layout, nullptr);
        if (state.renderPass) vkDestroyRenderPass(state.device, state.renderPass, nullptr);
        if (state.sampler) vkDestroySampler(state.device, state.sampler, nullptr);
        if (state.descriptorPool) vkDestroyDescriptorPool(state.device, state.descriptorPool, nullptr);
        if (state.descriptors) vkDestroyDescriptorSetLayout(state.device, state.descriptors, nullptr);
        if (state.commandPool) vkDestroyCommandPool(state.device, state.commandPool, nullptr);
        if (state.acquired) vkDestroySemaphore(state.device, state.acquired, nullptr);
        if (state.rendered) vkDestroySemaphore(state.device, state.rendered, nullptr);
        if (state.completed) vkDestroyFence(state.device, state.completed, nullptr);
        if (vkDestroyDevice) vkDestroyDevice(state.device, nullptr);
    }
    if (state.surface && vkDestroySurfaceKHR) vkDestroySurfaceKHR(state.instance, state.surface, nullptr);
    if (state.instance && vkDestroyInstance) vkDestroyInstance(state.instance, nullptr);
    if (state.window) glfwDestroyWindow(state.window);
    glfwTerminate(); state = State{};
}

bool shouldClose() {
    state.previous = state.keys; state.wheel = 0;
    glfwPollEvents();
    return glfwWindowShouldClose(state.window);
}
void setFrameRate(int fps) { state.fps = fps; }
bool keyDown(int key) { return key >= 0 && key < 512 && state.keys[key]; }
bool keyPressed(int key) { return keyDown(key) && !state.previous[key]; }
bool mouseDown(int button) { return glfwGetMouseButton(state.window, button) == GLFW_PRESS; }
float mouseX() { double x{}, y{}; glfwGetCursorPos(state.window, &x, &y); return static_cast<float>(x); }
float mouseY() { double x{}, y{}; glfwGetCursorPos(state.window, &x, &y); return static_cast<float>(y); }
float mouseWheel() { return state.wheel; }
int nextCharacter() { if (state.characters.empty()) return 0; int c = state.characters.front(); state.characters.pop_front(); return c; }
int width() { int w{}, h{}; glfwGetWindowSize(state.window, &w, &h); return w; }
int height() { int w{}, h{}; glfwGetWindowSize(state.window, &w, &h); return h; }

void beginFrame() {
    state.frameStart = std::chrono::steady_clock::now();
    check(vkWaitForFences(state.device, 1, &state.completed, VK_TRUE, UINT64_MAX), "wait for frame");
    int w{}, h{}; glfwGetFramebufferSize(state.window, &w, &h);
    if (!w || !h) return;
    auto result = vkAcquireNextImageKHR(state.device, state.swapchain, UINT64_MAX, state.acquired,
                                        VK_NULL_HANDLE, &state.imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        check(vkDeviceWaitIdle(state.device), "wait before swapchain rebuild");
        destroySwapchain(); createSwapchain(); createFramebuffers(); return;
    }
    if (result != VK_SUBOPTIMAL_KHR) check(result, "acquire image");
    check(vkResetCommandBuffer(state.commands, 0), "reset frame commands");
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    check(vkBeginCommandBuffer(state.commands, &begin), "begin frame");
    VkClearValue clear{}; clear.color.float32[3] = 1;
    VkRenderPassBeginInfo pass{}; pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = state.renderPass; pass.framebuffer = state.framebuffers[state.imageIndex];
    pass.renderArea.extent = state.extent; pass.clearValueCount = 1; pass.pClearValues = &clear;
    vkCmdBeginRenderPass(state.commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(state.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
    VkViewport viewport{0, 0, static_cast<float>(state.extent.width), static_cast<float>(state.extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, state.extent};
    vkCmdSetViewport(state.commands, 0, 1, &viewport); vkCmdSetScissor(state.commands, 0, 1, &scissor);
    state.recording = true;
}

void endFrame() {
    if (state.recording) {
        vkCmdEndRenderPass(state.commands);
        if (state.captureWanted) recordCapture();
        check(vkEndCommandBuffer(state.commands), "end frame commands");
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &state.acquired;
        submit.pWaitDstStageMask = &stage; submit.commandBufferCount = 1;
        submit.pCommandBuffers = &state.commands;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &state.rendered;
        check(vkResetFences(state.device, 1, &state.completed), "reset frame fence");
        check(vkQueueSubmit(state.queue, 1, &submit, state.completed), "submit frame");
        VkPresentInfoKHR present{}; present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &state.rendered;
        present.swapchainCount = 1; present.pSwapchains = &state.swapchain;
        present.pImageIndices = &state.imageIndex;
        auto result = vkQueuePresentKHR(state.queue, &present);
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) check(result, "present frame");
        // A single in-flight frame also guarantees present-semaphore reuse is safe.
        check(vkQueueWaitIdle(state.queue), "complete presentation");
        if (state.readback) finishCapture();
        state.recording = false;
    }
    if (state.fps > 0) std::this_thread::sleep_until(state.frameStart + std::chrono::microseconds(1'000'000 / state.fps));
}

void sprite(Texture2D texture, Rectangle source, Rectangle destination, Vector2 pivot,
            float angle, Color tint, float hue, float brightness) {
    if (!state.recording) return;
    auto found = state.textures.find(texture.id);
    if (found == state.textures.end()) return;
    if (source.width < 0) source.x -= source.width;
    if (source.height < 0) source.y -= source.height;
    float radians = angle * 0.017453292519943295f;
    PushConstants constants{
        {destination.x, destination.y, destination.width, destination.height},
        {source.x, source.y, source.width, source.height},
        {pivot.x, pivot.y, std::cos(radians), std::sin(radians)},
        {tint.r / 255.f, tint.g / 255.f, tint.b / 255.f, tint.a / 255.f},
        {hue, brightness, 0, 0},
        {640, 360, static_cast<float>(texture.width), static_cast<float>(texture.height)}};
    vkCmdPushConstants(state.commands, state.layout, VK_SHADER_STAGE_VERTEX_BIT |
                        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
    vkCmdBindDescriptorSets(state.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, state.layout,
                            0, 1, &found->second.descriptor, 0, nullptr);
    vkCmdDraw(state.commands, 6, 1, 0, 0);
}
void rectangle(float x, float y, float w, float h, Color color) {
    sprite(state.white, {0, 0, 1, 1}, {x, y, w, h}, {0, 0}, 0, color);
}
void text(const char* content, float x, float y, float size, Color color) {
    float origin = x, scale = size / 48;
    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(content); *c; ++c) {
        if (*c == '\n') { x = origin; y += size * 1.25f; continue; }
        int glyph = (*c >= 32 && *c < 127 ? *c : '?') - 32;
        sprite(state.font, {static_cast<float>(glyph % 16 * 64), static_cast<float>(glyph / 16 * 64), 64, 64},
               {x, y, 64 * scale, 64 * scale}, {0, 0}, 0, color);
        x += state.advances[glyph] * scale;
    }
}
void requestCapture() { state.captureWanted = true; }

void recordCapture() {
    if (!state.captureSupported) throw std::runtime_error("This Vulkan surface does not support screenshots");
    if (state.format != VK_FORMAT_B8G8R8A8_UNORM && state.format != VK_FORMAT_B8G8R8A8_SRGB &&
        state.format != VK_FORMAT_R8G8B8A8_UNORM && state.format != VK_FORMAT_R8G8B8A8_SRGB) {
        throw std::runtime_error("Unsupported Vulkan screenshot pixel format");
    }
    VkDeviceSize bytes = static_cast<VkDeviceSize>(state.extent.width) * state.extent.height * 4;
    VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = bytes; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    check(vkCreateBuffer(state.device, &info, nullptr, &state.readback), "create screenshot buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(state.device, state.readback, &requirements);
    state.readbackMemory = allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkBindBufferMemory(state.device, state.readback, state.readbackMemory, 0), "bind screenshot memory");
    VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = state.swapImages[state.imageIndex];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(state.commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {state.extent.width, state.extent.height, 1};
    vkCmdCopyImageToBuffer(state.commands, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           state.readback, 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = 0;
    vkCmdPipelineBarrier(state.commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    state.captureWanted = false;
}

void finishCapture() {
    VkDeviceSize bytes = static_cast<VkDeviceSize>(state.extent.width) * state.extent.height * 4;
    void* mapped{};
    check(vkMapMemory(state.device, state.readbackMemory, 0, bytes, 0, &mapped), "map screenshot");
    state.screenshot.resize(static_cast<std::size_t>(bytes));
    std::memcpy(state.screenshot.data(), mapped, state.screenshot.size());
    if (state.format == VK_FORMAT_B8G8R8A8_UNORM || state.format == VK_FORMAT_B8G8R8A8_SRGB) {
        for (std::size_t i = 0; i < state.screenshot.size(); i += 4) {
            std::swap(state.screenshot[i], state.screenshot[i + 2]);
        }
    }
    vkUnmapMemory(state.device, state.readbackMemory);
    vkDestroyBuffer(state.device, state.readback, nullptr);
    vkFreeMemory(state.device, state.readbackMemory, nullptr);
    state.readback = VK_NULL_HANDLE; state.readbackMemory = VK_NULL_HANDLE;
}

Image capture() {
    if (state.screenshot.empty()) throw std::runtime_error("No Vulkan screenshot has been requested");
    void* pixels = MemAlloc(static_cast<unsigned int>(state.screenshot.size()));
    if (!pixels) throw std::runtime_error("Cannot allocate screenshot pixels");
    std::memcpy(pixels, state.screenshot.data(), state.screenshot.size());
    return {pixels, static_cast<int>(state.extent.width), static_cast<int>(state.extent.height),
            1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}
}
