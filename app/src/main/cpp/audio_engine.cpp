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
static constexpr int BUF = 4096;
static int16_t g_buf[2][BUF];
static std::atomic<int> g_idx{0};
static std::atomic<bool> g_on{true};
static void cb(SLAndroidSimpleBufferQueueItf, void *) {
    int i = g_idx.fetch_add(1) & 1;
    memset(g_buf[i], 0, sizeof(int16_t) * BUF);
    (*g_bq)->Enqueue(g_bq, g_buf[i], sizeof(int16_t) * BUF);
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
    (*g_bq)->Enqueue(g_bq, g_buf[0], sizeof(int16_t) * BUF);
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
void ultraman_audio_sample_cb(int16_t, int16_t) {}
size_t ultraman_audio_batch_cb(const int16_t *, size_t f) { return f; }
void ultraman_set_audio_enabled(bool on) { g_on.store(on); }
