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
extern void ultraman_audio_start(double);
extern void ultraman_audio_stop();
extern struct retro_hw_render_callback* ultraman_get_hw_cb();
extern void ultraman_video_prepare_fbo();
extern void ultraman_video_set_hw_size(int, int);
extern bool ultraman_video_is_ready();

static std::atomic<bool> s_run{false};
static std::atomic<bool> s_loaded{false};
static std::thread s_thread;
static std::string s_system_dir;
static std::atomic<int> s_frames{0};
static std::map<std::string, std::string> s_vars;
static bool s_hw_render = false;
static int s_backend = 0;

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
    LOGI("set_backend=%d", b);
}

void ultraman_set_options(int r, bool v, bool a) {
    s_vars["ppsspp_internal_resolution"] = resolution_for(r);
    s_vars["ppsspp_vsync"] = v ? "enabled" : "disabled";
    s_vars["ppsspp_audio_latency"] = a ? "low" : "high";
    s_vars["ppsspp_backend"] = "Auto";
    s_vars["ppsspp_software_rendering"] = "disabled";
    s_vars["ppsspp_gpu_hardware_transform"] = "enabled";
    s_vars["ppsspp_rendering_mode"] = "buffered";
    s_vars["ppsspp_language"] = "Simplified Chinese";
    s_vars["ppsspp_audio_enable"] = a ? "enabled" : "disabled";
    LOGI("set_options res=%d(%s) vsync=%d audio=%d", r, resolution_for(r), v, a);
}

static const char *lookup_var(const char *key) {
    if (!key) return nullptr;
    auto it = s_vars.find(key);
    if (it != s_vars.end()) {
        LOGI("GET_VAR %s -> %s", key, it->second.c_str());
        return it->second.c_str();
    }
    return nullptr;
}
static bool env_cb(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            LOGI("env PIXEL_FORMAT=%d", *(int*)data);
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data = true;
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
            *(const char **)data = s_system_dir.c_str();
            return true;
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
            return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES: {
            LOGI("=== CORE VARIABLES ===");
            const struct retro_variable *v = (const struct retro_variable *)data;
            while (v && v->key) {
                LOGI("VAR %s = %s", v->key, v->value ? v->value : "(null)");
                v++;
            }
            LOGI("=== END ===");
            return true;
        }
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
            LOGI("SET_HW_RENDER type=%d backend=%d", cb->context_type, s_backend);
            if (cb->context_type == RETRO_HW_CONTEXT_VULKAN) {
                LOGE("Vulkan refused");
                return false;
            }
            if (cb->context_type == RETRO_HW_CONTEXT_OPENGL_ES3 ||
                cb->context_type == RETRO_HW_CONTEXT_OPENGL_ES2 ||
                cb->context_type == RETRO_HW_CONTEXT_OPENGL_CORE ||
                cb->context_type == RETRO_HW_CONTEXT_OPENGL) {
                struct retro_hw_render_callback *ours = ultraman_get_hw_cb();
                int requested = cb->context_type;
                cb->context_reset           = ours->context_reset;
                cb->context_destroy         = ours->context_destroy;
                cb->get_current_framebuffer = ours->get_current_framebuffer;
                cb->get_proc_address        = ours->get_proc_address;
                cb->depth                   = ours->depth;
                cb->stencil                 = ours->stencil;
                cb->bottom_left_origin      = ours->bottom_left_origin;
                cb->version_major           = (requested == RETRO_HW_CONTEXT_OPENGL_ES2) ? 2 : 3;
                cb->version_minor           = 0;
                cb->cache_context           = ours->cache_context;
                cb->debug_context           = ours->debug_context;
                s_hw_render = true;
                LOGI("HW render accepted type=%d ver=%u.%u",
                     requested, cb->version_major, cb->version_minor);
                if (ultraman_video_is_ready()) {
                    ultraman_video_prepare_fbo();
                } else {
                    LOGE("SET_HW_RENDER: video not ready yet");
                }
                return true;
            }
            LOGE("HW type %d unsupported", cb->context_type);
            return false;
        }
        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
            return false;
        case RETRO_ENVIRONMENT_SET_MESSAGE:
            return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = 0; return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: *(bool *)data = true; return true;
        case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS: *(unsigned *)data = 1; return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 1; return true;
        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
            *(unsigned *)data = RETRO_HW_CONTEXT_OPENGL_ES3;
            return true;
        default:
            LOGI("env unhandled=%u", cmd);
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
    s_system_dir = "/data/data/com.ultraman.fe0/files/system";
    mkdir(s_system_dir.c_str(), 0777);
    retro_set_environment(env_cb);
    retro_set_video_refresh(ultraman_video_cb);
    retro_set_audio_sample(on_a);
    retro_set_audio_sample_batch(on_ab);
    retro_set_input_poll(on_ip);
    retro_set_input_state(on_is);
    retro_init();
    LOGI("retro_init ok");
    struct retro_system_info info{};
    retro_get_system_info(&info);
    LOGI("core: %s %s", info.library_name ? info.library_name : "?",
         info.library_version ? info.library_version : "?");
    struct retro_system_av_info av{};
    retro_get_system_av_info(&av);
    LOGI("av %ux%u @%.2f/%.0f",
         av.geometry.base_width, av.geometry.base_height,
         av.timing.fps, av.timing.sample_rate);
    int scale = 2;
    auto rit = s_vars.find("ppsspp_internal_resolution");
    if (rit != s_vars.end()) {
        int rw = 0, rh = 0;
        if (sscanf(rit->second.c_str(), "%dx%d", &rw, &rh) == 2 && rw > 0) {
            scale = rw / 480;
            if (scale < 1) scale = 1;
            if (scale > 8) scale = 8;
        }
    }
    int base_w = (int)av.geometry.base_width;
    int base_h = (int)av.geometry.base_height;
    if (base_w <= 0) base_w = 480;
    if (base_h <= 0) base_h = 272;
    int hw_w = base_w * scale;
    int hw_h = base_h * scale;
    LOGI("hw geometry base=%dx%d scale=%d -> %dx%d",
         base_w, base_h, scale, hw_w, hw_h);

    ultraman_video_set_geometry(base_w, base_h, av.timing.fps);
    ultraman_video_set_hw_size(hw_w, hw_h);
    LOGI("geometry set %dx%d (scale=%d)", hw_w, hw_h, scale);
    struct retro_game_info game{};
    game.path = path.c_str();
    game.data = nullptr;
    game.size = 0;
    game.meta = nullptr;
    if (!retro_load_game(&game)) { LOGE("retro_load_game FAILED"); return false; }
    LOGI("retro_load_game ok hw=%d", s_hw_render);
    ultraman_audio_start(av.timing.sample_rate);
    s_loaded.store(true);
    return true;
}
void ultraman_start_loop() {
    if (s_run.exchange(true)) return;
    LOGI("start loop");
    s_thread = std::thread([]{
        while (s_run.load()) {
            retro_run();
            int f = s_frames.fetch_add(1) + 1;
            if (f == 1 || f == 10 || f == 60 || f == 300) LOGI("frame %d", f);
        }
    });
}
void ultraman_stop_loop() {
    if (!s_run.exchange(false)) return;
    LOGI("stop loop");
    if (s_thread.joinable()) s_thread.join();
    if (s_loaded.exchange(false)) {
        retro_unload_game();
        retro_deinit();
        ultraman_video_stop();
        ultraman_audio_stop();
    }
}
bool ultraman_save_state(const char *path) {
    size_t size = retro_serialize_size();
    if (size == 0) { LOGE("serialize_size=0"); return false; }
    void *buf = malloc(size);
    if (!buf) return false;
    if (!retro_serialize(buf, size)) { free(buf); return false; }
    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); LOGE("open fail: %s", path); return false; }
    size_t w = fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);
    LOGI("state saved %zu -> %s", w, path);
    return w == size;
}
bool ultraman_load_state(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { LOGE("no file: %s", path); return false; }
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
    LOGI("state load %zu from %s -> %d", r, path, ok);
    return ok;
}
bool ultraman_save_sram(const char *path) {
    void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!data || size == 0) { LOGE("no sram"); return false; }
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t w = fwrite(data, 1, size, f);
    fclose(f);
    LOGI("sram saved %zu -> %s", w, path);
    return w == size;
}
bool ultraman_load_sram(const char *path) {
    void *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!data || size == 0) { LOGE("no sram"); return false; }
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t r = fread(data, 1, size, f);
    fclose(f);
    LOGI("sram load %zu from %s", r, path);
    return r > 0;
}
