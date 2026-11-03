#define _GNU_SOURCE
#include <time.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    printf("Step 1: eglInitialize OK\n");
    fflush(stdout);

    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, attrs, &cfg, 1, &n);
    printf("Step 2: eglChooseConfig OK\n");
    fflush(stdout);

    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    printf("Step 3: eglCreateContext OK\n");
    fflush(stdout);

    EGLint pattribs[] = { EGL_WIDTH, 512, EGL_HEIGHT, 512, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    printf("Step 4: eglCreatePbufferSurface OK\n");
    fflush(stdout);

    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("Step 5: eglMakeCurrent OK\n");
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    fflush(stdout);

    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    printf("Step 6: glClear OK\n");
    fflush(stdout);

    glFinish();
    printf("Step 7: glFinish OK\n");
    fflush(stdout);

    GLubyte pixels[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    printf("Step 8: glReadPixels OK (pixel=%02x%02x%02x%02x)\n", pixels[0], pixels[1], pixels[2], pixels[3]);
    fflush(stdout);

    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    printf("Step 9: Done\n");
    return 0;
}
