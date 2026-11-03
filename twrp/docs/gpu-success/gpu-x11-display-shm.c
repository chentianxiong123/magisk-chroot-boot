#define _GNU_SOURCE
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <unistd.h>

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "precision mediump float; uniform float u_time; void main() { vec2 uv = gl_FragCoord.xy / 512.0; gl_FragColor = vec4(0.5 + 0.5 * sin(u_time + uv.x * 10.0), 0.5 + 0.5 * sin(u_time + uv.y * 10.0), 0.5 + 0.5 * sin(u_time + (uv.x + uv.y) * 10.0), 1.0); }";

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main() {
    // 1. Open X11 display
    Display* xdisplay = XOpenDisplay(":0");
    if (!xdisplay) { printf("FAIL: XOpenDisplay\n"); return 1; }
    printf("X11 display: OK\n");
    int screen = DefaultScreen(xdisplay);
    Window root = RootWindow(xdisplay, screen);
    Window window = XCreateSimpleWindow(xdisplay, root, 100, 100, 512, 512, 1, BlackPixel(xdisplay, screen), WhitePixel(xdisplay, screen));
    XStoreName(xdisplay, window, "Mali-T860 GPU Test (XShm)");
    XSelectInput(xdisplay, window, ExposureMask | KeyPressMask);
    XMapWindow(xdisplay, window);
    XFlush(xdisplay);
    printf("X11 window: OK\n");

    // 2. XShm image（共享内存，快）
    XShmSegmentInfo shminfo;
    XImage* ximage = XShmCreateImage(xdisplay, DefaultVisual(xdisplay, screen), 24, ZPixmap, NULL, &shminfo, 512, 512);
    shminfo.shmid = shmget(IPC_PRIVATE, ximage->bytes_per_line * ximage->height, IPC_CREAT | 0777);
    shminfo.shmaddr = ximage->data = (char*)shmat(shminfo.shmid, 0, 0);
    shminfo.readOnly = False;
    XShmAttach(xdisplay, &shminfo);
    printf("XShm image: OK (%d bytes)\n", ximage->bytes_per_line * ximage->height);

    // 3. EGL + Mali GPU
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

    // 4. Shaders
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { printf("FAIL shader link\n"); return 5; }
    glUseProgram(prog);
    printf("Shaders: OK\n");

    // 5. Full-screen quad
    GLfloat verts[] = { -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 };
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(prog, "pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
    GLint utime = glGetUniformLocation(prog, "u_time");

    // 6. Render loop（GPU 渲染 → glReadPixels → XShmPutImage）
    int frames = 300;
    double start = now_ms();
    for (int frame = 0; frame < frames; frame++) {
        glUniform1f(utime, (GLfloat)frame * 0.1f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glFinish();
        
        // Read pixels (GPU → CPU)
        glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, ximage->data);
        
        // Convert RGBA → BGRA (X11 24-bit)
        unsigned char* data = (unsigned char*)ximage->data;
        for (int i = 0; i < 512 * 512; i++) {
            unsigned char r = data[i*4+0];
            unsigned char g = data[i*4+1];
            unsigned char b = data[i*4+2];
            data[i*4+0] = b;
            data[i*4+1] = g;
            data[i*4+2] = r;
            data[i*4+3] = 255;
        }
        
        // Put image to X11 window（XShm，快）
        XShmPutImage(xdisplay, window, DefaultGC(xdisplay, screen), ximage, 0, 0, 0, 0, 512, 512, False);
        XFlush(xdisplay);
        
        if (frame % 30 == 0) {
            double elapsed = now_ms() - start;
            printf("Frame %d: %.2f FPS (%.2f ms/frame)\n", frame, frame * 1000.0 / elapsed, elapsed / frame);
            fflush(stdout);
        }
    }
    double elapsed = now_ms() - start;
    printf("\n%d frames in %.2f ms (%.2f FPS, %.2f ms/frame)\n", frames, elapsed, frames * 1000.0 / elapsed, elapsed / frames);

    // Cleanup
    XShmDetach(xdisplay, &shminfo);
    XDestroyImage(ximage);
    shmdt(shminfo.shmaddr);
    shmctl(shminfo.shmid, IPC_RMID, 0);
    XDestroyWindow(xdisplay, window);
    XCloseDisplay(xdisplay);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
