#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize\n"); return 1; }
    printf("EGL_VENDOR: %s\n", eglQueryString(dpy, EGL_VENDOR));
    
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n)) { printf("FAIL eglChooseConfig n=%d\n", n); return 2; }
    
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL eglCreateContext err=0x%x\n", eglGetError()); return 3; }
    
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, 0, NULL);
    if (surf == EGL_NO_SURFACE) { printf("FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 4; }
    printf("Window surface: OK\n");
    
    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    
    // Render red triangle 60 times
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    GLfloat verts[] = { 0,0.5,0, -0.5,-0.5,0, 0.5,-0.5,0 };
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    
    for (int i = 0; i < 60; i++) {
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        eglSwapBuffers(dpy, surf);
        usleep(16667);  // ~60fps
    }
    printf("60 frames rendered OK\n");
    
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
