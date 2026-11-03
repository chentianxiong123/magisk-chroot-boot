#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <hybris/hwcomposer/hwcomposer_window.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef HAL_PIXEL_FORMAT_RGBA_8888
#define HAL_PIXEL_FORMAT_RGBA_8888 1
#endif

extern "C" {
struct ANativeWindow* HWCNativeWindowCreate(unsigned int, unsigned int, unsigned int, void*, void*);
void HWCNativeWindowDestroy(struct ANativeWindow*);
}

static void present_callback(void* data, struct ANativeWindow* window, struct ANativeWindowBuffer* buffer) {
    fprintf(stdout, "[DBG] present_callback\n"); fflush(stdout);
}

static void dump_window(const char* tag, ANativeWindow* w) {
    if (!w) { fprintf(stdout, "[%s] window=NULL\n", tag); return; }
    unsigned long* p = (unsigned long*)w;
    fprintf(stdout, "[%s] window=%p magic=0x%lx ver=%lu incRef=%p decRef=%p setSwap=%p\n",
        tag, (void*)w, p[0], p[1], (void*)p[4], (void*)p[5], (void*)p[6]);
    fflush(stdout);
}

int main() {
    fprintf(stdout, "[DBG] step1 eglGetDisplay\n"); fflush(stdout);
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize\n"); return 1; }
    fprintf(stdout, "[DBG] step2 eglInitialize OK\n"); fflush(stdout);

    fprintf(stdout, "[DBG] step3 HWCNativeWindowCreate\n"); fflush(stdout);
    ANativeWindow* window = (ANativeWindow*)HWCNativeWindowCreate(1080, 1920, HAL_PIXEL_FORMAT_RGBA_8888, (void*)present_callback, NULL);
    if (!window) { printf("FAIL HWCNativeWindowCreate\n"); return 2; }
    dump_window("after-create", window);

    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n)) { printf("FAIL eglChooseConfig n=%d\n", n); return 3; }
    fprintf(stdout, "[DBG] step5 eglChooseConfig OK\n"); fflush(stdout);

    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL eglCreateContext err=0x%x\n", eglGetError()); return 4; }
    fprintf(stdout, "[DBG] step6 eglCreateContext OK\n"); fflush(stdout);

    dump_window("before-createsurface", window);
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)window, NULL);
    dump_window("after-createsurface", window);
    if (surf == EGL_NO_SURFACE) { printf("FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 5; }
    fprintf(stdout, "[DBG] step7 eglCreateWindowSurface OK surf=%p\n", (void*)surf); fflush(stdout);

    eglMakeCurrent(dpy, surf, surf, ctx);
    fprintf(stdout, "[DBG] step8 eglMakeCurrent OK renderer=%s\n", glGetString(GL_RENDERER)); fflush(stdout);

    static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
    static const char* fsrc = "precision mediump float; void main() { gl_FragColor = vec4(1.0,0.0,0.0,1.0); }";
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { printf("FAIL shader link\n"); return 6; }
    glUseProgram(prog);

    GLfloat verts[] = { -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 };
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(prog, "pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, 0);

    for (int frame = 0; frame < 60; frame++) {
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        if (frame == 0) { dump_window("before-first-swap", window); }
        eglSwapBuffers(dpy, surf);
        if (frame % 10 == 0) { fprintf(stdout, "[DBG] frame %d swapped OK\n", frame); fflush(stdout); }
    }
    printf("60 frames OK\n");

    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    HWCNativeWindowDestroy((struct ANativeWindow*)window);
    eglTerminate(dpy);
    return 0;
}
