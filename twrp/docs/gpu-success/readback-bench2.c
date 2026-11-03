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
static const char* fsrc = "precision mediump float; uniform float u_time; void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(u_time + gl_FragCoord.x / 100.0), 0.5 + 0.5 * sin(u_time + gl_FragCoord.y / 100.0), 0.5 + 0.5 * sin(u_time + (gl_FragCoord.x + gl_FragCoord.y) / 100.0), 1.0); }";

static double now_ms() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int fb = -1;
static void* fb_mem = NULL;
static long fb_stride_bytes = 0;
static int fb_w = 0, fb_h = 0;
static long fb_size = 0;

static int init_fb() {
    fb = open("/dev/graphics/fb0", O_RDWR);
    if (fb < 0) { printf("FAIL open fb0\n"); return -1; }
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) { printf("FAIL VSCREENINFO\n"); return -1; }
    fb_w = vinfo.xres; fb_h = vinfo.yres;
    fb_stride_bytes = vinfo.xres * vinfo.bits_per_pixel / 8;
    fb_size = fb_stride_bytes * fb_h;
    fb_mem = mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (fb_mem == MAP_FAILED) { printf("FAIL mmap\n"); return -1; }
    printf("FB: %dx%d %d bpp stride=%ld\n", fb_w, fb_h, vinfo.bits_per_pixel, fb_stride_bytes);
    return 0;
}

// benchmark one size: render WxH PBuffer, readback, copy to fb (centered)
static void bench_size(EGLDisplay dpy, EGLConfig cfg, int W, int H, int frames) {
    EGLint pattribs[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    if (surf == EGL_NO_SURFACE) { printf("  %dx%d: FAIL pbuffer\n", W, H); return; }
    eglMakeCurrent(dpy, surf, surf, eglGetCurrentContext());

    unsigned char* pixels = (unsigned char*)malloc(W * H * 4);
    if (!pixels) { eglDestroySurface(dpy, surf); return; }

    // offset in fb to center the image
    long dst_x = (fb_w - W) / 2; if (dst_x < 0) dst_x = 0;
    long dst_y = (fb_h - H) / 2; if (dst_y < 0) dst_y = 0;

    double t0 = now_ms();
    for (int f = 0; f < frames; f++) {
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        unsigned char* fb_ptr = (unsigned char*)fb_mem;
        long row_bytes = (long)W * 4;
        for (int y = 0; y < H; y++) {
            memcpy(fb_ptr + (dst_y + y) * fb_stride_bytes + dst_x * 4, pixels + y * row_bytes, row_bytes);
        }
    }
    double t1 = now_ms();
    double total = t1 - t0;
    printf("  %4dx%-4d: %5.2f FPS  %7.2f ms/frame  (read+copy %dx%d=%ld KB)\n",
        W, H, frames * 1000.0 / total, total / frames, W, H, (long)W * H * 4 / 1024);

    free(pixels);
    eglDestroySurface(dpy, surf);
}

int main() {
    if (init_fb() < 0) return 1;

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, attrs, &cfg, 1, &n);
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    eglMakeCurrent(dpy, eglGetCurrentSurface(EGL_READ), eglGetCurrentSurface(EGL_DRAW), ctx);

    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("=== readback size benchmark (10 frames each) ===\n");

    int sizes[][2] = {
        {256, 256}, {512, 512}, {768, 768},
        {540, 960}, {720, 1280}, {1080, 1920},
    };
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);
    for (int i = 0; i < nsizes; i++) {
        bench_size(dpy, cfg, sizes[i][0], sizes[i][1], 10);
    }

    eglTerminate(dpy);
    return 0;
}
