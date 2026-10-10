#include <android/log.h>
#include <atomic>
#include <thread>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include "libretro.h"
#include "ulog.h"
#define LOGI(...) ulog_write(ANDROID_LOG_INFO, "UltramanFE0", __VA_ARGS__)
#define LOGE(...) ulog_write(ANDROID_LOG_ERROR, "UltramanFE0", __VA_ARGS__)

extern void ultraman_video_cb(const void *, unsigned, unsigned, size_t);
extern void ultraman_audio_sample_cb(int16_t, int16_t);
extern size_t ultraman_audio_batch_cb(const int16_t *, size_t);
extern void ultraman_poll_input_cb();
extern int16_t ultraman_input_state_cb(unsigned, unsigned, unsigned, unsigned);
extern void ultraman_video_set_geometry(unsigned, unsigned, double);
extern void ultraman_video_start();
extern void ultraman_video_stop();
extern void ultraman_audio_start(double);
extern void ultraman_audio_stop();

static std::atomic<bool> s_run{false};
static std::atomic<bool> s_loaded{false};
static std::thread s_thread;
static std::string s_system_dir;
static std::atomic<int> s_frames{0};
static std::map<std::string, std::string> s_vars;
static int s_resolution = 2;
static bool s_vsync = true;
static bool s_audio = true;

void ultraman_set_options(int r, bool v, bool a) {
    s_resolution = r;
    s_vsync = v;
    s_audio = a;
    s_vars["ppsspp_internal_resolution"] = std::to_string(r) + "x";
    s_vars["ppsspp_vsync"] = v ? "enabled" : "disabled";
    s_vars["ppsspp_audio"] = a ? "enabled" : "disabled";
    s_vars["ppsspp_graphics_backend"] = "opengl";
    LOGI("options: res=%d vsync=%d audio=%d", r, v, a);
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
            LOGI("env: PIXEL_FORMAT=%d", *(int*)data);
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data = true;
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
            *(const char **)data = s_system_dir.c_str();
            return true;
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS: return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES: {
            LOGI("env: SET_VARIABLES");
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            struct retro_variable *v = (struct retro_variable *)data;
            const char *val = lookup_var(v->key);
            if (val) {
                v->value = val;
                return true;
            }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *(bool *)data = false;
            return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE:
            return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            *(unsigned *)data = 0;
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
        default:
            LOGI("env: unhandled cmd=%u", cmd);
            return false;
    }
}

static void on_a(int16_t l, int16_t r) { ultraman_audio_sample_cb(l, r); }
static size_t on_ab(const int16_t *d, size_t f) { return ultraman_audio_batch_cb(d, f); }
static void on_ip() { ultraman_poll_input_cb(); }
static int16_t on_is(unsigned p, unsigned d, unsigned i, unsigned id) { return ultraman_input_state_cb(p, d, i, id); }

bool ultraman_load_game(const std::string &path) {
    LOGI("load_game: %s", path.c_str());
    s_system_dir = "/data/data/com.ultraman.fe0/files/system";

    ultraman_set_options(s_resolution, s_vsync, s_audio);

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
    LOGI("av: %ux%u @%.2f / %.0f",
         av.geometry.base_width, av.geometry.base_height,
         av.timing.fps, av.timing.sample_rate);

    ultraman_video_set_geometry(av.geometry.base_width, av.geometry.base_height, av.timing.fps);

    struct retro_game_info game{};
    game.path = path.c_str();
    game.data = nullptr;
    game.size = 0;
    game.meta = nullptr;

    if (!retro_load_game(&game)) {
        LOGE("retro_load_game FAILED");
        return false;
    }
    LOGI("retro_load_game ok");

    ultraman_video_start();
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
            if (f == 1 || f == 10 || f == 60 || f == 300) {
                LOGI("frame %d", f);
            }
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
