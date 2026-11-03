// probe 2: glEGLImageTargetTexture2DOES 崩溃根因
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <hybris/gralloc/gralloc.h>

extern void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);

static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;

int main() {
    printf("=== probe 2: EGLImage render target ===\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);

    // check surfaceless context
    const char* exts = eglQueryString(dpy, EGL_EXTENSIONS);
    printf("EGL_KHR_surfaceless_context: %s\n", strstr(exts, "EGL_KHR_surfaceless_context") ? "YES" : "NO");

    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    EGLint pbAttr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface pbuf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, pbuf, pbuf, ctx);
    printf("pbuffer context OK\n");

    // check GL_OES_EGL_image
    const char* glExts = (const char*)glGetString(GL_EXTENSIONS);
    printf("GL_OES_EGL_image: %s\n", strstr(glExts, "GL_OES_EGL_image") ? "YES" : "NO");
    printf("GL_OES_EGL_image_external: %s\n", strstr(glExts, "GL_OES_EGL_image_external") ? "YES" : "NO");

    // gralloc allocate
    hybris_gralloc_initialize(0);
    buffer_handle_t handle = NULL; uint32_t stride = 0;
    // try different usage combos
    int usages[] = { 0x100 | 0x800, 0x100 | 0x800 | 0x1000, 0x800, 0x100 };
    const char* usageNames[] = { "HW_RENDER|HW_COMPOSER", "HW_RENDER|HW_COMPOSER|HW_FB", "HW_COMPOSER", "HW_RENDER" };
    int format = 1; // RGBA_8888

    p_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    p_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    for (int u = 0; u < 4; u++) {
        printf("\n=== usage: %s (0x%x) ===\n", usageNames[u], usages[u]);
        buffer_handle_t h = NULL; uint32_t s = 0;
        int ret = hybris_gralloc_allocate(512, 512, format, usages[u], &h, &s);
        printf("allocate: ret=%d stride=%u handle=%p\n", ret, s, h);
        if (ret < 0) continue;

        EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)h, NULL);
        printf("eglCreateImageKHR: img=%p\n", img);
        if (img == EGL_NO_IMAGE_KHR) { hybris_gralloc_release(h, 1); continue; }

        // try bind as texture
        printf("calling glEGLImageTargetTexture2DOES...\n"); fflush(stdout);
        GLuint tex; glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
        printf("glEGLImageTargetTexture2DOES done\n");

        GLenum err = glGetError();
        printf("GL error: 0x%x\n", err);

        // try FBO
        GLuint fbo; glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        printf("FBO status: 0x%x (%s)\n", fboStatus, fboStatus == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE");
        if (fboStatus == GL_FRAMEBUFFER_COMPLETE) {
            printf("*** ZERO-COPY RENDER TARGET OK ***\n");
        }

        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        hybris_gralloc_release(h, 1);
        p_eglDestroyImageKHR(dpy, img);
    }

    hybris_gralloc_deinitialize();
    printf("=== probe 2 done ===\n");
    return 0;
}
