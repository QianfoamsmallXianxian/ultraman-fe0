#include <jni.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <execinfo.h>
#include <dlfcn.h>

static char g_crash_path[512] = {0};
static int  g_crash_fd = -1;

static void wstr(int fd, const char* s) {
    if (s && fd >= 0) write(fd, s, strlen(s));
}

static void whex(int fd, unsigned long v) {
    char b[32];
    int n = snprintf(b, sizeof(b), "0x%lx", v);
    if (n > 0) write(fd, b, n);
}

static void wdec(int fd, long v) {
    char b[32];
    int n = snprintf(b, sizeof(b), "%ld", v);
    if (n > 0) write(fd, b, n);
}

static void crash_handler(int sig, siginfo_t* info, void* ucontext) {
    if (g_crash_fd < 0 && g_crash_path[0]) {
        g_crash_fd = open(g_crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    int fd = g_crash_fd;
    if (fd < 0) fd = 2;

    wstr(fd, "=== NATIVE CRASH ===\n");
    wstr(fd, "signal="); wdec(fd, sig); wstr(fd, "\n");
    if (info) {
        wstr(fd, "fault_addr=");
        whex(fd, (unsigned long)info->si_addr);
        wstr(fd, "\ncode="); wdec(fd, info->si_code);
        wstr(fd, "\n");
    }
    wstr(fd, "time="); wdec(fd, (long)time(nullptr)); wstr(fd, "\n");
    wstr(fd, "---- backtrace ----\n");

    void* buffer[64];
    int count = backtrace(buffer, 64);
    for (int i = 0; i < count; i++) {
        char line[32];
        int n = snprintf(line, sizeof(line), "#%02d ", i);
        if (n > 0) write(fd, line, n);

        whex(fd, (unsigned long)buffer[i]);
        wstr(fd, " ");

        Dl_info dl;
        memset(&dl, 0, sizeof(dl));
        if (dladdr(buffer[i], &dl) && dl.dli_fname) {
            wstr(fd, dl.dli_fname);
            wstr(fd, " (");
            wstr(fd, dl.dli_sname ? dl.dli_sname : "?");
            wstr(fd, ")");
        } else {
            wstr(fd, "?");
        }
        wstr(fd, "\n");
    }
    wstr(fd, "---- end ----\n");

    fsync(fd);

    signal(sig, SIG_DFL);
    raise(sig);
}

void install_native_crash_handler(const char* path) {
    if (path) snprintf(g_crash_path, sizeof(g_crash_path), "%s", path);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGTRAP, &sa, nullptr);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ultraman_fe0_NativeCrashLogger_installNativeCrashHandler(
        JNIEnv* env, jclass, jstring jdir) {
    const char* dir = env->GetStringUTFChars(jdir, nullptr);
    char path[512];
    snprintf(path, sizeof(path), "%s/crash_native.log", dir);
    install_native_crash_handler(path);
    env->ReleaseStringUTFChars(jdir, dir);
}
