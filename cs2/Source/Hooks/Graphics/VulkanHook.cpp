#include "VulkanHook.h"

#include <ctime>
#include <cstdio>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>

#include <vulkan/vulkan.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <CS2/Constants/DllNames.h>
#include <GlobalContext/HookQuiesce.h>
#include <Hooks/Graphics/VulkanResolutionChain.h>
#include <MemorySearch/BytePattern.h>
#include <MemorySearch/HybridPatternFinder.h>
#include <MemorySearch/PatternStringWildcard.h>
#include <Platform/DynamicLibrary.h>
#include <hooks/vac_hook.h>
#include <Utils/CrashLogger.h>
#include <Utils/SpinLock.h>
#include <Utils/StatusReport.h>
#include <Utils/VerifyConsole.h>

#include <UI/ImGui/GUI.h>
#include <UI/ImGui/GuiLog.h>
#include <UI/ImGui/SdlImGuiBackend.h>
#include <UI/ImGui/ShadowStamp.h>







namespace
{

struct LoaderFunctions {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
};

LoaderFunctions loader;



VkInstance fakeInstance = VK_NULL_HANDLE;
VkPhysicalDevice fakePhysicalDevice = VK_NULL_HANDLE;
std::uint32_t queueFamily = 0;

std::atomic<VkDevice> gameDevice{VK_NULL_HANDLE};


struct DeviceFunctions {
    PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
    PFN_vkCreateSemaphore createSemaphore = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkGetSwapchainImagesKHR getSwapchainImagesKHR = nullptr;
    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkDestroyImageView destroyImageView = nullptr;
    PFN_vkCreateFramebuffer createFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer destroyFramebuffer = nullptr;
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers freeCommandBuffers = nullptr;
    PFN_vkCreateRenderPass createRenderPass = nullptr;
    PFN_vkDestroyRenderPass destroyRenderPass = nullptr;
    PFN_vkCreateDescriptorPool createDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool destroyDescriptorPool = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass cmdEndRenderPass = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements getImageMemoryRequirements = nullptr;
    PFN_vkBindImageMemory bindImageMemory = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage = nullptr;
    PFN_vkCreateSampler createSampler = nullptr;
    PFN_vkDestroySampler destroySampler = nullptr;
    
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;

    [[nodiscard]] bool complete() const noexcept
    {
        return getSwapchainImagesKHR && createImageView && destroyImageView && createFramebuffer && destroyFramebuffer
            && createCommandPool && destroyCommandPool && allocateCommandBuffers && freeCommandBuffers
            && createRenderPass && destroyRenderPass && createDescriptorPool && destroyDescriptorPool
            && resetCommandBuffer && beginCommandBuffer && endCommandBuffer && cmdBeginRenderPass && cmdEndRenderPass
            && queueSubmit && createSemaphore && destroySemaphore && deviceWaitIdle
            && createFence && waitForFences && resetFences && destroyFence
            && createImage && destroyImage && getImageMemoryRequirements && bindImageMemory
            && allocateMemory && freeMemory && mapMemory && unmapMemory
            && createBuffer && destroyBuffer && getBufferMemoryRequirements && bindBufferMemory
            && cmdPipelineBarrier && cmdCopyBufferToImage && createSampler && destroySampler
            && getPhysicalDeviceMemoryProperties;
    }
};

DeviceFunctions deviceFunctions;
bool deviceFunctionsResolved = false;




constexpr std::size_t kMaxFrames = 10;
struct FrameResources {
    ImGui_ImplVulkanH_Frame frame{};
    VkSemaphore renderCompleteSemaphore = VK_NULL_HANDLE; 
};
FrameResources frames[kMaxFrames]{};




VkRenderPass renderPass = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkRenderPass retiredRenderPasses[kMaxFrames]{};
VkDescriptorPool retiredDescriptorPools[kMaxFrames]{};
std::size_t retiredCount = 0;
bool renderTargetsCreated = false;
bool rendererInitialized = false;
VkExtent2D swapchainExtent{};                            
VkFormat swapchainFormatCaptured = VK_FORMAT_UNDEFINED;  











VkFence uploadSlotFences[kMaxFrames]{};
std::uint64_t menuUploadFrame = 0; 



struct AvatarUploadState {
    
    const unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkFence usedFence = VK_NULL_HANDLE; 
    bool uploadRecorded = false;
};

AvatarUploadState music;
std::atomic<bool> musicRequestPending{false};
AvatarUploadState avatar;
std::atomic<bool> avatarRequestPending{false};


AvatarUploadState logo;
std::atomic<bool> logoRequestPending{false};



constexpr int kMaxLuaTextures = 8; 
AvatarUploadState luaTextures[kMaxLuaTextures];
std::atomic<bool> luaTexturePending[kMaxLuaTextures]{};
struct RetiredTexture {
    bool used = false;
    AvatarUploadState state;
};
constexpr int kMaxRetiredTextures = 24;
RetiredTexture retiredLuaTextures[kMaxRetiredTextures];
int retiredCursor = 0;

[[nodiscard]] std::uint32_t findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags properties) noexcept
{
    VkPhysicalDeviceMemoryProperties props{};
    deviceFunctions.getPhysicalDeviceMemoryProperties(fakePhysicalDevice, &props);
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}






void processTextureUpload(VkDevice device, VkCommandBuffer commandBuffer, VkFence submitFence,
    AvatarUploadState& avatar, std::atomic<bool>& avatarRequestPending) noexcept
{
    if (avatar.descriptor != VK_NULL_HANDLE || submitFence == VK_NULL_HANDLE)
        return;

    if (!avatar.uploadRecorded) {
        if (!avatarRequestPending.exchange(false, std::memory_order_acq_rel))
            return;

        const std::size_t pixelSize = static_cast<std::size_t>(avatar.width) * static_cast<std::size_t>(avatar.height) * 4u;
        bool ok = pixelSize > 0;

        if (ok) {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = pixelSize;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            ok = deviceFunctions.createBuffer(device, &bufferInfo, nullptr, &avatar.staging) == VK_SUCCESS;
        }
        if (ok) {
            VkMemoryRequirements req{};
            deviceFunctions.getBufferMemoryRequirements(device, avatar.staging, &req);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = req.size;
            allocInfo.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            ok = allocInfo.memoryTypeIndex != UINT32_MAX
                && deviceFunctions.allocateMemory(device, &allocInfo, nullptr, &avatar.stagingMemory) == VK_SUCCESS
                && deviceFunctions.bindBufferMemory(device, avatar.staging, avatar.stagingMemory, 0) == VK_SUCCESS;
        }
        if (ok) {
            void* mapped = nullptr;
            ok = deviceFunctions.mapMemory(device, avatar.stagingMemory, 0, pixelSize, 0, &mapped) == VK_SUCCESS;
            if (ok) {
                std::memcpy(mapped, avatar.pixels, pixelSize);
                deviceFunctions.unmapMemory(device, avatar.stagingMemory);
            }
        }
        if (ok) {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.extent = {static_cast<std::uint32_t>(avatar.width), static_cast<std::uint32_t>(avatar.height), 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ok = deviceFunctions.createImage(device, &imageInfo, nullptr, &avatar.image) == VK_SUCCESS;
        }
        if (ok) {
            VkMemoryRequirements req{};
            deviceFunctions.getImageMemoryRequirements(device, avatar.image, &req);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = req.size;
            allocInfo.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            ok = allocInfo.memoryTypeIndex != UINT32_MAX
                && deviceFunctions.allocateMemory(device, &allocInfo, nullptr, &avatar.imageMemory) == VK_SUCCESS
                && deviceFunctions.bindImageMemory(device, avatar.image, avatar.imageMemory, 0) == VK_SUCCESS;
        }
        if (ok) {
            VkImageMemoryBarrier toTransfer{};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.srcAccessMask = 0;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = avatar.image;
            toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            deviceFunctions.cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {static_cast<std::uint32_t>(avatar.width), static_cast<std::uint32_t>(avatar.height), 1};
            deviceFunctions.cmdCopyBufferToImage(commandBuffer, avatar.staging, avatar.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            VkImageMemoryBarrier toShader{};
            toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.image = avatar.image;
            toShader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            deviceFunctions.cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);

            avatar.uploadRecorded = true;
            avatar.usedFence = submitFence;
            gui_log::write("hook: texture upload recorded (%dx%d)", avatar.width, avatar.height);
        }

        std::free(const_cast<unsigned char*>(avatar.pixels)); 
        avatar.pixels = nullptr;
        if (!ok)
            gui_log::write("hook: texture upload setup FAILED");
        return;
    }

    
    
    
    if (avatar.usedFence != VK_NULL_HANDLE
        && deviceFunctions.waitForFences(device, 1, &avatar.usedFence, VK_TRUE, 0) != VK_SUCCESS)
        return;
    avatar.usedFence = VK_NULL_HANDLE;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = avatar.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (deviceFunctions.createImageView(device, &viewInfo, nullptr, &avatar.view) != VK_SUCCESS)
        return;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (deviceFunctions.createSampler(device, &samplerInfo, nullptr, &avatar.sampler) != VK_SUCCESS)
        return;

    avatar.descriptor = ImGui_ImplVulkan_AddTexture(avatar.sampler, avatar.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    deviceFunctions.destroyBuffer(device, avatar.staging, nullptr);
    avatar.staging = VK_NULL_HANDLE;
    deviceFunctions.freeMemory(device, avatar.stagingMemory, nullptr);
    avatar.stagingMemory = VK_NULL_HANDLE;
    gui_log::write("hook: texture ready");
}


void destroyTextureState(VkDevice device, AvatarUploadState& avatar, std::atomic<bool>& avatarRequestPending) noexcept
{
    if (device == VK_NULL_HANDLE)
        return;
    if (avatar.descriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(avatar.descriptor);
        avatar.descriptor = VK_NULL_HANDLE;
    }
    if (avatar.sampler != VK_NULL_HANDLE) {
        deviceFunctions.destroySampler(device, avatar.sampler, nullptr);
        avatar.sampler = VK_NULL_HANDLE;
    }
    if (avatar.view != VK_NULL_HANDLE) {
        deviceFunctions.destroyImageView(device, avatar.view, nullptr);
        avatar.view = VK_NULL_HANDLE;
    }
    if (avatar.image != VK_NULL_HANDLE) {
        deviceFunctions.destroyImage(device, avatar.image, nullptr);
        avatar.image = VK_NULL_HANDLE;
    }
    if (avatar.imageMemory != VK_NULL_HANDLE) {
        deviceFunctions.freeMemory(device, avatar.imageMemory, nullptr);
        avatar.imageMemory = VK_NULL_HANDLE;
    }
    if (avatar.staging != VK_NULL_HANDLE) {
        deviceFunctions.destroyBuffer(device, avatar.staging, nullptr);
        avatar.staging = VK_NULL_HANDLE;
    }
    if (avatar.stagingMemory != VK_NULL_HANDLE) {
        deviceFunctions.freeMemory(device, avatar.stagingMemory, nullptr);
        avatar.stagingMemory = VK_NULL_HANDLE;
    }
    if (avatar.pixels) {
        std::free(const_cast<unsigned char*>(avatar.pixels));
        avatar.pixels = nullptr;
    }
    avatarRequestPending.store(false, std::memory_order_release);
    avatar.uploadRecorded = false;
    avatar.usedFence = VK_NULL_HANDLE;
}








struct ShadowUploadState {
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkFence usedFence = VK_NULL_HANDLE; 
    bool uploadRecorded = false;
    bool failed = false;
};

ShadowUploadState shadow;

void processShadowUpload(VkDevice device, VkCommandBuffer commandBuffer, VkFence submitFence) noexcept
{
    if (shadow.descriptor != VK_NULL_HANDLE || shadow.failed || submitFence == VK_NULL_HANDLE)
        return;

    if (!shadow.uploadRecorded) {
        auto* pixels = static_cast<std::uint8_t*>(std::malloc(static_cast<std::size_t>(VulkanHook::shadow_texture::kStampSize)
            * VulkanHook::shadow_texture::kStampSize * 4u));
        if (!pixels) {
            gui_log::write("hook: shadow stamp allocation FAILED (fake shadows stay)");
            shadow.failed = true;
            return;
        }
        VulkanHook::shadow_texture::generateShadowStamp(pixels);

        bool ok = true;
        if (ok) {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = VulkanHook::shadow_texture::kStampSize * VulkanHook::shadow_texture::kStampSize * 4u;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            ok = deviceFunctions.createBuffer(device, &bufferInfo, nullptr, &shadow.staging) == VK_SUCCESS;
        }
        if (ok) {
            VkMemoryRequirements req{};
            deviceFunctions.getBufferMemoryRequirements(device, shadow.staging, &req);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = req.size;
            allocInfo.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            ok = allocInfo.memoryTypeIndex != UINT32_MAX
                && deviceFunctions.allocateMemory(device, &allocInfo, nullptr, &shadow.stagingMemory) == VK_SUCCESS
                && deviceFunctions.bindBufferMemory(device, shadow.staging, shadow.stagingMemory, 0) == VK_SUCCESS;
        }
        if (ok) {
            void* mapped = nullptr;
            ok = deviceFunctions.mapMemory(device, shadow.stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS;
            if (ok) {
                std::memcpy(mapped, pixels, VulkanHook::shadow_texture::kStampSize * VulkanHook::shadow_texture::kStampSize * 4u);
                deviceFunctions.unmapMemory(device, shadow.stagingMemory);
            }
        }
        if (ok) {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.extent = {static_cast<std::uint32_t>(VulkanHook::shadow_texture::kStampSize), static_cast<std::uint32_t>(VulkanHook::shadow_texture::kStampSize), 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ok = deviceFunctions.createImage(device, &imageInfo, nullptr, &shadow.image) == VK_SUCCESS;
        }
        if (ok) {
            VkMemoryRequirements req{};
            deviceFunctions.getImageMemoryRequirements(device, shadow.image, &req);
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = req.size;
            allocInfo.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            ok = allocInfo.memoryTypeIndex != UINT32_MAX
                && deviceFunctions.allocateMemory(device, &allocInfo, nullptr, &shadow.imageMemory) == VK_SUCCESS
                && deviceFunctions.bindImageMemory(device, shadow.image, shadow.imageMemory, 0) == VK_SUCCESS;
        }
        if (ok) {
            VkImageMemoryBarrier toTransfer{};
            toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toTransfer.srcAccessMask = 0;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = shadow.image;
            toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            deviceFunctions.cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {static_cast<std::uint32_t>(VulkanHook::shadow_texture::kStampSize), static_cast<std::uint32_t>(VulkanHook::shadow_texture::kStampSize), 1};
            deviceFunctions.cmdCopyBufferToImage(commandBuffer, shadow.staging, shadow.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            VkImageMemoryBarrier toShader{};
            toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.image = shadow.image;
            toShader.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            deviceFunctions.cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
        }

        std::free(pixels);
        if (!ok) {
            
            if (shadow.staging != VK_NULL_HANDLE) {
                deviceFunctions.destroyBuffer(device, shadow.staging, nullptr);
                shadow.staging = VK_NULL_HANDLE;
            }
            if (shadow.stagingMemory != VK_NULL_HANDLE) {
                deviceFunctions.freeMemory(device, shadow.stagingMemory, nullptr);
                shadow.stagingMemory = VK_NULL_HANDLE;
            }
            if (shadow.image != VK_NULL_HANDLE) {
                deviceFunctions.destroyImage(device, shadow.image, nullptr);
                shadow.image = VK_NULL_HANDLE;
            }
            if (shadow.imageMemory != VK_NULL_HANDLE) {
                deviceFunctions.freeMemory(device, shadow.imageMemory, nullptr);
                shadow.imageMemory = VK_NULL_HANDLE;
            }
            shadow.failed = true;
            gui_log::write("hook: shadow stamp upload setup FAILED (fake shadows stay)");
            return;
        }
        shadow.uploadRecorded = true;
        shadow.usedFence = submitFence;
        gui_log::write("hook: shadow stamp upload recorded");
        return;
    }

    
    
    if (shadow.usedFence != VK_NULL_HANDLE
        && deviceFunctions.waitForFences(device, 1, &shadow.usedFence, VK_TRUE, 0) != VK_SUCCESS)
        return;
    shadow.usedFence = VK_NULL_HANDLE;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadow.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (deviceFunctions.createImageView(device, &viewInfo, nullptr, &shadow.view) != VK_SUCCESS) {
        shadow.failed = true;
        gui_log::write("hook: shadow stamp view creation FAILED (fake shadows stay)");
        return;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (deviceFunctions.createSampler(device, &samplerInfo, nullptr, &shadow.sampler) != VK_SUCCESS) {
        shadow.failed = true;
        gui_log::write("hook: shadow stamp sampler creation FAILED (fake shadows stay)");
        return;
    }

    shadow.descriptor = ImGui_ImplVulkan_AddTexture(shadow.sampler, shadow.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    deviceFunctions.destroyBuffer(device, shadow.staging, nullptr);
    shadow.staging = VK_NULL_HANDLE;
    deviceFunctions.freeMemory(device, shadow.stagingMemory, nullptr);
    shadow.stagingMemory = VK_NULL_HANDLE;
    gui_log::write("hook: shadow stamp ready");
}

void destroyShadowTexture(VkDevice device) noexcept
{
    if (device == VK_NULL_HANDLE)
        return;
    if (shadow.descriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(shadow.descriptor);
        shadow.descriptor = VK_NULL_HANDLE;
    }
    if (shadow.sampler != VK_NULL_HANDLE) {
        deviceFunctions.destroySampler(device, shadow.sampler, nullptr);
        shadow.sampler = VK_NULL_HANDLE;
    }
    if (shadow.view != VK_NULL_HANDLE) {
        deviceFunctions.destroyImageView(device, shadow.view, nullptr);
        shadow.view = VK_NULL_HANDLE;
    }
    if (shadow.image != VK_NULL_HANDLE) {
        deviceFunctions.destroyImage(device, shadow.image, nullptr);
        shadow.image = VK_NULL_HANDLE;
    }
    if (shadow.imageMemory != VK_NULL_HANDLE) {
        deviceFunctions.freeMemory(device, shadow.imageMemory, nullptr);
        shadow.imageMemory = VK_NULL_HANDLE;
    }
    if (shadow.staging != VK_NULL_HANDLE) {
        deviceFunctions.destroyBuffer(device, shadow.staging, nullptr);
        shadow.staging = VK_NULL_HANDLE;
    }
    if (shadow.stagingMemory != VK_NULL_HANDLE) {
        deviceFunctions.freeMemory(device, shadow.stagingMemory, nullptr);
        shadow.stagingMemory = VK_NULL_HANDLE;
    }
    shadow.usedFence = VK_NULL_HANDLE;
    shadow.uploadRecorded = false;
}

SpinLock renderLock;

[[nodiscard]] std::int64_t monotonicMs() noexcept; 
std::int64_t lastSkipLogMs = 0;                    



struct SwapSite {
    volatile std::uint64_t* site;
    std::uint64_t original;
    std::uint64_t replacement;
};

constexpr std::size_t kMaxSwapSites = 256; 
SwapSite swapSites[kMaxSwapSites];
std::size_t swapSiteCount = 0;

using AcquireNextImageKHRFunc = VkResult (*)(VkDevice, VkSwapchainKHR, std::uint64_t, VkSemaphore, VkFence, std::uint32_t*);
using QueuePresentKHRFunc = VkResult (*)(VkQueue, const VkPresentInfoKHR*);
using CreateSwapchainKHRFunc = VkResult (*)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);

AcquireNextImageKHRFunc originalAcquireNextImageKHR = nullptr;
QueuePresentKHRFunc originalQueuePresentKHR = nullptr;
CreateSwapchainKHRFunc originalCreateSwapchainKHR = nullptr;



void cleanupRenderTargets(VkDevice device) noexcept;
[[nodiscard]] VkSemaphore renderImGui(VkQueue queue, const VkPresentInfoKHR* presentInfo) noexcept;



VkResult VKAPI_CALL hkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, std::uint64_t timeout, VkSemaphore semaphore, VkFence fence, std::uint32_t* pImageIndex) noexcept
{
    VkDevice expected = VK_NULL_HANDLE;
    if (gameDevice.compare_exchange_strong(expected, device, std::memory_order_release, std::memory_order_acquire))
        gui_log::write("hook: game device captured %p", reinterpret_cast<void*>(device));
    if (originalAcquireNextImageKHR)
        return originalAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
    gui_log::write("hook: acquire with null original!");
    return VK_ERROR_DEVICE_LOST;
}

VkResult VKAPI_CALL hkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) noexcept
{
    
    
    
    
    
    
    
    gui_log::write("hook: swapchain recreation (extent %ux%u) - syncing",
        pCreateInfo ? pCreateInfo->imageExtent.width : 0, pCreateInfo ? pCreateInfo->imageExtent.height : 0);
    {
        const std::lock_guard guard{renderLock};
        if (PFN_vkDeviceWaitIdle waitIdle = loader.getDeviceProcAddr
                ? reinterpret_cast<PFN_vkDeviceWaitIdle>(loader.getDeviceProcAddr(device, "vkDeviceWaitIdle"))
                : nullptr)
            waitIdle(device);
        cleanupRenderTargets(device);
    }
    swapchainExtent = pCreateInfo ? pCreateInfo->imageExtent : VkExtent2D{};
    
    
    swapchainFormatCaptured = pCreateInfo ? pCreateInfo->imageFormat : VK_FORMAT_UNDEFINED;

    if (originalCreateSwapchainKHR)
        return originalCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    gui_log::write("hook: createSwapchain with null original!");
    return VK_ERROR_INITIALIZATION_FAILED;
}

VkResult VKAPI_CALL hkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) noexcept
{
    
    
    fva::hooks::apply_deferred_link_hide();
    
    
    CrashLogger::trace(0x100);
    const VkSemaphore menuDone = renderImGui(queue, pPresentInfo);
    CrashLogger::trace(0x107);

    
    
    
    
    
    
    VkPresentInfoKHR patched{};
    VkSemaphore waitSemaphores[9]{};
    if (menuDone != VK_NULL_HANDLE && pPresentInfo->waitSemaphoreCount < 8) {
        for (std::uint32_t i = 0; i < pPresentInfo->waitSemaphoreCount; ++i)
            waitSemaphores[i] = pPresentInfo->pWaitSemaphores[i];
        waitSemaphores[pPresentInfo->waitSemaphoreCount] = menuDone;
        patched = *pPresentInfo;
        patched.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount + 1;
        patched.pWaitSemaphores = waitSemaphores;
        pPresentInfo = &patched;
    }

    if (originalQueuePresentKHR) {
        const VkResult result = originalQueuePresentKHR(queue, pPresentInfo);
        CrashLogger::trace(0x108);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            gui_log::write("hook: present returned %d (swapchain recreation refreshes semaphores)", static_cast<int>(result));
        return result;
    }
    gui_log::write("hook: present with null original!");
    return VK_ERROR_DEVICE_LOST;
}





[[nodiscard]] bool isTargetName(const char* name) noexcept
{
    return std::strcmp(name, "vkQueuePresentKHR") == 0
        || std::strcmp(name, "vkAcquireNextImageKHR") == 0
        || std::strcmp(name, "vkCreateSwapchainKHR") == 0;
}




void hookSlotFor(const char* targetName, volatile std::uint64_t* slot) noexcept
{
    if (!slot || *slot == 0)
        return; 

    
    for (std::size_t i = 0; i < swapSiteCount; ++i) {
        if (swapSites[i].site == slot)
            return;
    }
    if (swapSiteCount >= kMaxSwapSites)
        return;

    
    
    const std::uint64_t original = *slot;
    std::uint64_t replacement = 0;
    if (std::strcmp(targetName, "vkQueuePresentKHR") == 0) {
        originalQueuePresentKHR = reinterpret_cast<QueuePresentKHRFunc>(original);
        replacement = reinterpret_cast<std::uint64_t>(&hkQueuePresentKHR);
    } else if (std::strcmp(targetName, "vkAcquireNextImageKHR") == 0) {
        originalAcquireNextImageKHR = reinterpret_cast<AcquireNextImageKHRFunc>(original);
        replacement = reinterpret_cast<std::uint64_t>(&hkAcquireNextImageKHR);
    } else {
        originalCreateSwapchainKHR = reinterpret_cast<CreateSwapchainKHRFunc>(original);
        replacement = reinterpret_cast<std::uint64_t>(&hkCreateSwapchainKHR);
    }

    *slot = replacement;
    swapSites[swapSiteCount++] = SwapSite{slot, original, replacement};
}




[[nodiscard]] std::size_t hookRendererCacheSlots() noexcept
{
    const DynamicLibrary renderer{"librendersystemvulkan.so"};
    if (!static_cast<bool>(renderer)) {
        gui_log::write("tryInstall: librendersystemvulkan not loaded");
        return 0;
    }

    const auto text = renderer.getCodeSection();
    if (text.raw().empty()) {
        gui_log::write("tryInstall: renderer .text section empty");
        return 0;
    }
    gui_log::write("tryInstall: scanning .text (%zu bytes)", text.raw().size());

    HybridPatternFinder finder{text.raw(), BytePattern{vulkan_resolution_chain::kIterationPattern, kPatternStringWildcard}};

    std::size_t iterations = 0;
    std::size_t presentSites = 0;
    
    
    char name[vulkan_resolution_chain::kMaxNameLength];
    char pending[vulkan_resolution_chain::kMaxNameLength]{};
    bool pendingValid = false;
    while (const auto* match = finder.findNextOccurrence()) {
        ++iterations;
        if (pendingValid) {
            const auto before = swapSiteCount;
            hookSlotFor(pending, vulkan_resolution_chain::storeSlot(match));
            if (swapSiteCount > before)
                gui_log::write("tryInstall: hooked %s at slot %p (original %p)",
                    pending, reinterpret_cast<const void*>(const_cast<std::uint64_t*>(swapSites[swapSiteCount - 1].site)),
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(swapSites[swapSiteCount - 1].original)));
            if (std::strcmp(pending, "vkQueuePresentKHR") == 0)
                ++presentSites;
            pendingValid = false;
        }

        vulkan_resolution_chain::readName(match, name);
        if (isTargetName(name)) {
            std::strcpy(pending, name);
            pendingValid = true;
        }
    }
    gui_log::write("tryInstall: chain scan done (%zu iterations, %zu present site(s))", iterations, presentSites);
    return presentSites;
}




void verifySwaps() noexcept
{
    for (std::size_t i = 0; i < swapSiteCount; ++i) {
        if (*swapSites[i].site != swapSites[i].replacement) {
            *swapSites[i].site = swapSites[i].replacement;
            gui_log::write("verify: re-asserted site %p",
                reinterpret_cast<const void*>(const_cast<std::uint64_t*>(swapSites[i].site)));
        }
    }
}








struct PointerCopyScanResult {
    std::size_t sitesSwapped = 0;
    std::uint64_t bytesScanned = 0;
    bool truncatedByBudget = false;
};

[[nodiscard]] PointerCopyScanResult scanWritableMappingsForValue(std::uint64_t needle, std::uint64_t replacement) noexcept
{
    constexpr std::uint64_t kMaxTotalScanBytes = 3ull << 30;  
    constexpr std::uint64_t kMaxMappingScanBytes = 1ull << 30; 

    PointerCopyScanResult result;
    std::uint64_t scannedBytes = 0;

    const auto fd = LinuxPlatformApi::open("/proc/self/maps", O_RDONLY);
    if (fd < 0)
        return result;

    auto handleMapping = [&](std::uintptr_t start, std::uintptr_t end, const char* perms, const char* path) {
        if (perms[1] != 'w')
            return;
        if (!path || std::strstr(path, "libMangoHud") != nullptr)
            return; 

        const std::uint64_t bytes = end - start;
        if (bytes > kMaxMappingScanBytes) {
            result.truncatedByBudget = true;
            return;
        }
        if (scannedBytes + bytes > kMaxTotalScanBytes) {
            result.truncatedByBudget = true;
            return;
        }
        scannedBytes += bytes;

        auto* site = reinterpret_cast<volatile std::uint64_t*>(start);
        auto* const siteEnd = reinterpret_cast<volatile std::uint64_t*>(end);
        for (; site != siteEnd && swapSiteCount < kMaxSwapSites; ++site) {
            if (*site != needle)
                continue;

            
            
            bool alreadyRecorded = false;
            for (std::size_t i = 0; i < swapSiteCount; ++i) {
                if (swapSites[i].site == site) {
                    alreadyRecorded = true;
                    break;
                }
            }
            if (alreadyRecorded)
                continue;

            *site = replacement;
            swapSites[swapSiteCount++] = SwapSite{site, needle, replacement};
            ++result.sitesSwapped;
        }
    };

    
    char line[512];
    std::size_t lineLength = 0;
    std::int64_t fileOffset = 0;
    while (swapSiteCount < kMaxSwapSites) {
        char chunk[4096];
        const auto n = LinuxPlatformApi::pread(fd, chunk, sizeof(chunk), fileOffset);
        if (n <= 0)
            break;
        fileOffset += n;

        for (std::int64_t i = 0; i < n; ++i) {
            const char c = chunk[i];
            if (c == '\n') {
                line[lineLength] = '\0';
                lineLength = 0;

                std::uintptr_t start = 0, end = 0;
                char perms[8] = {};
                if (std::sscanf(line, "%lx-%lx %7s", &start, &end, perms) == 3)
                    handleMapping(start, end, perms, std::strstr(line, "/") ? std::strrchr(line, ' ') + 1 : "");
            } else if (lineLength + 1 < sizeof(line)) {
                line[lineLength++] = c;
            } else {
                lineLength = 0; 
            }
        }
        if (result.truncatedByBudget)
            break;
    }

    LinuxPlatformApi::close(fd);
    result.bytesScanned = scannedBytes;
    return result;
}



PFN_vkVoidFunction VKAPI_CALL imguiLoaderFunc(const char* name, void*) noexcept
{
    
    
    return loader.getInstanceProcAddr(fakeInstance, name);
}

[[nodiscard]] bool createFakeInstanceForImGui() noexcept
{
    constexpr const char* instanceExtension = "VK_KHR_surface";

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.enabledExtensionCount = 1;
    instanceInfo.ppEnabledExtensionNames = &instanceExtension;

#ifndef NDEBUG
    
    
    
    
    {
        constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
        const auto enumerateLayers = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
            loader.getInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties"));
        std::uint32_t layerCount = 0;
        VkLayerProperties layers[16]{};
        if (enumerateLayers && enumerateLayers(&layerCount, nullptr) == VK_SUCCESS && layerCount > 0) {
            if (layerCount > std::size(layers))
                layerCount = static_cast<std::uint32_t>(std::size(layers));
            enumerateLayers(&layerCount, layers);
            for (std::uint32_t i = 0; i < layerCount; ++i) {
                if (std::strcmp(layers[i].layerName, kValidationLayer) == 0) {
                    instanceInfo.enabledLayerCount = 1;
                    instanceInfo.ppEnabledLayerNames = &kValidationLayer;
                    gui_log::write("tryInstall: validation layer enabled (debug build)");
                    break;
                }
            }
        }
    }
#endif

    if (loader.createInstance(&instanceInfo, nullptr, &fakeInstance) != VK_SUCCESS || !fakeInstance)
        return false;

    ImGui_ImplVulkan_LoadFunctions(&imguiLoaderFunc, nullptr);

    fakePhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(fakeInstance);
    if (fakePhysicalDevice == VK_NULL_HANDLE)
        return false;

    queueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(fakePhysicalDevice);
    return true;
}



[[nodiscard]] VkFormat swapchainFormat() noexcept
{
    
    
    
    if (swapchainFormatCaptured != VK_FORMAT_UNDEFINED)
        return swapchainFormatCaptured;
    return gui_sdl::isUsingWayland() ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
}

[[nodiscard]] bool createRenderTarget(VkDevice device, VkSwapchainKHR swapchain) noexcept
{
    
    
    
    
    std::uint32_t imageCount = 0;
    deviceFunctions.getSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    if (imageCount == 0 || imageCount > kMaxFrames) {
        gui_log::write("hook: unsupported swapchain image count %u", imageCount);
        return false;
    }

    VkImage backbuffers[kMaxFrames]{};
    deviceFunctions.getSwapchainImagesKHR(device, swapchain, &imageCount, backbuffers);

    const VkFormat format = swapchainFormat();

    VkCommandPool commandPools[kMaxFrames]{};
    VkCommandBuffer commandBuffers[kMaxFrames]{};
    VkImageView views[kMaxFrames]{};
    VkFramebuffer framebuffers[kMaxFrames]{};
    VkSemaphore semaphores[kMaxFrames]{};
    VkRenderPass newRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool newDescriptorPool = VK_NULL_HANDLE;

    auto fail = [&](const char* step) noexcept {
        gui_log::write("hook: createRenderTarget FAILED at %s (attempt cleaned up)", step);
        for (std::uint32_t i = 0; i < imageCount; ++i) {
            if (semaphores[i])
                deviceFunctions.destroySemaphore(device, semaphores[i], nullptr);
            if (framebuffers[i])
                deviceFunctions.destroyFramebuffer(device, framebuffers[i], nullptr);
            if (views[i])
                deviceFunctions.destroyImageView(device, views[i], nullptr);
            if (commandBuffers[i])
                deviceFunctions.freeCommandBuffers(device, commandPools[i], 1, &commandBuffers[i]);
            if (commandPools[i])
                deviceFunctions.destroyCommandPool(device, commandPools[i], nullptr);
        }
        if (newDescriptorPool)
            deviceFunctions.destroyDescriptorPool(device, newDescriptorPool, nullptr);
        if (newRenderPass)
            deviceFunctions.destroyRenderPass(device, newRenderPass, nullptr);
        return false;
    };

    for (std::uint32_t i = 0; i < imageCount; ++i) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily;
        if (deviceFunctions.createCommandPool(device, &poolInfo, nullptr, &commandPools[i]) != VK_SUCCESS)
            return fail("command pool");

        VkCommandBufferAllocateInfo cmdInfo{};
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdInfo.commandPool = commandPools[i];
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        if (deviceFunctions.allocateCommandBuffers(device, &cmdInfo, &commandBuffers[i]) != VK_SUCCESS)
            return fail("command buffer");
    }

    
    
    
    
    
    
    
    
    
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachment{};
    colorAttachment.attachment = 0;
    colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachment;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (deviceFunctions.createRenderPass(device, &renderPassInfo, nullptr, &newRenderPass) != VK_SUCCESS)
        return fail("render pass");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    for (std::uint32_t i = 0; i < imageCount; ++i) {
        viewInfo.image = backbuffers[i];
        if (deviceFunctions.createImageView(device, &viewInfo, nullptr, &views[i]) != VK_SUCCESS)
            return fail("image view");
    }

    VkImageView framebufferAttachments[1] = {};
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = newRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = framebufferAttachments;
    framebufferInfo.layers = 1;

    for (std::uint32_t i = 0; i < imageCount; ++i) {
        framebufferAttachments[0] = views[i];
        if (deviceFunctions.createFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
            return fail("framebuffer");
    }

    
    
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (std::uint32_t i = 0; i < imageCount; ++i) {
        if (deviceFunctions.createSemaphore(device, &semaphoreInfo, nullptr, &semaphores[i]) != VK_SUCCESS)
            return fail("semaphore");
    }

    
    constexpr VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; 
    poolInfo.maxSets = poolSize.descriptorCount;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (deviceFunctions.createDescriptorPool(device, &poolInfo, nullptr, &newDescriptorPool) != VK_SUCCESS)
        return fail("descriptor pool");

    
    
    
    for (auto& fence : uploadSlotFences) {
        if (fence != VK_NULL_HANDLE)
            continue;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (deviceFunctions.createFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
            fence = VK_NULL_HANDLE;
            gui_log::write("hook: upload slot fence creation failed (sync degraded)");
        }
    }

    
    
    if (renderPass != VK_NULL_HANDLE && retiredCount < kMaxFrames) {
        retiredRenderPasses[retiredCount] = renderPass;
        retiredDescriptorPools[retiredCount] = descriptorPool;
        ++retiredCount;
    }
    renderPass = newRenderPass;
    descriptorPool = newDescriptorPool;
    for (std::uint32_t i = 0; i < imageCount; ++i) {
        frames[i].frame.Backbuffer = backbuffers[i];
        frames[i].frame.CommandPool = commandPools[i];
        frames[i].frame.CommandBuffer = commandBuffers[i];
        frames[i].frame.BackbufferView = views[i];
        frames[i].frame.Framebuffer = framebuffers[i];
        frames[i].renderCompleteSemaphore = semaphores[i];
    }
    renderTargetsCreated = true;
    return true;
}

void cleanupRenderTargets(VkDevice device) noexcept
{
    if (device == VK_NULL_HANDLE)
        return;

    renderTargetsCreated = false;

    for (auto& frameResource : frames) {
        auto& frame = frameResource.frame;
        if (frame.CommandBuffer) {
            deviceFunctions.freeCommandBuffers(device, frame.CommandPool, 1, &frame.CommandBuffer);
            frame.CommandBuffer = VK_NULL_HANDLE;
        }
        if (frame.CommandPool) {
            deviceFunctions.destroyCommandPool(device, frame.CommandPool, nullptr);
            frame.CommandPool = VK_NULL_HANDLE;
        }
        if (frame.BackbufferView) {
            deviceFunctions.destroyImageView(device, frame.BackbufferView, nullptr);
            frame.BackbufferView = VK_NULL_HANDLE;
        }
        if (frame.Framebuffer) {
            deviceFunctions.destroyFramebuffer(device, frame.Framebuffer, nullptr);
            frame.Framebuffer = VK_NULL_HANDLE;
        }
        if (frameResource.renderCompleteSemaphore) {
            deviceFunctions.destroySemaphore(device, frameResource.renderCompleteSemaphore, nullptr);
            frameResource.renderCompleteSemaphore = VK_NULL_HANDLE;
        }
    }

    
    
    
    
    
    auto drainUploadFence = [&](VkFence& fence, const char* what) noexcept {
        if (fence == VK_NULL_HANDLE)
            return;
        if (deviceFunctions.waitForFences(device, 1, &fence, VK_TRUE, 50'000'000) != VK_SUCCESS)
            gui_log::write("cleanup: %s upload fence wait failed (finalize retries)", what);
        fence = VK_NULL_HANDLE;
    };
    if (avatar.uploadRecorded && avatar.descriptor == VK_NULL_HANDLE)
        drainUploadFence(avatar.usedFence, "avatar");
    if (shadow.uploadRecorded && shadow.descriptor == VK_NULL_HANDLE)
        drainUploadFence(shadow.usedFence, "shadow");

    
    
    for (auto& fence : uploadSlotFences) {
        if (fence != VK_NULL_HANDLE) {
            deviceFunctions.destroyFence(device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
    }
}






[[nodiscard]] VkSemaphore renderImGui(VkQueue queue, const VkPresentInfoKHR* presentInfo) noexcept
{
    VkSemaphore menuDone = VK_NULL_HANDLE;
    if (HookQuiesce::isShuttingDown())
        return VK_NULL_HANDLE;

    HookQuiesce::InFlight flight;

    const VkDevice device = gameDevice.load(std::memory_order_acquire);
    if (device == VK_NULL_HANDLE || !GUI::isInitialized())
        return VK_NULL_HANDLE;

    if (!deviceFunctionsResolved) {
        
        deviceFunctions.getSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(loader.getDeviceProcAddr(device, "vkGetSwapchainImagesKHR"));
        deviceFunctions.createImageView = reinterpret_cast<PFN_vkCreateImageView>(loader.getDeviceProcAddr(device, "vkCreateImageView"));
        deviceFunctions.destroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(loader.getDeviceProcAddr(device, "vkDestroyImageView"));
        deviceFunctions.createFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(loader.getDeviceProcAddr(device, "vkCreateFramebuffer"));
        deviceFunctions.destroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(loader.getDeviceProcAddr(device, "vkDestroyFramebuffer"));
        deviceFunctions.createCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(loader.getDeviceProcAddr(device, "vkCreateCommandPool"));
        deviceFunctions.destroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(loader.getDeviceProcAddr(device, "vkDestroyCommandPool"));
        deviceFunctions.allocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(loader.getDeviceProcAddr(device, "vkAllocateCommandBuffers"));
        deviceFunctions.freeCommandBuffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(loader.getDeviceProcAddr(device, "vkFreeCommandBuffers"));
        deviceFunctions.createRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(loader.getDeviceProcAddr(device, "vkCreateRenderPass"));
        deviceFunctions.destroyRenderPass = reinterpret_cast<PFN_vkDestroyRenderPass>(loader.getDeviceProcAddr(device, "vkDestroyRenderPass"));
        deviceFunctions.createDescriptorPool = reinterpret_cast<PFN_vkCreateDescriptorPool>(loader.getDeviceProcAddr(device, "vkCreateDescriptorPool"));
        deviceFunctions.destroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(loader.getDeviceProcAddr(device, "vkDestroyDescriptorPool"));
        deviceFunctions.resetCommandBuffer = reinterpret_cast<PFN_vkResetCommandBuffer>(loader.getDeviceProcAddr(device, "vkResetCommandBuffer"));
        deviceFunctions.beginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(loader.getDeviceProcAddr(device, "vkBeginCommandBuffer"));
        deviceFunctions.endCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(loader.getDeviceProcAddr(device, "vkEndCommandBuffer"));
        deviceFunctions.cmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(loader.getDeviceProcAddr(device, "vkCmdBeginRenderPass"));
        deviceFunctions.cmdEndRenderPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(loader.getDeviceProcAddr(device, "vkCmdEndRenderPass"));
        deviceFunctions.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(loader.getDeviceProcAddr(device, "vkQueueSubmit"));
        deviceFunctions.createFence = reinterpret_cast<PFN_vkCreateFence>(loader.getDeviceProcAddr(device, "vkCreateFence"));
        deviceFunctions.waitForFences = reinterpret_cast<PFN_vkWaitForFences>(loader.getDeviceProcAddr(device, "vkWaitForFences"));
        deviceFunctions.resetFences = reinterpret_cast<PFN_vkResetFences>(loader.getDeviceProcAddr(device, "vkResetFences"));
        deviceFunctions.destroyFence = reinterpret_cast<PFN_vkDestroyFence>(loader.getDeviceProcAddr(device, "vkDestroyFence"));
        deviceFunctions.createImage = reinterpret_cast<PFN_vkCreateImage>(loader.getDeviceProcAddr(device, "vkCreateImage"));
        deviceFunctions.destroyImage = reinterpret_cast<PFN_vkDestroyImage>(loader.getDeviceProcAddr(device, "vkDestroyImage"));
        deviceFunctions.getImageMemoryRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(loader.getDeviceProcAddr(device, "vkGetImageMemoryRequirements"));
        deviceFunctions.bindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(loader.getDeviceProcAddr(device, "vkBindImageMemory"));
        deviceFunctions.allocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(loader.getDeviceProcAddr(device, "vkAllocateMemory"));
        deviceFunctions.freeMemory = reinterpret_cast<PFN_vkFreeMemory>(loader.getDeviceProcAddr(device, "vkFreeMemory"));
        deviceFunctions.mapMemory = reinterpret_cast<PFN_vkMapMemory>(loader.getDeviceProcAddr(device, "vkMapMemory"));
        deviceFunctions.unmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(loader.getDeviceProcAddr(device, "vkUnmapMemory"));
        deviceFunctions.createBuffer = reinterpret_cast<PFN_vkCreateBuffer>(loader.getDeviceProcAddr(device, "vkCreateBuffer"));
        deviceFunctions.destroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(loader.getDeviceProcAddr(device, "vkDestroyBuffer"));
        deviceFunctions.getBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(loader.getDeviceProcAddr(device, "vkGetBufferMemoryRequirements"));
        deviceFunctions.bindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(loader.getDeviceProcAddr(device, "vkBindBufferMemory"));
        deviceFunctions.cmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(loader.getDeviceProcAddr(device, "vkCmdPipelineBarrier"));
        deviceFunctions.cmdCopyBufferToImage = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(loader.getDeviceProcAddr(device, "vkCmdCopyBufferToImage"));
        deviceFunctions.createSampler = reinterpret_cast<PFN_vkCreateSampler>(loader.getDeviceProcAddr(device, "vkCreateSampler"));
        deviceFunctions.destroySampler = reinterpret_cast<PFN_vkDestroySampler>(loader.getDeviceProcAddr(device, "vkDestroySampler"));
        deviceFunctions.getPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(loader.getInstanceProcAddr(fakeInstance, "vkGetPhysicalDeviceMemoryProperties"));
        deviceFunctions.deviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(loader.getDeviceProcAddr(device, "vkDeviceWaitIdle"));
        deviceFunctions.createSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(loader.getDeviceProcAddr(device, "vkCreateSemaphore"));
        deviceFunctions.destroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(loader.getDeviceProcAddr(device, "vkDestroySemaphore"));
        if (!deviceFunctions.complete()) {
            StatusReport::record("VulkanHook: device function resolution failed", false);
            return VK_NULL_HANDLE;
        }
        deviceFunctionsResolved = true;
        gui_log::write("hook: device functions resolved");
        StatusReport::record("VulkanHook: game device captured, renderer functions resolved", true);
    }

    verifySwaps();

    
    
    const std::lock_guard guard{renderLock};

    for (std::uint32_t i = 0; i < presentInfo->swapchainCount; ++i) {
        const VkSwapchainKHR swapchain = presentInfo->pSwapchains[i];
        if (!renderTargetsCreated) {
            gui_log::write("hook: creating render targets (swapchain %p, format %d%s)",
                reinterpret_cast<void*>(swapchain), static_cast<int>(swapchainFormat()),
                swapchainFormatCaptured != VK_FORMAT_UNDEFINED ? "" : " - GUESSED, no create call captured");
            if (!createRenderTarget(device, swapchain)) {
                gui_log::write("hook: createRenderTarget FAILED");
                continue;
            }
            gui_log::write("hook: render targets created");
        }
        
        
        
        
        
        if (swapchainExtent.width == 0) {
            const auto [w, h] = ImGui::GetIO().DisplaySize;
            const auto [sx, sy] = ImGui::GetIO().DisplayFramebufferScale;
            const float fx = (sx > 0.0f && sx < 8.0f) ? sx : 1.0f;
            const float fy = (sy > 0.0f && sy < 8.0f) ? sy : 1.0f;
            swapchainExtent = {static_cast<std::uint32_t>(w * fx + 0.5f), static_cast<std::uint32_t>(h * fy + 0.5f)};
        }

        const std::uint32_t imageIndex = presentInfo->pImageIndices[i];
        if (imageIndex >= kMaxFrames) {
            
            
            if (GUI::isMenuOpen() && monotonicMs() - lastSkipLogMs > 1000) {
                lastSkipLogMs = monotonicMs();
                gui_log::write("hook: present skipped - imageIndex %u >= kMaxFrames (menu flicker source)", imageIndex);
            }
            continue;
        }

        auto* fd = &frames[imageIndex].frame;
        VkSemaphore frameDone = frames[imageIndex].renderCompleteSemaphore;
        if (frameDone == VK_NULL_HANDLE) {
            gui_log::write("hook: no render-complete semaphore for image %u - skipping frame", imageIndex);
            continue;
        }
        VkPipelineStageFlags menuWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        
        
        
        
        
        
        
        
        constexpr bool kChainIntoGameSemaphores = false;
        VkSemaphore waitAll[2] = {};
        std::uint32_t waitCount = 0;
        if constexpr (kChainIntoGameSemaphores) {
            if (presentInfo->waitSemaphoreCount == 1) {
                waitAll[0] = presentInfo->pWaitSemaphores[0];
                waitAll[1] = frameDone;
                waitCount = 2;
            }
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        if (waitCount > 0) {
            submitInfo.waitSemaphoreCount = waitCount;
            submitInfo.pWaitSemaphores = waitAll;
            submitInfo.pWaitDstStageMask = &menuWaitStage;
        }
        
        
        
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &frameDone;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        deviceFunctions.resetCommandBuffer(fd->CommandBuffer, 0);
        deviceFunctions.beginCommandBuffer(fd->CommandBuffer, &beginInfo);

        
        
        
        VkExtent2D extent = swapchainExtent;
        if (extent.width == 0 || extent.height == 0) {
            
            const auto [width, height] = ImGui::GetIO().DisplaySize;
            const auto [sx, sy] = ImGui::GetIO().DisplayFramebufferScale;
            const float fx = (sx > 0.0f && sx < 8.0f) ? sx : 1.0f;
            const float fy = (sy > 0.0f && sy < 8.0f) ? sy : 1.0f;
            extent = {static_cast<std::uint32_t>(width * fx + 0.5f), static_cast<std::uint32_t>(height * fy + 0.5f)};
        }
        
        
        constexpr std::uint64_t kTraceFenceWait = 0x101;
        constexpr std::uint64_t kTraceAvatar = 0x102;
        constexpr std::uint64_t kTraceBeginPass = 0x103;
        constexpr std::uint64_t kTraceRendererInit = 0x104;
        constexpr std::uint64_t kTraceGuiRender = 0x105;
        constexpr std::uint64_t kTraceSubmit = 0x106;

        
        
        
        
        const ImGuiIO& imguiIo = ImGui::GetIO();
        const bool willRender = imguiIo.DisplaySize.x * imguiIo.DisplayFramebufferScale.x > 0.0f
            && imguiIo.DisplaySize.y * imguiIo.DisplayFramebufferScale.y > 0.0f;
        const std::size_t uploadSlot = (menuUploadFrame + 1) % kMaxFrames;
        VkFence slotFence = VK_NULL_HANDLE;
        if (willRender) {
            slotFence = uploadSlotFences[uploadSlot];
            if (slotFence != VK_NULL_HANDLE) {
                
                
                if (deviceFunctions.waitForFences(device, 1, &slotFence, VK_TRUE, 5'000'000) != VK_SUCCESS) {
                    gui_log::write("hook: upload slot fence wait timed out (slot %zu)", uploadSlot);
                    slotFence = VK_NULL_HANDLE; 
                } else {
                    deviceFunctions.resetFences(device, 1, &slotFence);
                }
            }
        }
        CrashLogger::trace(kTraceFenceWait);

        
        
        if (willRender && rendererInitialized) {
            processTextureUpload(device, fd->CommandBuffer, slotFence, music, musicRequestPending);
            processTextureUpload(device, fd->CommandBuffer, slotFence, avatar, avatarRequestPending);
            CrashLogger::trace(kTraceAvatar);
            processTextureUpload(device, fd->CommandBuffer, slotFence, logo, logoRequestPending);
            for (int i = 0; i < kMaxLuaTextures; ++i)
                processTextureUpload(device, fd->CommandBuffer, slotFence, luaTextures[i], luaTexturePending[i]);
            processShadowUpload(device, fd->CommandBuffer, slotFence);
        }

        VkRenderPassBeginInfo renderPassBegin{};
        renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBegin.renderPass = renderPass;
        renderPassBegin.framebuffer = fd->Framebuffer;
        renderPassBegin.renderArea.extent = extent;
        deviceFunctions.cmdBeginRenderPass(fd->CommandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
        CrashLogger::trace(kTraceBeginPass);

        if (!rendererInitialized) {
            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.Instance = fakeInstance;
            initInfo.PhysicalDevice = fakePhysicalDevice;
            initInfo.Device = device;
            initInfo.QueueFamily = queueFamily;
            initInfo.Queue = queue;
            initInfo.PipelineCache = VK_NULL_HANDLE;
            initInfo.DescriptorPool = descriptorPool;
            initInfo.Subpass = 0;
            initInfo.MinImageCount = 2;
            
            
            
            
            
            
            
            
            initInfo.ImageCount = kMaxFrames;
            initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            initInfo.RenderPass = renderPass;
            rendererInitialized = ImGui_ImplVulkan_Init(&initInfo);
            if (!rendererInitialized) {
                StatusReport::record("VulkanHook: ImGui_ImplVulkan_Init failed", false);
                gui_log::write("hook: ImGui_ImplVulkan_Init FAILED");
                deviceFunctions.cmdEndRenderPass(fd->CommandBuffer);
                deviceFunctions.endCommandBuffer(fd->CommandBuffer);
                continue;
            }
            gui_log::write("hook: ImGui Vulkan renderer initialized");
            StatusReport::record("VulkanHook: ImGui Vulkan renderer initialized", true);
        }
        CrashLogger::trace(kTraceRendererInit);

        const bool rendered = GUI::render(fd->CommandBuffer);
        if (rendered && willRender)
            ++menuUploadFrame;
        CrashLogger::trace(kTraceGuiRender);

        deviceFunctions.cmdEndRenderPass(fd->CommandBuffer);
        deviceFunctions.endCommandBuffer(fd->CommandBuffer);

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &fd->CommandBuffer;
        const VkFence submitFence = rendered && willRender ? slotFence : VK_NULL_HANDLE;
        const VkResult submitted = deviceFunctions.queueSubmit(queue, 1, &submitInfo, submitFence);
        CrashLogger::trace(kTraceSubmit);
        
        
        if (submitted != VK_SUCCESS) {
            gui_log::write("hook: menu submit failed (%d) - presenting without our wait", static_cast<int>(submitted));
            continue;
        }
        if (menuDone == VK_NULL_HANDLE)
            menuDone = frameDone; 
    }
    return menuDone;
}

[[nodiscard]] std::int64_t monotonicMs() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1'000'000;
}

} 



void VulkanHook::logo_texture::request(const void* pixelsRgba, int width, int height) noexcept
{
    if (logo.descriptor != VK_NULL_HANDLE || logo.uploadRecorded || width <= 0 || height <= 0) {
        std::free(const_cast<void*>(pixelsRgba)); 
        return;
    }
    logo.pixels = static_cast<const unsigned char*>(pixelsRgba);
    logo.width = width;
    logo.height = height;
    logoRequestPending.store(true, std::memory_order_release);
}

void* VulkanHook::logo_texture::query() noexcept
{
    return logo.descriptor != VK_NULL_HANDLE ? logo.descriptor : nullptr;
}

void VulkanHook::music_texture::request(const void* pixelsRgba, int width, int height) noexcept
{
    if (music.descriptor != VK_NULL_HANDLE || music.uploadRecorded || width <= 0 || height <= 0) {
        std::free(const_cast<void*>(pixelsRgba));
        return;
    }
    music.pixels = static_cast<const unsigned char*>(pixelsRgba);
    music.width = width;
    music.height = height;
    musicRequestPending.store(true, std::memory_order_release);
}

void* VulkanHook::music_texture::query() noexcept
{
    return music.descriptor;
}

void VulkanHook::music_texture::release() noexcept
{
    if (music.descriptor == VK_NULL_HANDLE && !music.uploadRecorded && music.pixels == nullptr)
        return;
    // Covers change infrequently. Wait for previous draws before freeing the old image,
    // rather than retaining every past album until the overlay shuts down.
    waitUntilDeviceIdle();
    destroyTextureState(gameDevice.load(std::memory_order_acquire), music, musicRequestPending);
}

void VulkanHook::avatar_texture::request(const void* pixelsRgba, int width, int height) noexcept
{
    if (avatar.descriptor != VK_NULL_HANDLE || avatar.uploadRecorded || width <= 0 || height <= 0) {
        std::free(const_cast<void*>(pixelsRgba));
        return;
    }
    avatar.pixels = static_cast<const unsigned char*>(pixelsRgba);
    avatar.width = width;
    avatar.height = height;
    avatarRequestPending.store(true, std::memory_order_release);
}

void* VulkanHook::avatar_texture::query() noexcept
{
    return avatar.descriptor;
}

void* VulkanHook::shadow_texture::query() noexcept
{
    return shadow.descriptor;
}



void VulkanHook::lua_texture::request(const int index, const void* pixelsRgba, const int width, const int height) noexcept
{
    if (index < 0 || index >= kMaxLuaTextures || width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        std::free(const_cast<void*>(pixelsRgba));
        return;
    }
    AvatarUploadState& state = luaTextures[index];
    if (state.descriptor != VK_NULL_HANDLE || state.uploadRecorded) {
        std::free(const_cast<void*>(pixelsRgba)); 
        return;
    }
    state.pixels = static_cast<const unsigned char*>(pixelsRgba);
    state.width = width;
    state.height = height;
    luaTexturePending[index].store(true, std::memory_order_release);
}

void* VulkanHook::lua_texture::query(const int index) noexcept
{
    if (index < 0 || index >= kMaxLuaTextures)
        return nullptr;
    return luaTextures[index].descriptor != VK_NULL_HANDLE ? luaTextures[index].descriptor : nullptr;
}

void VulkanHook::lua_texture::release(const int index) noexcept
{
    if (index < 0 || index >= kMaxLuaTextures)
        return;
    AvatarUploadState& state = luaTextures[index];
    if (state.descriptor == VK_NULL_HANDLE && !state.uploadRecorded && state.pixels == nullptr)
        return; 
    
    
    
    
    RetiredTexture& retired = retiredLuaTextures[retiredCursor];
    retiredCursor = (retiredCursor + 1) % kMaxRetiredTextures;
    if (retired.used) {
        
        std::free(const_cast<unsigned char*>(retired.state.pixels));
        retired.state.pixels = nullptr;
    }
    retired.used = true;
    retired.state = state;
    state = AvatarUploadState{};
    luaTexturePending[index].store(false, std::memory_order_release);
}



bool VulkanHook::tryInstall() noexcept
{
    static std::atomic<bool> installed{false};
    static std::atomic<bool> trying{false};
    constexpr std::int64_t kRetryIntervalMs = 250;

    if (installed.load(std::memory_order_acquire)) {
        verifySwaps(); 
        return true;
    }
    if (HookQuiesce::isShuttingDown())
        return false; 
    if (!GUI::isInitialized())
        return false; 

    const std::int64_t now = monotonicMs();
    static std::atomic<std::int64_t> lastAttempt{-kRetryIntervalMs};
    const std::int64_t last = lastAttempt.load(std::memory_order_acquire);
    if (now - last < kRetryIntervalMs)
        return false;
    std::int64_t expectedLast = last;
    if (!lastAttempt.compare_exchange_strong(expectedLast, now, std::memory_order_acq_rel))
        return false;
    gui_log::write("tryInstall: attempt begin");

    bool expected = false;
    if (!trying.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return installed.load(std::memory_order_acquire); 

    if (!loader.createInstance) {
        const DynamicLibrary vulkan{cs2::VULKAN_DLL};
        if (!static_cast<bool>(vulkan)) {
            gui_log::write("tryInstall: libvulkan not loaded yet");
            trying.store(false, std::memory_order_release);
            return false; 
        }
        loader.getInstanceProcAddr = vulkan.getFunctionAddress("vkGetInstanceProcAddr").as<PFN_vkGetInstanceProcAddr>();
        loader.getDeviceProcAddr = vulkan.getFunctionAddress("vkGetDeviceProcAddr").as<PFN_vkGetDeviceProcAddr>();
        loader.createInstance = vulkan.getFunctionAddress("vkCreateInstance").as<PFN_vkCreateInstance>();
        loader.destroyInstance = vulkan.getFunctionAddress("vkDestroyInstance").as<PFN_vkDestroyInstance>();
        if (!loader.getInstanceProcAddr || !loader.getDeviceProcAddr || !loader.createInstance || !loader.destroyInstance) {
            StatusReport::record("VulkanHook: libvulkan exports missing", false);
            trying.store(false, std::memory_order_release);
            return false;
        }
    }

    if (!fakeInstance && !createFakeInstanceForImGui()) {
        if (fakeInstance) {
            loader.destroyInstance(fakeInstance, nullptr);
            fakeInstance = VK_NULL_HANDLE;
        }
        StatusReport::record("VulkanHook: ImGui instance bootstrap failed", false);
        gui_log::write("tryInstall: FAILED - ImGui instance bootstrap");
        trying.store(false, std::memory_order_release);
        return false;
    }
    gui_log::write("tryInstall: fake instance ready (PD %p, queue family %u)",
        reinterpret_cast<void*>(fakePhysicalDevice), queueFamily);

    
    
    
    const std::size_t presentSites = hookRendererCacheSlots();
    if (presentSites == 0) {
        
        
        
        restorePointers();
        trying.store(false, std::memory_order_release);
        return false;
    }

    
    
    
    
    const auto presentCopies = scanWritableMappingsForValue(reinterpret_cast<std::uint64_t>(originalQueuePresentKHR), reinterpret_cast<std::uint64_t>(&hkQueuePresentKHR));
    const auto acquireCopies = scanWritableMappingsForValue(reinterpret_cast<std::uint64_t>(originalAcquireNextImageKHR), reinterpret_cast<std::uint64_t>(&hkAcquireNextImageKHR));
    const auto createCopies = scanWritableMappingsForValue(reinterpret_cast<std::uint64_t>(originalCreateSwapchainKHR), reinterpret_cast<std::uint64_t>(&hkCreateSwapchainKHR));
    gui_log::write("tryInstall: copy scan: present=%d acquire=%d createSwapchain=%d extra site(s), %d MB scanned%s",
        static_cast<int>(presentCopies.sitesSwapped), static_cast<int>(acquireCopies.sitesSwapped), static_cast<int>(createCopies.sitesSwapped),
        static_cast<int>((presentCopies.bytesScanned + acquireCopies.bytesScanned + createCopies.bytesScanned) >> 20),
        (presentCopies.truncatedByBudget || acquireCopies.truncatedByBudget || createCopies.truncatedByBudget) ? " (BUDGET TRUNCATED)" : "");

    installed.store(true, std::memory_order_release);
    trying.store(false, std::memory_order_release);

    gui_log::write("tryInstall: INSTALLED (%zu present cache site(s), %d total site(s))", presentSites, static_cast<int>(swapSiteCount));
    StatusReport::record("VulkanHook: presentation path hooked (pointer-cache swap)", true);
    VerifyConsole::write(2.0f, "vulkan", "hooked: %d present cache slot(s)", static_cast<int>(presentSites));
    return true;
}

void VulkanHook::restorePointers() noexcept
{
    while (swapSiteCount > 0) {
        const auto& site = swapSites[--swapSiteCount];
        *site.site = site.original;
    }
}

void VulkanHook::waitUntilDeviceIdle() noexcept
{
    const VkDevice device = gameDevice.load(std::memory_order_acquire);
    if (device == VK_NULL_HANDLE || !deviceFunctionsResolved)
        return;
    deviceFunctions.deviceWaitIdle(device);
}

void VulkanHook::destroyResources() noexcept
{
    
    
    destroyTextureState(gameDevice.load(std::memory_order_acquire), music, musicRequestPending);
    destroyTextureState(gameDevice.load(std::memory_order_acquire), avatar, avatarRequestPending);
    destroyTextureState(gameDevice.load(std::memory_order_acquire), logo, logoRequestPending);
    for (int i = 0; i < kMaxLuaTextures; ++i)
        destroyTextureState(gameDevice.load(std::memory_order_acquire), luaTextures[i], luaTexturePending[i]);
    destroyShadowTexture(gameDevice.load(std::memory_order_acquire));
    
    
    
    
    for (auto& retired : retiredLuaTextures) {
        if (retired.used) {
            std::atomic<bool> dummyPending{false};
            destroyTextureState(gameDevice.load(std::memory_order_acquire), retired.state, dummyPending);
            retired.used = false;
        }
    }

    
    
    
    if (rendererInitialized) {
        ImGui_ImplVulkan_Shutdown();
        rendererInitialized = false;
    }

    const VkDevice device = gameDevice.load(std::memory_order_acquire);
    if (device != VK_NULL_HANDLE) {
        if (PFN_vkDeviceWaitIdle waitIdle = loader.getDeviceProcAddr
                ? reinterpret_cast<PFN_vkDeviceWaitIdle>(loader.getDeviceProcAddr(device, "vkDeviceWaitIdle"))
                : nullptr)
            waitIdle(device);
        cleanupRenderTargets(device);
        
        
        
        for (std::size_t i = 0; i < retiredCount; ++i) {
            if (retiredRenderPasses[i]) {
                deviceFunctions.destroyRenderPass(device, retiredRenderPasses[i], nullptr);
                retiredRenderPasses[i] = VK_NULL_HANDLE;
            }
            if (retiredDescriptorPools[i]) {
                deviceFunctions.destroyDescriptorPool(device, retiredDescriptorPools[i], nullptr);
                retiredDescriptorPools[i] = VK_NULL_HANDLE;
            }
        }
        retiredCount = 0;
        if (renderPass) {
            deviceFunctions.destroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
        if (descriptorPool) {
            deviceFunctions.destroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }
    }

    if (fakeInstance) {
        loader.destroyInstance(fakeInstance, nullptr);
        fakeInstance = VK_NULL_HANDLE;
        fakePhysicalDevice = VK_NULL_HANDLE;
    }

    gameDevice.store(VK_NULL_HANDLE, std::memory_order_release);
}
