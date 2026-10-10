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
extern void ultraman_push_key(int, int, bool);

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *, void *) {
    ulog_init("/storage/emulated/0/Android/data/com.ultraman.fe0/files/logs");
    ulog_init("/data/data/com.ultraman.fe0/files/logs");
    ulog_init("/sdcard/ultraman-fe0-logs");
    ulog_write(ANDROID_LOG_INFO, "UltramanFE0", "=== JNI_OnLoad ===");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeInit(JNIEnv *, jclass, jobject) {
    ulog_write(ANDROID_LOG_INFO, "UltramanFE0", "nativeInit");
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeLoadGame(JNIEnv *env, jclass, jstring jp) {
    const char *p = env->GetStringUTFChars(jp, nullptr);
    ulog_write(ANDROID_LOG_INFO, "UltramanFE0", "nativeLoadGame: %s", p);
    bool ok = ultraman_load_game(p);
    env->ReleaseStringUTFChars(jp, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeStart(JNIEnv *, jclass) { ultraman_start_loop(); }
extern "C" JNIEXPORT void JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeStop(JNIEnv *, jclass) { ultraman_stop_loop(); }
extern "C" JNIEXPORT void JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeSetSurface(JNIEnv *env, jclass, jobject s) { ultraman_set_surface(env, s); }
extern "C" JNIEXPORT void JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeSetAudioEnabled(JNIEnv *, jclass, jboolean on) { ultraman_set_audio_enabled(on == JNI_TRUE); }
extern "C" JNIEXPORT void JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeKey(JNIEnv *, jclass, jint p, jint id, jboolean d) { ultraman_push_key(p, id, d == JNI_TRUE); }
extern "C" JNIEXPORT void JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeReset(JNIEnv *, jclass) { retro_reset(); }
extern "C" JNIEXPORT jstring JNICALL Java_com_ultraman_fe0_EmulatorBridge_nativeVersion(JNIEnv *env, jclass) {
    return env->NewStringUTF("ppsspp");
}
