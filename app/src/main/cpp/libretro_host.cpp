#include <android/log.h>
#include <atomic>
#include <thread>
#include <string>
#include <cstring>
#include <cstdio>
#include <map>
#include <sys/stat.h>
#include "libretro.h"
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
extern bool ultraman_video_is_ready();
extern bool ultraman_video_create_fbo_now();
extern bool ultraman_video_make_current();
extern void ultraman_video_release_current();
extern void ultraman_audio_start(double);
extern void ultraman_audio_stop();
extern struct retro_hw_render_callback* ultraman_get_hw_cb();

static std::atomic<bool> s_run{false};
static std::atomic<bool> s_loaded{false};
static std::thread s_thread;
static std::string s_system_dir;
static std::atomic<int> s_frames{0};
static std::map<std::string, std::string> s_vars;
static bool s_hw_render = false;
static int s_backend = 0;
static int s_render_w = 960;
static int s_render_h = 544;
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
    if (b == 0) s_vars["ppsspp_backend"] = "OpenGL";
    else if (b == 1) s_vars["ppsspp_backend"] = "Vulkan";
    else s_vars["ppsspp_backend"] = "OpenGL";
    LOGI("set_backend=%d ppsspp_backend=%s", b, s_vars["ppsspp_backend"].c_str());
}

void ultraman_set_options(int r, bool v, bool a) {
    s_vars["ppsspp_internal_resolution"] = resolution_for(r);
    s_vars["ppsspp_vsync"] = v ? "enabled" : "disabled";
    s_vars["ppsspp_audio_latency"] = a ? "low" : "high";
    if (s_vars.find("ppsspp_backend") == s_vars.end())
        s_vars["ppsspp_backend"] = "OpenGL";
    s_vars["ppsspp_software_rendering"] = "disabled";
    s_vars["ppsspp_gpu_hardware_transform"] = "enabled";
    s_vars["ppsspp_rendering_mode"] = "buffered";
    s_vars["ppsspp_language"] = "Simplified Chinese";
    s_vars["ppsspp_audio_enable"] = a ? "enabled" : "disabled";
    int mult = r; if (mult < 1) mult = 1; if (mult > 8) mult = 8;
    s_render_w = 480 * mult;
    s_render_h = 272 * mult;
    LOGI("set_options res=%d(%s) -> %dx%d vsync=%d audio=%d",
         r, resolution_for(r), s_render_w, s_render_h, v, a);
}

static const char *lookup_var(const char *key) {
    if (!key) return nullptr;
    auto it = s_vars.find(key);
    if (it != s_vars.end()) return it->second.c_str();
    return nullptr;
}

static bool env_cb(unsigned cmd, void *data) {
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
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
            struct retro_system_av_info *info = (struct retro_system_av_info *)data;
            unsigned w = 0, h = 0;
            double fps = 0.0;
            if (info) {
                w = info->geometry.base_width;
                h = info->geometry.base_height;
                fps = info->timing.fps;
            }
            if (w == 0 || h == 0 || w > 4096 || h > 4096) {
                LOGW("av_info invalid %ux%u -> ignore (keep %dx%d)",
                     w, h, s_render_w, s_render_h);
            } else {
                LOGI("av_info core says %ux%u fps=%.2f (keep %dx%d)",
                     w, h, fps, s_render_w, s_render_h);
            }
            /* 关键：不调用 set_hw_size，不触发 rebuild */
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
            LOGI("SET_HW_RENDER type=%d", cb->context_type);

            if (cb->context_type == RETRO_HW_CONTEXT_VULKAN) {
                LOGW("Vulkan refused");
                return false;
            }

            int requested = cb->context_type;
            if (requested != RETRO_HW_CONTEXT_OPENGL_ES3 &&
                requested != RETRO_HW_CONTEXT_OPENGL_ES2 &&
                requested != RETRO_HW_CONTEXT_OPENGL_CORE &&
                requested != RETRO_HW_CONTEXT_OPENGL) {
                LOGE("HW type %d unsupported", requested);
                return false;
            }

            struct retro_hw_render_callback *ours = ultraman_get_hw_cb();

            cb->context_reset           = ours->context_reset;
            cb->context_destroy         = ours->context_destroy;
            cb->get_current_framebuffer = ours->get_current_framebuffer;
            cb->get_proc_address        = ours->get_proc_address;
            cb->depth                   = true;
            cb->stencil                 = true;
            cb->bottom_left_origin      = false;
            cb->cache_context           = false;
            cb->debug_context           = false;

            if (requested == RETRO_HW_CONTEXT_OPENGL_ES2) {
                cb->version_major = 2;
                cb->version_minor = 0;
            } else {
                cb->version_major = 3;
                cb->version_minor = 0;
            }

            s_hw_render = true;
            LOGI("HW accepted: type=%d ver=%u.%u", requested,
                 cb->version_major, cb->version_minor);

            if (ultraman_video_is_ready()) {
                ultraman_video_create_fbo_now();
            }
            return true;
        }
        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
            return false;
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
        case 45:
        case 0x1002D:
            LOGW("GET_VFS_INTERFACE not impl");
            return false;
        case 0x10033:
            *(bool *)data = true;
            return true;
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
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
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
    s_system_dir = "/data/data/com.ultraman.fe0/files/system";
    mkdir(s_system_dir.c_str(), 0777);

    retro_set_environment(env_cb);
    retro_set_video_refresh(ultraman_video_cb);
    retro_set_audio_sample(on_a);
    retro_set_audio_sample_batch(on_ab);
    retro_set_input_poll(on_ip);
    retro_set_input_state(on_is);

    retro_init();
    struct retro_system_info info{};
    retro_get_system_info(&info);
    LOGI("core: %s %s", info.library_name ? info.library_name : "?",
         info.library_version ? info.library_version : "?");

    struct retro_system_av_info av{};
    retro_get_system_av_info(&av);
    LOGI("av %ux%u @%.2f/%.0f",
         av.geometry.base_width, av.geometry.base_height,
         av.timing.fps, av.timing.sample_rate);

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

    ultraman_audio_start(av.timing.sample_rate);
    s_loaded.store(true);
    return true;
}

void ultraman_start_loop() {
    if (s_run.exchange(true)) return;
    LOGI("start loop");

    ultraman_video_release_current();

    s_thread = std::thread([]{
        LOGI("loop thread started");
        if (s_hw_render) {
            LOGI("loop: binding EGL context");
            ultraman_video_make_current();
            extern void ultraman_video_dump_gl_errors();
            ultraman_video_dump_gl_errors();
        }
        while (s_run.load()) {
            if (!s_run.load()) break;
            retro_run();
            int f = s_frames.fetch_add(1) + 1;
            if (s_hw_render) ultraman_video_present();
            if (f <= 3 || f == 30 || f == 60 || f == 120 || f == 300) {
                LOGI("frame %d", f);
            }
            if (!s_run.load()) {
                LOGI("loop: stop flag set, exiting");
                break;
            }
        }
        LOGI("loop thread ended");
    });
}

void ultraman_stop_loop() {
    if (!s_run.exchange(false)) return;
    LOGI("stop loop: joining thread");
    if (s_thread.joinable()) s_thread.join();
    LOGI("stop loop: thread joined");

    if (s_loaded.exchange(false)) {
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

bool ultraman_save_state(const char *path) {
    size_t size = retro_serialize_size();
    if (size == 0) return false;
    void *buf = malloc(size);
    if (!buf) return false;
    if (!retro_serialize(buf, size)) { free(buf); return false; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return false; }
    size_t w = fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);
    return w == size;
}

bool ultraman_load_state(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    bool ok = retro_unserialize(buf, r);
    free(buf);
    return ok;
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

void ultraman_on_video_ready() {
    if (!s_deferred_reset_cb) {
        LOGI("video_ready: no pending reset");
        return;
    }
    void (*fn)() = s_deferred_reset_cb;
    s_deferred_reset_cb = nullptr;
    LOGI("video_ready: invoking deferred context_reset");
    if (fn) fn();
    LOGI("video_ready: deferred done");
}
