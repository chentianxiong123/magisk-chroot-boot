// probe 5: Mali kernel gralloc import 失败根因（小 buffer + surfaceless + framebuffer）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <hybris/gralloc/gralloc.h>
#include <hardware/fb.h>

extern void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

int main() {
    printf("=== probe 5: Mali gralloc import root cause ===\n");

    // 1. check mali0 kernel interface
    printf("\n=== Mali kernel interface ===\n");
    system("ls -la /dev/mali0 2>&1");
    system("dmesg | grep -i mali | tail -5");

    // 2. EGL init with surfaceless
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    const char* exts = eglQueryString(dpy, EGL_EXTENSIONS);
    printf("surfaceless: %s\n", strstr(exts, "EGL_KHR_surfaceless_context") ? "YES" : "NO");

    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);

    // try surfaceless context (no surface)
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    printf("create context (no surface): %p\n", ctx);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    if (eglGetError() == EGL_SUCCESS) {
        printf("surfaceless context OK\n");
    } else {
        printf("surfaceless FAILED, fall back to pbuffer\n");
        EGLint pbAttr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
        EGLSurface pbuf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
        eglMakeCurrent(dpy, pbuf, pbuf, ctx);
        printf("pbuffer fallback OK\n");
    }

    p_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    p_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    // 3. test with small buffers (256x256) and HW_RENDER|HW_COMPOSER
    hybris_gralloc_initialize(0);
    int usages[] = { 0x900, 0x300, 0x100 };
    const char* names[] = { "RENDER|COMPOSER", "RENDER|TEXTURE", "RENDER" };

    for (int i = 0; i < 3; i++) {
        printf("\n=== 256x256 usage: %s (0x%x) ===\n", names[i], usages[i]);
        buffer_handle_t h = NULL; uint32_t s = 0;
        int ret = hybris_gralloc_allocate(256, 256, 1, usages[i], &h, &s);
        printf("  allocate: ret=%d stride=%u handle=%p\n", ret, s, h);
        if (ret < 0) continue;

        EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)h, NULL);
        printf("  eglCreateImageKHR: img=%p\n", img);
        if (img == EGL_NO_IMAGE_KHR) { hybris_gralloc_release(h, 1); continue; }

        printf("  calling glEGLImageTargetTexture2DOES (small buffer)...\n"); fflush(stdout);
        GLuint tex; glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
        printf("  survived bind\n");
        GLenum err = glGetError();
        printf("  GL error: 0x%x\n", err);

        // FBO
        GLuint fbo; glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        printf("  FBO status: 0x%x\n", fboStatus);

        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        p_eglDestroyImageKHR(dpy, img);
        hybris_gralloc_release(h, 1);
    }

    // 4. check mali kernel log after attempts
    printf("\n=== Mali kernel log after ===\n");
    system("dmesg | grep -iE 'mali|ion' | tail -20");

    hybris_gralloc_deinitialize();
    printf("=== probe 5 done ===\n");
    return 0;
}
