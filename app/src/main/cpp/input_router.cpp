#include <atomic>
#include "libretro.h"

static std::atomic<int16_t> s_st[4][16];
static std::atomic<int16_t> s_ax[4];
static std::atomic<int16_t> s_ay[4];

void ultraman_poll_input_cb() {}

int16_t ultraman_input_state_cb(unsigned p, unsigned d, unsigned i, unsigned id) {
    if (p >= 4) return 0;
    if (d == RETRO_DEVICE_JOYPAD) {
        if (id >= 16) return 0;
        return s_st[p][id].load();
    }
    if (d == RETRO_DEVICE_ANALOG) {
        if (i != RETRO_DEVICE_INDEX_ANALOG_LEFT) return 0;
        if (id == RETRO_DEVICE_ID_ANALOG_X) return s_ax[p].load();
        if (id == RETRO_DEVICE_ID_ANALOG_Y) return s_ay[p].load();
    }
    return 0;
}

void ultraman_push_key(int p, int id, bool down) {
    if (p < 0 || p >= 4 || id < 0 || id >= 16) return;
    s_st[p][id].store(down ? 1 : 0);
}

void ultraman_push_analog(int p, int x, int y) {
    if (p < 0 || p >= 4) return;
    s_ax[p].store((int16_t)x);
    s_ay[p].store((int16_t)y);
}
