#define _GNU_SOURCE
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <unistd.h>

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "precision mediump float; uniform float u_time; void main() { vec2 uv = gl_FragCoord.xy / 512.0; gl_FragColor = vec4(0.5 + 0.5 * sin(u_time + uv.x * 10.0), 0.5 + 0.5 * sin(u_time + uv.y * 10.0), 0.5 + 0.5 * sin(u_time + (uv.x + uv.y) * 10.0), 1.0); }";

int main() {
    // 1. Open X11 display
    Display* xdisplay = XOpenDisplay(":0");
    if (!xdisplay) { printf("FAIL: XOpenDisplay\n"); return 1; }
    printf("X11 display: OK\n");
    int screen = DefaultScreen(xdisplay);
    Window root = RootWindow(xdisplay, screen);
    Window window = XCreateSimpleWindow(xdisplay, root, 100, 100, 512, 512, 1, BlackPixel(xdisplay, screen), WhitePixel(xdisplay, screen));
    XStoreName(xdisplay, window, "Mali-T860 GPU Test");
    XSelectInput(xdisplay, window, ExposureMask | KeyPressMask);
    XMapWindow(xdisplay, window);
    XFlush(xdisplay);
    printf("X11 window: OK\n");
    XImage* ximage = XCreateImage(xdisplay, DefaultVisual(xdisplay, screen), 24, ZPixmap, 0, NULL, 512, 512, 32, 0);
    ximage->data = (char*)malloc(512 * 512 * 4);

    // 2. EGL + Mali GPU
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

    // 3. Shaders
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

    // 4. Full-screen quad
    GLfloat verts[] = { -1,-1,0, 1,-1,0, 1,1,0, -1,1,0 };
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(prog, "pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
    GLint utime = glGetUniformLocation(prog, "u_time");

    // 5. Render loop (60 frames)
    for (int frame = 0; frame < 60; frame++) {
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
        
        // Put image to X11 window
        XPutImage(xdisplay, window, DefaultGC(xdisplay, screen), ximage, 0, 0, 0, 0, 512, 512);
        XFlush(xdisplay);
        
        if (frame % 10 == 0) {
            printf("Frame %d: rendered + displayed\n", frame);
            fflush(stdout);
        }
        usleep(16667);  // 60 FPS
    }
    printf("60 frames rendered + displayed OK\n");

    // Cleanup
    XDestroyImage(ximage);
    XDestroyWindow(xdisplay, window);
    XCloseDisplay(xdisplay);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
