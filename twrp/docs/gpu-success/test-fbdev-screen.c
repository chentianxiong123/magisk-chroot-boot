#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "precision mediump float; uniform float u_time; void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(u_time + gl_FragCoord.x / 100.0), 0.5 + 0.5 * sin(u_time + gl_FragCoord.y / 100.0), 0.5 + 0.5 * sin(u_time + (gl_FragCoord.x + gl_FragCoord.y) / 100.0), 1.0); }";

int main() {
    // 1. Open framebuffer
    int fb = open("/dev/graphics/fb0", O_RDWR);
    if (fb < 0) { printf("FAIL open fb0\n"); return 1; }
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) < 0) { printf("FAIL ioctl VSCREENINFO\n"); return 2; }
    printf("FB: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    int fb_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    void* fb_mem = mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (fb_mem == MAP_FAILED) { printf("FAIL mmap fb0\n"); return 3; }
    printf("FB mmap: OK (%d bytes)\n", fb_size);

    // 2. EGL + Mali GPU
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize\n"); return 4; }
    printf("EGL_VENDOR: %s\n", eglQueryString(dpy, EGL_VENDOR));
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n)) { printf("FAIL eglChooseConfig\n"); return 5; }
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL eglCreateContext\n"); return 6; }
    EGLint pattribs[] = { EGL_WIDTH, 512, EGL_HEIGHT, 512, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    if (surf == EGL_NO_SURFACE) { printf("FAIL eglCreatePbufferSurface\n"); return 7; }
    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

    // 3. Shaders
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { printf("FAIL shader link\n"); return 8; }
    glUseProgram(prog);

    // 4. Full-screen quad
    GLfloat verts[] = { -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 };
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(prog, "pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
    GLint utime = glGetUniformLocation(prog, "u_time");

    // 5. Render loop（GPU 渲染 → glReadPixels → mmap 写 fb0）
    for (int frame = 0; frame < 60; frame++) {
        glUniform1f(utime, (GLfloat)frame * 0.1f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glFinish();
        
        // Read pixels (GPU → CPU)
        static unsigned char pixels[512 * 512 * 4];
        glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        
        // Convert RGBA → BGRA (fb0 24-bit)
        unsigned char* fb_ptr = (unsigned char*)fb_mem;
        for (int y = 0; y < 512; y++) {
            for (int x = 0; x < 512; x++) {
                int src_idx = (y * 512 + x) * 4;
                int dst_idx = (y * vinfo.xres + x) * 4;
                unsigned char r = pixels[src_idx + 0];
                unsigned char g = pixels[src_idx + 1];
                unsigned char b = pixels[src_idx + 2];
                fb_ptr[dst_idx + 0] = b;
                fb_ptr[dst_idx + 1] = g;
                fb_ptr[dst_idx + 2] = r;
                fb_ptr[dst_idx + 3] = 255;
            }
        }
        
        if (frame % 10 == 0) {
            printf("Frame %d: rendered + wrote fb0\n", frame);
            fflush(stdout);
        }
        usleep(16667);  // 60 FPS
    }
    printf("60 frames rendered + wrote fb0 OK\n");

    // Cleanup
    munmap(fb_mem, fb_size);
    close(fb);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
