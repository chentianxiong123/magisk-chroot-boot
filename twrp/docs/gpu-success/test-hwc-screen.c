#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <hybris/hwcomposer/hwcomposer_window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern "C" {
struct ANativeWindow* HWCNativeWindowCreate(unsigned int, unsigned int, unsigned int, void*, void*);
void HWCNativeWindowDestroy(struct ANativeWindow*);
}

static void present_callback(void* data, struct ANativeWindow* window, struct ANativeWindowBuffer* buffer) {
    // present callback（hwcomposer 提交 buffer）
}

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "precision mediump float; uniform float u_time; void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(u_time + gl_FragCoord.x / 100.0), 0.5 + 0.5 * sin(u_time + gl_FragCoord.y / 100.0), 0.5 + 0.5 * sin(u_time + (gl_FragCoord.x + gl_FragCoord.y) / 100.0), 1.0); }";

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize\n"); return 1; }
    printf("EGL_VENDOR: %s\n", eglQueryString(dpy, EGL_VENDOR));

    // 创建 HWComposerNativeWindow（真实窗口，渲染到屏幕）
    ANativeWindow* window = (ANativeWindow*)HWCNativeWindowCreate(1080, 1920, HAL_PIXEL_FORMAT_RGBA_8888, (void*)present_callback, NULL);
    if (!window) { printf("FAIL HWCNativeWindowCreate\n"); return 2; }
    printf("HWCNativeWindow: OK (1080x1920)\n");

    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n)) { printf("FAIL eglChooseConfig n=%d\n", n); return 3; }

    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL eglCreateContext err=0x%x\n", eglGetError()); return 4; }

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)window, NULL);
    if (surf == EGL_NO_SURFACE) { printf("FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 5; }
    printf("Window surface: OK\n");

    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

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

    // 渲染 60 帧（eglSwapBuffers 显示到屏幕）
    for (int frame = 0; frame < 60; frame++) {
        glUniform1f(utime, (GLfloat)frame * 0.1f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        eglSwapBuffers(dpy, surf);
        if (frame % 10 == 0) {
            printf("Frame %d: rendered + swapped\n", frame);
            fflush(stdout);
        }
        usleep(16667);  // 60 FPS
    }
    printf("60 frames rendered + swapped OK\n");

    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    HWCNativeWindowDestroy((struct ANativeWindow*)window);
    eglTerminate(dpy);
    return 0;
}
