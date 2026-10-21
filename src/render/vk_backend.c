/*
 * Vulkan render back end: device, window, swapchain, clears, flips
 * (milestone 1), triangles with blend, depth, stencil and alpha test
 * (milestone 2), register combiners and four texture stages as one
 * uber-shader (milestone 3). Design: docs/vulkan-backend.md.
 *
 * Threading: every entry point is called on the NV2A poll thread, one at a
 * time (nv2a_backend.h), so this file keeps no locks. Vulkan, the window and
 * the swapchain are created lazily by the first call.
 *
 * Frame model: clears and draws between two flips are recorded into one
 * command buffer; flip blits the surface the title last rendered into to the
 * swapchain, submits and presents. One frame in flight for now.
 *
 * Switches:
 *   AMPED2_VK_VALIDATE=1      enable VK_LAYER_KHRONOS_validation (if installed)
 *   AMPED2_VK_DUMP=<prefix>   write the presented surface as <prefix><flip number>.bmp
 *   AMPED2_VK_DUMP_EVERY=<n>  ... every n-th flip (default 60)
 *   AMPED2_VK_GPU=<substr>    pick the physical device whose name contains it
 *   AMPED2_VK_TEXLOG=1        log the first 60 re-decodes of cached textures
 *   AMPED2_VK_NO_RC=1         ignore the combiner extension (milestone-2 look)
 *   AMPED2_TIME_TRACE=1       log the game's clock next to wall time and flips (every 5 s)
 *
 * Graphics settings (src/settings/settings.h):
 *   resolution scale  every render target is allocated at its guest size times
 *                     the scale; vertices arrive in guest surface pixels and the
 *                     shaders normalise by the guest size, so nothing else moves.
 *   aspect ratio      the surfaces the title flips (its back buffers) are also
 *                     widened to the ratio, and every vertex drawn into them is
 *                     narrowed by 4:3 / ratio (push constant x3d): the picture
 *                     keeps its proportions in the middle, the world's geometry
 *                     past the original screen edges fills the sides (Hor+) and
 *                     the HUD stays 4:3. Menus are presented 4:3 (the middle of
 *                     the surface only); a ride is told from a menu by its draws
 *                     into an off-screen render target (the 512x512 shadow /
 *                     reflection map, only drawn in a ride -- in_ride()).
 *                     Render targets that are only sampled keep their shape.
 *   window mode       windowed, fullscreen or borderless on the chosen display;
 *                     Alt+Enter / F11 toggle windowed <-> fullscreen.
 *   frame rate        unlocked: the title's flips are not tied to the vblank
 *                     (main.c clears RECOMP_FLIP_VSYNC); frame_pace() caps them.
 */
#include "volk.h"
#include <SDL.h>
#include <SDL_vulkan.h>

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv2a_backend.h"     /* xboxrecomp/src/kernel */
#include "nv2a_overlay.h"     /* the video overlay (XMV movies) */

/* SPIR-V, compiled from src/render/shaders/ at build time (glslc -mfmt=c). */
static const uint32_t k_vert_spv[] =
#include "nv2a.vert.inc"
;
static const uint32_t k_frag_spv[] =
#include "nv2a.frag.inc"
;
static const uint32_t k_vsh_spv[] =
#include "nv2a_vsh.vert.inc"
;
#include "keyboard_sdl.h"     /* xboxrecomp/src/input */
#include "vk_backend.h"
#include "settings/settings.h"

#define VKB_MAX_SURFACES    16
#define VKB_MAX_SWAP_IMAGES 8
#define VKB_MAX_TEXTURES    512
#define VKB_MAX_BAD_TEXTURES 16
#define VKB_MAX_SAMPLERS    32
#define VKB_VERTEX_RING     (32u << 20)    /* vertex bytes per frame */
#define VKB_STAGING_RING    (64u << 20)    /* texture upload bytes per frame */

/* A guest colour surface, as a GPU image. Keyed by guest address and size:
 * a title that reuses an address at another size gets a fresh image. */
typedef struct {
    uint32_t       va, width, height;      /* width, height: the image (scaled) */
    uint32_t       gw, gh;                 /* the guest surface's size */
    uint32_t       logical_w, logical_h;   /* guest width/aa_sx, height/aa_sy */
    int            wide;                   /* widened to the aspect ratio */
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
    VkImageLayout  layout;
    uint32_t       last_used;              /* flip number, for eviction */
} VkbSurface;

/* A guest depth/stencil surface (Nv2aRenderState.zeta_va), same size as the
 * colour surface it is drawn with. */
typedef struct {
    uint32_t       va, width, height;
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
    VkImageLayout  layout;
    uint32_t       last_used;
} VkbDepth;

/* A decoded guest texture. Re-validated once per frame by hashing the
 * decoded texels: a title rewrites textures in place (SSX 3's FMV frame is
 * one 640x480 texture updated every frame). */
typedef struct {
    uint32_t       offset, width, height, color, pitch;
    uint32_t       cube_stride;            /* 0: 2D; else six layers, a cube view */
    uint64_t       hash;
    uint32_t       checked;                /* flip number of the last check */
    uint32_t       last_used;
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
} VkbTexture;

/* Host-visible buffer used as a per-frame bump allocator. */
/* A texture that failed to decode, remembered for the rest of the frame. */
typedef struct {
    uint32_t       offset, width, height, color, flip;
} VkbBadTexture;

typedef struct {
    VkBuffer       buffer;
    VkDeviceMemory memory;
    uint8_t       *mapped;
    VkDeviceSize   size, used;
    VkDeviceSize   wanted;         /* bytes asked for this frame, refused ones too */
    VkBufferUsageFlags usage;
} VkbRing;

/* Push constants shared by both shaders (layout matches nv2a.vert/.frag). */
typedef struct {
    float    surface_size[2];
    float    tex_size[2];
    float    z_scale;
    uint32_t flags;          /* 1 textured | 2 combiners | func<<8 | ref<<16
                              * | 1<<31 alpha test */
    float    x3d;            /* NDC x factor for perspective vertices */
} VkbPush;

/* One batch's register-combiner state, std140, as nv2a.frag's RC block. */
typedef struct {
    uint32_t cw[8][4];       /* color_icw, alpha_icw, color_ocw, alpha_ocw */
    float    c0[8][4], c1[8][4];
    uint32_t fin[4];         /* final0, final1, control, stage_program */
    float    fc0[4], fc1[4], fog_color[4];
    float    tex_scale[4][4];
    uint32_t misc[4];        /* clip_plane_mode, dot_mapping, stage_input,
                              * bit per stage sampling a cube map */
    float    eye[4];         /* SET_EYE_VECTOR */
} VkbCombiners;

/* Bindings of the push-descriptor set: four stage samplers, the UBO, then the
 * same four stages as cube samplers. */
#define VKB_BIND_RC   4
#define VKB_BIND_CUBE 5
#define VKB_NBIND     9
/* ... and for batches whose vertex program runs on the GPU (nv2a_vsh.vert):
 * its constants, the program, the attribute formats (UBOs) and the vertex
 * bytes (SSBO). */
#define VKB_BIND_VCONST 9
#define VKB_BIND_VPROG  10
#define VKB_BIND_VATTR  11
#define VKB_BIND_VDATA  12
#define VKB_NBIND_VSH   13

/* nv2a_vsh.vert's Attrs block, std140. */
typedef struct {
    uint32_t fmt[16][4];     /* byte offset into the vertex bytes, stride, type, size (0: imm) */
    float    imm[16][4];
    float    misc[3][4];     /* aa xy, uv scale zw | fog p0, p1, enable, mode | program length */
} VkbVshAttrs;

static struct {
    int               failed;              /* init failed: stay inert */
    int               ready;
    SDL_Window       *window;
    VkInstance        instance;
    VkSurfaceKHR      wsi;
    VkPhysicalDevice  gpu;
    VkPhysicalDeviceMemoryProperties memprops;
    VkDevice          device;
    uint32_t          queue_family;
    VkQueue           queue;

    VkSwapchainKHR    swapchain;
    VkFormat          swap_format;
    VkExtent2D        swap_extent;
    uint32_t          swap_count;
    VkImage           swap_images[VKB_MAX_SWAP_IMAGES];
    VkSemaphore       sem_render_done[VKB_MAX_SWAP_IMAGES];
    volatile int      swap_dirty;          /* resized / out of date (any thread) */
    int               host_main;           /* window owned by amped2_vk_host_main */

    VkCommandPool     cmd_pool;
    VkCommandBuffer   cmd;
    VkFence           frame_fence;
    VkSemaphore       sem_acquire;
    int               recording;

    VkbSurface        surfaces[VKB_MAX_SURFACES];
    VkbSurface       *last;                /* last surface cleared/drawn */
    uint32_t          flips;
    uint32_t          scale;               /* resolution scale (settings) */
    double            widen;               /* aspect ratio / (4/3), >= 1 */
    uint32_t          shown_va[4];         /* surfaces the title has flipped */
    uint32_t          rt_flip;             /* last flip with draws off screen, +1 */
    VkPresentModeKHR  present_mode;

    VkFormat          depth_format;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout  pipe_layout;
    VkPipeline        pipeline;
    VkPipeline        pipeline_vsh;        /* vertex programs on the GPU; NULL: CPU only */
    VkPipeline        bound;               /* bound in V.cmd (NULL after a new scope) */
    VkDeviceSize      ssbo_align;          /* minStorageBufferOffsetAlignment */
    uint32_t          frame_serial;        /* command buffers begun */
    uint32_t          vconst_gen, vconst_serial;   /* constants already in vring ... */
    VkDeviceSize      vconst_off;              /* ... at this offset */
    uint32_t          vprog_gen, vprog_serial;
    VkDeviceSize      vprog_off;
    VkSampler         samplers[VKB_MAX_SAMPLERS];
    uint32_t          sampler_keys[VKB_MAX_SAMPLERS];
    uint32_t          nsamplers;
    VkbRing           vring, staging;
    VkDeviceSize      ubo_align;           /* minUniformBufferOffsetAlignment */
    VkbCombiners      last_rc;             /* last UBO written this frame ... */
    VkDeviceSize      last_rc_off;         /* ... and where (~0: none yet) */
    VkbDepth          depths[VKB_MAX_SURFACES];
    VkbTexture        textures[VKB_MAX_TEXTURES];
    VkbBadTexture     bad[VKB_MAX_BAD_TEXTURES];
    uint32_t          nbad;
    VkbTexture        white;               /* bound when a batch is untextured */
    VkbTexture        white_cube;          /* ... and in every unused cube slot */
    uint32_t         *decode_buf;          /* decode scratch, grown on demand */
    size_t            decode_cap;

    /* The attachments of the open dynamic-rendering scope, if any. */
    int               rendering;
    VkbSurface       *rt_color;
    VkbDepth         *rt_depth;

    uint32_t          frame_draws;         /* draw() calls since the last flip */
    int               first_draw_logged;
    uint32_t          stat_decodes;        /* texture decodes since the last log */
    uint64_t          stat_decode_px;      /* ... and their texels */
    uint32_t          stat_new;            /* ... of which first sight / cache misses */
    uint32_t          stat_hashes;         /* texture re-validations since the last log */
    uint64_t          stat_hash_bytes;     /* ... and the guest bytes they read */
    uint32_t          last_frame_draws;
    uint32_t          title_ms, title_flips;   /* window-title FPS counter */

    const char       *dump_prefix;
    uint32_t          dump_every;

    /* The NV2A video overlay (movies), composed on top at present time. */
    VkImage           ov_image;
    VkDeviceMemory    ov_memory;
    VkImageView       ov_view;
    VkImageLayout     ov_layout;
    uint32_t         *ov_host;             /* VKB_OV_W x VKB_OV_H XRGB */
    uint64_t          ov_uploaded;         /* overlay seq in ov_image */
    uint64_t          ov_presented;        /* overlay seq last presented */
} V;

#define VKB_OV_W 640u
#define VKB_OV_H 480u

/* ---- helpers --------------------------------------------------------- */

#define VK_CHECK(call)                                                      \
    do {                                                                    \
        VkResult r_ = (call);                                               \
        if (r_ != VK_SUCCESS) {                                             \
            fprintf(stderr, "  [VK] %s failed: %d\n", #call, (int)r_);      \
            return 0;                                                       \
        }                                                                   \
    } while (0)

static uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags want)
{
    uint32_t i;
    for (i = 0; i < V.memprops.memoryTypeCount; i++)
        if ((type_bits & (1u << i))
                && (V.memprops.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

/* One layout transition with deliberately broad masks: correctness first,
 * milestone 4 narrows them. */
static void transition(VkImage image, VkImageAspectFlags aspect,
                       VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier b;

    memset(&b, 0, sizeof b);
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;   /* cube maps: 6 */
    vkCmdPipelineBarrier(V.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                         0, NULL, 0, NULL, 1, &b);
}

static void surface_to(VkbSurface *s, VkImageLayout to)
{
    if (s->layout != to) {
        transition(s->image, VK_IMAGE_ASPECT_COLOR_BIT, s->layout, to);
        s->layout = to;
    }
}

/* ---- device and window ------------------------------------------------ */

static int create_instance(void)
{
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    const char *exts[16];
    unsigned n = 16;
    const char *layers[1];
    uint32_t nlayers = 0;

    if (!SDL_Vulkan_GetInstanceExtensions(V.window, &n, exts)) {
        fprintf(stderr, "  [VK] SDL_Vulkan_GetInstanceExtensions: %s\n", SDL_GetError());
        return 0;
    }
    if (getenv("AMPED2_VK_VALIDATE")) {
        uint32_t count = 0, i;
        VkLayerProperties props[64];
        vkEnumerateInstanceLayerProperties(&count, NULL);
        if (count > 64) count = 64;
        vkEnumerateInstanceLayerProperties(&count, props);
        for (i = 0; i < count; i++)
            if (!strcmp(props[i].layerName, "VK_LAYER_KHRONOS_validation"))
                layers[nlayers++] = "VK_LAYER_KHRONOS_validation";
        fprintf(stderr, "  [VK] validation layer %s\n",
                nlayers ? "enabled" : "requested but not installed");
    }

    memset(&app, 0, sizeof app);
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Amped 2 Recomp";
    app.pEngineName = "xboxrecomp";
    app.apiVersion = VK_API_VERSION_1_3;

    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = n;
    ci.ppEnabledExtensionNames = exts;
    ci.enabledLayerCount = nlayers;
    ci.ppEnabledLayerNames = layers;
    VK_CHECK(vkCreateInstance(&ci, NULL, &V.instance));
    volkLoadInstance(V.instance);
    return 1;
}

/* Prefer a discrete GPU that has a queue family doing graphics and present
 * and speaks Vulkan 1.3; AMPED2_VK_GPU overrides by name. */
static int pick_gpu(void)
{
    VkPhysicalDevice devs[16];
    uint32_t n = 16, i, best_score = 0;
    const char *want = getenv("AMPED2_VK_GPU");

    vkEnumeratePhysicalDevices(V.instance, &n, devs);
    for (i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        VkQueueFamilyProperties qf[16];
        uint32_t nq = 16, q, score;

        vkGetPhysicalDeviceProperties(devs[i], &p);
        if (p.apiVersion < VK_API_VERSION_1_3)
            continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qf);
        for (q = 0; q < nq; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, V.wsi, &present);
            if ((qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
                break;
        }
        if (q == nq)
            continue;
        score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
              : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (want && strstr(p.deviceName, want))
            score = 100;
        if (score > best_score) {
            best_score = score;
            V.gpu = devs[i];
            V.queue_family = q;
        }
    }
    if (!V.gpu) {
        fprintf(stderr, "  [VK] no Vulkan 1.3 GPU with graphics+present\n");
        return 0;
    }
    {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(V.gpu, &p);
        fprintf(stderr, "  [VK] GPU: %s (Vulkan %u.%u)\n", p.deviceName,
                VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion));
        V.ubo_align = p.limits.minUniformBufferOffsetAlignment;
        if (V.ubo_align < 16)
            V.ubo_align = 16;
        V.ssbo_align = p.limits.minStorageBufferOffsetAlignment;
        if (V.ssbo_align < 16)
            V.ssbo_align = 16;
    }
    vkGetPhysicalDeviceMemoryProperties(V.gpu, &V.memprops);
    return 1;
}

static int device_has_extension(const char *name)
{
    VkExtensionProperties props[512];
    uint32_t n = 512, i;
    vkEnumerateDeviceExtensionProperties(V.gpu, NULL, &n, props);
    for (i = 0; i < n; i++)
        if (!strcmp(props[i].extensionName, name))
            return 1;
    return 0;
}

/* Vulkan 1.3 core gives dynamic rendering and the first two dynamic-state
 * extensions (cull, depth, stencil); VK_EXT_extended_dynamic_state3 adds the
 * blend state, so one pipeline covers every NV2A state combination. Push
 * descriptors bind each batch's texture without descriptor pools. */
static int create_device(void)
{
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci;
    VkPhysicalDeviceVulkan13Features f13;
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3, have3;
    VkPhysicalDeviceFeatures2 query;
    VkDeviceCreateInfo ci;
    const char *exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
        VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
    };
    uint32_t i;

    for (i = 0; i < sizeof exts / sizeof exts[0]; i++)
        if (!device_has_extension(exts[i])) {
            fprintf(stderr, "  [VK] device lacks %s\n", exts[i]);
            return 0;
        }
    memset(&have3, 0, sizeof have3);
    have3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
    memset(&query, 0, sizeof query);
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &have3;
    vkGetPhysicalDeviceFeatures2(V.gpu, &query);
    if (!have3.extendedDynamicState3ColorBlendEnable
            || !have3.extendedDynamicState3ColorBlendEquation
            || !have3.extendedDynamicState3ColorWriteMask) {
        fprintf(stderr, "  [VK] device lacks dynamic blend state (EDS3)\n");
        return 0;
    }

    memset(&qci, 0, sizeof qci);
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = V.queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    memset(&eds3, 0, sizeof eds3);
    eds3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
    eds3.extendedDynamicState3ColorBlendEnable = VK_TRUE;
    eds3.extendedDynamicState3ColorBlendEquation = VK_TRUE;
    eds3.extendedDynamicState3ColorWriteMask = VK_TRUE;
    memset(&f13, 0, sizeof f13);
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.pNext = &eds3;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;

    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pNext = &f13;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = sizeof exts / sizeof exts[0];
    ci.ppEnabledExtensionNames = exts;
    VK_CHECK(vkCreateDevice(V.gpu, &ci, NULL, &V.device));
    volkLoadDevice(V.device);
    vkGetDeviceQueue(V.device, V.queue_family, 0, &V.queue);

    /* Z24S8 is the NV2A's format; not every GPU renders to it (AMD does not),
     * so fall back to 32-bit float depth with 8-bit stencil. */
    {
        static const VkFormat cand[] = { VK_FORMAT_D24_UNORM_S8_UINT,
                                         VK_FORMAT_D32_SFLOAT_S8_UINT };
        for (i = 0; i < 2; i++) {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(V.gpu, cand[i], &fp);
            if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                V.depth_format = cand[i];
                break;
            }
        }
        if (!V.depth_format) {
            fprintf(stderr, "  [VK] no depth/stencil format\n");
            return 0;
        }
    }
    return 1;
}

static void destroy_swapchain(void)
{
    uint32_t i;
    if (!V.swapchain)
        return;
    vkDeviceWaitIdle(V.device);
    for (i = 0; i < V.swap_count; i++)
        vkDestroySemaphore(V.device, V.sem_render_done[i], NULL);
    vkDestroySwapchainKHR(V.device, V.swapchain, NULL);
    V.swapchain = VK_NULL_HANDLE;
}

/* The title paces itself on the emulated VBlank, so presentation must not
 * block the NV2A thread on the host's refresh: MAILBOX, else IMMEDIATE, else
 * FIFO (always available). Unlocked at the display's refresh rate is the
 * exception: FIFO is then the pacing (frame_pace for the other limits). */
static int create_swapchain(void)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[32];
    VkPresentModeKHR modes[8];
    uint32_t nf = 32, nm = 8, i;
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainCreateInfoKHR ci;
    int w, h;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(V.gpu, V.wsi, &caps);
    vkGetPhysicalDeviceSurfaceFormatsKHR(V.gpu, V.wsi, &nf, formats);
    vkGetPhysicalDeviceSurfacePresentModesKHR(V.gpu, V.wsi, &nm, modes);

    V.swap_format = formats[0].format;
    for (i = 0; i < nf; i++)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
            V.swap_format = VK_FORMAT_B8G8R8A8_UNORM;
    for (i = 0; i < nm; i++)
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            mode = VK_PRESENT_MODE_MAILBOX_KHR;
    if (mode != VK_PRESENT_MODE_MAILBOX_KHR)
        for (i = 0; i < nm; i++)
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
                mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (g_amped2_settings.framerate == AMPED2_FPS_UNLOCKED && g_amped2_settings.frame_limit == 0)
        mode = VK_PRESENT_MODE_FIFO_KHR;
    V.present_mode = mode;

    SDL_Vulkan_GetDrawableSize(V.window, &w, &h);
    if (caps.currentExtent.width != UINT32_MAX) {
        V.swap_extent = caps.currentExtent;
    } else {
        V.swap_extent.width = (uint32_t)w;
        V.swap_extent.height = (uint32_t)h;
    }
    if (!V.swap_extent.width || !V.swap_extent.height)
        return 0;                          /* minimised: try next flip */

    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = V.wsi;
    ci.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount)
        ci.minImageCount = caps.maxImageCount;
    ci.imageFormat = V.swap_format;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = V.swap_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(V.device, &ci, NULL, &V.swapchain));

    V.swap_count = VKB_MAX_SWAP_IMAGES;
    vkGetSwapchainImagesKHR(V.device, V.swapchain, &V.swap_count, V.swap_images);
    for (i = 0; i < V.swap_count; i++) {
        VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
        VK_CHECK(vkCreateSemaphore(V.device, &sci, NULL, &V.sem_render_done[i]));
    }
    V.swap_dirty = 0;
    fprintf(stderr, "  [VK] swapchain %ux%u, %u images, %s\n",
            V.swap_extent.width, V.swap_extent.height, V.swap_count,
            mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox"
            : mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" : "fifo");
    return 1;
}

static int ring_create(VkbRing *r, VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo bci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;

    memset(&bci, 0, sizeof bci);
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    VK_CHECK(vkCreateBuffer(V.device, &bci, NULL, &r->buffer));
    vkGetBufferMemoryRequirements(V.device, r->buffer, &req);
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX)
        return 0;
    VK_CHECK(vkAllocateMemory(V.device, &ai, NULL, &r->memory));
    VK_CHECK(vkBindBufferMemory(V.device, r->buffer, r->memory, 0));
    VK_CHECK(vkMapMemory(V.device, r->memory, 0, size, 0, (void **)&r->mapped));
    r->size = size;
    r->used = 0;
    r->wanted = 0;
    r->usage = usage;
    return 1;
}

/* Space for `bytes` in this frame's slice of the ring, or NULL when full. */
static void *ring_alloc(VkbRing *r, VkDeviceSize bytes, VkDeviceSize align,
                        VkDeviceSize *offset)
{
    VkDeviceSize at = (r->used + align - 1) & ~(align - 1);
    r->wanted = ((r->wanted + align - 1) & ~(align - 1)) + bytes;
    if (at + bytes > r->size)
        return NULL;
    r->used = at + bytes;
    *offset = at;
    return r->mapped + at;
}

/* Start a frame's slices. A ring that had to refuse allocations in the last
 * frame (the frame's draws after that were dropped: a race's HUD, the rider's
 * upper body and hair come late in the frame) grows first; the GPU is idle on
 * it here (frame fence). */
static void ring_reset(VkbRing *r, const char *what)
{
    if (r->wanted > r->size && r->buffer) {
        VkDeviceSize size = r->size;
        VkBufferUsageFlags usage = r->usage;
        while (size < r->wanted + r->wanted / 4 && size < (VkDeviceSize)1 << 30)
            size *= 2;
        vkUnmapMemory(V.device, r->memory);
        vkDestroyBuffer(V.device, r->buffer, NULL);
        vkFreeMemory(V.device, r->memory, NULL);
        memset(r, 0, sizeof *r);
        if (!ring_create(r, size, usage)) {
            fprintf(stderr, "  [VK] %s ring: %llu MB failed\n", what,
                    (unsigned long long)(size >> 20));
            return;
        }
        fprintf(stderr, "  [VK] %s ring grown to %llu MB\n", what,
                (unsigned long long)(size >> 20));
    }
    r->used = 0;
    r->wanted = 0;
}

/* A device-local 2D image with one view; with cube set, six layers and a
 * cube view (w == h). */
static int image_create_ex(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                           VkImageAspectFlags aspect, int cube, VkImage *img,
                           VkDeviceMemory *mem, VkImageView *view)
{
    VkImageCreateInfo ci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    VkImageViewCreateInfo vci;

    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent.width = w;
    ci.extent.height = h;
    ci.extent.depth = 1;
    ci.mipLevels = 1;
    ci.arrayLayers = cube ? 6 : 1;
    ci.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(V.device, &ci, NULL, img));
    vkGetImageMemoryRequirements(V.device, *img, &req);
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX)
        return 0;
    VK_CHECK(vkAllocateMemory(V.device, &ai, NULL, mem));
    VK_CHECK(vkBindImageMemory(V.device, *img, *mem, 0));
    memset(&vci, 0, sizeof vci);
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = *img;
    vci.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = aspect;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = cube ? 6 : 1;
    VK_CHECK(vkCreateImageView(V.device, &vci, NULL, view));
    return 1;
}

static int image_create(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                        VkImageAspectFlags aspect, VkImage *img,
                        VkDeviceMemory *mem, VkImageView *view)
{
    return image_create_ex(w, h, fmt, usage, aspect, 0, img, mem, view);
}

static VkShaderModule shader_module(const uint32_t *code, size_t bytes)
{
    VkShaderModuleCreateInfo ci;
    VkShaderModule m = VK_NULL_HANDLE;

    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode = code;
    vkCreateShaderModule(V.device, &ci, NULL, &m);
    return m;
}

/* The one graphics pipeline. Everything the NV2A changes per batch is dynamic
 * state; what stays fixed is the vertex layout (Nv2aVertex), triangle lists,
 * the attachment formats and filled polygons. */
static int create_pipeline(void)
{
    VkDescriptorSetLayoutBinding bind[VKB_NBIND_VSH];
    VkDescriptorSetLayoutCreateInfo dslci;
    VkPushConstantRange pcr;
    VkPipelineLayoutCreateInfo plci;
    VkShaderModule vs, fs;
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription vbind[2];
    VkVertexInputAttributeDescription vattr[9];
    VkPipelineVertexInputStateCreateInfo vi;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportStateCreateInfo vp;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineColorBlendAttachmentState cba;
    VkPipelineColorBlendStateCreateInfo cb;
    VkPipelineDynamicStateCreateInfo dyn;
    VkPipelineRenderingCreateInfo prci;
    VkGraphicsPipelineCreateInfo gpci;
    VkFormat color_fmt = VK_FORMAT_B8G8R8A8_UNORM;
    static const VkDynamicState dynamic[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT, VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
        VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,
        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,
    };
    int i;

    memset(bind, 0, sizeof bind);
    for (i = 0; i < VKB_NBIND_VSH; i++) {
        bind[i].binding = (uint32_t)i;
        bind[i].descriptorType = i == VKB_BIND_VDATA ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                               : (i == VKB_BIND_RC || i >= VKB_NBIND)
                                 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                 : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bind[i].descriptorCount = 1;
        bind[i].stageFlags = i >= VKB_NBIND ? VK_SHADER_STAGE_VERTEX_BIT
                                            : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    memset(&dslci, 0, sizeof dslci);
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dslci.bindingCount = VKB_NBIND_VSH;
    dslci.pBindings = bind;
    VK_CHECK(vkCreateDescriptorSetLayout(V.device, &dslci, NULL, &V.set_layout));

    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(VkbPush);
    memset(&plci, 0, sizeof plci);
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &V.set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(V.device, &plci, NULL, &V.pipe_layout));

    vs = shader_module(k_vert_spv, sizeof k_vert_spv);
    fs = shader_module(k_frag_spv, sizeof k_frag_spv);
    if (!vs || !fs)
        return 0;
    memset(stages, 0, sizeof stages);
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;

    /* Binding 0: Nv2aVertex. Binding 1: Nv2aVertexExt, its stride dynamic so
     * a batch without the extension can bind one zeroed element (stride 0). */
    vbind[0].binding = 0;
    vbind[0].stride = sizeof(Nv2aVertex);
    vbind[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vbind[1].binding = 1;
    vbind[1].stride = sizeof(Nv2aVertexExt);
    vbind[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vattr[0].location = 0; vattr[0].binding = 0;
    vattr[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vattr[0].offset = (uint32_t)offsetof(Nv2aVertex, x);
    vattr[1].location = 1; vattr[1].binding = 0;
    vattr[1].format = VK_FORMAT_B8G8R8A8_UNORM;        /* 0xAARRGGBB, LE bytes B,G,R,A */
    vattr[1].offset = (uint32_t)offsetof(Nv2aVertex, diffuse);
    vattr[2].location = 2; vattr[2].binding = 0;
    vattr[2].format = VK_FORMAT_R32G32_SFLOAT;
    vattr[2].offset = (uint32_t)offsetof(Nv2aVertex, u);
    for (i = 0; i < 4; i++) {
        vattr[3 + i].location = 3 + (uint32_t)i; vattr[3 + i].binding = 1;
        vattr[3 + i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vattr[3 + i].offset = (uint32_t)(offsetof(Nv2aVertexExt, tex) + i * 16);
    }
    vattr[7].location = 7; vattr[7].binding = 1;
    vattr[7].format = VK_FORMAT_B8G8R8A8_UNORM;
    vattr[7].offset = (uint32_t)offsetof(Nv2aVertexExt, specular);
    vattr[8].location = 8; vattr[8].binding = 1;
    vattr[8].format = VK_FORMAT_R32_SFLOAT;
    vattr[8].offset = (uint32_t)offsetof(Nv2aVertexExt, fog);
    memset(&vi, 0, sizeof vi);
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = vbind;
    vi.vertexAttributeDescriptionCount = 9;
    vi.pVertexAttributeDescriptions = vattr;

    memset(&ia, 0, sizeof ia);
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    memset(&vp, 0, sizeof vp);
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    memset(&rs, 0, sizeof rs);
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    /* The executor already clipped at the near plane; window z outside the
     * buffer's range is clamped, not clipped, as SET_CLIP_MIN/MAX only clip
     * what the title asks for (nv2a_backend.h notes). */
    rs.depthClampEnable = VK_FALSE;
    memset(&ms, 0, sizeof ms);
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    memset(&ds, 0, sizeof ds);
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    memset(&cba, 0, sizeof cba);
    cba.colorWriteMask = 0xF;
    memset(&cb, 0, sizeof cb);
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    memset(&dyn, 0, sizeof dyn);
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = sizeof dynamic / sizeof dynamic[0];
    dyn.pDynamicStates = dynamic;
    memset(&prci, 0, sizeof prci);
    prci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    prci.colorAttachmentCount = 1;
    prci.pColorAttachmentFormats = &color_fmt;
    prci.depthAttachmentFormat = V.depth_format;
    prci.stencilAttachmentFormat = V.depth_format;

    memset(&gpci, 0, sizeof gpci);
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.pNext = &prci;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dyn;
    gpci.layout = V.pipe_layout;
    VK_CHECK(vkCreateGraphicsPipelines(V.device, VK_NULL_HANDLE, 1, &gpci, NULL, &V.pipeline));
    vkDestroyShaderModule(V.device, vs, NULL);

    /* The same pipeline with nv2a_vsh.vert: no vertex input (it reads the
     * vertex bytes from binding 12 by gl_VertexIndex), so no dynamic stride.
     * Without it every program batch takes the executor's CPU path. */
    if (!getenv("AMPED2_VK_CPU_VSH") && (vs = shader_module(k_vsh_spv, sizeof k_vsh_spv))) {
        VkResult r;
        stages[0].module = vs;
        memset(&vi, 0, sizeof vi);
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        dyn.dynamicStateCount = sizeof dynamic / sizeof dynamic[0] - 1;
        r = vkCreateGraphicsPipelines(V.device, VK_NULL_HANDLE, 1, &gpci, NULL, &V.pipeline_vsh);
        if (r != VK_SUCCESS) {
            fprintf(stderr, "  [VK] vertex-program pipeline failed (%d): CPU transforms\n", (int)r);
            V.pipeline_vsh = VK_NULL_HANDLE;
        }
        vkDestroyShaderModule(V.device, vs, NULL);
    }
    vkDestroyShaderModule(V.device, fs, NULL);
    return 1;
}

/* ---- window: mode, display, size (settings) --------------------------- */

static int window_display(void)
{
    int n = SDL_GetNumVideoDisplays(), d = g_amped2_settings.display;
    return (d >= 0 && d < n) ? d : 0;
}

/* The game window as the settings ask for it. Windowed: 2/3 of the display's
 * height (at least 480) at the aspect ratio, centred on the chosen display. */
static SDL_Window *open_window(const char *title)
{
    int d = window_display(), h = 960, w;
    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
    SDL_DisplayMode dm;
    SDL_Window *win;

    if (SDL_GetDesktopDisplayMode(d, &dm) == 0 && dm.h > 0) {
        h = dm.h * 2 / 3;
        if (h < 480) h = 480;
    }
    w = (int)(h * amped2_aspect_value(g_amped2_settings.aspect_ratio) + 0.5);
    if (g_amped2_settings.window_mode == AMPED2_FULLSCREEN && SDL_GetDesktopDisplayMode(d, &dm) == 0) {
        w = dm.w;                          /* exclusive, at the desktop's mode */
        h = dm.h;
        flags |= SDL_WINDOW_FULLSCREEN;
    } else if (g_amped2_settings.window_mode == AMPED2_BORDERLESS) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED_DISPLAY(d),
                           SDL_WINDOWPOS_CENTERED_DISPLAY(d), w, h, flags);
    if (win)
        fprintf(stderr, "  [VK] window: %s on display %d (%s)\n",
                g_amped2_settings.window_mode == AMPED2_FULLSCREEN ? "fullscreen"
                : g_amped2_settings.window_mode == AMPED2_BORDERLESS ? "borderless" : "windowed",
                d, SDL_GetDisplayName(d) ? SDL_GetDisplayName(d) : "?");
    return win;
}

/* Alt+Enter / F11: windowed <-> the configured fullscreen mode (borderless if
 * the setting is windowed). On the thread that owns the window. */
static void toggle_fullscreen(void)
{
    Uint32 now = SDL_GetWindowFlags(V.window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    Uint32 to = 0;

    if (!now)
        to = g_amped2_settings.window_mode == AMPED2_FULLSCREEN ? SDL_WINDOW_FULLSCREEN
                                                                : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (to == SDL_WINDOW_FULLSCREEN) {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(V.window), &dm) == 0)
            SDL_SetWindowDisplayMode(V.window, &dm);
    }
    SDL_SetWindowFullscreen(V.window, to);
    V.swap_dirty = 1;
}

/* Window events both pumps share; 1 if the event was a window hotkey (kept
 * from the game: Enter is Start on the keyboard pad). */
static int window_event(const SDL_Event *ev)
{
    if (ev->type == SDL_WINDOWEVENT
            && (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED
                || ev->window.event == SDL_WINDOWEVENT_RESIZED))
        V.swap_dirty = 1;
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat
            && (ev->key.keysym.sym == SDLK_F11
                || (ev->key.keysym.sym == SDLK_RETURN && (ev->key.keysym.mod & KMOD_ALT)))) {
        toggle_fullscreen();
        return 1;
    }
    return 0;
}

static int init_vulkan(void)
{
    VkCommandPoolCreateInfo pci;
    VkCommandBufferAllocateInfo ai;
    VkFenceCreateInfo fci;
    VkSemaphoreCreateInfo sci;
    const char *e;

    if (volkInitialize() != VK_SUCCESS) {
        fprintf(stderr, "  [VK] no Vulkan loader (libvulkan / vulkan-1.dll)\n");
        return 0;
    }
    if (!V.window) {
        /* No amped2_vk_host_main: open the window here, on the NV2A thread,
         * which then also pumps its events (from flip). */
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "  [VK] SDL video: %s\n", SDL_GetError());
            return 0;
        }
        V.window = open_window("Amped 2 (Recomp, Vulkan)");
        if (!V.window) {
            fprintf(stderr, "  [VK] SDL_CreateWindow: %s\n", SDL_GetError());
            return 0;
        }
        xbox_InputSdlInit();
    }
    if (!create_instance())
        return 0;
    if (!SDL_Vulkan_CreateSurface(V.window, V.instance, &V.wsi)) {
        fprintf(stderr, "  [VK] SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
        return 0;
    }
    if (!pick_gpu() || !create_device() || !create_swapchain() || !create_pipeline())
        return 0;
    if (!ring_create(&V.vring, VKB_VERTEX_RING,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                     | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            || !ring_create(&V.staging, VKB_STAGING_RING, VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
        return 0;

    memset(&pci, 0, sizeof pci);
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = V.queue_family;
    VK_CHECK(vkCreateCommandPool(V.device, &pci, NULL, &V.cmd_pool));
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = V.cmd_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(V.device, &ai, &V.cmd));
    memset(&fci, 0, sizeof fci);
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(V.device, &fci, NULL, &V.frame_fence));
    memset(&sci, 0, sizeof sci);
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(V.device, &sci, NULL, &V.sem_acquire));

    V.scale = (uint32_t)g_amped2_settings.resolution_scale;
    if (V.scale < 1 || V.scale > AMPED2_SCALE_MAX)
        V.scale = 1;
    V.widen = amped2_aspect_value(g_amped2_settings.aspect_ratio) / (4.0 / 3.0);
    if (V.widen < 1.0)
        V.widen = 1.0;
    fprintf(stderr, "  [VK] internal resolution %ux, aspect ratio %s\n", V.scale,
            amped2_aspect_name(g_amped2_settings.aspect_ratio));
    V.dump_prefix = getenv("AMPED2_VK_DUMP");
    e = getenv("AMPED2_VK_DUMP_EVERY");
    V.dump_every = e && atoi(e) > 0 ? (uint32_t)atoi(e) : 60;
    return 1;
}

/* First call from the executor brings everything up; a failure leaves the
 * back end inert (the title keeps running, nothing is drawn). */
static int ensure_ready(void)
{
    if (V.ready)
        return 1;
    if (V.failed)
        return 0;
    if (!init_vulkan()) {
        V.failed = 1;
        fprintf(stderr, "  [VK] back end disabled\n");
        return 0;
    }
    V.ready = 1;
    return 1;
}

static int ensure_recording(void)
{
    VkCommandBufferBeginInfo bi;

    if (V.recording)
        return 1;
    vkWaitForFences(V.device, 1, &V.frame_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(V.device, 1, &V.frame_fence);
    vkResetCommandBuffer(V.cmd, 0);
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(V.cmd, &bi));
    V.recording = 1;
    V.bound = VK_NULL_HANDLE;
    V.frame_serial++;
    /* The previous frame has retired (fence above): its ring slices are free,
     * and a ring that refused some of them can be replaced by a larger one. */
    ring_reset(&V.vring, "vertex");
    ring_reset(&V.staging, "staging");
    V.last_rc_off = ~(VkDeviceSize)0;
    return 1;
}

/* ---- surfaces --------------------------------------------------------- */

static void end_rendering(void)
{
    if (V.rendering) {
        vkCmdEndRendering(V.cmd);
        V.rendering = 0;
    }
}

static void surface_free(VkbSurface *s)
{
    if (s->view)
        vkDestroyImageView(V.device, s->view, NULL);
    if (s->image)
        vkDestroyImage(V.device, s->image, NULL);
    if (s->memory)
        vkFreeMemory(V.device, s->memory, NULL);
    memset(s, 0, sizeof *s);
}

/* Whether the title has flipped the surface at va: a back buffer, which gets
 * the aspect ratio. Known from the first flip on; the first frames are drawn
 * 4:3, then the surface is re-created wide (surface_get). */
static int surface_shown(uint32_t va)
{
    int i;
    for (i = 0; i < 4; i++)
        if (V.shown_va[i] == va)
            return 1;
    return 0;
}

static void note_shown(uint32_t va)
{
    int i;
    if (!va || surface_shown(va))
        return;
    for (i = 0; i < 4; i++)
        if (!V.shown_va[i]) {
            V.shown_va[i] = va;
            return;
        }
}

/* Evicting an image the frame being recorded still references would free it
 * under the command buffer, so only images unused this frame are candidates;
 * a full cache of in-use images fails the request (logged once). */
static VkbSurface *surface_get(const Nv2aSurface *g)
{
    VkbSurface *s, *victim = NULL;
    int i, wide = V.widen > 1.0 && surface_shown(g->color_va);

    for (i = 0; i < VKB_MAX_SURFACES; i++) {
        s = &V.surfaces[i];
        if (s->image && s->va == g->color_va && s->gw == g->width && s->gh == g->height) {
            if (s->wide != wide && s->last_used != V.flips) {
                /* Became a back buffer: re-create it wide (below). */
                vkDeviceWaitIdle(V.device);
                if (V.last == s) V.last = NULL;
                surface_free(s);
                break;
            }
            s->last_used = V.flips;
            return s;
        }
    }
    for (i = 0; i < VKB_MAX_SURFACES; i++) {
        s = &V.surfaces[i];
        if (!s->image) { victim = s; break; }
        if (s->last_used != V.flips && (!victim || s->last_used < victim->last_used))
            victim = s;
    }
    if (!victim) {
        static int warned;
        if (!warned++)
            fprintf(stderr, "  [VK] surface cache full\n");
        return NULL;
    }
    if (victim->image) {
        vkDeviceWaitIdle(V.device);
        if (V.last == victim) V.last = NULL;
        surface_free(victim);
    }
    s = victim;
    s->gw = g->width;
    s->gh = g->height;
    s->wide = wide;
    s->width = g->width * V.scale;
    s->height = g->height * V.scale;
    if (wide)
        s->width = (uint32_t)((double)s->width * V.widen + 0.5) & ~1u;
    if (!image_create(s->width, s->height, VK_FORMAT_B8G8R8A8_UNORM,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, &s->image, &s->memory, &s->view)) {
        surface_free(s);
        return NULL;
    }
    s->va = g->color_va;
    s->logical_w = g->width / (g->aa_sx ? g->aa_sx : 1);
    s->logical_h = g->height / (g->aa_sy ? g->aa_sy : 1);
    s->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    s->last_used = V.flips;
    fprintf(stderr, "  [VK] surface 0x%08X %ux%u (logical %ux%u) as %ux%u%s\n",
            s->va, s->gw, s->gh, s->logical_w, s->logical_h, s->width, s->height,
            s->wide ? ", widened" : "");
    return s;
}

static void depth_free(VkbDepth *d)
{
    if (d->view)
        vkDestroyImageView(V.device, d->view, NULL);
    if (d->image)
        vkDestroyImage(V.device, d->image, NULL);
    if (d->memory)
        vkFreeMemory(V.device, d->memory, NULL);
    memset(d, 0, sizeof *d);
}

/* The depth/stencil image for a guest zeta surface. va 0 is a scratch buffer
 * for draws without one: the pipeline always has a depth attachment, which
 * keeps it a single pipeline; such draws run with the depth test off. */
static VkbDepth *depth_get(uint32_t va, uint32_t w, uint32_t h)
{
    VkbDepth *d, *victim = NULL;
    int i;

    for (i = 0; i < VKB_MAX_SURFACES; i++) {
        d = &V.depths[i];
        if (d->image && d->va == va && d->width == w && d->height == h) {
            d->last_used = V.flips;
            return d;
        }
    }
    for (i = 0; i < VKB_MAX_SURFACES; i++) {
        d = &V.depths[i];
        if (!d->image) { victim = d; break; }
        if (d->last_used != V.flips && (!victim || d->last_used < victim->last_used))
            victim = d;
    }
    if (!victim)
        return NULL;
    if (victim->image) {
        vkDeviceWaitIdle(V.device);
        depth_free(victim);
    }
    d = victim;
    if (!image_create(w, h, V.depth_format,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                      | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                      &d->image, &d->memory, &d->view)) {
        depth_free(d);
        return NULL;
    }
    d->va = va;
    d->width = w;
    d->height = h;
    d->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    d->last_used = V.flips;
    return d;
}

static void depth_to(VkbDepth *d, VkImageLayout to)
{
    if (d->layout != to) {
        transition(d->image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                   d->layout, to);
        d->layout = to;
    }
}

/* Open (or keep) a dynamic-rendering scope on these attachments. */
static void begin_rendering(VkbSurface *c, VkbDepth *d)
{
    VkRenderingAttachmentInfo ca, da;
    VkRenderingInfo ri;
    VkViewport vp;
    VkRect2D sc;

    if (V.rendering && V.rt_color == c && V.rt_depth == d)
        return;
    end_rendering();
    surface_to(c, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    depth_to(d, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    memset(&ca, 0, sizeof ca);
    ca.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ca.imageView = c->view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    da = ca;
    da.imageView = d->view;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    memset(&ri, 0, sizeof ri);
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent.width = c->width;
    ri.renderArea.extent.height = c->height;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    ri.pDepthAttachment = &da;
    ri.pStencilAttachment = &da;
    vkCmdBeginRendering(V.cmd, &ri);
    V.rendering = 1;
    V.rt_color = c;
    V.rt_depth = d;

    vp.x = 0; vp.y = 0;
    vp.width = (float)c->width; vp.height = (float)c->height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(V.cmd, 0, 1, &vp);
    sc.offset.x = 0; sc.offset.y = 0;
    sc.extent.width = c->width; sc.extent.height = c->height;
    vkCmdSetScissor(V.cmd, 0, 1, &sc);
}

/* ---- textures ------------------------------------------------------------ */

extern ptrdiff_t xbox_GetMemoryOffset(void);   /* xbox_memory_layout.h */

/* Hash of the guest bytes a texture lives in, to notice when the title has
 * rewritten it. Raw memory, not decoded texels: decoding every texture every
 * frame just to compare it was a third of the NV2A thread's time.
 *
 * Level 0's exact size: an upper bound (w*h*4 for everything swizzled) took
 * in whatever lies behind a DXT texture, and in a race that is data the
 * title rewrites every frame -- ~20 textures and 1.9 M texels were decoded
 * again each frame for nothing (race at 8-10 fps). Clamped to the guest
 * region the texture lies in. 64-bit multiply-xorshift per 8 bytes: several
 * GB/s, so a frame's worth of textures costs well under a millisecond. */
static uint64_t texture_bytes_face(const Nv2aTexture *g)
{
    uint64_t w = g->width, h = g->height;

    if (g->pitch)
        return (uint64_t)g->pitch * h;
    switch (g->color) {
    case 0x0C:                                  /* DXT1: 8 bytes per 4x4 */
        return ((w + 3) / 4) * ((h + 3) / 4) * 8;
    case 0x0E: case 0x0F:                       /* DXT3/5: 16 bytes per 4x4 */
        return ((w + 3) / 4) * ((h + 3) / 4) * 16;
    case 0x00: case 0x01: case 0x0B: case 0x19: /* Y8, AY8, P8, A8 */
        return w * h;
    case 0x02: case 0x03: case 0x04: case 0x05: /* A1R5G5B5 .. R5G6B5 */
    case 0x1A: case 0x27: case 0x28:            /* A8Y8, R6G5B5, G8B8 */
        return w * h * 2;
    default:                                    /* 32 bpp, or unknown: bound */
        return w * h * 4;
    }
}

/* A cube map: faces 0-4 with their mip levels, then face 5's level 0. */
static uint64_t texture_bytes(const Nv2aTexture *g)
{
    return (g->cube_stride ? 5ull * g->cube_stride : 0) + texture_bytes_face(g);
}

/* Whether the batch samples g as a cube map: a Vulkan cube needs square
 * faces, anything else reads face 0 as 2D. */
static int tex_is_cube(const Nv2aTexture *g)
{
    return g && g->cube_stride && g->width == g->height;
}

static uint64_t hash_guest_texture(const Nv2aTexture *g)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint64_t va = g->offset, bytes, limit, h = 0x9E3779B97F4A7C15ull, w;
    const uint8_t *p;
    size_t i, n;

    bytes = texture_bytes(g);
    if (va >= 0x80000000ull && va < 0x84000000ull)
        limit = 0x84000000ull - va;            /* contiguous window */
    else if (va < 0x04000000ull)
        limit = 0x04000000ull - va;            /* main RAM */
    else
        return 0;
    if (bytes > limit)
        bytes = limit;
    p = mem + va;
    n = (size_t)(bytes / 8);
    V.stat_hashes++;
    V.stat_hash_bytes += bytes;
    /* Four independent lanes: one multiply chain is latency-bound (~5 cycles
     * per 8 bytes); a race re-validates several hundred MB of textures a
     * second. */
    {
        uint64_t l[4] = { h, h ^ 0x632BE59BD9B4E019ull, h ^ 0x85EBCA77C2B2AE63ull,
                          h ^ 0x27D4EB2F165667C5ull };
        for (i = 0; i + 4 <= n; i += 4) {
            int k;
            for (k = 0; k < 4; k++) {
                memcpy(&w, p + (i + k) * 8, 8);
                l[k] ^= w;
                l[k] *= 0xBF58476D1CE4E5B9ull;
                l[k] ^= l[k] >> 31;
            }
        }
        for (; i < n; i++) {
            memcpy(&w, p + i * 8, 8);
            l[0] ^= w;
            l[0] *= 0xBF58476D1CE4E5B9ull;
            l[0] ^= l[0] >> 31;
        }
        h = l[0] ^ (l[1] * 0x9E3779B97F4A7C15ull) ^ (l[2] >> 7) ^ (l[3] << 13) ^ l[3];
    }
    return h | 1;                              /* 0 means "never uploaded" */
}

static void texture_free(VkbTexture *t)
{
    if (t->view)
        vkDestroyImageView(V.device, t->view, NULL);
    if (t->image)
        vkDestroyImage(V.device, t->image, NULL);
    if (t->memory)
        vkFreeMemory(V.device, t->memory, NULL);
    memset(t, 0, sizeof *t);
}

/* Copy w*h ARGB texels into the image (in the frame's command buffer, so draws
 * recorded before this one still see the old contents). */
static int texture_upload(VkbTexture *t, const uint32_t *argb)
{
    uint32_t layers = t->cube_stride ? 6 : 1;
    VkDeviceSize off, bytes = (VkDeviceSize)t->width * t->height * 4 * layers;
    void *dst = ring_alloc(&V.staging, bytes, 16, &off);
    VkBufferImageCopy region;

    if (!dst) {
        static int warned;
        if (!warned++)
            fprintf(stderr, "  [VK] texture staging full this frame\n");
        return 0;
    }
    memcpy(dst, argb, (size_t)bytes);     /* 0xAARRGGBB LE = B,G,R,A bytes */
    end_rendering();
    transition(t->image, VK_IMAGE_ASPECT_COLOR_BIT,
               t->hash ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    memset(&region, 0, sizeof region);
    region.bufferOffset = off;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = layers;   /* faces follow each other */
    region.imageExtent.width = t->width;
    region.imageExtent.height = t->height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(V.cmd, V.staging.buffer, t->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    transition(t->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return 1;
}

static int white_ready(void)
{
    static const uint32_t px = 0xFFFFFFFFu;
    if (V.white.hash)
        return 1;
    if (!V.white.image) {
        V.white.width = V.white.height = 1;
        if (!image_create(1, 1, VK_FORMAT_B8G8R8A8_UNORM,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, &V.white.image, &V.white.memory,
                          &V.white.view))
            return 0;
    }
    if (!texture_upload(&V.white, &px))
        return 0;
    V.white.hash = 1;
    return 1;
}

static int white_cube_ready(void)
{
    static const uint32_t px[6] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };
    if (V.white_cube.hash)
        return 1;
    if (!V.white_cube.image) {
        V.white_cube.width = V.white_cube.height = 1;
        V.white_cube.cube_stride = 4;
        if (!image_create_ex(1, 1, VK_FORMAT_B8G8R8A8_UNORM,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT, 1, &V.white_cube.image,
                             &V.white_cube.memory, &V.white_cube.view))
            return 0;
    }
    if (!texture_upload(&V.white_cube, px))
        return 0;
    V.white_cube.hash = 1;
    return 1;
}

/* The image view a batch samples for its texture: a surface the GPU rendered
 * (render-to-texture: guest memory never saw those pixels), or a cached
 * decode of guest memory, re-validated once per frame. NULL: draw untextured. */
static VkImageView texture_view(const Nv2aTexture *g)
{
    VkbTexture *t = NULL, *victim = NULL;
    size_t n;
    uint64_t h;
    uint32_t cube = tex_is_cube(g) ? g->cube_stride : 0, f;
    int i;

    /* ponytail: a cube map the GPU rendered (dynamic reflections) is not
     * looked up among the surfaces; it reads what guest memory holds. */
    for (i = 0; i < VKB_MAX_SURFACES && !cube; i++) {
        VkbSurface *s = &V.surfaces[i];
        if (s->image && s->va == g->offset && s->layout != VK_IMAGE_LAYOUT_UNDEFINED) {
            if (s == V.rt_color && V.rendering)
                return NULL;               /* sampling the target being drawn */
            end_rendering();
            surface_to(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            return s->view;
        }
    }
    if (!g->width || !g->height || g->width > 4096 || g->height > 4096)
        return NULL;

    for (i = 0; i < VKB_MAX_TEXTURES; i++) {
        VkbTexture *c = &V.textures[i];
        if (c->image && c->offset == g->offset && c->width == g->width
                && c->height == g->height && c->color == g->color && c->pitch == g->pitch
                && c->cube_stride == cube) {
            t = c;
            break;
        }
    }
    if (t && t->checked == V.flips) {
        t->last_used = V.flips;
        return t->view;
    }
    /* A texture that failed to decode this frame fails again: a format the
     * executor cannot decode (0x30, a depth texture in a race) was hashed and
     * retried at every draw -- most of the NV2A thread's time. */
    for (i = 0; i < VKB_MAX_BAD_TEXTURES; i++) {
        VkbBadTexture *b = &V.bad[i];
        if (b->flip == V.flips && b->offset == g->offset && b->width == g->width
                && b->height == g->height && b->color == g->color)
            return NULL;
    }
    /* Hash only what is cached: a first sight decodes anyway. */
    h = t ? hash_guest_texture(g) : 0;
    if (t && (!h || t->hash == h)) {           /* unchanged, or unhashable: once */
        t->checked = V.flips;
        t->last_used = V.flips;
        return t->view;
    }

    n = (size_t)g->width * g->height * (cube ? 6 : 1);
    if (n > V.decode_cap) {
        free(V.decode_buf);
        V.decode_buf = (uint32_t *)malloc(n * 4);
        V.decode_cap = V.decode_buf ? n : 0;
        if (!V.decode_buf)
            return NULL;
    }
    for (f = 0; f < (cube ? 6u : 1u); f++) {
        Nv2aTexture face = *g;
        size_t npx = (size_t)g->width * g->height;
        face.offset = g->offset + f * cube;
        if (!nv2a_backend_decode_texture(&face, V.decode_buf + f * npx)) {
            VkbBadTexture *b = &V.bad[V.nbad++ % VKB_MAX_BAD_TEXTURES];
            b->offset = g->offset; b->width = g->width; b->height = g->height;
            b->color = g->color;   b->flip = V.flips;
            if (getenv("AMPED2_VK_TEXLOG")) {
                static int nfail;
                if (nfail++ < 20)
                    fprintf(stderr, "  [VK] cannot decode %08X %ux%u fmt %02X\n", g->offset,
                            g->width, g->height, g->color);
            }
            return NULL;                   /* format the executor cannot decode */
        }
    }
    V.stat_decodes++;
    V.stat_decode_px += n;
    V.stat_new += !t;
    if (t && getenv("AMPED2_VK_TEXLOG")) {
        static int nlog;
        if (nlog++ < 60)
            fprintf(stderr, "  [VK] re-decode %08X %ux%u fmt %02X pitch %u hash %016llx\n",
                    g->offset, g->width, g->height, g->color, g->pitch,
                    (unsigned long long)h);
    }
    if (!t)
        h = hash_guest_texture(g);
    if (!h)
        h = 1;                             /* outside guest RAM: upload once */

    if (!t) {
        for (i = 0; i < VKB_MAX_TEXTURES; i++) {
            VkbTexture *c = &V.textures[i];
            if (!c->image) { victim = c; break; }
            if (c->last_used != V.flips && (!victim || c->last_used < victim->last_used))
                victim = c;
        }
        if (!victim)
            return NULL;
        if (victim->image) {
            vkDeviceWaitIdle(V.device);
            texture_free(victim);
        }
        t = victim;
        if (!image_create_ex(g->width, g->height, VK_FORMAT_B8G8R8A8_UNORM,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT, cube != 0, &t->image, &t->memory,
                             &t->view)) {
            texture_free(t);
            return NULL;
        }
        t->offset = g->offset; t->width = g->width; t->height = g->height;
        t->color = g->color;   t->pitch = g->pitch; t->cube_stride = cube;
    }
    if (!texture_upload(t, V.decode_buf))
        return t->hash ? t->view : NULL;
    t->hash = h;
    t->checked = V.flips;
    t->last_used = V.flips;
    return t->view;
}

/* NV2A wrap modes: 1 wrap, 2 mirror, 3 clamp to edge, 4 border. Filter word:
 * MIN in bits 16-23, MAG in bits 24-27, 1 = nearest. */
static VkSampler sampler_get(const Nv2aTexture *g)
{
    static const VkSamplerAddressMode modes[8] = {
        VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    uint32_t mag_nearest = g && ((g->filter >> 24) & 0xF) == 1;
    uint32_t min_nearest = g && ((g->filter >> 16) & 0xFF) == 1;
    uint32_t key = (g ? (g->addr_u & 7) | ((g->addr_v & 7) << 3) : 3 | (3 << 3))
                 | (mag_nearest << 6) | (min_nearest << 7);
    VkSamplerCreateInfo ci;
    uint32_t i;

    for (i = 0; i < V.nsamplers; i++)
        if (V.sampler_keys[i] == key)
            return V.samplers[i];
    if (V.nsamplers == VKB_MAX_SAMPLERS)
        return V.samplers[0];
    memset(&ci, 0, sizeof ci);
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = mag_nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.minFilter = min_nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = modes[key & 7];
    ci.addressModeV = modes[(key >> 3) & 7];
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    ci.maxLod = 0.0f;
    if (vkCreateSampler(V.device, &ci, NULL, &V.samplers[V.nsamplers]) != VK_SUCCESS)
        return V.nsamplers ? V.samplers[0] : VK_NULL_HANDLE;
    V.sampler_keys[V.nsamplers] = key;
    return V.samplers[V.nsamplers++];
}

/* ---- debug dump --------------------------------------------------------- */

/* Copy a surface back and write it as a bottom-up 24-bit BMP, the format the
 * executor's own RECOMP_FB_DUMP writes, so the two can be compared directly.
 * Waits for the GPU: a debugging aid, not for every frame. */
static void dump_surface(VkbSurface *s)
{
    VkBuffer buf;
    VkDeviceMemory mem;
    VkBufferCreateInfo bci;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    VkCommandBufferBeginInfo bi;
    VkBufferImageCopy region;
    VkSubmitInfo si;
    VkDeviceSize size = (VkDeviceSize)s->width * s->height * 4;
    uint8_t *px;
    char path[512];
    FILE *f;
    uint32_t y, x, row_bytes, pad;
    uint8_t hdr[54];
    uint32_t filesz;

    memset(&bci, 0, sizeof bci);
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(V.device, &bci, NULL, &buf) != VK_SUCCESS)
        return;
    vkGetBufferMemoryRequirements(V.device, buf, &req);
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(V.device, &ai, NULL, &mem) != VK_SUCCESS) {
        vkDestroyBuffer(V.device, buf, NULL);
        return;
    }
    vkBindBufferMemory(V.device, buf, mem, 0);

    vkWaitForFences(V.device, 1, &V.frame_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(V.device, 1, &V.frame_fence);
    vkResetCommandBuffer(V.cmd, 0);
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(V.cmd, &bi);
    surface_to(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    memset(&region, 0, sizeof region);
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = s->width;
    region.imageExtent.height = s->height;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(V.cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buf, 1, &region);
    vkEndCommandBuffer(V.cmd);
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &V.cmd;
    vkQueueSubmit(V.queue, 1, &si, V.frame_fence);
    vkWaitForFences(V.device, 1, &V.frame_fence, VK_TRUE, UINT64_MAX);

    vkMapMemory(V.device, mem, 0, size, 0, (void **)&px);
    /* Named by flip number, so a dump can be matched to log lines. */
    snprintf(path, sizeof path, "%s%06u.bmp", V.dump_prefix, V.flips);
    f = fopen(path, "wb");
    if (f) {
        row_bytes = s->width * 3;
        pad = (4 - (row_bytes & 3)) & 3;
        filesz = 54 + (row_bytes + pad) * s->height;
        memset(hdr, 0, sizeof hdr);
        hdr[0] = 'B'; hdr[1] = 'M';
        memcpy(hdr + 2, &filesz, 4);
        hdr[10] = 54; hdr[14] = 40;
        memcpy(hdr + 18, &s->width, 4);
        memcpy(hdr + 22, &s->height, 4);
        hdr[26] = 1; hdr[28] = 24;
        fwrite(hdr, 1, sizeof hdr, f);
        for (y = s->height; y-- > 0;) {
            const uint8_t *row = px + (size_t)y * s->width * 4;   /* B,G,R,A */
            for (x = 0; x < s->width; x++)
                fwrite(row + x * 4, 1, 3, f);
            fwrite("\0\0\0", 1, pad, f);
        }
        fclose(f);
    }
    vkUnmapMemory(V.device, mem);
    vkDestroyBuffer(V.device, buf, NULL);
    vkFreeMemory(V.device, mem, NULL);
}

/* ---- back-end entry points -------------------------------------------- */

/* ---- NV2A state (GL enum values, nv2a_backend.h) -> Vulkan ---------------- */

static VkBlendFactor blend_factor(uint32_t f)
{
    switch (f) {
    case 0x0000: return VK_BLEND_FACTOR_ZERO;
    case 0x0001: return VK_BLEND_FACTOR_ONE;
    case 0x0300: return VK_BLEND_FACTOR_SRC_COLOR;
    case 0x0301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x0302: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 0x0303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x0304: return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x0305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x0306: return VK_BLEND_FACTOR_DST_COLOR;
    case 0x0307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 0x0308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 0x8001: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 0x8002: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default:     return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp blend_op(uint32_t e)
{
    switch (e) {
    case 0x800A: return VK_BLEND_OP_SUBTRACT;
    case 0x800B: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case 0x8007: return VK_BLEND_OP_MIN;
    case 0x8008: return VK_BLEND_OP_MAX;
    default:     return VK_BLEND_OP_ADD;          /* 0x8006 */
    }
}

/* GL_NEVER 0x200 .. GL_ALWAYS 0x207 are in Vulkan's order. */
static VkCompareOp compare_op(uint32_t f)
{
    return (f >= 0x200 && f <= 0x207) ? (VkCompareOp)(f - 0x200) : VK_COMPARE_OP_ALWAYS;
}

static VkStencilOp stencil_op(uint32_t op)
{
    switch (op) {
    case 0x0000: return VK_STENCIL_OP_ZERO;
    case 0x1E01: return VK_STENCIL_OP_REPLACE;
    case 0x1E02: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 0x1E03: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case 0x150A: return VK_STENCIL_OP_INVERT;
    case 0x8507: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case 0x8508: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:     return VK_STENCIL_OP_KEEP;       /* 0x1E00 */
    }
}

static void apply_state(const Nv2aRenderState *rs)
{
    VkBool32 blend = rs->blend_enable ? VK_TRUE : VK_FALSE;
    VkColorBlendEquationEXT eq;
    VkColorComponentFlags mask = 0;
    float bc[4];

    vkCmdSetColorBlendEnableEXT(V.cmd, 0, 1, &blend);
    eq.srcColorBlendFactor = blend_factor(rs->blend_src);
    eq.dstColorBlendFactor = blend_factor(rs->blend_dst);
    eq.colorBlendOp = blend_op(rs->blend_eq);
    eq.srcAlphaBlendFactor = eq.srcColorBlendFactor;
    eq.dstAlphaBlendFactor = eq.dstColorBlendFactor;
    eq.alphaBlendOp = eq.colorBlendOp;
    vkCmdSetColorBlendEquationEXT(V.cmd, 0, 1, &eq);
    bc[0] = (float)((rs->blend_color >> 16) & 0xFF) / 255.0f;
    bc[1] = (float)((rs->blend_color >>  8) & 0xFF) / 255.0f;
    bc[2] = (float)( rs->blend_color        & 0xFF) / 255.0f;
    bc[3] = (float)((rs->blend_color >> 24) & 0xFF) / 255.0f;
    vkCmdSetBlendConstants(V.cmd, bc);

    if (rs->color_mask & 0x00010000u) mask |= VK_COLOR_COMPONENT_R_BIT;
    if (rs->color_mask & 0x00000100u) mask |= VK_COLOR_COMPONENT_G_BIT;
    if (rs->color_mask & 0x00000001u) mask |= VK_COLOR_COMPONENT_B_BIT;
    if (rs->color_mask & 0x01000000u) mask |= VK_COLOR_COMPONENT_A_BIT;
    vkCmdSetColorWriteMaskEXT(V.cmd, 0, 1, &mask);

    /* Culling stays off: the NV2A decides facing from the projected, mirrored
     * triangle (nv2a_backend.h advice). Per-triangle culling: milestone 4. */
    vkCmdSetCullMode(V.cmd, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(V.cmd, VK_FRONT_FACE_CLOCKWISE);

    /* GL writes depth only while the test is enabled, and so does the NV2A. */
    vkCmdSetDepthTestEnable(V.cmd, rs->depth_test_enable ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(V.cmd, (rs->depth_test_enable && rs->depth_write) ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(V.cmd, compare_op(rs->depth_func));

    vkCmdSetStencilTestEnable(V.cmd, rs->stencil_enable ? VK_TRUE : VK_FALSE);
    vkCmdSetStencilOp(V.cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                      stencil_op(rs->stencil_fail), stencil_op(rs->stencil_zpass),
                      stencil_op(rs->stencil_zfail), compare_op(rs->stencil_func));
    vkCmdSetStencilCompareMask(V.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, rs->stencil_func_mask & 0xFF);
    vkCmdSetStencilWriteMask(V.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, rs->stencil_write_mask & 0xFF);
    vkCmdSetStencilReference(V.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, rs->stencil_ref & 0xFF);
}

/* ---- back-end entry points -------------------------------------------- */

static void vkb_clear(const Nv2aSurface *g, const Nv2aRenderState *rs,
                      uint32_t flags, uint32_t argb, uint32_t zstencil)
{
    VkbSurface *s;
    VkImageSubresourceRange range;

    if (!ensure_ready() || !ensure_recording())
        return;
    if (!(s = surface_get(g)))
        return;
    V.last = s;
    end_rendering();
    memset(&range, 0, sizeof range);
    range.levelCount = 1;
    range.layerCount = 1;
    if (flags & 0xF0) {
        VkClearColorValue c;
        c.float32[0] = (float)((argb >> 16) & 0xFF) / 255.0f;
        c.float32[1] = (float)((argb >>  8) & 0xFF) / 255.0f;
        c.float32[2] = (float)( argb        & 0xFF) / 255.0f;
        c.float32[3] = (float)((argb >> 24) & 0xFF) / 255.0f;
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        surface_to(s, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(V.cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &c, 1, &range);
    }
    if (flags & 0x3) {
        /* Z24S8 clear value: depth in the top 24 bits, stencil in the low 8. */
        VkbDepth *d = depth_get(rs ? rs->zeta_va : 0, s->width, s->height);
        VkClearDepthStencilValue dv;
        if (d) {
            dv.depth = (float)((double)(zstencil >> 8) / 16777215.0);
            dv.stencil = zstencil & 0xFF;
            range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            depth_to(d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdClearDepthStencilImage(V.cmd, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        &dv, 1, &range);
        }
    }
}

static void unpack_argb(uint32_t c, float o[4])
{
    o[0] = (float)((c >> 16) & 0xFF) / 255.0f;
    o[1] = (float)((c >>  8) & 0xFF) / 255.0f;
    o[2] = (float)( c        & 0xFF) / 255.0f;
    o[3] = (float)( c >> 24        ) / 255.0f;
}

/* The batch's combiner state in this frame's ring, as nv2a.frag reads it.
 * Consecutive batches mostly share it, so an unchanged block is reused. */
static int combiners_upload(const Nv2aBatchExt *e, VkDeviceSize *off)
{
    const Nv2aCombiner *rc = e->rc;
    uint32_t flags = rc->control >> 8;
    VkbCombiners u;
    void *dst;
    int i;

    memset(&u, 0, sizeof u);
    for (i = 0; i < 8; i++) {
        u.cw[i][0] = rc->color_icw[i];
        u.cw[i][1] = rc->alpha_icw[i];
        u.cw[i][2] = rc->color_ocw[i];
        u.cw[i][3] = rc->alpha_ocw[i];
        /* One c0/c1 for every stage, or one per stage (COMBINERCOUNT). */
        unpack_argb(rc->factor0[(flags & 0x010) ? i : 0], u.c0[i]);
        unpack_argb(rc->factor1[(flags & 0x100) ? i : 0], u.c1[i]);
    }
    u.fin[0] = rc->final0;
    u.fin[1] = rc->final1;
    u.fin[2] = rc->control;
    u.fin[3] = rc->stage_program;
    unpack_argb(rc->final_c0, u.fc0);
    unpack_argb(rc->final_c1, u.fc1);
    unpack_argb(e->fog_color, u.fog_color);
    for (i = 0; i < 4; i++) {
        const Nv2aTexture *t = e->textures[i];
        /* Linear textures take texel coordinates, the rest normalised ones. */
        u.tex_scale[i][0] = (t && t->pitch && t->width) ? 1.0f / (float)t->width : 1.0f;
        u.tex_scale[i][1] = (t && t->pitch && t->height) ? 1.0f / (float)t->height : 1.0f;
    }
    u.misc[0] = e->clip_plane_mode;
    u.misc[1] = rc->dot_mapping;
    u.misc[2] = rc->stage_input;
    for (i = 0; i < 4; i++)
        if (tex_is_cube(e->textures[i]))
            u.misc[3] |= 1u << i;
    u.eye[0] = e->eye_vector[0];
    u.eye[1] = e->eye_vector[1];
    u.eye[2] = e->eye_vector[2];

    if (V.last_rc_off != ~(VkDeviceSize)0 && !memcmp(&u, &V.last_rc, sizeof u)) {
        *off = V.last_rc_off;
        return 1;
    }
    if (!(dst = ring_alloc(&V.vring, sizeof u, V.ubo_align, off)))
        return 0;
    memcpy(dst, &u, sizeof u);
    V.last_rc = u;
    V.last_rc_off = *off;
    return 1;
}

static void bind_pipeline(VkPipeline p)
{
    if (V.bound != p) {
        vkCmdBindPipeline(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
        V.bound = p;
    }
}

/* What both draw paths share: the target, the stage textures, the combiner
 * UBO, the fragment-stage descriptors and the push constants. */
typedef struct {
    VkbSurface            *s;
    VkbDepth              *d;
    int                    use_rc;
    const Nv2aBatchExt    *e;
    const Nv2aTexture     *stex[4];
    VkImageView            views[4];
    VkDeviceSize           rc_off;
    VkDescriptorImageInfo  dii[8];
    VkDescriptorBufferInfo dbi[1 + VKB_NBIND_VSH - VKB_NBIND];
    VkWriteDescriptorSet   w[VKB_NBIND_VSH];
    VkbPush                pc;
} VkbDrawCtx;

static int draw_prepare(const Nv2aSurface *g, const Nv2aBatch *b, VkbDrawCtx *c)
{
    static int no_rc = -1;
    int i;

    if (no_rc < 0)
        no_rc = getenv("AMPED2_VK_NO_RC") != NULL;
    memset(c, 0, sizeof *c);
    c->e = no_rc ? NULL : b->ext;
    c->use_rc = c->e && c->e->rc;
    if (!ensure_ready() || !ensure_recording() || !white_ready() || !white_cube_ready())
        return 0;
    if (!(c->s = surface_get(g)))
        return 0;
    V.last = c->s;
    V.frame_draws++;
    if (V.shown_va[0] && !surface_shown(c->s->va))
        V.rt_flip = V.flips + 1;            /* off screen (in_ride) */
    if (!(c->d = depth_get(b->state->zeta_va, c->s->width, c->s->height)))
        return 0;

    /* Textures first: an upload or a render-to-texture transition ends the
     * rendering scope, which begin_rendering below reopens. With combiners
     * every stage samples what the extension names; without, stage 0 is the
     * batch's one texture. */
    if (c->use_rc)
        for (i = 0; i < 4; i++)
            c->stex[i] = c->e->textures[i];
    else
        c->stex[0] = b->texture;
    for (i = 0; i < 4; i++)
        if (c->stex[i])
            c->views[i] = texture_view(c->stex[i]);
    if (c->use_rc && !combiners_upload(c->e, &c->rc_off))
        return 0;
    return 1;
}

/* Fragment-stage descriptors (bindings 0-8) and push constants. */
static void draw_frag_state(const Nv2aBatch *b, VkbDrawCtx *c)
{
    const Nv2aRenderState *rs = b->state;
    int i;

    for (i = 0; i < 4; i++) {
        /* A stage with nothing usable reads white, as on the CPU path. A cube
         * map goes to the stage's cube binding, white to its 2D one. */
        int cube = c->views[i] && c->use_rc && tex_is_cube(c->stex[i]);
        VkSampler smp = sampler_get(c->views[i] ? c->stex[i] : NULL);
        int k;
        c->dii[i].sampler = smp;
        c->dii[i].imageView = c->views[i] && !cube ? c->views[i] : V.white.view;
        c->dii[4 + i].sampler = smp;
        c->dii[4 + i].imageView = cube ? c->views[i] : V.white_cube.view;
        for (k = 0; k < 2; k++) {
            int d = k ? VKB_BIND_CUBE + i : i, at = k ? 5 + i : i;
            c->dii[i + 4 * k].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            c->w[at].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            c->w[at].dstBinding = (uint32_t)d;
            c->w[at].descriptorCount = 1;
            c->w[at].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            c->w[at].pImageInfo = &c->dii[i + 4 * k];
        }
    }
    /* The UBO binding must be valid even when the shader skips it: without
     * combiners rc_off stays 0, an in-range block it never reads. */
    c->dbi[0].buffer = V.vring.buffer;
    c->dbi[0].offset = c->rc_off;
    c->dbi[0].range = sizeof(VkbCombiners);
    c->w[VKB_BIND_RC].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    c->w[VKB_BIND_RC].dstBinding = VKB_BIND_RC;
    c->w[VKB_BIND_RC].descriptorCount = 1;
    c->w[VKB_BIND_RC].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    c->w[VKB_BIND_RC].pBufferInfo = &c->dbi[0];

    /* Vertices are in guest surface pixels, whatever size the image has. */
    c->pc.surface_size[0] = (float)c->s->gw;
    c->pc.surface_size[1] = (float)c->s->gh;
    c->pc.x3d = c->s->wide ? (float)(1.0 / V.widen) : 1.0f;
    c->pc.tex_size[0] = (!c->use_rc && c->views[0]) ? (float)b->texture->width : 1.0f;
    c->pc.tex_size[1] = (!c->use_rc && c->views[0]) ? (float)b->texture->height : 1.0f;
    c->pc.z_scale = 1.0f / 16777215.0f;
    c->pc.flags = ((!c->use_rc && c->views[0]) ? 1u : 0u)
                | (c->use_rc ? 2u : 0u)
                | ((rs->alpha_func >= 0x200 && rs->alpha_func <= 0x207
                    ? rs->alpha_func - 0x200 : 7u) << 8)
                | ((rs->alpha_ref & 0xFF) << 16)
                | (rs->alpha_test_enable ? 0x80000000u : 0u);
}

static void vkb_draw(const Nv2aSurface *g, const Nv2aBatch *b)
{
    VkbDrawCtx c;
    VkDeviceSize voff[2], vstride[2];
    VkDeviceSize bytes = (VkDeviceSize)b->count * sizeof(Nv2aVertex);
    VkDeviceSize ebytes;
    VkBuffer vbufs[2];
    const Nv2aBatchExt *e;
    void *dst, *edst;

    if (!b->count || !draw_prepare(g, b, &c))
        return;
    e = c.e;
    ebytes = e ? (VkDeviceSize)b->count * sizeof(Nv2aVertexExt) : sizeof(Nv2aVertexExt);
    if (!(dst = ring_alloc(&V.vring, bytes, 16, &voff[0]))
            || !(edst = ring_alloc(&V.vring, ebytes, 16, &voff[1]))) {
        static int warned;
        if (!warned++)
            fprintf(stderr, "  [VK] vertex ring full this frame\n");
        return;
    }
    memcpy(dst, b->vertices, (size_t)bytes);
    if (e)
        memcpy(edst, e->vertices, (size_t)ebytes);
    else
        memset(edst, 0, (size_t)ebytes);

    begin_rendering(c.s, c.d);
    bind_pipeline(V.pipeline);
    apply_state(b->state);
    draw_frag_state(b, &c);
    vkCmdPushDescriptorSetKHR(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pipe_layout,
                              0, VKB_NBIND, c.w);
    vkCmdPushConstants(V.cmd, V.pipe_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof c.pc, &c.pc);
    vbufs[0] = vbufs[1] = V.vring.buffer;
    vstride[0] = sizeof(Nv2aVertex);
    vstride[1] = e ? sizeof(Nv2aVertexExt) : 0;
    vkCmdBindVertexBuffers2(V.cmd, 0, 2, vbufs, voff, NULL, vstride);
    vkCmdDraw(V.cmd, b->count, 1, 0, 0);
}

/* A batch whose vertex program nv2a_vsh.vert runs (Nv2aProgramBatch). The
 * attributes' spans are copied once each -- interleaved attributes share one
 * span -- and the indices go in as a triangle list. Constants and program
 * are uploaded only when they changed since the last batch of this frame. */
static int vkb_draw_program(const Nv2aSurface *g, const Nv2aBatch *b,
                            const Nv2aProgramBatch *p)
{
    VkbDrawCtx c;
    VkbVshAttrs *at;
    struct { const uint8_t *lo, *hi; int a; } sp[16], t;
    VkDeviceSize aoff, doff, ioff, total = 0, pos;
    uint32_t ns = 0, i, j;
    uint8_t *dst;
    void *m;

    if (!V.pipeline_vsh || !p->index_count || p->program_len > 136)
        return 0;
    if (!draw_prepare(g, b, &c))
        return 1;                           /* nothing to draw into: dropped, as draw */

    /* Spans, sorted by address; overlapping ones (interleaved) merge. */
    for (i = 0; i < 16; i++)
        if (p->attr[i].data) {
            sp[ns].lo = p->attr[i].data;
            sp[ns].hi = p->attr[i].data + p->attr[i].bytes;
            sp[ns].a = (int)i;
            ns++;
        }
    for (i = 1; i < ns; i++)
        for (j = i; j > 0 && sp[j - 1].lo > sp[j].lo; j--) {
            t = sp[j]; sp[j] = sp[j - 1]; sp[j - 1] = t;
        }
    for (i = 0; i < ns; ) {
        const uint8_t *lo = sp[i].lo, *hi = sp[i].hi;
        for (j = i + 1; j < ns && sp[j].lo <= hi + 64; j++)
            if (sp[j].hi > hi)
                hi = sp[j].hi;
        total += 8 + (VkDeviceSize)(hi - lo);
        i = j;
    }
    if (!(dst = ring_alloc(&V.vring, total + 8, V.ssbo_align, &doff))
            || !(at = ring_alloc(&V.vring, sizeof *at, V.ubo_align, &aoff))
            || !(m = ring_alloc(&V.vring, (VkDeviceSize)p->index_count * 4, 16, &ioff))) {
        static int warned;
        if (!warned++)
            fprintf(stderr, "  [VK] vertex ring full this frame\n");
        return 1;
    }
    memcpy(m, p->indices, (size_t)p->index_count * 4);
    memset(at, 0, sizeof *at);
    pos = 0;
    for (i = 0; i < ns; ) {
        const uint8_t *lo = sp[i].lo, *hi = sp[i].hi;
        VkDeviceSize base;
        for (j = i + 1; j < ns && sp[j].lo <= hi + 64; j++)
            if (sp[j].hi > hi)
                hi = sp[j].hi;
        /* Keep the span's alignment within a word: loads stay aligned. */
        base = ((pos + 3) & ~(VkDeviceSize)3) + ((uintptr_t)lo & 3);
        memcpy(dst + base, lo, (size_t)(hi - lo));
        for (; i < j; i++)
            at->fmt[sp[i].a][0] = (uint32_t)(base + (VkDeviceSize)(sp[i].lo - lo));
        pos = base + (VkDeviceSize)(hi - lo);
    }
    memset(dst + pos, 0, (size_t)(total + 8 - pos < 8 ? total + 8 - pos : 8));
    for (i = 0; i < 16; i++) {
        at->fmt[i][1] = p->attr[i].stride;
        at->fmt[i][2] = p->attr[i].type;
        at->fmt[i][3] = p->attr[i].data ? p->attr[i].size : 0;
        memcpy(at->imm[i], p->imm[i], sizeof at->imm[i]);
    }
    at->misc[0][0] = p->aa_scale[0];
    at->misc[0][1] = p->aa_scale[1];
    at->misc[0][2] = p->uv_scale[0];
    at->misc[0][3] = p->uv_scale[1];
    at->misc[1][0] = p->fog_param[0];
    at->misc[1][1] = p->fog_param[1];
    at->misc[1][2] = p->fog_enable ? 1.0f : 0.0f;
    at->misc[1][3] = (float)p->fog_mode;
    at->misc[2][0] = (float)p->program_len;

    if (V.vconst_serial != V.frame_serial || V.vconst_gen != p->constants_gen) {
        void *k = ring_alloc(&V.vring, 192 * 16, V.ubo_align, &V.vconst_off);
        if (!k)
            return 1;
        memcpy(k, p->constants, 192 * 16);
        V.vconst_serial = V.frame_serial;
        V.vconst_gen = p->constants_gen;
    }
    if (V.vprog_serial != V.frame_serial || V.vprog_gen != p->program_gen) {
        uint32_t (*q)[4] = ring_alloc(&V.vring, 136 * 16, V.ubo_align, &V.vprog_off);
        if (!q)
            return 1;
        memcpy(q, p->program, (size_t)p->program_len * 16);
        memset(q + p->program_len, 0, (size_t)(136 - p->program_len) * 16);
        V.vprog_serial = V.frame_serial;
        V.vprog_gen = p->program_gen;
    }

    begin_rendering(c.s, c.d);
    bind_pipeline(V.pipeline_vsh);
    apply_state(b->state);
    draw_frag_state(b, &c);
    {
        static const uint32_t bindings[4] = { VKB_BIND_VCONST, VKB_BIND_VPROG,
                                              VKB_BIND_VATTR, VKB_BIND_VDATA };
        VkDeviceSize offs[4], ranges[4];
        offs[0] = V.vconst_off; ranges[0] = 192 * 16;
        offs[1] = V.vprog_off;  ranges[1] = 136 * 16;
        offs[2] = aoff;         ranges[2] = sizeof *at;
        offs[3] = doff;         ranges[3] = total + 8;
        for (i = 0; i < 4; i++) {
            VkWriteDescriptorSet *w = &c.w[VKB_NBIND + i];
            c.dbi[1 + i].buffer = V.vring.buffer;
            c.dbi[1 + i].offset = offs[i];
            c.dbi[1 + i].range = ranges[i];
            w->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w->dstBinding = bindings[i];
            w->descriptorCount = 1;
            w->descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                       : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w->pBufferInfo = &c.dbi[1 + i];
        }
    }
    vkCmdPushDescriptorSetKHR(V.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pipe_layout,
                              0, VKB_NBIND_VSH, c.w);
    vkCmdPushConstants(V.cmd, V.pipe_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof c.pc, &c.pc);
    vkCmdBindIndexBuffer(V.cmd, V.vring.buffer, ioff, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(V.cmd, p->index_count, 1, 0, 0, 0);
    return 1;
}

static void pump_events(void)
{
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            fprintf(stderr, "  [VK] window closed\n");
            fflush(stderr);
            _Exit(0);
        }
        if (!window_event(&ev))
            xbox_InputSdlEvent(&ev);
    }
}

/* A ride, not a menu: something was drawn into an off-screen render target
 * in the last half second of flips. Amped 2 renders its 512x512 target only
 * on the mountain (checked: created after the level loads, never in the
 * menus). Decides how a widened back buffer is presented. */
volatile unsigned amped2_vk_flip_count;

static int in_ride(void)
{
    return V.rt_flip && V.flips + 1 - V.rt_flip < 30;
}

/* The logical (title-visible) size of the surface, scaled to fit the window
 * with its aspect kept; the rest of the window stays black. */
static void fit_rect(const VkbSurface *s, VkOffset3D out[2])
{
    double sw = (double)V.swap_extent.width, sh = (double)V.swap_extent.height;
    double aspect = (double)s->logical_w / (double)s->logical_h
                  * (s->wide && in_ride() ? V.widen : 1.0);
    double w = sw, h = sw / aspect;

    if (h > sh) { h = sh; w = sh * aspect; }
    out[0].x = (int32_t)((sw - w) / 2); out[0].y = (int32_t)((sh - h) / 2); out[0].z = 0;
    out[1].x = out[0].x + (int32_t)w;   out[1].y = out[0].y + (int32_t)h;   out[1].z = 1;
}

/* "title | FPS: x | draws: n", once a second -- the format of the CPU
 * renderer's window (fb_present_sdl.c), so the two read the same. Wall time:
 * clock() would be CPU time on POSIX. */
static void update_title(void)
{
    uint32_t now = SDL_GetTicks();
    char tb[192];

    if (!V.title_ms) {
        V.title_ms = now;
        V.title_flips = V.flips;
        return;
    }
    if (now - V.title_ms < 1000)
        return;
    snprintf(tb, sizeof tb, "Amped 2 (Recomp, Vulkan) | FPS: %.1f | draws: %u",
             (double)(V.flips - V.title_flips) * 1000.0 / (double)(now - V.title_ms),
             V.last_frame_draws);
    SDL_SetWindowTitle(V.window, tb);
    {
        /* The same figure in the log every 5 s, for runs nobody watches. */
        static uint32_t logged;
        if (now - logged >= 5000) {
            if (getenv("AMPED2_TIME_TRACE")) {
                /* The game's clock (accumulated frame time x 30, at 0x3AD0C4,
                 * see docs/notes.md): ~30 ticks per second at any frame rate. */
                const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
                fprintf(stderr, "  [TT] wall %u ms flips %u ticks30 %d\n", now, V.flips,
                        *(const int32_t *)(m + 0x3AD0C4));
            }
            fprintf(stderr, "  [VK] %s | decodes/5s: %u (%u new, %.1f Mtexels) | hashed: %u, %.1f MB\n",
                    tb + sizeof "Amped 2 (Recomp, Vulkan) | " - 1, V.stat_decodes,
                    V.stat_new, (double)V.stat_decode_px / 1e6, V.stat_hashes,
                    (double)V.stat_hash_bytes / 1e6);
            V.stat_decodes = V.stat_new = V.stat_hashes = 0;
            V.stat_hash_bytes = 0;
            V.stat_decode_px = 0;
            logged = now;
        }
    }
    V.title_ms = now;
    V.title_flips = V.flips;
}

/* Fit w x h into the window, aspect kept (fit_rect for a size, not a surface). */
static void fit_wh(uint32_t w, uint32_t h, VkOffset3D out[2])
{
    double sw = (double)V.swap_extent.width, sh = (double)V.swap_extent.height;
    double aspect = (double)w / (double)h;
    double fw = sw, fh = sw / aspect;

    if (fh > sh) { fh = sh; fw = sh * aspect; }
    out[0].x = (int32_t)((sw - fw) / 2); out[0].y = (int32_t)((sh - fh) / 2); out[0].z = 0;
    out[1].x = out[0].x + (int32_t)fw;   out[1].y = out[0].y + (int32_t)fh;   out[1].z = 1;
}

/* Record the overlay on top of the swapchain image `img` (TRANSFER_DST).
 * The frame is uploaded only when the overlay latched a new one. Returns 1 if
 * the overlay was drawn. Colour keying is not honoured here yet (Amped 2's
 * movies are full screen and unkeyed); the CPU path does it. */
static int overlay_record(VkImage img)
{
    VkDeviceSize off;
    void *dst;
    VkImageBlit blit;
    uint64_t seq;

    if (!nv2a_overlay_active())
        return 0;
    if (!V.ov_image) {
        V.ov_host = (uint32_t *)malloc((size_t)VKB_OV_W * VKB_OV_H * 4u);
        if (!V.ov_host
                || !image_create(VKB_OV_W, VKB_OV_H, VK_FORMAT_B8G8R8A8_UNORM,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT, &V.ov_image, &V.ov_memory,
                                 &V.ov_view)) {
            fprintf(stderr, "  [VK] overlay image: allocation failed\n");
            free(V.ov_host);
            V.ov_host = NULL;
            V.ov_image = VK_NULL_HANDLE;
            return 0;
        }
        V.ov_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        V.ov_uploaded = 0;
    }
    seq = nv2a_overlay_seq();
    if (seq != V.ov_uploaded) {
        VkBufferImageCopy region;
        memset(V.ov_host, 0, (size_t)VKB_OV_W * VKB_OV_H * 4u);
        nv2a_overlay_compose_xrgb(V.ov_host, VKB_OV_W, VKB_OV_H, VKB_OV_W);
        dst = ring_alloc(&V.staging, (VkDeviceSize)VKB_OV_W * VKB_OV_H * 4u, 16, &off);
        if (!dst)
            return 0;
        memcpy(dst, V.ov_host, (size_t)VKB_OV_W * VKB_OV_H * 4u);   /* XRGB LE = B,G,R,X */
        transition(V.ov_image, VK_IMAGE_ASPECT_COLOR_BIT, V.ov_layout,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        memset(&region, 0, sizeof region);
        region.bufferOffset = off;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = VKB_OV_W;
        region.imageExtent.height = VKB_OV_H;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(V.cmd, V.staging.buffer, V.ov_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transition(V.ov_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        V.ov_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        V.ov_uploaded = seq;
    }
    if (V.ov_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        return 0;
    memset(&blit, 0, sizeof blit);
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1].x = (int32_t)VKB_OV_W;
    blit.srcOffsets[1].y = (int32_t)VKB_OV_H;
    blit.srcOffsets[1].z = 1;
    blit.dstSubresource = blit.srcSubresource;
    fit_wh(VKB_OV_W, VKB_OV_H, blit.dstOffsets);
    vkCmdBlitImage(V.cmd, V.ov_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);
    V.ov_presented = seq;
    return 1;
}

/* Unlocked with a fixed limit: hold the flip until its slot. Sleeps for the
 * bulk and spins the last ~1 ms (a plain sleep overshoots by up to a
 * scheduler tick, which shows as uneven frames at 240+ fps). After a stall
 * the schedule restarts instead of rushing to catch up. */
static void frame_pace(void)
{
    static uint64_t next;
    uint64_t freq = SDL_GetPerformanceFrequency(), now = SDL_GetPerformanceCounter(), period;
    int fps = g_amped2_settings.frame_limit;

    if (g_amped2_settings.framerate != AMPED2_FPS_UNLOCKED || fps <= 0)
        return;
    period = freq / (uint64_t)fps;
    if (!next || now > next + 2 * period)
        next = now;
    next += period;
    while ((now = SDL_GetPerformanceCounter()) < next) {
        uint64_t left_us = (next - now) * 1000000u / freq;
        if (left_us > 1500)
            SDL_Delay((Uint32)((left_us - 1000) / 1000));
        else
            sched_yield();
    }
}

/* Present the current picture. real_flip: the title flipped (count it, log
 * it, dump it); otherwise a present for a new overlay frame between flips. */
static void present(int real_flip)
{
    uint32_t idx;
    VkResult r;
    VkImage img;
    VkClearColorValue black;
    VkImageSubresourceRange range;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si;
    VkPresentInfoKHR pi;
    VkbSurface *s;

    if (!ensure_ready())
        return;
    if (!V.host_main) {
        pump_events();
        update_title();
    }
    if (real_flip) {
        V.flips++;
        amped2_vk_flip_count = V.flips;
        if (V.last)
            note_shown(V.last->va);
    }
    if (real_flip && (V.flips == 1 || (V.frame_draws && !V.first_draw_logged))) {
        /* Boot time to the first flip and the first frame with draws. */
        fprintf(stderr, "  [VK] %s at %.1f s (flip %u)\n",
                V.flips == 1 ? "first flip" : "first frame with draws",
                (double)SDL_GetTicks() / 1000.0, V.flips);
        if (V.frame_draws)
            V.first_draw_logged = 1;
    }
    if (real_flip) {
        V.last_frame_draws = V.frame_draws;
        V.frame_draws = 0;
    }
    if (V.swap_dirty) {
        destroy_swapchain();
        if (!create_swapchain())
            return;
    }
    if (!ensure_recording())
        return;

    /* Bounded: the NV2A thread must never block on the window (a hidden or
     * unresponsive window can withhold images indefinitely). Without an image
     * the frame's work is still submitted, just not shown. */
    r = vkAcquireNextImageKHR(V.device, V.swapchain, 100000000ull /* 100 ms */,
                              V.sem_acquire, VK_NULL_HANDLE, &idx);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        VkSubmitInfo none;
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
            V.swap_dirty = 1;
        end_rendering();
        vkEndCommandBuffer(V.cmd);
        V.recording = 0;
        memset(&none, 0, sizeof none);
        none.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        none.commandBufferCount = 1;
        none.pCommandBuffers = &V.cmd;
        vkQueueSubmit(V.queue, 1, &none, V.frame_fence);
        return;
    }
    img = V.swap_images[idx];
    end_rendering();

    memset(&range, 0, sizeof range);
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    memset(&black, 0, sizeof black);
    transition(img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdClearColorImage(V.cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &black, 1, &range);
    s = V.last;
    if (s && s->layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        VkImageBlit blit;
        memset(&blit, 0, sizeof blit);
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1].x = (int32_t)s->width;
        blit.srcOffsets[1].y = (int32_t)s->height;
        blit.srcOffsets[1].z = 1;
        if (s->wide && real_flip) {
            static int was = -1;
            if (was != in_ride()) {
                was = in_ride();
                fprintf(stderr, "  [VK] flip %u: presenting %s\n", V.flips,
                        was ? "wide (ride)" : "4:3 (menu)");
            }
        }
        if (s->wide && !in_ride()) {
            /* A menu on a widened surface: its 4:3 middle. */
            int32_t w43 = (int32_t)((double)s->width / V.widen + 0.5);
            blit.srcOffsets[0].x = ((int32_t)s->width - w43) / 2;
            blit.srcOffsets[1].x = blit.srcOffsets[0].x + w43;
        }
        blit.dstSubresource = blit.srcSubresource;
        fit_rect(s, blit.dstOffsets);
        surface_to(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdBlitImage(V.cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);
    }
    overlay_record(img);
    transition(img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer(V.cmd);
    V.recording = 0;

    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &V.sem_acquire;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &V.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &V.sem_render_done[idx];
    if (vkQueueSubmit(V.queue, 1, &si, V.frame_fence) != VK_SUCCESS) {
        fprintf(stderr, "  [VK] submit failed\n");
        return;
    }
    if (real_flip)
        frame_pace();
    memset(&pi, 0, sizeof pi);
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &V.sem_render_done[idx];
    pi.swapchainCount = 1;
    pi.pSwapchains = &V.swapchain;
    pi.pImageIndices = &idx;
    r = vkQueuePresentKHR(V.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        V.swap_dirty = 1;

    if (real_flip && V.dump_prefix && s && V.flips % V.dump_every == 0)
        dump_surface(s);
}

static void vkb_flip(void)
{
    present(1);
}

/* Between flips: a new overlay frame (a movie) is presented on its own. */
static void vkb_idle(void)
{
    if (!V.ready || !nv2a_overlay_active())
        return;
    if (nv2a_overlay_seq() == V.ov_presented)
        return;
    present(0);
}

static const Nv2aBackend s_vk_backend = { vkb_clear, vkb_draw, vkb_flip, vkb_idle,
                                           vkb_draw_program };

/* ---- the window on the process main thread ------------------------------ */

extern void xbox_InputSetMainPump(int on);    /* src/input/xinput_device.c */

static int (*s_guest_main)(void);
static int s_guest_rc;
static Uint32 s_guest_done_event = (Uint32)-1;

static void *guest_thread(void *unused)
{
    SDL_Event ev;

    (void)unused;
    s_guest_rc = s_guest_main();
    memset(&ev, 0, sizeof ev);
    ev.type = s_guest_done_event;
    SDL_PushEvent(&ev);
    return NULL;
}

/* The guest's thread, with xbox_HostWindowMain's stack rule: at least the
 * main thread's and generously more, since lifted code recurses on it. */
static int start_guest_thread(void)
{
    pthread_attr_t attr;
    pthread_t th;
    struct rlimit rl;
    size_t stack = 64u << 20;
    int rc;

    if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY
            && rl.rlim_cur > stack)
        stack = (size_t)rl.rlim_cur;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack);
    rc = pthread_create(&th, &attr, guest_thread, NULL);
    pthread_attr_destroy(&attr);
    if (rc != 0)
        return 0;
    pthread_detach(th);
    return 1;
}

int amped2_vk_host_main(int (*guest_main)(void))
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "  [VK] SDL init: %s; running without a window thread\n",
                SDL_GetError());
        return guest_main();
    }
    V.window = open_window("Amped 2 (Recomp, Vulkan) | loading");
    s_guest_done_event = SDL_RegisterEvents(1);
    if (!V.window || s_guest_done_event == (Uint32)-1) {
        fprintf(stderr, "  [VK] window: %s; running without a window thread\n",
                SDL_GetError());
        if (V.window) {
            SDL_DestroyWindow(V.window);
            V.window = NULL;
        }
        return guest_main();
    }
    xbox_InputSetMainPump(1);
    xbox_InputSdlInit();
    V.host_main = 1;
    s_guest_main = guest_main;
    if (!start_guest_thread()) {
        fprintf(stderr, "  [VK] cannot start the guest thread\n");
        V.host_main = 0;
        xbox_InputSetMainPump(0);
        return guest_main();
    }
    fprintf(stderr, "  [VK] window open; guest runs on its own thread\n");

    /* Pump from the start, through the whole load, so the desktop never sees
     * an unresponsive window; refresh the FPS title while frames come. */
    for (;;) {
        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, 16)) {
            do {
                if (ev.type == s_guest_done_event)
                    return s_guest_rc;
                if (ev.type == SDL_QUIT) {
                    fprintf(stderr, "  [VK] window closed\n");
                    fflush(stderr);
                    _Exit(0);
                }
                if (!window_event(&ev))
                    xbox_InputSdlEvent(&ev);
            } while (SDL_PollEvent(&ev));
        }
        if (V.flips)
            update_title();
    }
}

int amped2_vk_register(void)
{
    nv2a_backend_register(&s_vk_backend);
    fprintf(stderr, "  [VK] Vulkan back end registered (starts on the first frame)\n");
    return 0;
}
