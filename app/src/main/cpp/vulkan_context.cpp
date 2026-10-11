#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include "vulkan_context.h"
#include "ulog.h"
#include <android/log.h>
#include <vector>
#include <cstring>
#include <mutex>

#define LOGI(...) ulog_write(ANDROID_LOG_INFO,  "UltramanFE0", __VA_ARGS__)
#define LOGE(...) ulog_write(ANDROID_LOG_ERROR, "UltramanFE0", __VA_ARGS__)
#define LOGW(...) ulog_write(ANDROID_LOG_WARN,  "UltramanFE0", __VA_ARGS__)

static VkInstance         g_instance    = VK_NULL_HANDLE;
static VkPhysicalDevice   g_phys        = VK_NULL_HANDLE;
static VkDevice           g_device      = VK_NULL_HANDLE;
static VkQueue            g_queue       = VK_NULL_HANDLE;
static uint32_t           g_queue_fam   = 0;
static VkSurfaceKHR       g_surface     = VK_NULL_HANDLE;
static VkSwapchainKHR     g_swapchain   = VK_NULL_HANDLE;
static VkFormat           g_format      = VK_FORMAT_UNDEFINED;
static VkExtent2D         g_extent      = {0, 0};
static ANativeWindow*     g_window      = nullptr;
static bool               g_initialized = false;
static bool               g_sw_ready    = false;

static std::vector<VkImage>        g_images;
static std::vector<VkImageView>    g_views;
static std::vector<VkFramebuffer>  g_fbs;
static std::vector<VkCommandBuffer> g_cmds;
static std::vector<VkSemaphore>    g_render_done_sems;
static std::vector<VkSemaphore>    g_image_avail_sems;
static std::vector<VkFence>        g_in_flight_fences;

static VkRenderPass       g_render_pass = VK_NULL_HANDLE;
static VkCommandPool      g_cmd_pool    = VK_NULL_HANDLE;

static uint32_t           g_sync_index  = 0;
static uint32_t           g_frame_idx   = 0;

static std::mutex         g_queue_mtx;

static struct retro_vulkan_context g_retro_ctx = {};
static struct retro_hw_render_interface_vulkan g_retro_iface = {};

static VkImageView g_src_view       = VK_NULL_HANDLE;
static VkImageLayout g_src_layout   = VK_IMAGE_LAYOUT_UNDEFINED;
static std::vector<VkSemaphore> g_wait_sems;
static std::vector<VkCommandBuffer> g_core_cmds;

#include <unordered_map>
static std::unordered_map<VkImageView, VkImage> g_view_to_image;


static void transition_to_present(VkCommandBuffer cmd, VkImage img);

static bool create_instance() {
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "UltramanFE0";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.pEngineName = "UltramanVK";
    ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_1;

    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = exts;

    VkResult r = vkCreateInstance(&ci, nullptr, &g_instance);
    if (r != VK_SUCCESS) { LOGE("vkCreateInstance=%d", r); return false; }
    LOGI("VkInstance ok");
    return true;
}

static bool pick_device() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_instance, &n, nullptr);
    if (!n) { LOGE("no GPU"); return false; }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(g_instance, &n, devs.data());

    int best = 0, bestScore = -1;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(devs[i], &p);
        int s = (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1000 : 500;
        LOGI("GPU[%u] %s type=%d s=%d", i, p.deviceName, p.deviceType, s);
        if (s > bestScore) { best = i; bestScore = s; }
    }
    g_phys = devs[best];

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(g_phys, &qn, qs.data());
    for (uint32_t i = 0; i < qn; i++) {
        if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_queue_fam = i; break; }
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_queue_fam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char* dext[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dext;

    VkResult r = vkCreateDevice(g_phys, &dci, nullptr, &g_device);
    if (r != VK_SUCCESS) { LOGE("vkCreateDevice=%d", r); return false; }
    vkGetDeviceQueue(g_device, g_queue_fam, 0, &g_queue);
    LOGI("VkDevice ok queue_fam=%u", g_queue_fam);
    return true;
}

static bool create_swapchain(ANativeWindow* win, uint32_t w, uint32_t h) {
    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = win;
    VkResult r = vkCreateAndroidSurfaceKHR(g_instance, &sci, nullptr, &g_surface);
    if (r != VK_SUCCESS) { LOGE("createSurface=%d", r); return false; }

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_phys, g_surface, &caps);

    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_phys, g_surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_phys, g_surface, &fn, fmts.data());

    VkSurfaceFormatKHR c = fmts[0];
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM) { c = f; break; }
    }
    g_format = c.format;

    VkExtent2D ext;
    if (caps.currentExtent.width != 0xFFFFFFFF) ext = caps.currentExtent;
    else { ext.width = w; ext.height = h; }
    g_extent = ext;

    uint32_t cnt = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && cnt > caps.maxImageCount) cnt = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sc{};
    sc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sc.surface = g_surface;
    sc.minImageCount = cnt;
    sc.imageFormat = c.format;
    sc.imageColorSpace = c.colorSpace;
    sc.imageExtent = ext;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sc.clipped = VK_TRUE;

    r = vkCreateSwapchainKHR(g_device, &sc, nullptr, &g_swapchain);
    if (r != VK_SUCCESS) { LOGE("createSwapchain=%d", r); return false; }

    uint32_t ic = 0;
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &ic, nullptr);
    g_images.resize(ic);
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &ic, g_images.data());

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = g_format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    g_views.resize(ic);
    for (uint32_t i = 0; i < ic; i++) {
        vi.image = g_images[i];
        vkCreateImageView(g_device, &vi, nullptr, &g_views[i]);
        g_view_to_image[g_views[i]] = g_images[i];
    }

    g_render_done_sems.resize(ic);
    g_image_avail_sems.resize(ic);
    g_in_flight_fences.resize(ic);
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < ic; i++) {
        vkCreateSemaphore(g_device, &si, nullptr, &g_render_done_sems[i]);
        vkCreateSemaphore(g_device, &si, nullptr, &g_image_avail_sems[i]);
        vkCreateFence(g_device, &fi, nullptr, &g_in_flight_fences[i]);
    }

    VkCommandPoolCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp.queueFamilyIndex = g_queue_fam;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(g_device, &cp, nullptr, &g_cmd_pool);

    g_cmds.resize(ic);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g_cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = ic;
    vkAllocateCommandBuffers(g_device, &ai, g_cmds.data());

    LOGI("swapchain %ux%u images=%u fmt=%d", ext.width, ext.height, ic, g_format);
    g_sw_ready = true;
    return true;
}

bool vk_init(ANativeWindow* win, uint32_t w, uint32_t h) {
    if (g_initialized) return true;
    g_window = win;
    if (!create_instance()) return false;
    if (!pick_device()) return false;
    if (!create_swapchain(win, w, h)) return false;

    g_retro_ctx.instance = g_instance;
    g_retro_ctx.gpu = g_phys;
    g_retro_ctx.device = g_device;
    g_retro_ctx.queue_family_index = g_queue_fam;
    g_retro_ctx.queue = g_queue;
    g_retro_ctx.presentation_queue_family_index = g_queue_fam;
    g_retro_ctx.presentation_queue = g_queue;

    g_initialized = true;
    LOGI("Vulkan context ready");
    return true;
}

void vk_shutdown() {
    if (g_device) vkDeviceWaitIdle(g_device);

    for (auto s : g_render_done_sems) if (s) vkDestroySemaphore(g_device, s, nullptr);
    for (auto s : g_image_avail_sems) if (s) vkDestroySemaphore(g_device, s, nullptr);
    for (auto f : g_in_flight_fences) if (f) vkDestroyFence(g_device, f, nullptr);
    for (auto v : g_views) if (v) vkDestroyImageView(g_device, v, nullptr);
    for (auto fb : g_fbs) if (fb) vkDestroyFramebuffer(g_device, fb, nullptr);

    if (g_render_pass) vkDestroyRenderPass(g_device, g_render_pass, nullptr);
    if (g_cmd_pool) vkDestroyCommandPool(g_device, g_cmd_pool, nullptr);
    if (g_swapchain) vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
    if (g_surface) vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
    if (g_device) vkDestroyDevice(g_device, nullptr);
    if (g_instance) vkDestroyInstance(g_instance, nullptr);

    g_render_pass = VK_NULL_HANDLE;
    g_cmd_pool = VK_NULL_HANDLE;
    g_swapchain = VK_NULL_HANDLE;
    g_surface = VK_NULL_HANDLE;
    g_device = VK_NULL_HANDLE;
    g_instance = VK_NULL_HANDLE;
    g_initialized = false;
    g_sw_ready = false;
}

bool vk_recreate_swapchain(ANativeWindow* win, uint32_t w, uint32_t h) {
    if (!g_device) return false;
    vkDeviceWaitIdle(g_device);
    for (auto v : g_views) if (v) vkDestroyImageView(g_device, v, nullptr);
    g_views.clear();
    if (g_swapchain) vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
    if (g_surface) vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
    g_swapchain = VK_NULL_HANDLE;
    g_surface = VK_NULL_HANDLE;
    g_window = win;
    return create_swapchain(win, w, h);
}

static void cb_set_image(void* h, const struct retro_vulkan_image* img,
                          uint32_t num_sems, const VkSemaphore* sems,
                          uint32_t src_qf, uint32_t dst_qf) {
    (void)h; (void)src_qf; (void)dst_qf;
    if (!img || !img->image_view) {
        g_src_view = VK_NULL_HANDLE;
        g_src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return;
    }
    g_src_view = img->image_view;
    g_src_layout = img->image_layout;

    g_wait_sems.clear();
    for (uint32_t i = 0; i < num_sems && sems; i++) {
        g_wait_sems.push_back(sems[i]);
    }
}

static uint32_t cb_get_sync_index(void* h) {
    (void)h;
    return g_sync_index;
}

static uint32_t cb_get_sync_index_mask(void* h) {
    (void)h;
    return (1u << g_sync_index);
}

static void cb_set_command_buffers(void* h, uint32_t n, const VkCommandBuffer* cmd) {
    (void)h;
    g_core_cmds.clear();
    for (uint32_t i = 0; i < n && cmd; i++) {
        g_core_cmds.push_back(cmd[i]);
    }
}

static void cb_wait_sync_index(void* h) {
    (void)h;
    if (!g_sw_ready || g_images.empty()) return;
    vkWaitForFences(g_device, 1, &g_in_flight_fences[g_sync_index], VK_TRUE, UINT64_MAX);
}

static void cb_lock_queue(void* h) {
    (void)h;
    g_queue_mtx.lock();
}

static void cb_unlock_queue(void* h) {
    (void)h;
    g_queue_mtx.unlock();
}

static void cb_set_signal_semaphore(void* h, VkSemaphore s) {
    (void)h; (void)s;
}

struct retro_hw_render_interface_vulkan* vk_get_retro_interface() {
    g_retro_iface.interface_type = static_cast<enum retro_hw_render_interface_type>(RETRO_HW_RENDER_INTERFACE_VULKAN);
    g_retro_iface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    g_retro_iface.handle = nullptr;
    g_retro_iface.set_image = cb_set_image;
    g_retro_iface.get_sync_index = cb_get_sync_index;
    g_retro_iface.get_sync_index_mask = cb_get_sync_index_mask;
    g_retro_iface.set_command_buffers = cb_set_command_buffers;
    g_retro_iface.wait_sync_index = cb_wait_sync_index;
    g_retro_iface.lock_queue = cb_lock_queue;
    g_retro_iface.unlock_queue = cb_unlock_queue;
    g_retro_iface.set_signal_semaphore = cb_set_signal_semaphore;
    g_retro_iface.context = &g_retro_ctx;
    return &g_retro_iface;
}

bool vk_render_frame() {
    if (!g_sw_ready || g_images.empty() || g_cmds.empty()) return false;

    vkWaitForFences(g_device, 1, &g_in_flight_fences[g_frame_idx], VK_TRUE, UINT64_MAX);

    uint32_t img_idx = 0;
    VkResult r = vkAcquireNextImageKHR(
        g_device, g_swapchain, UINT64_MAX,
        g_image_avail_sems[g_frame_idx], VK_NULL_HANDLE, &img_idx);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;

    vkResetFences(g_device, 1, &g_in_flight_fences[g_frame_idx]);

    g_sync_index = img_idx;

    VkCommandBuffer cmd = g_cmds[g_frame_idx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    VkImage srcImage = VK_NULL_HANDLE;
    if (g_src_view != VK_NULL_HANDLE) {
        auto it = g_view_to_image.find(g_src_view);
        if (it != g_view_to_image.end()) srcImage = it->second;
    }

    if (srcImage != VK_NULL_HANDLE) {
        vk_barrier_image(cmd, srcImage,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        vk_barrier_image(cmd, g_images[img_idx],
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        vk_blit_image_to_swapchain(cmd, srcImage, g_images[img_idx]);

        vk_barrier_image(cmd, g_images[img_idx],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        vk_barrier_image(cmd, srcImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    } else {
        vk_barrier_image(cmd, g_images[img_idx],
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            0, 0,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g_image_avail_sems[g_frame_idx];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g_render_done_sems[g_frame_idx];

    vkQueueSubmit(g_queue, 1, &si, g_in_flight_fences[g_frame_idx]);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g_render_done_sems[g_frame_idx];
    pi.swapchainCount = 1;
    pi.pSwapchains = &g_swapchain;
    pi.pImageIndices = &img_idx;

    vkQueuePresentKHR(g_queue, &pi);

    g_frame_idx = (g_frame_idx + 1) % g_images.size();
    return true;
}


static void transition_to_present(VkCommandBuffer cmd, VkImage img) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

static void acquire_barrier(VkCommandBuffer cmd, VkImage img,
                             uint32_t src_qf, uint32_t dst_qf) {
    if (src_qf == dst_qf) return;
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcQueueFamilyIndex = src_qf;
    b.dstQueueFamilyIndex = dst_qf;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

static void release_barrier(VkCommandBuffer cmd, VkImage img,
                             uint32_t src_qf, uint32_t dst_qf) {
    if (src_qf == dst_qf) return;
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcQueueFamilyIndex = src_qf;
    b.dstQueueFamilyIndex = dst_qf;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}


static void vk_blit_image_to_swapchain(VkCommandBuffer cmd, VkImage src, VkImage dst) {
    if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;

    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[0] = {0, 0, 0};
    region.srcOffsets[1] = {(int32_t)g_extent.width, (int32_t)g_extent.height, 1};
    region.dstOffsets[0] = {0, 0, 0};
    region.dstOffsets[1] = {(int32_t)g_extent.width, (int32_t)g_extent.height, 1};

    vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region, VK_FILTER_NEAREST);
}

static void vk_barrier_image(VkCommandBuffer cmd, VkImage img,
                              VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                              VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}
