// probe 9: 只看 GL/EGL 扩展，不分配任何 buffer（避免崩溃）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

int main() {
    printf("=== probe 9: extensions only ===\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    printf("EGL vendor: %s\n", eglQueryString(dpy, EGL_VENDOR));

    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

    printf("GL renderer: %s\n", glGetString(GL_RENDERER));
    printf("GL version: %s\n", glGetString(GL_VERSION));

    // dump all GL extensions (one per line for grep)
    printf("\n=== GL extensions ===\n");
    const char* glExts = (const char*)glGetString(GL_EXTENSIONS);
    char* exts = strdup(glExts);
    char* tok = strtok(exts, " ");
    while (tok) {
        printf("GL_EXT: %s\n", tok);
        tok = strtok(NULL, " ");
    }
    free(exts);

    // key extensions check
    printf("\n=== KEY extensions for zero-copy ===\n");
    printf("GL_OES_EGL_image: %s\n", strstr(glExts, "GL_OES_EGL_image ") ? "YES" : "NO");
    printf("GL_OES_EGL_image_external: %s\n", strstr(glExts, "GL_OES_EGL_image_external") ? "YES" : "NO");
    printf("GL_OES_EGL_sync: %s\n", strstr(glExts, "GL_OES_EGL_sync") ? "YES" : "NO");
    printf("GL_OES_fbo_render_mipmap: %s\n", strstr(glExts, "GL_OES_fbo_render_mipmap") ? "YES" : "NO");
    printf("GL_ARM_rgba8: %s\n", strstr(glExts, "GL_ARM_rgba8") ? "YES" : "NO");
    printf("GL_OES_rgb8_rgba8: %s\n", strstr(glExts, "GL_OES_rgb8_rgba8") ? "YES" : "NO");

    // EGL extensions
    const char* eglExts = eglQueryString(dpy, EGL_EXTENSIONS);
    printf("EGL_KHR_image_base: %s\n", strstr(eglExts, "EGL_KHR_image_base") ? "YES" : "NO");
    printf("EGL_KHR_image: %s\n", strstr(eglExts, "EGL_KHR_image ") ? "YES" : "NO");
    printf("EGL_ANDROID_image_native_buffer: %s\n", strstr(eglExts, "EGL_ANDROID_image_native_buffer") ? "YES" : "NO");
    printf("EGL_ANDROID_native_fence_sync: %s\n", strstr(eglExts, "EGL_ANDROID_native_fence_sync") ? "YES" : "NO");
    printf("EGL_ANDROID_recordable: %s\n", strstr(eglExts, "EGL_ANDROID_recordable") ? "YES" : "NO");
    printf("EGL_KHR_surfaceless_context: %s\n", strstr(eglExts, "EGL_KHR_surfaceless_context") ? "YES" : "NO");

    printf("=== probe 9 done ===\n");
    return 0;
}
