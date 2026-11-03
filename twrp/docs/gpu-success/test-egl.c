#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { printf("FAIL: eglGetDisplay\n"); return 1; }
    if (!eglInitialize(dpy, NULL, NULL)) {
        printf("FAIL: eglInitialize err=0x%x\n", eglGetError());
        return 2;
    }
    printf("EGL initialized OK\n");
    const char *vendor = eglQueryString(dpy, EGL_VENDOR);
    const char *version = eglQueryString(dpy, EGL_VERSION);
    printf("EGL_VENDOR: %s\n", vendor ? vendor : "(null)");
    printf("EGL_VERSION: %s\n", version ? version : "(null)");

    EGLConfig cfg;
    EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n) || n < 1) {
        printf("FAIL: eglChooseConfig n=%d\n", n);
        return 3;
    }
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL: eglCreateContext\n"); return 4; }

    EGLint pattribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    if (surf == EGL_NO_SURFACE) { printf("FAIL: eglCreatePbufferSurface\n"); return 5; }
    eglMakeCurrent(dpy, surf, surf, ctx);

    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *glver = glGetString(GL_VERSION);
    const GLubyte *glslver = glGetString(GL_SHADING_LANGUAGE_VERSION);
    printf("GL_RENDERER: %s\n", renderer ? (const char*)renderer : "(null)");
    printf("GL_VERSION: %s\n", glver ? (const char*)glver : "(null)");
    printf("GLSL_VERSION: %s\n", glslver ? (const char*)glslver : "(null)");

    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
