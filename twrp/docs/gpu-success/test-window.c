#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }";

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize\n"); return 1; }
    printf("EGL_VENDOR: %s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("EGL_VERSION: %s\n", eglQueryString(dpy, EGL_VERSION));
    
    // Try eglCreateWindowSurface with default window (EGL_DEFAULT_DISPLAY)
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n)) { printf("FAIL eglChooseConfig\n"); return 2; }
    printf("Config: %d matches\n", n);
    
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL eglCreateContext\n"); return 3; }
    
    // Create window surface (hwcomposer platform should provide default window)
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, 0, NULL);
    if (surf == EGL_NO_SURFACE) { printf("FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 4; }
    printf("Window surface: OK\n");
    
    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(dpy, surf);
    printf("Swap OK\n");
    
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
