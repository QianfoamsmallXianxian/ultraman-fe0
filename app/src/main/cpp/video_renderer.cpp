#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <mutex>
#include <map>
#include <string>
#include <atomic>
#include <cstring>
#include "ulog.h"
#include "libretro.h"

#define LOGI(...) ulog_write(ANDROID_LOG_INFO,  "UltramanFE0", __VA_ARGS__)
#define LOGE(...) ulog_write(ANDROID_LOG_ERROR, "UltramanFE0", __VA_ARGS__)
#define LOGW(...) ulog_write(ANDROID_LOG_WARN,  "UltramanFE0", __VA_ARGS__)

typedef PFNGLGENFRAMEBUFFERSPROC       PFN_GLGENFB;
typedef PFNGLBINDFRAMEBUFFERPROC       PFN_GLBINDFB;
typedef PFNGLDELETEFRAMEBUFFERSPROC    PFN_GLDELFB;
typedef PFNGLFRAMEBUFFERTEXTURE2DPROC  PFN_GLFBTEX;
typedef PFNGLFRAMEBUFFERRENDERBUFFERPROC PFN_GLFBRBO;
typedef PFNGLCHECKFRAMEBUFFERSTATUSPROC PFN_GLFBSTATUS;
typedef PFNGLGENRENDERBUFFERSPROC      PFN_GLGENRBO;
typedef PFNGLRENDERBUFFERSTORAGEPROC   PFN_GLRBOSTORE;
typedef PFNGLBLITFRAMEBUFFERPROC       PFN_GLBLIT;

static void* s_pfn_glGenFramebuffers        = nullptr;
static void* s_pfn_glBindFramebuffer        = nullptr;
static void* s_pfn_glDeleteFramebuffers     = nullptr;
static void* s_pfn_glFramebufferTexture2D   = nullptr;
static void* s_pfn_glFramebufferRenderbuffer= nullptr;
static void* s_pfn_glCheckFramebufferStatus = nullptr;
static void* s_pfn_glGenRenderbuffers       = nullptr;
static void* s_pfn_glRenderbufferStorage    = nullptr;
static void* s_pfn_glBlitFramebuffer        = nullptr;

#define GL_GEN_FB       ((PFN_GLGENFB)s_pfn_glGenFramebuffers)
#define GL_BIND_FB      ((PFN_GLBINDFB)s_pfn_glBindFramebuffer)
#define GL_DEL_FB       ((PFN_GLDELFB)s_pfn_glDeleteFramebuffers)
#define GL_FB_TEX2D     ((PFN_GLFBTEX)s_pfn_glFramebufferTexture2D)
#define GL_FB_RBO       ((PFN_GLFBRBO)s_pfn_glFramebufferRenderbuffer)
#define GL_FB_STATUS    ((PFN_GLFBSTATUS)s_pfn_glCheckFramebufferStatus)
#define GL_GEN_RBO      ((PFN_GLGENRBO)s_pfn_glGenRenderbuffers)
#define GL_RBO_STORAGE  ((PFN_GLRBOSTORE)s_pfn_glRenderbufferStorage)
#define GL_BLIT_FB      ((PFN_GLBLIT)s_pfn_glBlitFramebuffer)

static void load_gl_funcs() {
    s_pfn_glGenFramebuffers         = (void*)eglGetProcAddress("glGenFramebuffers");
    s_pfn_glBindFramebuffer         = (void*)eglGetProcAddress("glBindFramebuffer");
    s_pfn_glDeleteFramebuffers      = (void*)eglGetProcAddress("glDeleteFramebuffers");
    s_pfn_glFramebufferTexture2D    = (void*)eglGetProcAddress("glFramebufferTexture2D");
    s_pfn_glFramebufferRenderbuffer = (void*)eglGetProcAddress("glFramebufferRenderbuffer");
    s_pfn_glCheckFramebufferStatus  = (void*)eglGetProcAddress("glCheckFramebufferStatus");
    s_pfn_glGenRenderbuffers        = (void*)eglGetProcAddress("glGenRenderbuffers");
    s_pfn_glRenderbufferStorage     = (void*)eglGetProcAddress("glRenderbufferStorage");
    s_pfn_glBlitFramebuffer         = (void*)eglGetProcAddress("glBlitFramebuffer");
    LOGI("load_gl_funcs: blit=%p bindfb=%p genfb=%p",
         s_pfn_glBlitFramebuffer, s_pfn_glBindFramebuffer, s_pfn_glGenFramebuffers);
}


extern void ultraman_on_video_ready();
#define GLLOCK std::lock_guard<std::recursive_mutex> _lk(g_gl_mtx)


static ANativeWindow* g_win = nullptr;
static EGLDisplay     g_dpy = EGL_NO_DISPLAY;
static EGLSurface     g_sfc = EGL_NO_SURFACE;
static EGLContext     g_ctx = EGL_NO_CONTEXT;
static EGLConfig      g_cfg = nullptr;
static std::recursive_mutex g_gl_mtx;
static std::atomic<bool> g_pending_rebuild{false};
static int g_pending_w = 0;
static int g_pending_h = 0;
static std::atomic<bool> g_video_ready{false};
static std::atomic<bool> g_hw_mode{false};
static std::atomic<bool> g_hw_ready{false};
static std::atomic<bool> g_surface_ok{false};
static int g_win_w = 0;
static int g_win_h = 0;
static EGLSurface g_current_sfc = EGL_NO_SURFACE;

static GLuint g_sw_tex   = 0;
static GLuint g_sw_prog  = 0;
static GLuint g_sw_vbo   = 0;
static GLint  g_sw_pos   = -1;
static GLint  g_sw_uv    = -1;

static GLuint g_hw_fbo   = 0;
static GLuint g_hw_tex   = 0;
static GLuint g_hw_depth = 0;
static int    g_hw_w     = 960;
static int    g_hw_h     = 544;
static int    g_view_w   = 0;
static int    g_view_h   = 0;
static std::atomic<int> g_sw_frames{0};
static std::atomic<int> g_hw_frames{0};
static void*  g_gles_lib = nullptr;

static const char* SW_VS =
    "#version 300 es\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main(){v_uv=a_uv;gl_Position=vec4(a_pos,0.0,1.0);}\n";

static const char* SW_FS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 o;\n"
    "void main(){o=texture(u_tex,v_uv);}\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLchar log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        LOGE("shader: %s", log);
    }
    return s;
}

static void sw_setup() {
    GLuint v = compile_shader(GL_VERTEX_SHADER, SW_VS);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, SW_FS);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLchar log[512];
        glGetProgramInfoLog(p, 512, nullptr, log);
        LOGE("link: %s", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    g_sw_prog = p;
    g_sw_pos = glGetAttribLocation(p, "a_pos");
    g_sw_uv  = glGetAttribLocation(p, "a_uv");

    float verts[] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f
    };
    glGenBuffers(1, &g_sw_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_sw_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenTextures(1, &g_sw_tex);
    glBindTexture(GL_TEXTURE_2D, g_sw_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    LOGI("sw setup done");
}

static void clear_gl_errors() {
    GLenum e;
    int c = 0;
    while ((e = glGetError()) != GL_NO_ERROR && c < 16) {
        LOGW("clear_gl_error 0x%x", e);
        c++;
    }
}

static bool ensure_current() {
    GLLOCK;
    if (eglGetCurrentContext() == g_ctx) return true;
    if (g_dpy == EGL_NO_DISPLAY || g_sfc == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT) return false;
    if (!eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
        LOGE("ensure_current fail 0x%x", eglGetError());
        g_current_sfc = EGL_NO_SURFACE;
        return false;
    }
    g_current_sfc = g_sfc;
    clear_gl_errors();
    return true;
}

static void reload_gl_after_egl() {
    const char* libs[] = {
        "libGLESv2.so",
        "libGLESv3.so",
        "libEGL.so",
        "libGLESv2_adreno.so",
        "/vendor/lib64/egl/libGLESv2_adreno.so",
        "/system/lib64/egl/libGLESv2_adreno.so",
        nullptr
    };
    for (int i = 0; libs[i]; i++) {
        void* h = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
        if (!h) h = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL);
        LOGI("reload_gl %s -> %p", libs[i], h);
    }
}

static void gl_state_reset() {
    GL_BIND_FB(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glViewport(0, 0, 1, 1);
    glClearColor(0.f, 0.f, 0.f, 1.f);
}

static void hw_destroy_fbo() {
    if (g_hw_fbo)   { GL_DEL_FB(1, &g_hw_fbo);    g_hw_fbo = 0; }
    if (g_hw_tex)   { glDeleteTextures(1, &g_hw_tex);        g_hw_tex = 0; }
    if (g_hw_depth) { glDeleteRenderbuffers(1, &g_hw_depth); g_hw_depth = 0; }
}

static bool hw_create_fbo(int w, int h) {
    if (w <= 0 || h <= 0) { LOGE("hw_create_fbo: bad size %dx%d", w, h); return false; }
    g_hw_w = w;
    g_hw_h = h;

    glGenTextures(1, &g_hw_tex);
    glBindTexture(GL_TEXTURE_2D, g_hw_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GL_GEN_RBO(1, &g_hw_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_hw_depth);
    GL_RBO_STORAGE(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

    GL_GEN_FB(1, &g_hw_fbo);
    GL_BIND_FB(GL_FRAMEBUFFER, g_hw_fbo);
    GL_FB_TEX2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_hw_tex, 0);
    GL_FB_RBO(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, g_hw_depth);

    GLenum st = GL_FB_STATUS(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("FBO incomplete: 0x%x", st);
        hw_destroy_fbo();
        return false;
    }
    LOGI("FBO %u %dx%d (tex=%u depth=%u)", g_hw_fbo, w, h, g_hw_tex, g_hw_depth);
    return true;
}

static void hw_context_reset() {
    GLLOCK;
    LOGI(">>> context_reset ENTER");
    if (g_dpy == EGL_NO_DISPLAY || g_ctx == EGL_NO_CONTEXT || g_sfc == EGL_NO_SURFACE) {
        LOGE("context_reset: EGL not ready");
        return;
    }
    if (!eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
        LOGE("context_reset: eglMakeCurrent fail 0x%x", eglGetError());
        return;
    }
    g_current_sfc = g_sfc;
    g_hw_ready.store(false);
    if (g_hw_fbo) {
        LOGI("context_reset: destroying existing FBO %u", g_hw_fbo);
        hw_destroy_fbo();
    }
    if (!hw_create_fbo(g_hw_w, g_hw_h)) {
        LOGE("context_reset: create FBO failed");
        gl_state_reset();
        return;
    }
    g_hw_ready.store(true);
    gl_state_reset();
    LOGI(">>> context_reset EXIT fbo=%u %dx%d", g_hw_fbo, g_hw_w, g_hw_h);
}

static void hw_context_destroy() {
    GLLOCK;
    LOGI("context_destroy");
    if (g_hw_fbo) hw_destroy_fbo();
    g_hw_ready.store(false);
}

static uintptr_t hw_get_current_fbo() {
    return (uintptr_t)g_hw_fbo;
}

static std::atomic<int> s_proc_count{0};
static std::mutex g_proc_cache_mtx;
static std::map<std::string, void*> g_proc_cache;


static void* resolve_gl_proc(const char* sym) {
    if (!sym) return nullptr;

    void* p = nullptr;

    if (g_dpy != EGL_NO_DISPLAY && g_sfc != EGL_NO_SURFACE && g_ctx != EGL_NO_CONTEXT) {
        EGLContext cur = eglGetCurrentContext();
        if (cur != g_ctx) {
            if (eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
                g_current_sfc = g_sfc;
            }
        }
        p = (void*)eglGetProcAddress(sym);
    }

    if (!p) {
        if (!g_gles_lib) {
            g_gles_lib = dlopen("libGLESv2.so", RTLD_NOW | RTLD_GLOBAL);
        }
        if (g_gles_lib) p = dlsym(g_gles_lib, sym);
    }

    if (!p) {
        static void* egl_lib = nullptr;
        if (!egl_lib) egl_lib = dlopen("libEGL.so", RTLD_NOW);
        if (egl_lib) p = dlsym(egl_lib, sym);
    }

    return p;
}

static retro_proc_address_t hw_get_proc(const char* sym) {
    if (!sym) return nullptr;
    int n = s_proc_count.fetch_add(1) + 1;

    {
        std::lock_guard<std::mutex> lk(g_proc_cache_mtx);
        auto it = g_proc_cache.find(sym);
        if (it != g_proc_cache.end()) {
            return (retro_proc_address_t)it->second;
        }
    }

    void* p = resolve_gl_proc(sym);

    {
        std::lock_guard<std::mutex> lk(g_proc_cache_mtx);
        g_proc_cache[sym] = p;
    }

    int key = (strcmp(sym, "glBlitFramebuffer") == 0 ||
               strcmp(sym, "glBindFramebuffer") == 0 ||
               strcmp(sym, "glGenFramebuffers") == 0 ||
               strcmp(sym, "glCheckFramebufferStatus") == 0 ||
               strcmp(sym, "glFramebufferTexture2D") == 0 ||
               strcmp(sym, "glFramebufferRenderbuffer") == 0 ||
               strcmp(sym, "glDeleteFramebuffers") == 0 ||
               strcmp(sym, "glGenRenderbuffers") == 0 ||
               strcmp(sym, "glRenderbufferStorage") == 0 ||
               strcmp(sym, "glReadPixels") == 0 ||
               strcmp(sym, "glTexImage2D") == 0 ||
               strcmp(sym, "glGetIntegerv") == 0);

    if (key || n <= 20 || n % 100 == 0) {
        LOGI("proc[%d] %s -> %p", n, sym, p);
    }
    return (retro_proc_address_t)p;
}

struct retro_hw_render_callback* ultraman_get_hw_cb() {
    static struct retro_hw_render_callback cb;
    memset(&cb, 0, sizeof(cb));
    cb.context_type          = RETRO_HW_CONTEXT_OPENGL_ES3;
    cb.context_reset         = hw_context_reset;
    cb.get_current_framebuffer = hw_get_current_fbo;
    cb.get_proc_address      = hw_get_proc;
    cb.depth                 = true;
    cb.stencil               = true;
    cb.bottom_left_origin    = false;
    cb.version               = 3;
    cb.cache_context         = false;
    cb.context_destroy       = hw_context_destroy;
    cb.debug_context         = false;
    return &cb;
}

void ultraman_video_set_hw(bool hw, int w, int h) {
    g_hw_mode.store(hw);
    if (w > 0) g_hw_w = w;
    if (h > 0) g_hw_h = h;
    LOGI("video_set_hw %s %dx%d", hw ? "HW" : "SW", g_hw_w, g_hw_h);
}

static std::atomic<int> g_present_step{0};
void ultraman_video_present() {
#define PSTEP(n) do { g_present_step.store(n); } while(0)
#define PSTEP_END

    static std::atomic<int> s_call{0};
    int c = s_call.fetch_add(1) + 1;
    if (c <= 3) {
        LOGI("present call #%d vready=%d hwmode=%d hwready=%d fbo=%u surf=%d",
             c, g_video_ready.load()?1:0, g_hw_mode.load()?1:0,
             g_hw_ready.load()?1:0, g_hw_fbo, g_surface_ok.load()?1:0);
    }
    if (!g_video_ready.load() || !g_hw_mode.load()) return;
    if (!g_surface_ok.load() || !g_win) return;
    if (g_dpy == EGL_NO_DISPLAY || g_sfc == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT) return;

    GLLOCK;

    if (g_pending_rebuild.exchange(false)) {
        if (g_pending_w > 0 && g_pending_h > 0 && g_dpy != EGL_NO_DISPLAY) {
            LOGI("rebuild FBO: %dx%d -> %dx%d",
                 g_hw_w, g_hw_h, g_pending_w, g_pending_h);
            if (ensure_current()) {
                int old_fbo = (int)g_hw_fbo;
                hw_destroy_fbo();
                g_hw_ready.store(false);
                g_hw_w = g_pending_w;
                g_hw_h = g_pending_h;
                if (hw_create_fbo(g_hw_w, g_hw_h)) {
                    g_hw_ready.store(true);
                    LOGI("rebuild OK: fbo %d -> %u (%dx%d)",
                         old_fbo, g_hw_fbo, g_hw_w, g_hw_h);
                } else {
                    LOGE("rebuild FAIL");
                }
                gl_state_reset();
            }
        }
    }

    if (!g_hw_ready.load() || !g_hw_fbo) return;
    PSTEP(1);
    if (!ensure_current()) return;
    PSTEP(2);

    int winW = g_win_w;
    int winH = g_win_h;
    if (winW <= 0 || winH <= 0) {
        winW = ANativeWindow_getWidth(g_win);
        winH = ANativeWindow_getHeight(g_win);
        g_win_w = winW;
        g_win_h = winH;
    }
    if (winW <= 0 || winH <= 0) return;

    static int stable_w = 0, stable_h = 0, unstable_count = 0;
    if (winW != stable_w || winH != stable_h) {
        unstable_count++;
        if (unstable_count < 3) return;
        stable_w = winW;
        stable_h = winH;
        unstable_count = 0;
        LOGI("surface stable %dx%d", winW, winH);
    }

    int vpW = winW;
    int vpH = winH;
    int vpX = 0;
    int vpY = 0;

    PSTEP(3);
    GL_BIND_FB(GL_READ_FRAMEBUFFER, g_hw_fbo);
    GL_BIND_FB(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    PSTEP(4);
    GL_BLIT_FB(
        0, 0, g_hw_w, g_hw_h,
        0, 0, winW, winH,
        GL_COLOR_BUFFER_BIT, GL_LINEAR
    );
    PSTEP(5);
    GL_BIND_FB(GL_FRAMEBUFFER, 0);

    PSTEP(6);
    if (!eglSwapBuffers(g_dpy, g_sfc)) return;
    PSTEP(7);

    int f = g_hw_frames.fetch_add(1) + 1;
    if (f <= 3 || f == 300 || f == 600) {
        LOGI("present #%d %dx%d -> %dx%d vp=%dx%d@%d,%d",
             f, g_hw_w, g_hw_h, winW, winH, vpW, vpH, vpX, vpY);
    }
}

void ultraman_video_cb(const void* data, unsigned w, unsigned h, size_t pitch) {
    (void)pitch;
    if (g_hw_mode.load()) return;
    if (!g_video_ready.load() || !g_surface_ok.load()) return;
    if (!data || !g_win) return;
    if (g_dpy == EGL_NO_DISPLAY || g_sfc == EGL_NO_SURFACE) return;

    GLLOCK;
    if (!ensure_current()) return;

    int winW = ANativeWindow_getWidth(g_win);
    int winH = ANativeWindow_getHeight(g_win);
    if (winW <= 0 || winH <= 0) return;

    GL_BIND_FB(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_sw_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_sw_prog);
    glBindBuffer(GL_ARRAY_BUFFER, g_sw_vbo);
    glEnableVertexAttribArray((GLuint)g_sw_pos);
    glVertexAttribPointer((GLuint)g_sw_pos, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray((GLuint)g_sw_uv);
    glVertexAttribPointer((GLuint)g_sw_uv, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    eglSwapBuffers(g_dpy, g_sfc);

    int f = g_sw_frames.fetch_add(1) + 1;
    if (f == 1 || f == 60) LOGI("sw frame #%d %ux%u", f, w, h);
}

void ultraman_video_set_geometry(unsigned w, unsigned h, double) {
    if (w > 0) g_hw_w = (int)w;
    if (h > 0) g_hw_h = (int)h;
}

void ultraman_video_start() {
    GLLOCK;
    if (g_video_ready.load()) return;
    if (!g_win) { LOGE("video_start: no window"); return; }
    LOGI("video_start hw=%d", g_hw_mode.load() ? 1 : 0);

    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) { LOGE("no display"); return; }
    if (!eglInitialize(g_dpy, nullptr, nullptr)) { LOGE("eglInitialize fail"); return; }
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, attribs, &g_cfg, 1, &n) || n == 0) {
        LOGE("no EGL config");
        eglTerminate(g_dpy);
        g_dpy = EGL_NO_DISPLAY;
        return;
    }

    g_sfc = eglCreateWindowSurface(g_dpy, g_cfg, g_win, nullptr);
    if (g_sfc == EGL_NO_SURFACE) {
        LOGE("no surface 0x%x", eglGetError());
        eglTerminate(g_dpy);
        g_dpy = EGL_NO_DISPLAY;
        return;
    }

    EGLint cta[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, cta);
    if (g_ctx == EGL_NO_CONTEXT) {
        LOGE("no context 0x%x", eglGetError());
        eglDestroySurface(g_dpy, g_sfc);
        g_sfc = EGL_NO_SURFACE;
        eglTerminate(g_dpy);
        g_dpy = EGL_NO_DISPLAY;
        return;
    }
    if (!eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
        LOGE("eglMakeCurrent fail");
        return;
    }

    LOGI("GL version: %s", glGetString(GL_VERSION));
    LOGI("GL renderer: %s", glGetString(GL_RENDERER));

    reload_gl_after_egl();

    if (!g_hw_mode.load()) sw_setup();

    g_video_ready.store(true);
    g_surface_ok.store(true);
    g_view_w = ANativeWindow_getWidth(g_win);
    g_view_h = ANativeWindow_getHeight(g_win);
    LOGI("video ready %dx%d", g_view_w, g_view_h);

    load_gl_funcs();

    {
        void* blit = resolve_gl_proc("glBlitFramebuffer");
        void* bindfb = resolve_gl_proc("glBindFramebuffer");
        void* genfb = resolve_gl_proc("glGenFramebuffers");
        LOGI("prefetch_blit glBlitFramebuffer=%p glBindFramebuffer=%p glGenFramebuffers=%p",
             blit, bindfb, genfb);
        if (!blit) {
            LOGE("!!! glBlitFramebuffer is NULL, hw render will crash !!!");
        }
    }

    ultraman_on_video_ready();
}

void ultraman_video_stop() {
    GLLOCK;
    g_surface_ok.store(false);
    g_video_ready.store(false);
    g_hw_ready.store(false);

    if (g_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx);
        if (g_hw_fbo) hw_destroy_fbo();
        if (g_sw_tex)  { glDeleteTextures(1, &g_sw_tex);  g_sw_tex = 0; }
        if (g_sw_vbo)  { glDeleteBuffers(1, &g_sw_vbo);   g_sw_vbo = 0; }
        if (g_sw_prog) { glDeleteProgram(g_sw_prog);      g_sw_prog = 0; }

        eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
        if (g_sfc != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_sfc);
        eglTerminate(g_dpy);
    }
    g_dpy = EGL_NO_DISPLAY;
    g_ctx = EGL_NO_CONTEXT;
    g_sfc = EGL_NO_SURFACE;
    g_cfg = nullptr;
    g_current_sfc = EGL_NO_SURFACE;
    g_win_w = 0;
    g_win_h = 0;
}

void ultraman_set_surface(JNIEnv* env, jobject s) {
    bool need_start = false;
    bool need_recreate = false;

    {
        GLLOCK;
        ANativeWindow* new_win = s ? ANativeWindow_fromSurface(env, s) : nullptr;

        if (new_win == g_win) {
            if (new_win) ANativeWindow_release(new_win);
            if (!s) {
                g_surface_ok.store(false);
                LOGI("surface null (unchanged)");
            }
            return;
        }

        g_surface_ok.store(false);

        if (g_win) {
            ANativeWindow_release(g_win);
            g_win = nullptr;
        }

        if (g_video_ready.load() && g_dpy != EGL_NO_DISPLAY) {
            eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            g_current_sfc = EGL_NO_SURFACE;
            if (g_sfc != EGL_NO_SURFACE) {
                eglDestroySurface(g_dpy, g_sfc);
                g_sfc = EGL_NO_SURFACE;
            }
        }

        if (new_win) {
            g_win = new_win;
            g_win_w = ANativeWindow_getWidth(g_win);
            g_win_h = ANativeWindow_getHeight(g_win);
            LOGI("surface %p %dx%d", g_win, g_win_w, g_win_h);

            if (g_video_ready.load() && g_dpy != EGL_NO_DISPLAY && g_cfg) {
                need_recreate = true;
            } else if (!g_video_ready.load()) {
                need_start = true;
            }
        } else {
            LOGI("surface null");
        }
    }

    if (need_recreate) {
        GLLOCK;
        g_sfc = eglCreateWindowSurface(g_dpy, g_cfg, g_win, nullptr);
        if (g_sfc != EGL_NO_SURFACE) {
            if (eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
                g_current_sfc = g_sfc;
                g_surface_ok.store(true);
                LOGI("EGL surface recreated OK");
            } else {
                LOGE("EGL makeCurrent failed after recreate");
            }
        } else {
            LOGE("eglCreateWindowSurface failed 0x%x", eglGetError());
        }
    }

    if (need_start) {
        LOGI("surface arrived: start EGL");
        ultraman_video_start();
    }
}

bool ultraman_video_is_ready() {
    return g_video_ready.load();
}


void ultraman_video_set_hw_size(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (g_hw_w == w && g_hw_h == h) return;
    LOGI("set_hw_size request %dx%d (current %dx%d)", w, h, g_hw_w, g_hw_h);
    g_pending_w = w;
    g_pending_h = h;
    g_pending_rebuild.store(true);
    LOGI("rebuild scheduled: %dx%d", w, h);
}

bool ultraman_video_create_fbo_now() {
    GLLOCK;
    if (g_hw_fbo) {
        LOGI("create_fbo_now: already exists %u", g_hw_fbo);
        return true;
    }
    if (!g_video_ready.load()) {
        LOGE("create_fbo_now: video not ready");
        return false;
    }
    if (g_dpy == EGL_NO_DISPLAY || g_sfc == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT) {
        LOGE("create_fbo_now: EGL state invalid");
        return false;
    }
    if (!eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
        LOGE("create_fbo_now: eglMakeCurrent failed 0x%x", eglGetError());
        return false;
    }
    g_current_sfc = g_sfc;
    LOGI("create_fbo_now: creating %dx%d", g_hw_w, g_hw_h);
    if (!hw_create_fbo(g_hw_w, g_hw_h)) {
        LOGE("create_fbo_now: hw_create_fbo failed");
        return false;
    }
    g_hw_ready.store(true);
    gl_state_reset();
    LOGI("create_fbo_now: OK fbo=%u", g_hw_fbo);
    return true;
}

bool ultraman_video_make_current() {
    GLLOCK;
    if (!g_video_ready.load() || !g_surface_ok.load()) return false;
    if (g_dpy == EGL_NO_DISPLAY || g_sfc == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT) return false;
    if (eglGetCurrentContext() == g_ctx) return true;
    if (!eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
        LOGE("make_current failed 0x%x", eglGetError());
        return false;
    }
    g_current_sfc = g_sfc;
    return true;
}

void ultraman_video_release_current() {
    GLLOCK;
    if (g_dpy == EGL_NO_DISPLAY) return;
    if (eglGetCurrentContext() == EGL_NO_CONTEXT) return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    g_current_sfc = EGL_NO_SURFACE;
    LOGI("release_current done");
}


void ultraman_video_dump_gl_errors() {
    GLLOCK;
    if (g_dpy == EGL_NO_DISPLAY) { LOGW("dump: no display"); return; }
    if (eglGetCurrentContext() != g_ctx) LOGW("dump: ctx not current");
    GLenum e;
    int c = 0;
    while ((e = glGetError()) != GL_NO_ERROR && c < 16) {
        LOGW("dump_error 0x%x", e);
        c++;
    }
    if (c == 0) LOGW("dump: no GL errors");
}


int ultraman_video_present_step() {
    return g_present_step.load();
}
