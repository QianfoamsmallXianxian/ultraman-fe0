#include <jni.h>
#include <android/log.h>
#include <string>
#include "libretro.h"
#include "ulog.h"

extern bool ultraman_load_game(const std::string &path);
extern void ultraman_start_loop();
extern void ultraman_stop_loop();
extern void ultraman_set_surface(JNIEnv *, jobject);
extern void ultraman_set_audio_enabled(bool);
extern void ultraman_push_analog(int, int, int, int);
extern void ultraman_push_key(int, int, bool);
extern void ultraman_set_options(int, bool, bool);
extern void ultraman_set_backend(int);
extern bool ultraman_save_state(const char *);
extern bool ultraman_load_state(const char *);
extern bool ultraman_save_sram(const char *);
extern bool ultraman_load_sram(const char *);

#include <dlfcn.h>

static void preload_gl_libs() {
    const char* libs[] = {
        "libGLESv2.so",
        "libGLESv3.so",
        "libGLESv1_CM.so",
        "libEGL.so",
        "/vendor/lib64/egl/libGLESv2_adreno.so",
        "/vendor/lib/egl/libGLESv2_adreno.so",
        "/system/lib64/egl/libGLESv2_adreno.so",
        "/system/lib/egl/libGLESv2_adreno.so",
        "/odm/lib64/egl/libGLESv2_adreno.so",
        "/odm/lib/egl/libGLESv2_adreno.so",
        "/vendor/lib64/libGLESv2_adreno.so",
        "/system/lib64/libGLESv2_adreno.so",
        "/vendor/lib64/egl/libGLESv2_qualcomm.so",
        "/vendor/lib64/libGLESv2_qualcomm.so",
        nullptr
    };
    for (int i = 0; libs[i]; i++) {
        void* h = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL);
        ulog_write(ANDROID_LOG_INFO, "UltramanFE0",
            "preload %s -> %p", libs[i], h);
    }
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *, void *) {
    ulog_init("/storage/emulated/0/Android/data/com.pssp/files/logs");
    preload_gl_libs();
    ulog_init("/data/data/com.pssp/files/logs");
    ulog_write(ANDROID_LOG_INFO, "UltramanFE0", "=== JNI_OnLoad ===");
    return JNI_VERSION_1_6;
}
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeInit(JNIEnv *, jclass, jobject) {
    ulog_write(ANDROID_LOG_INFO, "UltramanFE0", "nativeInit");
}
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeSetBackend(JNIEnv *, jclass, jint b) {
    ultraman_set_backend(b);
}
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeSetOptions(JNIEnv *, jclass, jint r, jboolean v, jboolean a) {
    ultraman_set_options(r, v == JNI_TRUE, a == JNI_TRUE);
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_pssp_EmulatorBridge_nativeLoadGame(JNIEnv *env, jclass, jstring jp) {
    const char *p = env->GetStringUTFChars(jp, nullptr);
    ulog_write(ANDROID_LOG_INFO, "UltramanFE0", "nativeLoadGame: %s", p);
    bool ok = ultraman_load_game(p);
    env->ReleaseStringUTFChars(jp, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeStart(JNIEnv *, jclass) { ultraman_start_loop(); }
extern void ultraman_pause_render();
extern void ultraman_resume_render();
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativePause(JNIEnv *, jclass) {
    ultraman_pause_render();
}
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeResume(JNIEnv *, jclass) {
    { FILE* f = fopen("/storage/emulated/0/Android/data/com.pssp/files/native_dbg.log","a"); if(f){ fprintf(f,"JNI nativeResume called\n"); fclose(f);} }
    ultraman_resume_render();
}
extern int64_t ultraman_get_frame_count();
extern void ultraman_set_audio_buffer(int level);
extern "C" double ultraman_get_core_fps();
extern "C" JNIEXPORT jlong JNICALL Java_com_pssp_EmulatorBridge_nativeGetFrameCount(JNIEnv *, jclass) {
    return (jlong)ultraman_get_frame_count();
}

extern "C" JNIEXPORT jdouble JNICALL Java_com_pssp_EmulatorBridge_nativeGetCoreFps(JNIEnv *, jclass) {
    return ultraman_get_core_fps();
}
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeSetAudioBuffer(JNIEnv *, jclass, jint level) {
    ultraman_set_audio_buffer(level);
}
extern void ultraman_reset_context();
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeResetContext(JNIEnv *, jclass) {
    ultraman_reset_context();
}
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeStop(JNIEnv *, jclass) { ultraman_stop_loop(); }
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeSetSurface(JNIEnv *env, jclass, jobject s) { ultraman_set_surface(env, s); }
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeSetAudioEnabled(JNIEnv *, jclass, jboolean on) { ultraman_set_audio_enabled(on == JNI_TRUE); }
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeKey(JNIEnv *, jclass, jint p, jint id, jboolean d) { ultraman_push_key(p, id, d == JNI_TRUE); }

extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeAnalog(JNIEnv *, jclass, jint x, jint y) { ultraman_push_analog(0, 0, x, y); }
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeAnalogRight(JNIEnv *, jclass, jint x, jint y) { ultraman_push_analog(0, 1, x, y); }
extern "C" JNIEXPORT void JNICALL Java_com_pssp_EmulatorBridge_nativeReset(JNIEnv *, jclass) { retro_reset(); }
extern "C" JNIEXPORT jstring JNICALL Java_com_pssp_EmulatorBridge_nativeVersion(JNIEnv *env, jclass) {
    return env->NewStringUTF("ppsspp");
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_pssp_EmulatorBridge_nativeSaveState(JNIEnv *env, jclass, jstring jp) {
    const char *p = env->GetStringUTFChars(jp, nullptr);
    bool ok = ultraman_save_state(p);
    env->ReleaseStringUTFChars(jp, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_pssp_EmulatorBridge_nativeLoadState(JNIEnv *env, jclass, jstring jp) {
    const char *p = env->GetStringUTFChars(jp, nullptr);
    bool ok = ultraman_load_state(p);
    env->ReleaseStringUTFChars(jp, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_pssp_EmulatorBridge_nativeSaveSram(JNIEnv *env, jclass, jstring jp) {
    const char *p = env->GetStringUTFChars(jp, nullptr);
    bool ok = ultraman_save_sram(p);
    env->ReleaseStringUTFChars(jp, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_pssp_EmulatorBridge_nativeLoadSram(JNIEnv *env, jclass, jstring jp) {
    const char *p = env->GetStringUTFChars(jp, nullptr);
    bool ok = ultraman_load_sram(p);
    env->ReleaseStringUTFChars(jp, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}
