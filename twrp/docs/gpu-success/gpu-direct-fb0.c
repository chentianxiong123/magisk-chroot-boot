#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <time.h>

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "precision mediump float; uniform float u_time; void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(u_time + gl_FragCoord.x / 200.0), 0.5 + 0.5 * sin(u_time + gl_FragCoord.y / 200.0), 0.5 + 0.5 * sin(u_time + (gl_FragCoord.x + gl_FragCoord.y) / 200.0), 1.0); }";

static double now_ms() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024]; glGetShaderInfoLog(s, 1024, NULL, log);
        fprintf(stdout, "SHADER COMPILE FAIL (%s): %s\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        fflush(stdout);
        return 0;
    }
    return s;
}

int main() {
    int W = 1080, H = 1920;
    printf("=== GPU Direct 1080x1920 -> fb0 ===\n");

    int fb = open("/dev/graphics/fb0", O_RDWR);
    if (fb < 0) { printf("FAIL open fb0\n"); return 1; }
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) { printf("FAIL FBIOGET_VSCREENINFO\n"); return 2; }
    int fb_w = vinfo.xres, fb_h = vinfo.yres;
    long fb_stride_bytes = vinfo.xres * vinfo.bits_per_pixel / 8;
    long fb_size = fb_stride_bytes * fb_h;
    printf("FB: %dx%d %d bpp stride=%ld size=%ld\n", fb_w, fb_h, vinfo.bits_per_pixel, fb_stride_bytes, fb_size);

    void* fb_mem = mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (fb_mem == MAP_FAILED) { printf("FAIL mmap fb0\n"); return 3; }
    printf("FB mmap: OK\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, attrs, &cfg, 1, &n);
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    EGLint pattribs[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fsrc);
    if (!vs || !fs) { printf("FAIL shader compile\n"); return 4; }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024]; glGetProgramInfoLog(prog, 1024, NULL, log);
        printf("PROGRAM LINK FAIL: %s\n", log);
        return 4;
    }
    glUseProgram(prog);
    printf("Shaders: OK\n");

    GLfloat verts[] = { -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 };
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(prog, "pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
    GLint utime = glGetUniformLocation(prog, "u_time");

    unsigned char* pixels = (unsigned char*)malloc(W * H * 4);
    if (!pixels) { printf("FAIL malloc\n"); return 5; }

    int frames = 60;
    double t0 = now_ms();
    for (int frame = 0; frame < frames; frame++) {
        glUniform1f(utime, (GLfloat)frame * 0.1f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glFinish();
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        unsigned char* fb_ptr = (unsigned char*)fb_mem;
        long row_bytes = (long)W * 4;
        for (int y = 0; y < H; y++) {
            memcpy(fb_ptr + y * fb_stride_bytes, pixels + y * row_bytes, row_bytes);
        }
        if (frame % 10 == 0) {
            double t = now_ms();
            printf("Frame %d: %.2f FPS (%.2f ms/frame)\n", frame, (frame + 1) * 1000.0 / (t - t0), (t - t0) / (frame + 1));
            fflush(stdout);
        }
    }
    double t1 = now_ms();
    printf("\n%d frames in %.2f ms (%.2f FPS, %.2f ms/frame)\n", frames, t1 - t0, frames * 1000.0 / (t1 - t0), (t1 - t0) / frames);

    free(pixels);
    munmap(fb_mem, fb_size);
    close(fb);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
