#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <sys/resource.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <map>
#include <sys/stat.h>
#include "libretro.h"
#include <vector>
#include <sched.h>

#include <cstdarg>
#include <ctime>
#include <sys/stat.h>

static FILE* g_perf_log = nullptr;
static int   g_perf_count = 0;
static int64_t g_perf_bucket_start = 0;
static int64_t g_perf_retro_sum = 0;
static int64_t g_perf_present_sum = 0;
static int64_t g_perf_vk_sum = 0;
static double  g_perf_worst = 0;

static int64_t perf_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static void perf_open() {
    if (g_perf_log) return;
    mkdir("/storage/emulated/0/Android/data/com.pssp/files/logs", 0777);
    g_perf_log = fopen("/storage/emulated/0/Android/data/com.pssp/files/logs/perf.log", "a");
}
static void perf_write(const char* fmt, ...) {
    if (!g_perf_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_perf_log, fmt, ap);
    va_end(ap); fflush(g_perf_log);
}

static int pick_big_core() {
    int  best_cpu = -1;
    long best_hz  = -1;
    int  ncpu     = (int)sysconf(_SC_NPROCESSORS_ONLN);
    for (int c = 0; c < ncpu; c++) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        long hz = 0;
        if (fscanf(f, "%ld", &hz) != 1) hz = 0;
        fclose(f);
        if (hz > best_hz) { best_hz = hz; best_cpu = c; }
    }
    if (best_cpu < 0 && ncpu > 0) best_cpu = ncpu - 1;
    return best_cpu;
}

static void bind_to_big_core() {
    int cpu = pick_big_core();
    if (cpu < 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

static void perf_frame(int64_t total_ns, int64_t retro_ns, int64_t vk_ns, int64_t present_ns, int fno) {
    if (!g_perf_log) perf_open();
    g_perf_count++;
    g_perf_retro_sum   += retro_ns;
    g_perf_vk_sum      += vk_ns;
    g_perf_present_sum += present_ns;
    double ms = total_ns / 1e6;
    if (ms > g_perf_worst) g_perf_worst = ms;
    if (ms > 17.0)
        perf_write("[DROP] f=%d total=%.2f retro=%.2f vk=%.2f present=%.2f\n",
                   fno, ms, retro_ns/1e6, vk_ns/1e6, present_ns/1e6);
    if (fno > 0 && fno % 60 == 0) {
        int64_t now = perf_now_ns();
        if (g_perf_bucket_start == 0) {
            g_perf_bucket_start = now;
            g_perf_count = g_perf_retro_sum = g_perf_vk_sum = g_perf_present_sum = 0;
            g_perf_worst = 0;
            return;
        }
        double sec = (now - g_perf_bucket_start) / 1e9;
        double fps = g_perf_count / (sec > 0.001 ? sec : 0.001);
        perf_write("[F60] fps=%.1f worst=%.2fms retro=%.2fms vk=%.2fms present=%.2fms\n",
                   fps, g_perf_worst,
                   g_perf_retro_sum  /(g_perf_count*1e6),
                   g_perf_vk_sum     /(g_perf_count*1e6),
                   g_perf_present_sum/(g_perf_count*1e6));
        g_perf_count = g_perf_retro_sum = g_perf_vk_sum = g_perf_present_sum = 0;
        g_perf_worst = 0; g_perf_bucket_start = now;
    }
}

#include "libretro_vulkan.h"
#include "frame_profiler.h"

int g_use_vulkan = 0;

#include "ulog.h"
#define LOGI(...) ulog_write(ANDROID_LOG_INFO, "UltramanFE0", __VA_ARGS__)
#define LOGE(...) ulog_write(ANDROID_LOG_ERROR, "UltramanFE0", __VA_ARGS__)
#define LOGW(...) ulog_write(ANDROID_LOG_WARN, "UltramanFE0", __VA_ARGS__)

extern void ultraman_video_cb(const void *, unsigned, unsigned, size_t);
extern void ultraman_audio_sample_cb(int16_t, int16_t);
extern size_t ultraman_audio_batch_cb(const int16_t *, size_t);
extern void ultraman_poll_input_cb();
extern int16_t ultraman_input_state_cb(unsigned, unsigned, unsigned, unsigned);
extern void ultraman_video_set_geometry(unsigned, unsigned, double);
extern void ultraman_video_start();
extern void ultraman_video_stop();
extern void ultraman_video_set_hw(bool, int, int);
extern void ultraman_video_present();
extern int ultraman_video_present_step();
extern bool ultraman_video_is_ready();
extern bool ultraman_video_create_fbo_now();
extern bool ultraman_video_make_current();
extern void ultraman_video_release_current();
extern void ultraman_audio_start(double);
extern void ultraman_audio_stop();
extern struct retro_hw_render_callback* ultraman_get_hw_cb();

static std::atomic<bool> s_run{false};
static void do_pending_state();
static std::atomic<bool> s_loaded{false};
static double s_core_fps = 60.0;
static std::thread s_thread;
static std::string s_system_dir;
static std::atomic<int> s_frames{0};
static std::map<std::string, std::string> s_vars;
static bool s_hw_render = false;
static int s_backend = 0;
static int s_render_w = 960;
static int s_render_h = 544;
static void (*s_core_ctx_reset)() = nullptr;
static bool s_pending_ctx_reset = false;
static void (*s_core_ctx_destroy)() = nullptr;
static void (*s_deferred_reset_cb)() = nullptr;

static const char *resolution_for(int m) {
    switch (m) {
        case 1: return "480x272";
        case 2: return "960x544";
        case 3: return "1440x816";
        case 4: return "1920x1088";
        case 5: return "2400x1360";
        default: return "960x544";
    }
}

void ultraman_set_backend(int b) {
    s_backend = b;
    g_use_vulkan = (b == 1) ? 1 : 0;
    if (b == 0) s_vars["ppsspp_backend"] = "OpenGL";
    else if (b == 1) s_vars["ppsspp_backend"] = "Vulkan";
    else s_vars["ppsspp_backend"] = "OpenGL";
    LOGI("set_backend=%d ppsspp_backend=%s", b, s_vars["ppsspp_backend"].c_str());
}

void ultraman_set_options(int r, bool v, bool a) {
    s_vars["ppsspp_internal_resolution"] = resolution_for(r);
    if (s_vars.find("ppsspp_backend") == s_vars.end())
        s_vars["ppsspp_backend"] = "OpenGL";
    s_vars["ppsspp_gpu_hardware_transform"] = "enabled";
    s_vars["ppsspp_language"] = "Simplified Chinese";
    s_vars["ppsspp_frameskip"] = "0";
    s_vars["ppsspp_auto_frameskip"] = "disabled";
    s_vars["ppsspp_inflight_frames"] = "3";
    s_vars["ppsspp_texture_anisotropic_filtering"] = "4";
    s_vars["ppsspp_mulitsample_level"] = "1";
    s_vars["ppsspp_texture_scaling_type"] = "Auto";
    s_vars["ppsspp_texture_scaling_level"] = "1";
    s_vars["ppsspp_spline_quality"] = "1";
    s_vars["ppsspp_cropto16x9"] = "disabled";
    s_vars["ppsspp_skip_buffer_effects"] = "disabled";
    s_vars["ppsspp_skip_gpu_readbacks"] = "disabled";
    s_vars["ppsspp_cpu_core"] = "JIT";
    s_vars["ppsspp_fast_memory"] = "enabled";
    s_vars["ppsspp_ignore_bad_memory_access"] = "enabled";
    s_vars["ppsspp_io_timing_method"] = "Fast";
    s_vars["ppsspp_texture_filtering"] = "Auto";
    int mult = r; if (mult < 1) mult = 1; if (mult > 8) mult = 8;
    s_render_w = 480 * mult;
    s_render_h = 272 * mult;
    LOGI("set_options res=%d -> %dx%d vsync=%d audio=%d",
         r, s_render_w, s_render_h, v, a);
}

static const char *lookup_var(const char *key) {
    if (!key) return nullptr;
    auto it = s_vars.find(key);
    const char* ret = (it != s_vars.end()) ? it->second.c_str() : nullptr;
    return ret;
}

#include <cstdarg>
static void core_log_cb(enum retro_log_level level, const char *fmt, ...) {
    if (!fmt) return;
    int prio = ANDROID_LOG_INFO;
    switch (level) {
        case RETRO_LOG_DEBUG: prio = ANDROID_LOG_DEBUG; break;
        case RETRO_LOG_INFO:  prio = ANDROID_LOG_INFO;  break;
        case RETRO_LOG_WARN:  prio = ANDROID_LOG_WARN;  break;
        case RETRO_LOG_ERROR: prio = ANDROID_LOG_ERROR; break;
        default: break;
    }
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(prio, "PPSSPPcore", fmt, ap);
    va_end(ap);
}

static bool env_cb(unsigned cmd, void *data) {
    LOGI("env_cb cmd=%u", cmd);
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data = false;
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:            *(const char **)data = s_system_dir.c_str();
            return true;
        case RETRO_ENVIRONMENT_GET_OVERSCAN:
            *(bool *)data = false;
            return true;
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
            LOGI("SET_PERFORMANCE_LEVEL");
            return true;
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            LOGI("SET_INPUT_DESCRIPTORS");
            return true;
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
            LOGI("SET_CONTROLLER_INFO");
            return true;
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
            LOGI("SET_MEMORY_MAPS");
            return true;
        case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
            LOGI("SET_SUBSYSTEM_INFO");
            return true;
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
            LOGI("SET_SUPPORT_ACHIEVEMENTS");
            return true;
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
            if (data) {
                const unsigned char *raw = (const unsigned char *)data;
                char hex[3 * 32 + 1];
                for (int i = 0; i < 32; i++) {
                    snprintf(hex + i * 3, 4, "%02x ", raw[i]);
                }
                hex[3 * 32] = 0;
                LOGI("AV_INFO raw[0..31]: %s", hex);
                LOGI("AV_INFO sizeof=%zu", sizeof(struct retro_system_av_info));
            }
            struct retro_system_av_info *info = (struct retro_system_av_info *)data;
            unsigned w = 0, h = 0;
            double fps = 0.0;
            if (info) {
                w = info->geometry.base_width;
                h = info->geometry.base_height;
                fps = info->timing.fps;
            }
            if (w == 0 || h == 0 || w > 4096 || h > 4096) {
                LOGW("av_info invalid %ux%u fps=%.2f -> ignore", w, h, fps);
            } else {
                LOGI("av_info valid %ux%u fps=%.2f", w, h, fps);
                if (fps > 1.0 && fps < 1000.0) s_core_fps = fps;
            }
            return true;
        }
        case RETRO_ENVIRONMENT_SET_GEOMETRY: {
            struct retro_game_geometry *g = (struct retro_game_geometry *)data;
            if (g) {
                LOGI("geometry core says %ux%u (keep %dx%d)",
                     g->base_width, g->base_height, s_render_w, s_render_h);
            }
            return true;
        }
        case RETRO_ENVIRONMENT_SET_ROTATION:
            return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES:
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            struct retro_variable *v = (struct retro_variable *)data;
            const char *val = lookup_var(v->key);
            if (val) { v->value = val; return true; }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *(bool *)data = false;
            return true;
        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            struct retro_hw_render_callback *cb = (struct retro_hw_render_callback *)data;
            if (!cb) { LOGE("SET_HW_RENDER: null cb"); return false; }
            LOGI("SET_HW_RENDER req type=%d depth=%d stencil=%d ver=%u",
                 cb->context_type, cb->depth ? 1 : 0, cb->stencil ? 1 : 0, cb->version);

            if (cb->context_type == RETRO_HW_CONTEXT_VULKAN) {
                if (s_backend != 1) {
                    LOGW("Vulkan requested but backend != 1, refusing");
                    return false;
                }
                LOGI("accepting Vulkan");
                extern bool vk_init(ANativeWindow*, uint32_t, uint32_t);
                extern void ultraman_video_get_window_and_size(ANativeWindow**, uint32_t*, uint32_t*);
                ANativeWindow* win = nullptr;
                uint32_t w = 0, h = 0;
                ultraman_video_get_window_and_size(&win, &w, &h);
                if (!win) { LOGE("Vulkan: no native window"); return false; }
                if (!vk_init(win, w, h)) { LOGE("Vulkan init failed"); return false; }
                s_hw_render = true;
                LOGI("Vulkan HW accepted");
                return true;
            }

            if (cb->context_type == RETRO_HW_CONTEXT_OPENGL_ES3 ||
                cb->context_type == RETRO_HW_CONTEXT_OPENGL_ES2 ||
                cb->context_type == RETRO_HW_CONTEXT_OPENGL_CORE ||
                cb->context_type == RETRO_HW_CONTEXT_OPENGL) {
                if (s_backend == 1) {
                    LOGW("core requested GL, downgrading from Vulkan");
                    s_backend = 0;
                    g_use_vulkan = 0;
                }
                s_core_ctx_reset   = cb->context_reset;
                s_core_ctx_destroy = cb->context_destroy;
                struct retro_hw_render_callback *ours = ultraman_get_hw_cb();
                cb->get_current_framebuffer = ours->get_current_framebuffer;
                cb->get_proc_address        = ours->get_proc_address;
                cb->depth                   = true;
                cb->stencil                 = true;
                s_hw_render = true;
                LOGI("OpenGL HW accepted");
                return true;
            }
            LOGE("HW type %d unsupported", cb->context_type);
            return false;
        }
        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
            LOGI("GET_HW_RENDER_INTERFACE called, backend=%d", s_backend);
            if (s_backend != 1) {
                LOGW("not Vulkan backend, refusing interface");
                return false;
            }
            extern struct retro_hw_render_interface_vulkan* vk_get_retro_interface();
            struct retro_hw_render_interface_vulkan* iface = vk_get_retro_interface();
            if (!iface) { LOGE("vk interface null"); return false; }
            *(const struct retro_hw_render_interface_vulkan **)data = iface;
            LOGI("Vulkan interface handed to core");
            return true;
        }
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
            *(unsigned *)data = RETRO_HW_CONTEXT_OPENGL_ES3;
            return true;
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            if (data) *(int *)data = 3;
            return true;
        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
            *(bool *)data = false;
            return true;
        case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
            *(float *)data = 60.0f;
            return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            *(bool *)data = true;
            return true;
        case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
            *(unsigned *)data = 1;
            return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *(unsigned *)data = 1;
            return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            *(unsigned *)data = 0;
            return true;
        case RETRO_ENVIRONMENT_GET_USERNAME:
            *(const char **)data = "player";
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
            return true;
        case 0x1002D:
            LOGW("GET_VFS_INTERFACE not impl");
            return false;
        case 0x10033:
            LOGW("GET_INPUT_BITMASKS: return false");
            return false;
        case RETRO_ENVIRONMENT_SET_MESSAGE:
            return true;
        case RETRO_ENVIRONMENT_SHUTDOWN: {
            extern void ultraman_video_dump_gl_errors();
            ultraman_video_dump_gl_errors();
            LOGW(">>> core requested SHUTDOWN, stopping loop <<<");
            s_run.store(false);
            return true;
        }
        case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
            *(unsigned *)data = 1;
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            struct retro_log_callback *cb = (struct retro_log_callback *)data;
            if (cb) {
                cb->log = core_log_cb;
                LOGI("GET_LOG_INTERFACE: provided core_log_cb");
            }
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
            LOGI("GET_VFS_INTERFACE: no impl");
            return false;
        default:
            return false;
    }
}

static void on_a(int16_t l, int16_t r) { ultraman_audio_sample_cb(l, r); }
static size_t on_ab(const int16_t *d, size_t f) { return ultraman_audio_batch_cb(d, f); }
static void on_ip() { ultraman_poll_input_cb(); }
static int16_t on_is(unsigned p, unsigned d, unsigned i, unsigned id) {
    return ultraman_input_state_cb(p, d, i, id);
}

bool ultraman_load_game(const std::string &path) {
    LOGI("load_game: %s", path.c_str());

    s_system_dir = "/storage/emulated/0/Android/data/com.pssp/files/system";
    mkdir(s_system_dir.c_str(), 0777);

    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        LOGE("load_game: stat failed (path not accessible)");
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        LOGE("load_game: not a regular file");
        return false;
    }
    LOGI("load_game: file ok size=%lld", (long long)st.st_size);

    FILE *probe = fopen(path.c_str(), "rb");
    if (!probe) {
        LOGE("load_game: fopen failed");
        return false;
    }
    fclose(probe);
    LOGI("load_game: fopen ok");

    if (s_loaded.load()) {
        LOGI(">>> unload previous game <<<");
        retro_unload_game();
        retro_deinit();
        s_loaded.store(false);
    }

    retro_set_environment(env_cb);
    retro_set_video_refresh(ultraman_video_cb);
    retro_set_audio_sample(on_a);
    retro_set_audio_sample_batch(on_ab);
    retro_set_input_poll(on_ip);
    retro_set_input_state(on_is);
    extern bool ultraman_video_make_current();
    ultraman_video_make_current();
    LOGI(">>> calling retro_init <<<");
    retro_init();
    LOGI(">>> retro_init returned ok <<<");

    struct retro_system_info info{};
    retro_get_system_info(&info);
    LOGI("core: %s %s", info.library_name ? info.library_name : "?",
         info.library_version ? info.library_version : "?");

    struct retro_system_av_info av{};
    retro_get_system_av_info(&av);

    LOGI("av %ux%u @%.2f/%.0f",
         av.geometry.base_width, av.geometry.base_height,
         av.timing.fps, av.timing.sample_rate);
    if (av.timing.fps > 1.0 && av.timing.fps < 1000.0) s_core_fps = av.timing.fps;

    ultraman_video_set_geometry(av.geometry.base_width, av.geometry.base_height, av.timing.fps);
    ultraman_video_set_hw(true, s_render_w, s_render_h);
    LOGI("hw size %dx%d", s_render_w, s_render_h);

    struct retro_game_info game{};
    game.path = path.c_str();
    game.data = nullptr;
    game.size = 0;
    game.meta = nullptr;

    LOGI(">>> calling retro_load_game (render %dx%d) <<<", s_render_w, s_render_h);
    bool lg = retro_load_game(&game);
    LOGI(">>> retro_load_game returned %d <<<", lg ? 1 : 0);
    if (!lg) {
        LOGE("retro_load_game FAILED");
        return false;
    }
    LOGI("retro_load_game ok hw=%d", s_hw_render ? 1 : 0);

    if (s_hw_render && s_core_ctx_reset) {
        extern bool ultraman_video_create_fbo_now();
        LOGI(">>> calling core ctx_reset (after retro_load_game) <<<");
        ultraman_video_create_fbo_now();
        s_core_ctx_reset();
        LOGI(">>> core ctx_reset done <<<");
    } else {
        LOGW("no ctx_reset called (hw=%d)", s_hw_render ? 1 : 0);
    }

    ultraman_audio_start(av.timing.sample_rate);
    s_loaded.store(true);
    return true;
}

void ultraman_start_loop() {
    if (s_run.exchange(true)) return;
    LOGI("start loop");

    ultraman_video_release_current();

    s_thread = std::thread([]{
        {
            struct sched_param sp{};
            sp.sched_priority = 0;
            int r1 = sched_setscheduler(0, SCHED_FIFO, &sp);
            int r2 = setpriority(PRIO_PROCESS, 0, -3);
            bind_to_big_core();
            LOGI("loop prio: sched=%d nice=%d", r1, r2);
        }
        LOGI("loop thread started");
        if (s_hw_render) {
            LOGI("loop: binding EGL context");
            ultraman_video_make_current();
        }

        const double fps = 59.94;
        const int64_t frame_ns = (int64_t)(1000000000.0 / fps);
        auto next = std::chrono::steady_clock::now();

        while (s_run.load()) {
            if (!s_run.load()) break;

            auto t0 = std::chrono::steady_clock::now();

            int64_t _t0 = perf_now_ns();
            do_pending_state();
            if (g_use_vulkan == 1) { extern bool vk_prepare_frame(); vk_prepare_frame(); }
            retro_run();
            int64_t _t1 = perf_now_ns();
            int64_t _tv0 = perf_now_ns();
            if (g_use_vulkan == 1) {
                extern bool vk_render_frame();
                vk_render_frame();
            } else if (s_hw_render) {
                ultraman_video_present();
            }
            int64_t _tv1 = perf_now_ns();
            int f = s_frames.fetch_add(1) + 1;
            int64_t _t2 = perf_now_ns();
            int64_t _t3 = perf_now_ns();
            perf_frame((_t3 - _t0), (_t1 - _t0), (_tv1 - _tv0), (_t3 - _t2), (int)(s_frames.load()));

            if (f <= 3 || f == 300 || f == 1800) {
                auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                LOGI("frame %d cost %lldms", f, (long long)dur);
            }
            if (!s_run.load()) break;

            next += std::chrono::nanoseconds(frame_ns);
            { auto _n = std::chrono::steady_clock::now(); if (next < _n) next = _n + std::chrono::nanoseconds(frame_ns); }
            auto now = std::chrono::steady_clock::now();
            auto diff = next - now;
            auto diff_us = std::chrono::duration_cast<std::chrono::microseconds>(diff).count();
            if (diff_us > 500) {
                if (diff_us > 300) std::this_thread::sleep_for(std::chrono::microseconds(diff_us - 300));
                while (std::chrono::steady_clock::now() < next) {
                    asm volatile("yield" ::: "memory");
                }
            } else if (diff_us < -1000) {
                next = now;
            } else if (diff_us < -1000) {
                next = now;
            } else if (diff_us < -1000) {
                next = now;
            } else if (diff_us < -1000) {
                next = now;
            } else if (diff < std::chrono::nanoseconds(-1000000)) {
                next = now;
            }
        }
        LOGI("loop thread ended");
    });
}



static std::thread s_render_thread;

void ultraman_pause_render() {
    LOGI("pause_render: signaling stop");
    s_run.exchange(false);
    LOGI("pause_render: signaled (no join)");
}

void ultraman_resume_render() {
    LOGI("resume_render");
    if (!s_loaded.load()) {
        LOGW("resume: core not loaded, skip");
        return;
    }
    if (s_thread.joinable()) {
        LOGI("resume: joining old thread");
        s_thread.join();
        LOGI("resume: old thread joined");
    }
    if (s_run.exchange(true)) {
        LOGW("resume: already running");
        return;
    }

    extern bool ultraman_video_make_current();
    extern void ultraman_video_present();

    s_thread = std::thread([]{
        LOGI("resume thread started");
        if (s_hw_render) {
            ultraman_video_make_current();
        }
        const double fps = 59.94;
        const auto frame_dur = std::chrono::microseconds((long long)(1000000.0 / fps));
        auto next = std::chrono::steady_clock::now();
        while (s_run.load()) {
            int64_t _t0 = perf_now_ns();
            do_pending_state();
            if (g_use_vulkan == 1) { extern bool vk_prepare_frame(); vk_prepare_frame(); }
            retro_run();
            int64_t _t1 = perf_now_ns();
            int64_t _tv0 = perf_now_ns();
            if (g_use_vulkan == 1) {
                extern bool vk_render_frame();
                vk_render_frame();
            } else if (s_hw_render) {
                ultraman_video_present();
            }
            int64_t _tv1 = perf_now_ns();
            int64_t _t2 = perf_now_ns();
            int64_t _t3 = perf_now_ns();
            perf_frame((_t3 - _t0), (_t1 - _t0), (_tv1 - _tv0), (_t3 - _t2), (int)(s_frames.load()));
            next += frame_dur;
            auto now = std::chrono::steady_clock::now();
            if (next < now) next = now + frame_dur;
            if (next > now) std::this_thread::sleep_until(next);
            else next = now;
        }
        LOGI("resume thread ended");
    });
}

extern "C" double ultraman_get_core_fps() { return s_core_fps; }

void ultraman_stop_loop() {
    if (!s_run.exchange(false)) return;
    LOGI("stop loop: joining thread");
    if (s_thread.joinable()) s_thread.join();
    LOGI("stop loop: thread joined");

    if (s_loaded.exchange(false)) {
        if (s_core_ctx_destroy) {
            LOGI(">>> calling core ctx_destroy <<<");
            s_core_ctx_destroy();
            s_core_ctx_destroy = nullptr;
        }
        LOGI("stop loop: unload game");
        retro_unload_game();
        LOGI("stop loop: deinit core");
        retro_deinit();
    }

    extern void ultraman_video_stop();
    LOGI("stop loop: stop video");
    ultraman_video_stop();
    LOGI("stop loop: done");
}


static std::atomic<int>    s_state_cmd{0};
static std::atomic<bool>   s_state_done{true};
static std::atomic<bool>   s_state_result{false};
static std::vector<uint8_t> s_state_buf;

static void do_pending_state() {
    int cmd = s_state_cmd.exchange(0);
    if (cmd == 0) return;
    if (cmd == 1) {
        size_t sz = retro_serialize_size();
        if (sz > 0) {
            s_state_buf.resize(sz);
            s_state_result = retro_serialize(s_state_buf.data(), sz);
        } else {
            s_state_result = false;
        }
    } else if (cmd == 2) {
        s_state_result = retro_unserialize(s_state_buf.data(), s_state_buf.size());
    }
    s_state_done = true;
}

bool ultraman_save_state(const char *path) {
    s_state_done = false;
    s_state_result = false;
    s_state_cmd = 1;
    int waited = 0;
    while (!s_state_done && waited < 1000) {
        if (!s_run.load()) return false;
        usleep(500);
        waited++;
    }
    if (!s_state_done) return false;
    if (!s_state_result) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(s_state_buf.data(), 1, s_state_buf.size(), f);
    fclose(f);
    return w == s_state_buf.size();
}

bool ultraman_load_state(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }
    s_state_buf.resize((size_t)sz);
    size_t r = fread(s_state_buf.data(), 1, (size_t)sz, f);
    fclose(f);
    if (r != (size_t)sz) return false;

    s_state_done = false;
    s_state_result = false;
    s_state_cmd = 2;
    int waited = 0;
    while (!s_state_done && waited < 1000) {
        if (!s_run.load()) return false;
        usleep(500);
        waited++;
    }
    if (!s_state_done) return false;
    return s_state_result;
}

bool ultraman_save_sram(const char *path) {
    void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!data || size == 0) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(data, 1, size, f);
    fclose(f);
    return w == size;
}

bool ultraman_load_sram(const char *path) {
    void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!data || size == 0) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t r = fread(data, 1, size, f);
    fclose(f);
    return r > 0;
}

void ultraman_call_core_ctx_reset() {
    if (!s_core_ctx_reset) {
        LOGI("no core ctx_reset saved yet");
        return;
    }
    extern bool ultraman_video_create_fbo_now();
    LOGI(">>> calling core context_reset (after FBO ready) <<<");
    ultraman_video_create_fbo_now();
    s_core_ctx_reset();
    LOGI(">>> core context_reset done <<<");
}

void ultraman_on_video_ready() {
    LOGI("video_ready: video became ready");
}


int64_t ultraman_get_frame_count() {
    (void)s_pending_ctx_reset;
    (void)s_deferred_reset_cb;
    return (int64_t)s_frames.load();
}


void ultraman_reset_context() {
    LOGI("reset_context: invoking core ctx_reset");
    if (!s_hw_render || !s_core_ctx_reset) {
        LOGW("reset_context: no core ctx_reset saved");
        return;
    }
    extern bool ultraman_video_create_fbo_now();
    ultraman_video_create_fbo_now();
    s_core_ctx_reset();
    LOGI("reset_context: done");
}



void ultraman_notify_vulkan_recreate(ANativeWindow* win, uint32_t w, uint32_t h) {
    LOGI("notify_vulkan_recreate %p %ux%u", win, w, h);
    if (g_use_vulkan != 1 || !s_hw_render) {
        LOGW("not vulkan or not hw, skip");
        return;
    }
    extern bool vk_recreate_swapchain(ANativeWindow*, uint32_t, uint32_t);
    if (!vk_recreate_swapchain(win, w, h)) {
        LOGE("vk_recreate_swapchain failed");
        return;
    }
    if (s_core_ctx_reset) {
        LOGI(">>> core ctx_reset after swapchain recreate <<<");
        s_core_ctx_reset();
    }
}
