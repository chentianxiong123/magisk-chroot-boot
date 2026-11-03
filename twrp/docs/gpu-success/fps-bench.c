#define _GNU_SOURCE
#include <time.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "precision mediump float; uniform float u_time; void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(u_time + gl_FragCoord.x / 100.0), 0.0, 1.0, 1.0); }";

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main(int argc, char** argv) {
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
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    fflush(stdout);

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { printf("FAIL shader link\n"); return 5; }
    glUseProgram(prog);

    GLfloat verts[900];
    for (int i = 0; i < 300; i++) {
        GLfloat cx = (GLfloat)(i % 20) / 20.0 * 2.0 - 1.0;
        GLfloat cy = (GLfloat)(i / 20) / 15.0 * 2.0 - 1.0;
        GLfloat sz = 0.02;
        verts[i*9+0] = cx;     verts[i*9+1] = cy+sz;  verts[i*9+2] = 0;
        verts[i*9+3] = cx-sz;  verts[i*9+4] = cy-sz;  verts[i*9+5] = 0;
        verts[i*9+6] = cx+sz;  verts[i*9+7] = cy-sz;  verts[i*9+8] = 0;
    }
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(prog, "pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
    GLint utime = glGetUniformLocation(prog, "u_time");

    // warmup 5 帧（eglSwapBuffers）
    for (int i = 0; i < 5; i++) {
        glUniform1f(utime, (GLfloat)i * 0.01f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 900);
        eglSwapBuffers(dpy, surf);
    }
    printf("Warmup: 5 frames OK\n");
    fflush(stdout);

    // benchmark 60 帧（eglSwapBuffers + 每帧时间）
    int frames = 60;
    double* times = (double*)malloc(frames * sizeof(double));
    for (int i = 0; i < frames; i++) {
        double t0 = now_ms();
        glUniform1f(utime, (GLfloat)i * 0.01f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 900);
        eglSwapBuffers(dpy, surf);
        times[i] = now_ms() - t0;
    }
    double total = 0;
    for (int i = 0; i < frames; i++) total += times[i];
    double avg = total / frames;
    printf("Benchmark: %d frames in %.2f ms (%.2f FPS, %.2f ms/frame avg, 300 triangles, 512x512)\n",
           frames, total, frames * 1000.0 / total, avg);
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
