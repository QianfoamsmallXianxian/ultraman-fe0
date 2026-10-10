#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <mutex>
#include <atomic>
#include <cstring>
#include "ulog.h"
#include "libretro.h"

#define LOGI(...) ulog_write(ANDROID_LOG_INFO,  "UltramanFE0", __VA_ARGS__)
#define LOGE(...) ulog_write(ANDROID_LOG_ERROR, "UltramanFE0", __VA_ARGS__)

static ANativeWindow* g_win = nullptr;
static EGLDisplay     g_dpy = EGL_NO_DISPLAY;
static EGLSurface     g_sfc = EGL_NO_SURFACE;
static EGLContext     g_ctx = EGL_NO_CONTEXT;
static EGLConfig      g_cfg = nullptr;
static std::mutex     g_mtx;

static bool g_video_ready = false;
static bool g_hw_mode     = false;
static bool g_hw_ready    = false;

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

static int g_sw_frames = 0;
static int g_hw_frames = 0;
static bool g_first_hw_logged = false;
static bool g_first_sw_logged = false;

static void* g_gles_lib = nullptr;

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
        LOGE("shader compile: %s", log);
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
        LOGE("program link: %s", log);
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

static void gl_state_reset() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    if (g_hw_fbo)   { glDeleteFramebuffers(1, &g_hw_fbo);   g_hw_fbo = 0; }
    if (g_hw_tex)   { glDeleteTextures(1, &g_hw_tex);       g_hw_tex = 0; }
    if (g_hw_depth) { glDeleteRenderbuffers(1, &g_hw_depth);g_hw_depth = 0; }
}

static bool hw_create_fbo() {
    if (g_hw_fbo) return true;

    glGenTextures(1, &g_hw_tex);
    glBindTexture(GL_TEXTURE_2D, g_hw_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_hw_w, g_hw_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &g_hw_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_hw_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, g_hw_w, g_hw_h);

    glGenFramebuffers(1, &g_hw_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_hw_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_hw_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, g_hw_depth);

    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("FBO incomplete: 0x%x", st);
        hw_destroy_fbo();
        gl_state_reset();
        return false;
    }
    LOGI("FBO %u created %dx%d (tex=%u depth=%u)",
         g_hw_fbo, g_hw_w, g_hw_h, g_hw_tex, g_hw_depth);

    gl_state_reset();
    g_hw_ready = true;
    return true;
}

static void hw_context_reset() {
    LOGI("context_reset");
    if (g_dpy == EGL_NO_DISPLAY || g_ctx == EGL_NO_CONTEXT || g_sfc == EGL_NO_SURFACE) {
        LOGE("context_reset: EGL not ready");
        return;
    }
    eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx);
    LOGI("context_reset: eglMakeCurrent ok");
    g_hw_ready = false;
    hw_create_fbo();
    gl_state_reset();
}

static void hw_context_destroy() {
    LOGI("hw context_destroy");
    hw_destroy_fbo();
    g_hw_ready = false;
}

static uintptr_t hw_get_current_fbo() {
    return (uintptr_t)g_hw_fbo;
}

static retro_proc_address_t hw_get_proc(const char* sym) {
    if (!sym) return nullptr;
    void* p = (void*)eglGetProcAddress(sym);
    if (!p) {
        if (!g_gles_lib) {
            g_gles_lib = dlopen("libGLESv2.so", RTLD_NOW | RTLD_GLOBAL);
        }
        if (g_gles_lib) p = dlsym(g_gles_lib, sym);
    }
    if (!p) {
        static void* egl_lib = dlopen("libEGL.so", RTLD_NOW);
        if (egl_lib) p = dlsym(egl_lib, sym);
    }
    return (retro_proc_address_t)p;
}

struct retro_hw_render_callback* ultraman_get_hw_cb() {
    static struct retro_hw_render_callback cb;
    cb.context_type          = RETRO_HW_CONTEXT_OPENGL_ES3;
    cb.context_reset         = hw_context_reset;
    cb.context_destroy       = hw_context_destroy;
    cb.get_current_framebuffer = hw_get_current_fbo;
    cb.get_proc_address      = hw_get_proc;
    cb.depth                 = true;
    cb.stencil               = true;
    cb.bottom_left_origin    = false;
    cb.version_major         = 3;
    cb.version_minor         = 0;
    cb.cache_context         = false;
    cb.debug_context         = false;
    return &cb;
}

void ultraman_video_set_hw(bool hw, int w, int h) {
    g_hw_mode = hw;
    if (w > 0) g_hw_w = w;
    if (h > 0) g_hw_h = h;
    LOGI("video_set_hw %s %dx%d", hw ? "HW" : "SW", g_hw_w, g_hw_h);
}

void ultraman_video_set_geometry(unsigned w, unsigned h, double) {
    if (w > 0) g_hw_w = (int)w;
    if (h > 0) g_hw_h = (int)h;
}

static std::atomic<int> g_cb_count{0};
void ultraman_video_cb(const void* data, unsigned w, unsigned h, size_t pitch) {
    int cn = g_cb_count.fetch_add(1) + 1;
    if (cn == 1 || cn == 10 || cn == 60) {
        LOGI("CB_ENTER #%d hw=%d ready=%d fbo=%u data=%p %ux%u",
             cn, g_hw_mode, g_hw_ready, g_hw_fbo, data, w, h);
    }
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_video_ready || g_dpy == EGL_NO_DISPLAY || !g_win) return;

    eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx);
    int winW = ANativeWindow_getWidth(g_win);
    int winH = ANativeWindow_getHeight(g_win);

    if (g_hw_mode) {
        if (!g_hw_ready || !g_hw_fbo) {
            if (!g_first_hw_logged) {
                LOGE("hw mode but FBO not ready, skip");
                g_first_hw_logged = true;
            }
            return;
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_hw_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        glBlitFramebuffer(
            0, g_hw_h, g_hw_w, 0,
            0, 0, winW, winH,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        eglSwapBuffers(g_dpy, g_sfc);

        g_hw_frames++;
        if (g_hw_frames == 1 || g_hw_frames == 60 || g_hw_frames == 300) {
            LOGI("hw frame #%d src=%dx%d dst=%dx%d",
                 g_hw_frames, g_hw_w, g_hw_h, winW, winH);
        }
        return;
    }

    if (!data) {
        if (!g_first_sw_logged) {
            LOGI("sw frame skip (null data)");
            g_first_sw_logged = true;
        }
        return;
    }
    (void)pitch;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_sw_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
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

    g_sw_frames++;
    if (g_sw_frames == 1 || g_sw_frames == 60) {
        LOGI("sw frame #%d %ux%u -> %dx%d", g_sw_frames, w, h, winW, winH);
    }
}

void ultraman_video_start() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_win) { LOGE("video_start: no window"); return; }
    LOGI("video_start hw=%d", g_hw_mode);

    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) { LOGE("no display"); return; }
    if (!eglInitialize(g_dpy, nullptr, nullptr)) { LOGE("eglInitialize fail"); return; }
    eglBindAPI(EGL_OPENGL_ES_API);


    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, attribs, &g_cfg, 1, &n) || n == 0) {
        LOGE("no EGL config");
        return;
    }

    g_sfc = eglCreateWindowSurface(g_dpy, g_cfg, g_win, nullptr);
    if (g_sfc == EGL_NO_SURFACE) { LOGE("no surface"); return; }

    EGLint cta[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, cta);
    if (g_ctx == EGL_NO_CONTEXT) {
        LOGE("no context");
        eglDestroySurface(g_dpy, g_sfc);
        g_sfc = EGL_NO_SURFACE;
        return;
    }
    if (!eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
        LOGE("eglMakeCurrent fail");
        return;
    }

    LOGI("GL version: %s", glGetString(GL_VERSION));
    LOGI("GL renderer: %s", glGetString(GL_RENDERER));

    if (!g_hw_mode) {
        sw_setup();
    } else {
        LOGI("hw mode: skip sw setup, wait for context_reset");
    }

    g_video_ready = true;
    LOGI("video ready");
}

void ultraman_video_stop() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_video_ready = false;
    g_hw_ready = false;

    if (g_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx);
        if (g_hw_fbo || g_hw_tex || g_hw_depth) hw_destroy_fbo();
        if (g_sw_tex)   { glDeleteTextures(1, &g_sw_tex);   g_sw_tex = 0; }
        if (g_sw_vbo)   { glDeleteBuffers(1, &g_sw_vbo);    g_sw_vbo = 0; }
        if (g_sw_prog)  { glDeleteProgram(g_sw_prog);       g_sw_prog = 0; }

        eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
        if (g_sfc != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_sfc);
        eglTerminate(g_dpy);
    }
    g_dpy = EGL_NO_DISPLAY;
    g_ctx = EGL_NO_CONTEXT;
    g_sfc = EGL_NO_SURFACE;
    g_cfg = nullptr;
}

void ultraman_set_surface(JNIEnv* env, jobject s) {
    bool need_start = false;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_win) { ANativeWindow_release(g_win); g_win = nullptr; }
        if (s) {
            g_win = ANativeWindow_fromSurface(env, s);
            LOGI("surface %p %dx%d", g_win,
                 g_win ? ANativeWindow_getWidth(g_win) : 0,
                 g_win ? ANativeWindow_getHeight(g_win) : 0);
            if (g_win && !g_video_ready) need_start = true;
        } else {
            LOGI("surface null");
        }
    }
    if (need_start) {
        LOGI("surface arrived: auto-starting EGL");
        ultraman_video_start();
    }
}

void ultraman_video_prepare_fbo() {
    std::lock_guard<std::mutex> lk(g_mtx);
    LOGI("prepare_fbo: vready=%d", g_video_ready);
    if (!g_video_ready) {
        LOGE("prepare_fbo: video not ready");
        return;
    }
    eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx);
    if (!g_hw_fbo) {
        hw_create_fbo();
    } else {
        LOGI("prepare_fbo: FBO already %u", g_hw_fbo);
    }
    gl_state_reset();
}

void ultraman_video_set_hw_size(int w, int h) {
    if (w > 0) g_hw_w = w;
    if (h > 0) g_hw_h = h;
    LOGI("set_hw_size %dx%d", g_hw_w, g_hw_h);
}

bool ultraman_video_is_ready() {
    return g_video_ready;
}
