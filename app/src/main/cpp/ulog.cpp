#include "ulog.h"
#include <android/log.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/stat.h>
static FILE *g_file = nullptr;
static std::mutex g_mtx;
void ulog_init(const char *dir) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_file) return;
    mkdir(dir, 0777);
    char path[512];
    snprintf(path, sizeof(path), "%s/native.log", dir);
    g_file = fopen(path, "a");
    if (!g_file) {
        __android_log_print(ANDROID_LOG_ERROR, "UltramanFE0", "ulog open fail: %s", path);
    }
}
void ulog_write(int prio, const char *tag, const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(prio, tag, "%s", buf);
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_file) {
        time_t now = time(nullptr);
        struct tm tm;
        localtime_r(&now, &tm);
        char t[32];
        strftime(t, sizeof(t), "%H:%M:%S", &tm);
        fprintf(g_file, "[%s] %s\n", t, buf);
        fflush(g_file);
    }
}
