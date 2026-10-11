#ifndef FRAME_PROFILER_H
#define FRAME_PROFILER_H

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

enum PerfStage {
    STAGE_RETRO_RUN = 0,
    STAGE_PRESENT,
    STAGE_ACQUIRE,
    STAGE_SUBMIT,
    STAGE_PRESENT_KH,
    STAGE_BLIT_OR_DRAW,
    STAGE_BARRIER,
    STAGE_SWAP,
    STAGE_COUNT
};

const char* stage_name(int idx);

class FrameProfiler {
public:
    static FrameProfiler& get();

    void init(const char* dir);
    void shutdown();

    void frame_begin(int frame_no);
    void frame_end();

    void mark_begin(int stage);
    void mark_end(int stage);

    void note_vulkan_black(const char* reason);
    void note_vulkan_error(const char* reason);
    void note_gl_error(const char* reason);
    void note_frameskip(int skipped);
    void note_surface(int w, int h);
    void note_fbo(int w, int h);

    void dump_recent(const char* tag);

private:
    FrameProfiler() = default;
    ~FrameProfiler();
    FrameProfiler(const FrameProfiler&) = delete;
    FrameProfiler& operator=(const FrameProfiler&) = delete;

    void write_line(const std::string& s);

    std::mutex mtx_;
    FILE* fp_ = nullptr;
    char dir_[512] = {0};

    int64_t frame_begin_ns_ = 0;
    int64_t stage_begin_ns_[STAGE_COUNT] = {0};
    int64_t stage_total_ns_[STAGE_COUNT] = {0};
    int frame_no_ = 0;
    int last_surface_w_ = 0;
    int last_surface_h_ = 0;
    int last_fbo_w_ = 0;
    int last_fbo_h_ = 0;

    struct FrameRec {
        int no;
        int64_t total_ns;
        int64_t stages[STAGE_COUNT];
        int dropped;
    };
    std::vector<FrameRec> recent_;
    static constexpr int RECENT_MAX = 300;
};

#define PROF_FRAME_BEGIN(n) FrameProfiler::get().frame_begin(n)
#define PROF_FRAME_END()    FrameProfiler::get().frame_end()
#define PROF_MARK_BEGIN(s)  FrameProfiler::get().mark_begin(s)
#define PROF_MARK_END(s)    FrameProfiler::get().mark_end(s)

#endif
