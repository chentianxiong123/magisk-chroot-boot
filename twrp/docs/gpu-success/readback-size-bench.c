#define _GNU_SOURCE
#include <time.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("\n尺寸对比（glReadPixels 读回速度）:\n");
    printf("%-10s %-10s %-10s %-10s %-10s\n", "尺寸", "数据量", "渲染FPS", "读回ms", "总FPS");
    printf("------------------------------------------------------------\n");
    fflush(stdout);
    
    int sizes[] = {64, 128, 256, 512};
    for (int s = 0; s < 4; s++) {
        int W = sizes[s];
        EGLint pattribs[] = { EGL_WIDTH, W, EGL_HEIGHT, W, EGL_NONE };
        EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
        eglMakeCurrent(dpy, surf, surf, ctx);
        
        const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
        const char* fsrc = "precision highp float; uniform float u_time; void main() { vec2 uv = gl_FragCoord.xy / 512.0; float r = sin(uv.x * 100.0 + u_time) * cos(uv.y * 100.0 - u_time); float g = sin(uv.x * 50.0 - u_time) * cos(uv.y * 50.0 + u_time); gl_FragColor = vec4(0.5 + 0.5 * r, 0.5 + 0.5 * g, 0.5 + 0.5 * b, 1.0); }";
        // Fix shader (remove b)
        fsrc = "precision highp float; uniform float u_time; void main() { vec2 uv = gl_FragCoord.xy / 512.0; float r = sin(uv.x * 100.0 + u_time) * cos(uv.y * 100.0 - u_time); float g = sin(uv.x * 50.0 - u_time) * cos(uv.y * 50.0 + u_time); float b = sin((uv.x + uv.y) * 25.0 + u_time); gl_FragColor = vec4(0.5 + 0.5 * r, 0.5 + 0.5 * g, 0.5 + 0.5 * b, 1.0); }";
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
        glUseProgram(prog);
        
        GLfloat verts[] = { -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 };
        GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        GLint loc = glGetAttribLocation(prog, "pos");
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
        GLint utime = glGetUniformLocation(prog, "u_time");
        
        GLubyte* pixels = (GLubyte*)malloc(W * W * 4);
        
        // Warmup
        for (int i = 0; i < 5; i++) {
            glUniform1f(utime, (GLfloat)i * 0.01f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glFinish();
            glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        
        // Render only (no readback)
        int frames = 100;
        double start = now_ms();
        for (int i = 0; i < frames; i++) {
            glUniform1f(utime, (GLfloat)i * 0.01f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            eglSwapBuffers(dpy, surf);
        }
        double render_elapsed = now_ms() - start;
        double render_fps = frames * 1000.0 / render_elapsed;
        
        // Render + readback
        start = now_ms();
        for (int i = 0; i < frames; i++) {
            glUniform1f(utime, (GLfloat)i * 0.01f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glFinish();
            glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        double total_elapsed = now_ms() - start;
        double total_fps = frames * 1000.0 / total_elapsed;
        double readback_ms = total_elapsed / frames;
        
        printf("%-10d %-10s %-10.1f %-10.3f %-10.1f\n", 
               W, 
               (W*W*4/1024 < 1024 ? "KB" : "MB"),
               render_fps, 
               readback_ms, 
               total_fps);
        fflush(stdout);
        
        free(pixels);
        eglDestroySurface(dpy, surf);
    }
    
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
