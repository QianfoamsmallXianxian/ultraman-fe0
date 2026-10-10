#include <android/log.h>
#include <atomic>
#include <thread>
#include <string>
#include "libretro.h"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "UltramanFE0", __VA_ARGS__)
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
static std::thread s_thread;
static std::string s_dir;
static bool env_cb(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
            *(const char **)data = s_dir.c_str(); return true;
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS: return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES: return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE: return true;
        case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = 0; return true;
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: *(bool *)data = true; return true;
        case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS: *(unsigned *)data = 1; return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 1; return true;
        default: return false;
    }
}
static void on_a(int16_t l, int16_t r) { ultraman_audio_sample_cb(l, r); }
static size_t on_ab(const int16_t *d, size_t f) { return ultraman_audio_batch_cb(d, f); }
static void on_ip() { ultraman_poll_input_cb(); }
static int16_t on_is(unsigned p, unsigned d, unsigned i, unsigned id) { return ultraman_input_state_cb(p, d, i, id); }
bool ultraman_load_game(const std::string &path) {
    s_dir = "/data/data/com.ultraman.fe0/files/system";
    retro_set_environment(env_cb);
    retro_set_video_refresh(ultraman_video_cb);
    retro_set_audio_sample(on_a);
    retro_set_audio_sample_batch(on_ab);
    retro_set_input_poll(on_ip);
    retro_set_input_state(on_is);
    retro_init();
    struct retro_system_av_info av{};
    retro_get_system_av_info(&av);
    LOGI("av %ux%u @%.2f / %.0f", av.geometry.base_width, av.geometry.base_height, av.timing.fps, av.timing.sample_rate);
    ultraman_video_set_geometry(av.geometry.base_width, av.geometry.base_height, av.timing.fps);
    struct retro_game_info info{};
    info.path = path.c_str();
    info.data = nullptr;
    info.size = 0;
    info.meta = nullptr;
    retro_load_game(&info);
    ultraman_video_start();
    ultraman_audio_start(av.timing.sample_rate);
    return true;
}
void ultraman_start_loop() {
    if (s_run.exchange(true)) return;
    s_thread = std::thread([] { while (s_run.load()) retro_run(); });
}
void ultraman_stop_loop() {
    if (!s_run.exchange(false)) return;
    if (s_thread.joinable()) s_thread.join();
    retro_unload_game();
    retro_deinit();
    ultraman_video_stop();
    ultraman_audio_stop();
}
