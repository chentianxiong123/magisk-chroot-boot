#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <hybris/hwcomposer/hwcomposer_window.h>
#include <hybris/gralloc/gralloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef HAL_PIXEL_FORMAT_RGBA_8888
#define HAL_PIXEL_FORMAT_RGBA_8888 1
#endif

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "precision mediump float; uniform float u_time; void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(u_time + gl_FragCoord.x / 100.0), 0.5 + 0.5 * sin(u_time + gl_FragCoord.y / 100.0), 0.5 + 0.5 * sin(u_time + (gl_FragCoord.x + gl_FragCoord.y) / 100.0), 1.0); }";

extern "C" {
struct ANativeWindow* HWCNativeWindowCreate(unsigned int, unsigned int, unsigned int, void*, void*);
void HWCNativeWindowDestroy(struct ANativeWindow*);
}

static void present_callback(void* data, struct ANativeWindow* window, struct ANativeWindowBuffer* buffer) {
    fprintf(stdout, "[DBG] present_callback called\n"); fflush(stdout);
}

int main() {
    fprintf(stdout, "[DBG] step1 eglGetDisplay\n"); fflush(stdout);
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize\n"); return 1; }
    fprintf(stdout, "[DBG] step2 eglInitialize OK vendor=%s\n", eglQueryString(dpy, EGL_VENDOR)); fflush(stdout);

    fprintf(stdout, "[DBG] step3 HWCNativeWindowCreate\n"); fflush(stdout);
    ANativeWindow* window = (ANativeWindow*)HWCNativeWindowCreate(1080, 1920, HAL_PIXEL_FORMAT_RGBA_8888, (void*)present_callback, NULL);
    if (!window) { printf("FAIL HWCNativeWindowCreate\n"); return 2; }
    { unsigned long* p = (unsigned long*)window;
      fprintf(stdout, "[DBG] window=%p magic=0x%lx ver=%lu incRef=%p decRef=%p\n",
        (void*)window, p[0], p[1], (void*)p[4], (void*)p[5]); fflush(stdout); }
    fprintf(stdout, "[DBG] step4 HWCNativeWindow OK %p\n", (void*)window); fflush(stdout);

    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n)) { printf("FAIL eglChooseConfig n=%d\n", n); return 3; }
    fprintf(stdout, "[DBG] step5 eglChooseConfig OK\n"); fflush(stdout);

    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL eglCreateContext err=0x%x\n", eglGetError()); return 4; }
    fprintf(stdout, "[DBG] step6 eglCreateContext OK\n"); fflush(stdout);

    { unsigned long* p = (unsigned long*)window;
      fprintf(stdout, "[DBG] before-surface incRef=%p\n", (void*)p[4]); fflush(stdout); }
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)window, NULL);
    { unsigned long* p = (unsigned long*)window;
      fprintf(stdout, "[DBG] after-surface incRef=%p\n", (void*)p[4]); fflush(stdout); }
    if (surf == EGL_NO_SURFACE) { printf("FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 5; }
    fprintf(stdout, "[DBG] step7 eglCreateWindowSurface OK (buffer allocation happens on makeCurrent)\n"); fflush(stdout);

    eglMakeCurrent(dpy, surf, surf, ctx);
    fprintf(stdout, "[DBG] step8 eglMakeCurrent OK renderer=%s\n", glGetString(GL_RENDERER)); fflush(stdout);

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
    GLint utime = glGetUniformLocation(prog, "u_time");

    fprintf(stdout, "[DBG] step9 render loop start\n"); fflush(stdout);
    for (int frame = 0; frame < 60; frame++) {
        glUniform1f(utime, (GLfloat)frame * 0.1f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        fprintf(stdout, "[DBG] step10 before eglSwapBuffers frame=%d\n", frame); fflush(stdout);
        eglSwapBuffers(dpy, surf);
        fprintf(stdout, "[DBG] step11 after eglSwapBuffers frame=%d\n", frame); fflush(stdout);
        usleep(16667);
    }
    printf("60 frames rendered + swapped OK\n");

    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    HWCNativeWindowDestroy((struct ANativeWindow*)window);
    eglTerminate(dpy);
    return 0;
}
