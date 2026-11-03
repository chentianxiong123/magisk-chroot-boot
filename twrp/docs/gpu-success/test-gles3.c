#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
    eglChooseConfig(dpy, attrs, &cfg, 1, &n);
    printf("Config GLES3: %d matches\n", n);
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL: GLES3 context err=0x%x\n", eglGetError()); return 1; }
    EGLint pattribs[] = { EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    printf("GLSL_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    GLint maxv;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxv);
    printf("MAX_VERTEX_ATTRIBS: %d\n", maxv);
    return 0;
}
