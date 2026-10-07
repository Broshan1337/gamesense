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

// Vulkan presentation hook, pointer-cache-swap style - see VulkanHook.h for the full story.
// Nothing links against libvulkan: the ImGui backend resolves its function set through
// ImGui_ImplVulkan_LoadFunctions (IMGUI_IMPL_VULKAN_NO_PROTOTYPES), device functions come from
// vkGetDeviceProcAddr on the game's captured device (dlsym'd loader export), and the hooked
// pointers themselves are read straight out of the game's own cache. Every call goes through a
// pointer.
namespace
{

struct LoaderFunctions {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
};

LoaderFunctions loader;

// Fake instance + physical device stay alive for ImGui_ImplVulkan's lifetime (it wants handles;
// it operates on the game's real device).
VkInstance fakeInstance = VK_NULL_HANDLE;
VkPhysicalDevice fakePhysicalDevice = VK_NULL_HANDLE;
std::uint32_t queueFamily = 0;

std::atomic<VkDevice> gameDevice{VK_NULL_HANDLE};

// Everything the present-path code calls on the game's device.
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
    // Instance-level (the fake instance enumerates the real GPU on single-GPU systems).
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

// Our own render targets for the game's swapchain, indexed by backbuffer index. 10 slots
// comfortably exceeds any realizable swapchain depth (CS2 asks for 3-4); if a swapchain ever
// reports more, createRenderTarget fails loudly instead of silently skipping frame indices.
constexpr std::size_t kMaxFrames = 10;
struct FrameResources {
    ImGui_ImplVulkanH_Frame frame{};
    VkSemaphore renderCompleteSemaphore = VK_NULL_HANDLE; // signaled after OUR pass, waited by present
};
FrameResources frames[kMaxFrames]{};
// Committed only on success (see createRenderTarget): the ImGui backend captures the render
// pass (its pipelines) and the descriptor pool (its texture sets) at Init, so a failed retry
// attempt must never clobber the objects the backend still references. Render-target
// generations retired by swapchain recreations stay alive for the same reason, until teardown.
VkRenderPass renderPass = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkRenderPass retiredRenderPasses[kMaxFrames]{};
VkDescriptorPool retiredDescriptorPools[kMaxFrames]{};
std::size_t retiredCount = 0;
bool renderTargetsCreated = false;
bool rendererInitialized = false;
VkExtent2D swapchainExtent{};                            // from the game's create info; render pass must match it exactly
VkFormat swapchainFormatCaptured = VK_FORMAT_UNDEFINED;  // from the game's create info; UNDEFINED = no create call seen yet

// Upload-buffer sync. ImGui_ImplVulkan rotates its vertex/index upload buffers over kMaxFrames
// slots (initInfo.ImageCount) - one memcpy into the current slot per rendered frame. Unchained
// submission lets many menu submits be queued at once (GPU hitch, Steam overlay depth), and a
// fresh memcpy (or worse, a resize's vkDestroyBuffer+vkFreeMemory on a vertex-count jump -
// exactly what clicks/popovers/page switches cause) could then hit a buffer a still-executing
// submit is reading: garbled or half-faded menu frames. One fence per rotation slot closes the
// race: before frame N fills slot (N+1) % kMaxFrames, wait for the fence of the submit that
// used it (frame N-1-kMaxFrames - normally long done, so this is a no-op), reset it, submit
// against it. menuUploadFrame mirrors the backend's rotation: it advances exactly when
// RenderDrawData advances wrb->Index (a real render that passed the fb-size early-out).
VkFence uploadSlotFences[kMaxFrames]{};
std::uint64_t menuUploadFrame = 0; // present thread only, under renderLock

// --- avatar texture (see avatar_texture in VulkanHook.h) --------------------------------

struct AvatarUploadState {
    // staged request (present thread producer, present thread consumer)
    const unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    // GPU resources, created as the pipeline progresses
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkFence usedFence = VK_NULL_HANDLE; // the slot fence of the frame that recorded the upload
    bool uploadRecorded = false;
};

AvatarUploadState music;
std::atomic<bool> musicRequestPending{false};
AvatarUploadState avatar;
std::atomic<bool> avatarRequestPending{false};
// Menu logo (the swirl cutout): same machinery, second state. The UI stages decoded PNG bytes
// once; until the descriptor is live the UI keeps the NS monogram fallback.
AvatarUploadState logo;
std::atomic<bool> logoRequestPending{false};
// Lua texture pool (renderer.load_image). Slots recycle: release() hands the SLOT back while the
// retired GPU state moves to a ring that is drained during teardown (after waitUntilDeviceIdle),
// because an in-flight unchained submit may still sample the descriptor.
constexpr int kMaxLuaTextures = 8; // keep in sync with lua_texture::kMaxTextures in the header
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

// Called on the presentation path before the menu render pass. Two phases: record the upload
// into the current frame's command buffer (executed by that frame's regular submit), then poll
// the recording frame's slot fence on later frames and finalize (view + sampler + descriptor).
// The slot fence cannot be reset under us: slot reuse waits >= kMaxFrames frames out.
// Generic RGBA8 texture upload (see avatar_texture / logo_texture in VulkanHook.h).
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

        std::free(const_cast<unsigned char*>(avatar.pixels)); // consumed either way
        avatar.pixels = nullptr;
        if (!ok)
            gui_log::write("hook: texture upload setup FAILED");
        return;
    }

    // Upload submitted on an earlier frame: poll its fence, then finalize. A null fence means
    // cleanupRenderTargets already drained it (a swapchain recreation landed mid-upload) - the
    // image is in its final layout, go straight to finalizing.
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

// See processTextureUpload - same state shape.
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

// --- shadow stamp (see shadow_texture in VulkanHook.h) -----------------------------------
// A precomputed gaussian-blurred rounded box the UI draws as soft shadows under cards and
// popovers. Same staging machinery as the avatar texture: the upload is recorded into a menu
// frame's command buffer (before the render pass), submitted with that frame's slot fence, and
// the descriptor goes live once a fence poll proves the upload completed. Until then (or if
// the upload failed) the UI keeps its layered-rect fake. Present-thread only.

struct ShadowUploadState {
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkFence usedFence = VK_NULL_HANDLE; // the slot fence of the frame that recorded the upload
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
            // Clean up this attempt's partials; the UI keeps the layered-rect fake forever.
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

    // Upload submitted on an earlier frame: poll its fence, then finalize. A null fence means
    // cleanupRenderTargets already drained it - the image is in its final layout either way.
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

[[nodiscard]] std::int64_t monotonicMs() noexcept; // defined with the public API below
std::int64_t lastSkipLogMs = 0;                    // rate limiter for present-skip diagnostics

// --- pointer-cache swap records -------------------------------------------

struct SwapSite {
    volatile std::uint64_t* site;
    std::uint64_t original;
    std::uint64_t replacement;
};

constexpr std::size_t kMaxSwapSites = 256; // .bss slots + per-object heap copies
SwapSite swapSites[kMaxSwapSites];
std::size_t swapSiteCount = 0;

using AcquireNextImageKHRFunc = VkResult (*)(VkDevice, VkSwapchainKHR, std::uint64_t, VkSemaphore, VkFence, std::uint32_t*);
using QueuePresentKHRFunc = VkResult (*)(VkQueue, const VkPresentInfoKHR*);
using CreateSwapchainKHRFunc = VkResult (*)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);

AcquireNextImageKHRFunc originalAcquireNextImageKHR = nullptr;
QueuePresentKHRFunc originalQueuePresentKHR = nullptr;
CreateSwapchainKHRFunc originalCreateSwapchainKHR = nullptr;

// --- forward declarations ----------------------------------------------------

void cleanupRenderTargets(VkDevice device) noexcept;
[[nodiscard]] VkSemaphore renderImGui(VkQueue queue, const VkPresentInfoKHR* presentInfo) noexcept;

// --- hooks ---------------------------------------------------------------------

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
    // Swapchain recreation invalidates every backbuffer handle. The OLD views/framebuffers may
    // still be referenced by command buffers that are IN FLIGHT on the present queue -
    // destroying them here without syncing is a device-lost recipe (observed as an amdgpu
    // context reset that took the whole desktop down during window resize). ORDERING MATTERS:
    // take the render lock FIRST (no new command buffer can start recording against the old
    // targets), THEN wait for the device idle (drains everything already submitted), and only
    // then destroy. After the lock is released, rendering rebuilds against the new swapchain.
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
    // The render pass must be created with the exact swapchain format - never guessed. A
    // mismatched framebuffer (e.g. SRGB / HDR-capable formats) is a validation error at best.
    swapchainFormatCaptured = pCreateInfo ? pCreateInfo->imageFormat : VK_FORMAT_UNDEFINED;

    if (originalCreateSwapchainKHR)
        return originalCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    gui_log::write("hook: createSwapchain with null original!");
    return VK_ERROR_INITIALIZATION_FAILED;
}

VkResult VKAPI_CALL hkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) noexcept
{
    // One-shot deferred work, first present only (loader lock long released
    // here, unlike in our constructor): link_map self-hide. No-op afterwards.
    fva::hooks::apply_deferred_link_hide();
    // breadcrumb 0x100 = present entry; 0x107 = renderImGui returned; 0x108 = original present
    // returned (see the 0x1xx present-path breadcrumb block inside renderImGui)
    CrashLogger::trace(0x100);
    const VkSemaphore menuDone = renderImGui(queue, pPresentInfo);
    CrashLogger::trace(0x107);

    // Make the original present wait on our menu pass IN ADDITION to the game's own wait
    // semaphores. APPEND, never replace: the game's binary semaphores must keep their single
    // wait (replacing them here dropped the game's render-completion dependency). The
    // compositor then shows the image only after both the game's straggler passes and our menu
    // pass have finished - that is what closes the menu vanishing / pixelation / white-flash
    // races under load. If the wait list were absurdly long, present unpatched (as before).
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

// --- resolution-chain scan -------------------------------------------------------

// The name string lives in the module's .rodata; validity of the target decides whether the
// slot gets hooked.
[[nodiscard]] bool isTargetName(const char* name) noexcept
{
    return std::strcmp(name, "vkQueuePresentKHR") == 0
        || std::strcmp(name, "vkAcquireNextImageKHR") == 0
        || std::strcmp(name, "vkCreateSwapchainKHR") == 0;
}

// Swaps the cache slot for one target. The slot holding `targetName`'s pointer is written by
// the NEXT iteration's store (the store lags one name behind), so the caller passes the slot
// observed at the following match.
void hookSlotFor(const char* targetName, volatile std::uint64_t* slot) noexcept
{
    if (!slot || *slot == 0)
        return; // not populated yet - the caller retries later

    // The two compiled copies of the chain write the same slots - skip already-hooked ones.
    for (std::size_t i = 0; i < swapSiteCount; ++i) {
        if (swapSites[i].site == slot)
            return;
    }
    if (swapSiteCount >= kMaxSwapSites)
        return;

    // The slot's current value is what our handler must call through - record it on the right
    // original BEFORE the swap goes live, or the first hooked call jumps into null.
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

// Walks the resolution chain in the renderer's .text and hooks the three cache slots.
// Returns the number of vkQueuePresentKHR slots swapped (0 = pattern drifted or cache not
// populated - callers treat that as "not installed, retry").
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
    // `pending` holds the previous iteration's target name (if any); the CURRENT iteration's
    // store slot receives the previous iteration's resolved pointer.
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

// Re-asserts our handlers in case the game re-resolved its cache since install (a
// re-resolution writes the original pointer back). Called once per frame from the install
// retry path (which early-outs cheaply once installed).
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

// --- value scan for pointer copies --------------------------------------------

// The renderer fills its .bss cache once, but per-object structures (device/swapchain
// instances created before we injected) may hold their own copies of the function pointers.
// This scan walks every writable mapping of the process and swaps any qword equal to the
// original function pointer with our handler. One-time cost at install; the site list keeps
// them restorable and re-asserted.
struct PointerCopyScanResult {
    std::size_t sitesSwapped = 0;
    std::uint64_t bytesScanned = 0;
    bool truncatedByBudget = false;
};

[[nodiscard]] PointerCopyScanResult scanWritableMappingsForValue(std::uint64_t needle, std::uint64_t replacement) noexcept
{
    constexpr std::uint64_t kMaxTotalScanBytes = 3ull << 30;  // 3 GiB overall budget
    constexpr std::uint64_t kMaxMappingScanBytes = 1ull << 30; // skip giant arenas beyond this

    PointerCopyScanResult result;
    std::uint64_t scannedBytes = 0;

    const auto fd = LinuxPlatformApi::open("/proc/self/maps", O_RDONLY);
    if (fd < 0)
        return result;

    auto handleMapping = [&](std::uintptr_t start, std::uintptr_t end, const char* perms, const char* path) {
        if (perms[1] != 'w')
            return;
        if (!path || std::strstr(path, "libMangoHud") != nullptr)
            return; // never touch our own library's storage

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

            // Skip sites we already swapped (dedupe across the three needles is not needed -
            // the values differ - but a slot can match two phases of the install).
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

    // Stream /proc/self/maps line by line via pread with a running offset.
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
                lineLength = 0; // overlong line: drop it
            }
        }
        if (result.truncatedByBudget)
            break;
    }

    LinuxPlatformApi::close(fd);
    result.bytesScanned = scannedBytes;
    return result;
}

// --- fake instance (ImGui handle provider) ---------------------------------------

PFN_vkVoidFunction VKAPI_CALL imguiLoaderFunc(const char* name, void*) noexcept
{
    // Instance-level proc addresses dispatch on the object they are called with, so the
    // backend's device-level pointers work with the game's real device.
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
    // Debug builds: enable Khronos validation on OUR instance. Validation is instance-scoped,
    // so every call we make into the game's device (render pass layouts, load ops, semaphore
    // states, barriers) gets checked while the game's own submits stay untouched. Degrades
    // gracefully when the layer is not installed.
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

// --- render targets (donor parity) ----------------------------------------------

[[nodiscard]] VkFormat swapchainFormat() noexcept
{
    // Prefer the format the game actually created its swapchain with (captured in
    // hkCreateSwapchainKHR); the Wayland-based guess only covers the bootstrap case where the
    // swapchain predates our hook and no create call was ever seen.
    if (swapchainFormatCaptured != VK_FORMAT_UNDEFINED)
        return swapchainFormatCaptured;
    return gui_sdl::isUsingWayland() ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
}

[[nodiscard]] bool createRenderTarget(VkDevice device, VkSwapchainKHR swapchain) noexcept
{
    // Everything is built into locals and only committed to the globals on success. The ImGui
    // backend captured the PREVIOUS render pass (its pipelines) and descriptor pool (its
    // texture sets) at Init, so a failed retry must neither clobber those objects (the old
    // globals stay published) nor leak what this attempt already created (self-cleanup below).
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

    // Render pass: DONT_CARE load - donor parity, empirically proven. LOAD (with either
    // initialLayout) was tried here and black-screened the game while the menu still rendered:
    // the game's last pass leaves the swapchain image in a layout our pass cannot truthfully
    // claim at bootstrap (PRESENT_SRC_KHR is the likely actual), so the load executes against
    // stale image state and reads black. DONT_CARE makes the driver skip touching prior
    // contents entirely, which preserves them in practice on every driver this project targets
    // (proven across the donor's lifetime) - the menu composites over the game frame as
    // intended. Do not "fix" this back to LOAD without a truthful way to learn the game's
    // actual final layout.
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

    // Per-backbuffer semaphore chaining our menu pass into the frame dependencies (signaled by
    // the menu submit, appended to the original present's wait list - see hkQueuePresentKHR).
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (std::uint32_t i = 0; i < imageCount; ++i) {
        if (deviceFunctions.createSemaphore(device, &semaphoreInfo, nullptr, &semaphores[i]) != VK_SUCCESS)
            return fail("semaphore");
    }

    // The backend only ever allocates COMBINED_IMAGE_SAMPLER sets (font atlas + user textures).
    constexpr VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // avatar RemoveTexture
    poolInfo.maxSets = poolSize.descriptorCount;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (deviceFunctions.createDescriptorPool(device, &poolInfo, nullptr, &newDescriptorPool) != VK_SUCCESS)
        return fail("descriptor pool");

    // Signaled at creation: a slot with no prior submit must never block the wait. Fences
    // persist across recreations (submits and the avatar finalize path reference them), so
    // only the missing ones are created.
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

    // Commit: publish the new generation; retire the previous one - still referenced by the
    // backend's pipelines/sets, destroyed only at full teardown (destroyResources).
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

    // Texture uploads recorded into a menu frame's command buffer (avatar, shadow stamp)
    // finalize by polling that frame's slot fence. Tearing the fences down mid-upload would
    // orphan them (wait on a destroyed fence): drain the pending ones first and drop the
    // references - the uploads are complete after the wait, so their finalize steps run on a
    // later present. Safe to block here: both callers ran vkDeviceWaitIdle first.
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

    // Safe: both cleanupRenderTargets callers (swapchain recreation, full teardown) run
    // vkDeviceWaitIdle first, so no submit references these fences anymore.
    for (auto& fence : uploadSlotFences) {
        if (fence != VK_NULL_HANDLE) {
            deviceFunctions.destroyFence(device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
    }
}

// --- present-path render -----------------------------------------------------------

// ImGui renderer bootstrap on the first present, then the menu render itself. Mirrors the
// donor's RenderImGui flow. Returns the semaphore our pass signals - the caller APPENDS it to
// the original present's wait list - or VK_NULL_HANDLE when nothing was rendered/submitted.
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
        // One-time resolution against the game's real device.
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

    // ImGui does not like rendering multiple frames at once (donor's comment); serializes the
    // (theoretical) multiple present threads.
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
        // Bootstrap only (no swapchain create call seen yet): approximate the pixel extent
        // from point-space DisplaySize * framebuffer scale. Once hkCreateSwapchainKHR fires,
        // the game's real extent is always used instead. The scale is sanity-bounded - a
        // broken getSizeInPixels (0 or NaN) must not produce a zero render area (invalid
        // render pass, driver UB).
        if (swapchainExtent.width == 0) {
            const auto [w, h] = ImGui::GetIO().DisplaySize;
            const auto [sx, sy] = ImGui::GetIO().DisplayFramebufferScale;
            const float fx = (sx > 0.0f && sx < 8.0f) ? sx : 1.0f;
            const float fy = (sy > 0.0f && sy < 8.0f) ? sy : 1.0f;
            swapchainExtent = {static_cast<std::uint32_t>(w * fx + 0.5f), static_cast<std::uint32_t>(h * fy + 0.5f)};
        }

        const std::uint32_t imageIndex = presentInfo->pImageIndices[i];
        if (imageIndex >= kMaxFrames) {
            // Every skip is a present without the menu (flicker). Rate-limited so a capture
            // session that hits this repeatedly shows up as a burst in the log.
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

        // Semaphore chaining (kChainIntoGameSemaphores) is DISABLED: CS2 runs with Steam's
        // GameOverlay, which renders around the same present and shares those semaphores -
        // chaining stole a binary signal from Steam's overlay and froze the whole queue
        // (observed). Instead the menu submit runs unchained (no waits) and SIGNALS this
        // frame's semaphore; hkQueuePresentKHR APPENDS it to the original present's wait list.
        // Bare queue-submission ordering used to let the game's straggler passes race our menu
        // (the old 1-frame glitch trade-off); the appended wait closes that race without
        // consuming any of the game's own signals.
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
        // Signal unconditionally: the hooked present appends this semaphore to its wait list
        // (see hkQueuePresentKHR), so the compositor never sees a frame before the menu pass
        // completed - even on frames where the menu drew nothing.
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &frameDone;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        deviceFunctions.resetCommandBuffer(fd->CommandBuffer, 0);
        deviceFunctions.beginCommandBuffer(fd->CommandBuffer, &beginInfo);

        // The render area MUST match the framebuffer (swapchain image) extent. io.DisplaySize
        // is window-point-space and lags during a resize - driving the render pass from it
        // produced out-of-bounds render areas (driver GPU reset during resize).
        VkExtent2D extent = swapchainExtent;
        if (extent.width == 0 || extent.height == 0) {
            // Bootstrap only: bounded point-space * framebuffer scale (see above).
            const auto [width, height] = ImGui::GetIO().DisplaySize;
            const auto [sx, sy] = ImGui::GetIO().DisplayFramebufferScale;
            const float fx = (sx > 0.0f && sx < 8.0f) ? sx : 1.0f;
            const float fy = (sy > 0.0f && sy < 8.0f) ? sy : 1.0f;
            extent = {static_cast<std::uint32_t>(width * fx + 0.5f), static_cast<std::uint32_t>(height * fy + 0.5f)};
        }
        // Breadcrumb codes for CrashLogger (0x1xx = present path): a crash mid-frame names the
        // exact last stage in /tmp/gamesense_crash.txt instead of leaving a pc to guess at.
        constexpr std::uint64_t kTraceFenceWait = 0x101;
        constexpr std::uint64_t kTraceAvatar = 0x102;
        constexpr std::uint64_t kTraceBeginPass = 0x103;
        constexpr std::uint64_t kTraceRendererInit = 0x104;
        constexpr std::uint64_t kTraceGuiRender = 0x105;
        constexpr std::uint64_t kTraceSubmit = 0x106;

        // Upload-slot sync: fence-wait the buffer slot the ImGui backend rotation is about to
        // reuse (slot (menuUploadFrame + 1) % kMaxFrames), then submit this frame against that
        // fence. Only when this frame will actually run RenderDrawData - that is what advances
        // the backend rotation our counter mirrors (same fb-size condition, same skip cases).
        const ImGuiIO& imguiIo = ImGui::GetIO();
        const bool willRender = imguiIo.DisplaySize.x * imguiIo.DisplayFramebufferScale.x > 0.0f
            && imguiIo.DisplaySize.y * imguiIo.DisplayFramebufferScale.y > 0.0f;
        const std::size_t uploadSlot = (menuUploadFrame + 1) % kMaxFrames;
        VkFence slotFence = VK_NULL_HANDLE;
        if (willRender) {
            slotFence = uploadSlotFences[uploadSlot];
            if (slotFence != VK_NULL_HANDLE) {
                // Normally long-signaled (the slot's submit finished kMaxFrames frames ago) -
                // the cap only bounds a pathological stall; fail-open below self-heals.
                if (deviceFunctions.waitForFences(device, 1, &slotFence, VK_TRUE, 5'000'000) != VK_SUCCESS) {
                    gui_log::write("hook: upload slot fence wait timed out (slot %zu)", uploadSlot);
                    slotFence = VK_NULL_HANDLE; // fail-open, flagged in the log
                } else {
                    deviceFunctions.resetFences(device, 1, &slotFence);
                }
            }
        }
        CrashLogger::trace(kTraceFenceWait);

        // Texture uploads record into this frame's command buffer BEFORE the render pass
        // (transfers are illegal inside a render pass); readiness rides this frame's fence.
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
            // NOT the swapchain image count: ImGui_ImplVulkan_RenderDrawData rotates its
            // vertex/index upload buffers over ImageCount slots. Submission is unchained (no
            // semaphore waits), so up to ~swapchain-depth menu submits can be in flight at
            // once; with ImageCount=2 present k+2's memcpy overwrote the buffer present k was
            // still reading - garbled/white menu frames whenever the content changed (scroll).
            // kMaxFrames slots exceeds any realizable in-flight depth (acquire back-pressure
            // bounds the game to the swapchain depth), so a slot is only reused long after its
            // submit completed.
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
        // A failed submit would leave present waiting forever on an unsignaled semaphore -
        // never publish frameDone in that case; present unpatched, like the old behavior.
        if (submitted != VK_SUCCESS) {
            gui_log::write("hook: menu submit failed (%d) - presenting without our wait", static_cast<int>(submitted));
            continue;
        }
        if (menuDone == VK_NULL_HANDLE)
            menuDone = frameDone; // first swapchain's pass; CS2 presents exactly one
    }
    return menuDone;
}

[[nodiscard]] std::int64_t monotonicMs() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1'000'000;
}

} // namespace

// --- public API (avatar_texture, see VulkanHook.h) -----------------------------------

void VulkanHook::logo_texture::request(const void* pixelsRgba, int width, int height) noexcept
{
    if (logo.descriptor != VK_NULL_HANDLE || logo.uploadRecorded || width <= 0 || height <= 0) {
        std::free(const_cast<void*>(pixelsRgba)); // one texture per process; late requests dropped
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

// --- public API (lua_texture, see VulkanHook.h) --------------------------------------

void VulkanHook::lua_texture::request(const int index, const void* pixelsRgba, const int width, const int height) noexcept
{
    if (index < 0 || index >= kMaxLuaTextures || width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        std::free(const_cast<void*>(pixelsRgba));
        return;
    }
    AvatarUploadState& state = luaTextures[index];
    if (state.descriptor != VK_NULL_HANDLE || state.uploadRecorded) {
        std::free(const_cast<void*>(pixelsRgba)); // slot mid-upload / already live - reject
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
        return; // nothing staged - already free
    // Retire the whole state (staged-but-not-recorded, mid-upload or live); the slot resets to
    // empty. A staged request racing the retire loses its pixels pointer here - it is free()d
    // either by this copy of the state during teardown or was never recorded, and the uploader
    // tolerates a consumed slot on the next frame.
    RetiredTexture& retired = retiredLuaTextures[retiredCursor];
    retiredCursor = (retiredCursor + 1) % kMaxRetiredTextures;
    if (retired.used) {
        // Ring overflow: drop the oldest state's CPU pixels only (its GPU objects leak, bounded).
        std::free(const_cast<unsigned char*>(retired.state.pixels));
        retired.state.pixels = nullptr;
    }
    retired.used = true;
    retired.state = state;
    state = AvatarUploadState{};
    luaTexturePending[index].store(false, std::memory_order_release);
}

// --- public API ---------------------------------------------------------------------

bool VulkanHook::tryInstall() noexcept
{
    static std::atomic<bool> installed{false};
    static std::atomic<bool> trying{false};
    constexpr std::int64_t kRetryIntervalMs = 250;

    if (installed.load(std::memory_order_acquire)) {
        verifySwaps(); // cheap: keeps our handlers in place if the game re-resolves
        return true;
    }
    if (HookQuiesce::isShuttingDown())
        return false; // never start hooking during teardown
    if (!GUI::isInitialized())
        return false; // the hook needs the ImGui context (its loader wrapper + status reporting)

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
        return installed.load(std::memory_order_acquire); // another thread is already installing

    if (!loader.createInstance) {
        const DynamicLibrary vulkan{cs2::VULKAN_DLL};
        if (!static_cast<bool>(vulkan)) {
            gui_log::write("tryInstall: libvulkan not loaded yet");
            trying.store(false, std::memory_order_release);
            return false; // libvulkan not loaded yet - retry later
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

    // Handler globals are valid before any slot is swapped, so the game can start routing
    // through them from the first store on. The present path is what matters; acquire runs
    // first in the chain and captures the device before the first hooked present lands.
    const std::size_t presentSites = hookRendererCacheSlots();
    if (presentSites == 0) {
        // Cache not populated yet (or the pattern drifted). Restore anything we swapped and
        // retry; if a future CS2 update permanently breaks this, this is the escalation
        // trigger for the vendored-detour fallback.
        restorePointers();
        trying.store(false, std::memory_order_release);
        return false;
    }

    // The .bss cache slots are hooked - but structures created before our injection may hold
    // their own COPIES of the function pointers and call through those instead (observed for
    // vkQueuePresentKHR: acquire reaches us, present does not). Sweep writable memory for the
    // original pointer values and swap the copies as well.
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
    // Texture teardowns first: RemoveTexture writes into our descriptor pool, and the backend
    // shutdown below expects its own sets to still be registered while it runs.
    destroyTextureState(gameDevice.load(std::memory_order_acquire), music, musicRequestPending);
    destroyTextureState(gameDevice.load(std::memory_order_acquire), avatar, avatarRequestPending);
    destroyTextureState(gameDevice.load(std::memory_order_acquire), logo, logoRequestPending);
    for (int i = 0; i < kMaxLuaTextures; ++i)
        destroyTextureState(gameDevice.load(std::memory_order_acquire), luaTextures[i], luaTexturePending[i]);
    destroyShadowTexture(gameDevice.load(std::memory_order_acquire));
    // Retired lua textures (released slots) - the device is idle below before the render target
    // cleanup, but ImGui descriptor removal must happen while the backend still exists, so they
    // go here with the other texture states. A dedicated dummy pending flag: a retired state
    // never has a live request, and the shared slot flags must not be touched.
    for (auto& retired : retiredLuaTextures) {
        if (retired.used) {
            std::atomic<bool> dummyPending{false};
            destroyTextureState(gameDevice.load(std::memory_order_acquire), retired.state, dummyPending);
            retired.used = false;
        }
    }

    // Renderer shutdown first: ImGui_ImplVulkan owns descriptor sets allocated from our pool and
    // pipelines referencing our render pass. It must run while the ImGui context still exists
    // (GUI::destroy destroys the context afterwards).
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
        // Render pass / descriptor pool generations retired across swapchain recreations stay
        // alive until now: the backend's pipelines and texture sets referenced them until its
        // shutdown above (see the comment above `renderPass`).
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
