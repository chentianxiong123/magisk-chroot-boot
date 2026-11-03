#define _GNU_SOURCE
#include <time.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, attrs, &cfg, 1, &n);
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    EGLint pattribs[] = { EGL_WIDTH, 512, EGL_HEIGHT, 512, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    fflush(stdout);

    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(dpy, surf);
    printf("Warmup: 1 frame OK\n");
    fflush(stdout);

    // 30 帧（glClear + eglSwapBuffers，不用 shader）
    int frames = 30;
    double* times = (double*)malloc(frames * sizeof(double));
    for (int i = 0; i < frames; i++) {
        double t0 = now_ms();
        glClearColor(1.0, (float)(i % 255) / 255.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(dpy, surf);
        times[i] = now_ms() - t0;
        if (i < 5) {
            printf("Frame %d: %.1f ms\n", i, times[i]);
            fflush(stdout);
        }
    }
    printf("30 frames rendered OK\n");
    fflush(stdout);

    double total = 0;
    for (int i = 0; i < frames; i++) total += times[i];
    printf("Benchmark: %d frames in %.2f ms (%.2f FPS, %.2f ms/frame avg)\n",
           frames, total, frames * 1000.0 / total, total / frames);
    printf("Frame times (ms): ");
    for (int i = 0; i < frames && i < 10; i++) printf("%.1f ", times[i]);
    printf("...\n");
    fflush(stdout);

    free(times);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
