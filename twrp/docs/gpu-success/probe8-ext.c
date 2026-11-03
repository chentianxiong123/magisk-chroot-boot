// probe 8: Mali 扩展 + gralloc framebuffer buffer import
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <hybris/gralloc/gralloc.h>
#include <system/window.h>

extern void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

int main() {
    printf("=== probe 8: Mali extensions + framebuffer gralloc ===\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    printf("EGL vendor: %s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("EGL version: %s\n", eglQueryString(dpy, EGL_VERSION));

    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);

    // GL info
    printf("GL renderer: %s\n", glGetString(GL_RENDERER));
    printf("GL version: %s\n", glGetString(GL_VERSION));

    // full EGL extensions
    printf("\n=== ALL EGL extensions ===\n");
    const char* eglExts = eglQueryString(dpy, EGL_EXTENSIONS);
    printf("%s\n", eglExts);

    // full GL extensions
    printf("\n=== ALL GL extensions ===\n");
    const char* glExts = (const char*)glGetString(GL_EXTENSIONS);
    printf("%s\n", glExts);

    // skip fbdev (assert fails), just test SW buffer with HW_RENDER
    printf("\n=== SW buffer with HW_RENDER ===\n");
    hybris_gralloc_initialize(0);
    int fw = 256, fh = 256;

    buffer_handle_t fb = NULL; uint32_t stride = 0;
    int fbUsage = 0x1000 | 0x100 | 0x800; // HW_FB | HW_RENDER | HW_COMPOSER
    int ret = hybris_gralloc_allocate(fw, fh, 1, fbUsage, &fb, &stride);
    printf("allocate framebuffer: ret=%d stride=%u handle=%p\n", ret, stride, fb);
    if (ret < 0) goto done;

    // try lock framebuffer
    void* vaddr = NULL;
    ret = hybris_gralloc_lock(fb, 0x1000, 0, 0, fw, fh, &vaddr);
    printf("lock framebuffer: ret=%d vaddr=%p\n", ret, vaddr);
    if (vaddr) hybris_gralloc_unlock(fb);

    // try EGLImage + bind
    struct ANativeWindowBuffer anb;
    memset(&anb, 0, sizeof(anb));
    anb.width = fw; anb.height = fh; anb.stride = stride; anb.format = 1; anb.usage = fbUsage; anb.handle = fb;
    EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)&anb, NULL);
    printf("eglCreateImageKHR(framebuffer): img=%p\n", img);
    if (img != EGL_NO_IMAGE_KHR) {
        printf("binding framebuffer as texture...\n"); fflush(stdout);
        GLuint tex; glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
        printf("survived bind\n");
        GLenum err = glGetError();
        printf("GL error: 0x%x\n", err);
        glDeleteTextures(1, &tex);
        p_eglDestroyImageKHR(dpy, img);
    }

    hybris_gralloc_release(fb, 1);
    hybris_gralloc_deinitialize();

done:
    printf("=== probe 8 done ===\n");
    return 0;
}
