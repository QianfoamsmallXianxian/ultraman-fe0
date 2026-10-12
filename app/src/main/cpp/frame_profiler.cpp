#include "frame_profiler.h"
#include <android/log.h>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <chrono>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "UltramanFE0", __VA_ARGS__)

static const char* kStageNames[] = {
    "retro_run", "present", "acquire", "submit", "present_kh", "blit_draw", "barrier", "swap"
};

const char* stage_name(int idx) {
    if (idx < 0 || idx >= STAGE_COUNT) return "?";
    return kStageNames[idx];
}

static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

FrameProfiler& FrameProfiler::get() {
    static FrameProfiler inst;
    return inst;
}

FrameProfiler::~FrameProfiler() { shutdown(); }

void FrameProfiler::init(const char* dir) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (fp_) return;
    snprintf(dir_, sizeof(dir_), "%s", dir ? dir : "/storage/emulated/0/Android/data/com.pssp/files/logs");
    mkdir(dir_, 0777);
    char path[600];
    snprintf(path, sizeof(path), "%s/perf.log", dir_);
    fp_ = fopen(path, "a");
    if (fp_) {
        time_t t = time(nullptr);
        struct tm tm;
        localtime_r(&t, &tm);
        char tb[64];
        strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S", &tm);
        fprintf(fp_, "\n=== perf session start %s ===\n", tb);
        fflush(fp_);
        LOGI("profiler: %s", path);
    }
}

void FrameProfiler::shutdown() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (fp_) { fclose(fp_); fp_ = nullptr; }
}

void FrameProfiler::write_line(const std::string& s) {
    if (fp_) { fputs(s.c_str(), fp_); fflush(fp_); }
}

void FrameProfiler::frame_begin(int frame_no) {
    frame_no_ = frame_no;
    frame_begin_ns_ = now_ns();
    for (int i = 0; i < STAGE_COUNT; i++) stage_total_ns_[i] = 0;
}

void FrameProfiler::mark_begin(int stage) {
    if (stage < 0 || stage >= STAGE_COUNT) return;
    stage_begin_ns_[stage] = now_ns();
}

void FrameProfiler::mark_end(int stage) {
    if (stage < 0 || stage >= STAGE_COUNT) return;
    if (stage_begin_ns_[stage] == 0) return;
    stage_total_ns_[stage] += now_ns() - stage_begin_ns_[stage];
    stage_begin_ns_[stage] = 0;
}

void FrameProfiler::frame_end() {
    std::lock_guard<std::mutex> lk(mtx_);
    int64_t total = now_ns() - frame_begin_ns_;
    double ms = total / 1e6;

    FrameRec rec{};
    rec.no = frame_no_;
    rec.total_ns = total;
    for (int i = 0; i < STAGE_COUNT; i++) rec.stages[i] = stage_total_ns_[i];
    rec.dropped = (ms > 17.0) ? 1 : 0;

    recent_.push_back(rec);
    if ((int)recent_.size() > RECENT_MAX) recent_.erase(recent_.begin());

    static int64_t bucket_last = 0;
    static int bucket_count = 0;
    static double bucket_total = 0;
    static double bucket_worst = 0;

    bucket_count++;
    bucket_total += ms;
    if (ms > bucket_worst) bucket_worst = ms;

    if (frame_no_ % 60 == 0) {
        int64_t now = now_ns();
        if (bucket_last == 0) bucket_last = now;
        double sec = (now - bucket_last) / 1e9;
        double avg_fps = bucket_count / (sec > 0.001 ? sec : 0.001);
        double avg_ms = bucket_total / (bucket_count ? bucket_count : 1);

        char buf[256];
        snprintf(buf, sizeof(buf),
            "[F60] fps=%.1f avg=%.2fms worst=%.2fms surface=%dx%d fbo=%dx%d\n",
            avg_fps, avg_ms, bucket_worst,
            last_surface_w_, last_surface_h_,
            last_fbo_w_, last_fbo_h_);
        write_line(buf);

        bucket_last = now;
        bucket_count = 0;
        bucket_total = 0;
        bucket_worst = 0;
    }

    if (rec.dropped) {
        char buf[512];
        int n = snprintf(buf, sizeof(buf),
            "[DROP] f=%d total=%.2fms ", rec.no, ms);
        for (int i = 0; i < STAGE_COUNT; i++) {
            if (rec.stages[i] > 0) {
                n += snprintf(buf + n, sizeof(buf) - n, "%s=%.2f ",
                              kStageNames[i], rec.stages[i] / 1e6);
            }
        }
        n += snprintf(buf + n, sizeof(buf) - n, "\n");
        write_line(std::string(buf));
    }
}

void FrameProfiler::note_vulkan_black(const char* reason) {
    std::lock_guard<std::mutex> lk(mtx_);
    char buf[256];
    snprintf(buf, sizeof(buf), "[VK_BLACK] f=%d reason=%s\n", frame_no_, reason ? reason : "?");
    write_line(std::string(buf));
}

void FrameProfiler::note_vulkan_error(const char* reason) {
    std::lock_guard<std::mutex> lk(mtx_);
    char buf[256];
    snprintf(buf, sizeof(buf), "[VK_ERR] f=%d reason=%s\n", frame_no_, reason ? reason : "?");
    write_line(std::string(buf));
}

void FrameProfiler::note_gl_error(const char* reason) {
    std::lock_guard<std::mutex> lk(mtx_);
    char buf[256];
    snprintf(buf, sizeof(buf), "[GL_ERR] f=%d reason=%s\n", frame_no_, reason ? reason : "?");
    write_line(std::string(buf));
}

void FrameProfiler::note_frameskip(int skipped) {
    std::lock_guard<std::mutex> lk(mtx_);
    char buf[128];
    snprintf(buf, sizeof(buf), "[SKIP] f=%d skipped=%d\n", frame_no_, skipped);
    write_line(std::string(buf));
}

void FrameProfiler::note_surface(int w, int h) {
    std::lock_guard<std::mutex> lk(mtx_);
    last_surface_w_ = w;
    last_surface_h_ = h;
    char buf[128];
    snprintf(buf, sizeof(buf), "[SURF] f=%d %dx%d\n", frame_no_, w, h);
    write_line(std::string(buf));
}

void FrameProfiler::note_fbo(int w, int h) {
    std::lock_guard<std::mutex> lk(mtx_);
    last_fbo_w_ = w;
    last_fbo_h_ = h;
    char buf[128];
    snprintf(buf, sizeof(buf), "[FBO] f=%d %dx%d\n", frame_no_, w, h);
    write_line(std::string(buf));
}

void FrameProfiler::dump_recent(const char* tag) {
    std::lock_guard<std::mutex> lk(mtx_);
    char buf[128];
    snprintf(buf, sizeof(buf), "=== DUMP(%s) recent %zu frames ===\n",
             tag ? tag : "?", recent_.size());
    write_line(std::string(buf));
    for (auto& r : recent_) {
        int n = snprintf(buf, sizeof(buf), "  f=%d total=%.2fms ", r.no, r.total_ns / 1e6);
        std::string line(buf, n);
        for (int i = 0; i < STAGE_COUNT; i++) {
            if (r.stages[i] > 0) {
                char sb[64];
                int sn = snprintf(sb, sizeof(sb), "%s=%.2f ", kStageNames[i], r.stages[i] / 1e6);
                line.append(sb, sn);
            }
        }
        line += "\n";
        write_line(line);
    }
    write_line("=== DUMP END ===\n");
}
