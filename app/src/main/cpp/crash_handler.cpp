#include <jni.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <ctime>

static char g_crash_path[512] = {0};
static int g_crash_fd = -1;

static void crash_signal_handler(int sig) {
    if (g_crash_fd < 0 && g_crash_path[0]) {
        g_crash_fd = open(g_crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    if (g_crash_fd >= 0) {
        char buf[256];
        int n = snprintf(buf, sizeof(buf),
            "=== NATIVE CRASH ===\nsignal=%d\ntime=%ld\n",
            sig, (long)time(nullptr));
        write(g_crash_fd, buf, n);
        fsync(g_crash_fd);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_signals() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ultraman_fe0_NativeCrashLogger_installNativeCrashHandler(
        JNIEnv *env, jclass, jstring jdir) {
    const char *dir = env->GetStringUTFChars(jdir, nullptr);
    snprintf(g_crash_path, sizeof(g_crash_path), "%s/crash_native.log", dir);
    env->ReleaseStringUTFChars(jdir, dir);
    install_signals();
}
