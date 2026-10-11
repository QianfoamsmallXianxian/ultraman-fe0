#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/log.h>
#include <atomic>
#include <cstring>
#include "ulog.h"
#define LOGI(...) ulog_write(ANDROID_LOG_INFO, "UltramanFE0", __VA_ARGS__)

static SLObjectItf g_eng_obj = nullptr;
static SLEngineItf g_eng = nullptr;
static SLObjectItf g_out_obj = nullptr;
static SLObjectItf g_ply_obj = nullptr;
static SLPlayItf g_ply = nullptr;
static SLAndroidSimpleBufferQueueItf g_bq = nullptr;

#define MAX_BUF 8192
static int16_t g_buf[2][MAX_BUF];
static std::atomic<int> g_buf_samples{4096};
static std::atomic<int> g_idx{0};
static std::atomic<bool> g_on{true};

#define RING_FRAMES 16384
static int16_t g_ring[RING_FRAMES * 2];
static std::atomic<int> g_ring_head{0};
static std::atomic<int> g_ring_tail{0};

static void cb(SLAndroidSimpleBufferQueueItf, void *) {
    int i = g_idx.fetch_add(1) & 1;
    int buf_samples = g_buf_samples.load();
    int frames_needed = buf_samples / 2;
    if (frames_needed > MAX_BUF / 2) frames_needed = MAX_BUF / 2;

    int16_t* out = g_buf[i];
    int head = g_ring_head.load(std::memory_order_acquire);
    int tail = g_ring_tail.load(std::memory_order_acquire);
    int avail = (tail - head + RING_FRAMES) & (RING_FRAMES - 1);
    int take = avail < frames_needed ? avail : frames_needed;

    for (int k = 0; k < take; k++) {
        int idx = (head + k) & (RING_FRAMES - 1);
        out[k * 2]     = g_ring[idx * 2];
        out[k * 2 + 1] = g_ring[idx * 2 + 1];
    }
    if (take < frames_needed) {
        memset(out + take * 2, 0, sizeof(int16_t) * (frames_needed - take) * 2);
    }
    g_ring_head.store((head + take) & (RING_FRAMES - 1), std::memory_order_release);
    (*g_bq)->Enqueue(g_bq, out, sizeof(int16_t) * buf_samples);
}

void ultraman_set_audio_buffer(int level) {
    int size;
    if (level <= 0) size = 1024;
    else if (level == 1) size = 4096;
    else size = 8192;
    g_buf_samples.store(size);
    LOGI("audio buffer = %d samples", size);
}

void ultraman_audio_start(double sr) {
    LOGI("audio %.0f", sr);
    slCreateEngine(&g_eng_obj, 0, nullptr, 0, nullptr, nullptr);
    (*g_eng_obj)->Realize(g_eng_obj, SL_BOOLEAN_FALSE);
    (*g_eng_obj)->GetInterface(g_eng_obj, SL_IID_ENGINE, &g_eng);
    (*g_eng)->CreateOutputMix(g_eng, &g_out_obj, 0, nullptr, nullptr);
    (*g_out_obj)->Realize(g_out_obj, SL_BOOLEAN_FALSE);

    SLDataLocator_AndroidSimpleBufferQueue lb = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    SLDataFormat_PCM fmt = {
        SL_DATAFORMAT_PCM, 2, (SLuint32)(sr * 1000),
        SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
        SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource src = { &lb, &fmt };
    SLDataLocator_OutputMix lo = { SL_DATALOCATOR_OUTPUTMIX, g_out_obj };
    SLDataSink snk = { &lo, nullptr };
    const SLInterfaceID ids[1] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
    const SLboolean req[1] = { SL_BOOLEAN_TRUE };
    (*g_eng)->CreateAudioPlayer(g_eng, &g_ply_obj, &src, &snk, 1, ids, req);
    (*g_ply_obj)->Realize(g_ply_obj, SL_BOOLEAN_FALSE);
    (*g_ply_obj)->GetInterface(g_ply_obj, SL_IID_PLAY, &g_ply);
    (*g_ply_obj)->GetInterface(g_ply_obj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &g_bq);
    (*g_bq)->RegisterCallback(g_bq, cb, nullptr);
    (*g_ply)->SetPlayState(g_ply, SL_PLAYSTATE_PLAYING);
    (*g_bq)->Enqueue(g_bq, g_buf[0], sizeof(int16_t) * g_buf_samples.load());
}

void ultraman_audio_stop() {
    if (g_ply) (*g_ply)->SetPlayState(g_ply, SL_PLAYSTATE_STOPPED);
    if (g_ply_obj) (*g_ply_obj)->Destroy(g_ply_obj);
    if (g_out_obj) (*g_out_obj)->Destroy(g_out_obj);
    if (g_eng_obj) (*g_eng_obj)->Destroy(g_eng_obj);
    g_ply_obj = nullptr;
    g_out_obj = nullptr;
    g_eng_obj = nullptr;
}

static size_t audio_batch_impl(const int16_t* data, size_t frames) {
    if (!g_on.load() || !data || frames == 0) return frames;
    int head = g_ring_head.load(std::memory_order_acquire);
    int tail = g_ring_tail.load(std::memory_order_acquire);
    int space = (head - tail - 1 + RING_FRAMES) & (RING_FRAMES - 1);
    size_t written = 0;
    for (size_t i = 0; i < frames && written < (size_t)space; i++) {
        int idx = tail & (RING_FRAMES - 1);
        g_ring[idx * 2]     = data[i * 2];
        g_ring[idx * 2 + 1] = data[i * 2 + 1];
        tail = (tail + 1) & (RING_FRAMES - 1);
        written++;
    }
    g_ring_tail.store(tail, std::memory_order_release);
    return frames;
}

void ultraman_audio_sample_cb(int16_t l, int16_t r) {
    int16_t d[2] = { l, r };
    audio_batch_impl(d, 1);
}

size_t ultraman_audio_batch_cb(const int16_t* data, size_t frames) {
    return audio_batch_impl(data, frames);
}

void ultraman_set_audio_enabled(bool on) { g_on.store(on); }
