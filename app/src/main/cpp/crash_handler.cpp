#include <jni.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <dlfcn.h>
#include <unwind.h>
#include <ucontext.h>

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

static void dump_frame(int fd, int idx, uintptr_t pc) {
    char line[32];
    int n = snprintf(line, sizeof(line), "#%02d ", idx);
    if (n > 0) write(fd, line, n);
    whex(fd, (unsigned long)pc);
    wstr(fd, " ");

    Dl_info dl;
    memset(&dl, 0, sizeof(dl));
    if (dladdr((void*)pc, &dl) && dl.dli_fname) {
        wstr(fd, dl.dli_fname);
        wstr(fd, " (");
        wstr(fd, dl.dli_sname ? dl.dli_sname : "?");
        wstr(fd, ")");
        if (dl.dli_fbase) {
            uintptr_t off = pc - (uintptr_t)dl.dli_fbase;
            wstr(fd, " +0x");
            char ob[32];
            int on = snprintf(ob, sizeof(ob), "%lx", (unsigned long)off);
            if (on > 0) write(fd, ob, on);
        }
    } else {
        wstr(fd, "?");
    }
    wstr(fd, "\n");
}

struct BtState {
    int fd;
    int idx;
    int max;
};

static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) {
    BtState* st = (BtState*)arg;
    if (st->idx >= st->max) return _URC_END_OF_STACK;
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc == 0) return _URC_NO_REASON;
    dump_frame(st->fd, st->idx, pc);
    st->idx++;
    return _URC_NO_REASON;
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
        wstr(fd, "\ncode="); wdec(fd, info->si_code); wstr(fd, "\n");
    }
    wstr(fd, "time="); wdec(fd, (long)time(nullptr)); wstr(fd, "\n");

    if (ucontext) {
        ucontext_t* uc = (ucontext_t*)ucontext;
        uintptr_t pc = (uintptr_t)uc->uc_mcontext.pc;
        wstr(fd, "---- crash PC ----\n");
        wstr(fd, "PC=");
        whex(fd, (unsigned long)pc);
        wstr(fd, "\n");
        dump_frame(fd, 0, pc);

#if defined(__aarch64__)
        uintptr_t lr = (uintptr_t)uc->uc_mcontext.regs[30];
        uintptr_t fp = (uintptr_t)uc->uc_mcontext.regs[29];
        uintptr_t sp = (uintptr_t)uc->uc_mcontext.sp;
        wstr(fd, "LR="); whex(fd, (unsigned long)lr); wstr(fd, "\n");
        wstr(fd, "FP="); whex(fd, (unsigned long)fp); wstr(fd, "\n");
        wstr(fd, "SP="); whex(fd, (unsigned long)sp); wstr(fd, "\n");
        wstr(fd, "---- LR ----\n");
        dump_frame(fd, 1, lr);
#endif
    }

    wstr(fd, "---- unwind stack ----\n");
    BtState st;
    st.fd = fd;
    st.idx = 2;
    st.max = 64;
    _Unwind_Backtrace(unwind_cb, &st);

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
Java_com_pssp_NativeCrashLogger_installNativeCrashHandler(
        JNIEnv* env, jclass, jstring jdir) {
    const char* dir = env->GetStringUTFChars(jdir, nullptr);
    char path[512];
    snprintf(path, sizeof(path), "%s/crash_native.log", dir);
    install_native_crash_handler(path);
    env->ReleaseStringUTFChars(jdir, dir);
}
