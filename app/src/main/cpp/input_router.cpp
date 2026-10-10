#include <atomic>
#include "libretro.h"
static std::atomic<int16_t> s_st[4][16];
void ultraman_poll_input_cb() {}
int16_t ultraman_input_state_cb(unsigned p, unsigned d, unsigned, unsigned id) {
    if (d != RETRO_DEVICE_JOYPAD) return 0;
    if (p >= 4 || id >= 16) return 0;
    return s_st[p][id].load();
}
void ultraman_push_key(int p, int id, bool down) {
    if (p < 0 || p >= 4 || id < 0 || id >= 16) return;
    s_st[p][id].store(down ? 1 : 0);
}
