// probe 13: fbdev 平台 eglCreateWindowSurface（GPU 渲染到 fb0，零拷贝）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <hardware/fb.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define P(msg) fprintf(stderr, "[P13] %s\n", msg); fflush(stderr);

static GLuint mk(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); fprintf(stderr, "[P13] shader err: %s\n", log); }
    return s;
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    int frames = argc > 3 ? atoi(argv[3]) : 60;
    fprintf(stderr, "[P13] %dx%d %d frames\n", W, H, frames);

    // 1. fb0 (确认存在)
    int fbf = open("/dev/graphics/fb0", O_RDWR);
    P("open fb0 done");
    if (fbf < 0) { fprintf(stderr, "FAIL open fb0\n"); return 1; }
    struct fb_var_screeninfo vinfo;
    ioctl(fbf, FBIOGET_VSCREENINFO, &vinfo);
    fprintf(stderr, "[P13] fb0: %dx%d bpp=%d\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    close(fbf);

    // 2. gralloc HAL open("fb0") → framebuffer_device_t
    P("hw_get_module (gralloc)");
    const hw_module_t *grallocModule = NULL;
    int ret = hw_get_module("gralloc", &grallocModule);
    if (ret < 0) { fprintf(stderr, "FAIL hw_get_module gralloc err=%d\n", ret); return 2; }
    P("hw_get_module done");

    framebuffer_device_t *fbDev = NULL;
    ret = grallocModule->methods->open(grallocModule, GRALLOC_HARDWARE_FB0, (struct hw_device_t**)&fbDev);
    P("open gralloc fb0 done");
    if (ret < 0) { fprintf(stderr, "FAIL open gralloc fb0 err=%d\n", ret); return 3; }
    fprintf(stderr, "[P13] fbDev: %dx%d format=%d\n", fbDev->width, fbDev->height, fbDev->format);

    // 3. EGL init (fbdev platform)
    P("eglGetDisplay");
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) { fprintf(stderr, "FAIL eglInitialize err=0x%x\n", eglGetError()); return 4; }
    P("eglInitialize done");

    // 4. config
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
    if (!eglChooseConfig(dpy, ca, &cfg, 1, &nc) || nc == 0) { fprintf(stderr, "FAIL eglChooseConfig err=0x%x\n", eglGetError()); return 5; }
    P("eglChooseConfig done");

    // 5. eglCreateWindowSurface (fbdev platform, native window = framebuffer_device_t*)
    P("eglCreateWindowSurface (fbdev)");
    EGLSurface surface = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)fbDev, NULL);
    P("eglCreateWindowSurface done");
    if (surface == EGL_NO_SURFACE) { fprintf(stderr, "FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 6; }

    // 6. context
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    if (!eglMakeCurrent(dpy, surface, surface, ctx)) { fprintf(stderr, "FAIL eglMakeCurrent err=0x%x\n", eglGetError()); return 7; }
    P("context OK");

    // 7. shader
    GLuint v = mk(GL_VERTEX_SHADER, "attribute vec2 p; void main(){ gl_Position=vec4(p,0.0,1.0); }");
    GLuint f = mk(GL_FRAGMENT_SHADER, "precision mediump float; uniform float t; void main(){ gl_FragColor=vec4(abs(sin(t)), abs(sin(t*1.3)), abs(sin(t*0.7)), 1.0); }");
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v); glAttachShader(prog, f); glLinkProgram(prog);
    GLint a_p = glGetAttribLocation(prog, "p");
    GLint u_t = glGetUniformLocation(prog, "t");
    glUseProgram(prog);
    GLfloat quad[] = { -1,-1, 1,-1, 1,1, -1,1 };
    glEnableVertexAttribArray(a_p);
    glVertexAttribPointer(a_p, 2, GL_FLOAT, GL_FALSE, 0, quad);
    P("shader OK");

    // 8. render loop (GPU 渲染到 fb0，零拷贝)
    P("render loop start");
    for (int i = 0; i < frames; i++) {
        float t = i / 60.0f;
        glUniform1f(u_t, t);
        glViewport(0, 0, W, H);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        eglSwapBuffers(dpy, surface);
        if (i % 10 == 0) fprintf(stderr, "[P13] frame %d rendered+swapped\n", i);
    }
    P("render loop done");

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surface);
    fbDev->common.close((struct hw_device_t*)fbDev);

    P("ALL OK (GPU→fb0 zero-copy)");
    return 0;
}
