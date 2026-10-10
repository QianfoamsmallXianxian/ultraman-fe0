#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <mutex>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "UltramanFE0", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "UltramanFE0", __VA_ARGS__)

static ANativeWindow *g_win = nullptr;
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_sfc = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static std::mutex g_mtx;
static GLuint g_tex = 0;
static GLuint g_prog = 0;
static GLuint g_vbo = 0;
static GLint g_a_pos = -1;
static GLint g_a_uv = -1;
static bool g_video_ready = false;
static int g_frame_count = 0;

static const char *VS =
    "#version 300 es\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "out vec2 v_uv;\n"
    "void main(){v_uv=a_uv;gl_Position=vec4(a_pos,0.0,1.0);}\n";
static const char *FS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 o;\n"
    "void main(){o=texture(u_tex,v_uv);}\n";

static GLuint sh(GLenum t, const char *s) {
    GLuint x = glCreateShader(t);
    glShaderSource(x, 1, &s, nullptr);
    glCompileShader(x);
    GLint ok = 0;
    glGetShaderiv(x, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLchar log[512];
        glGetShaderInfoLog(x, 512, nullptr, log);
        LOGE("shader compile failed: %s", log);
    }
    return x;
}

static void setup() {
    GLuint v = sh(GL_VERTEX_SHADER, VS);
    GLuint f = sh(GL_FRAGMENT_SHADER, FS);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLchar log[512];
        glGetProgramInfoLog(p, 512, nullptr, log);
        LOGE("program link failed: %s", log);
    }
    glUseProgram(p);
    g_prog = p;
    g_a_pos = glGetAttribLocation(p, "a_pos");
    g_a_uv = glGetAttribLocation(p, "a_uv");
    LOGI("attribs: pos=%d uv=%d", g_a_pos, g_a_uv);

    float vts[] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f
    };
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vts), vts, GL_STATIC_DRAW);
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void ultraman_video_set_geometry(unsigned, unsigned, double) {}

void ultraman_video_cb(const void *data, unsigned w, unsigned h, size_t) {
    if (!data) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_video_ready || g_dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    int winW = ANativeWindow_getWidth(g_win);
    int winH = ANativeWindow_getHeight(g_win);
    glViewport(0, 0, winW, winH);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_prog);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray((GLuint)g_a_pos);
    glVertexAttribPointer((GLuint)g_a_pos, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    glEnableVertexAttribArray((GLuint)g_a_uv);
    glVertexAttribPointer((GLuint)g_a_uv, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(g_dpy, g_sfc);

    g_frame_count++;
    if (g_frame_count == 1 || g_frame_count == 60) {
        LOGI("video frame #%d %ux%u -> %dx%d", g_frame_count, w, h, winW, winH);
    }
}

void ultraman_video_start() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_win) {
        LOGE("video_start: no native window");
        return;
    }
    LOGI("video_start");

    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return; }
    if (!eglInitialize(g_dpy, nullptr, nullptr)) { LOGE("eglInitialize failed"); return; }
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint ca[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint n = 0;
    eglChooseConfig(g_dpy, ca, &cfg, 1, &n);
    if (n == 0) { LOGE("no EGL config"); return; }

    g_sfc = eglCreateWindowSurface(g_dpy, cfg, g_win, nullptr);
    if (g_sfc == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface failed"); return; }

    EGLint cta[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, cta);
    if (g_ctx == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed"); return; }

    if (!eglMakeCurrent(g_dpy, g_sfc, g_sfc, g_ctx)) {
        LOGE("eglMakeCurrent failed");
        return;
    }
    setup();
    g_video_ready = true;
    LOGI("video ready, GL=%s", glGetString(GL_VERSION));
}

void ultraman_video_stop() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_video_ready = false;
    if (g_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
        if (g_sfc != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_sfc);
        eglTerminate(g_dpy);
    }
    g_dpy = EGL_NO_DISPLAY;
    g_ctx = EGL_NO_CONTEXT;
    g_sfc = EGL_NO_SURFACE;
}

void ultraman_set_surface(JNIEnv *env, jobject s) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_win) { ANativeWindow_release(g_win); g_win = nullptr; }
    if (s) {
        g_win = ANativeWindow_fromSurface(env, s);
        LOGI("set_surface: %p (%dx%d)", g_win,
             g_win ? ANativeWindow_getWidth(g_win) : 0,
             g_win ? ANativeWindow_getHeight(g_win) : 0);
    } else {
        LOGI("set_surface: null");
    }
}
