// probe 3: 为什么 glEGLImageTargetTexture2DOES 崩？Mali kernel import gralloc buffer 失败？
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <hybris/gralloc/gralloc.h>

extern void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

int main() {
    printf("=== probe 3: Mali kernel gralloc import ===\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    EGLint pbAttr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface pbuf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, pbuf, pbuf, ctx);
    printf("pbuffer context OK\n");

    p_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    p_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    // 1. gralloc allocate with HW_RENDER only (no COMPOSER, no FB)
    hybris_gralloc_initialize(0);
    buffer_handle_t handle = NULL; uint32_t stride = 0;
    int usage = 0x100; // HW_RENDER only
    int ret = hybris_gralloc_allocate(512, 512, 1, usage, &handle, &stride);
    printf("allocate(HW_RENDER): ret=%d stride=%u handle=%p\n", ret, stride, handle);
    if (ret < 0) return 1;

    // 2. lock to get vaddr (verify buffer valid)
    void* vaddr = NULL;
    ret = hybris_gralloc_lock(handle, usage, 0, 0, 512, 512, &vaddr);
    printf("lock: ret=%d vaddr=%p\n", ret, vaddr);
    if (vaddr) {
        memset(vaddr, 0, 512 * stride * 4);
        hybris_gralloc_unlock(handle);
        printf("fill buffer OK\n");
    }

    // 3. create EGLImage
    EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)handle, NULL);
    printf("eglCreateImageKHR: img=%p\n", img);
    if (img == EGL_NO_IMAGE_KHR) { printf("FAIL image\n"); return 2; }

    // 4. try bind as texture (may crash here)
    printf("\n*** calling glEGLImageTargetTexture2DOES (will it crash?) ***\n"); fflush(stdout);
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
    printf("survived glEGLImageTargetTexture2DOES\n");
    GLenum err = glGetError();
    printf("GL error: 0x%x\n", err);

    // 5. check mali kernel log
    printf("\n=== Mali kernel log (last 20 lines) ===\n");
    system("dmesg | grep -i mali | tail -20");

    // cleanup
    glDeleteTextures(1, &tex);
    p_eglDestroyImageKHR(dpy, img);
    hybris_gralloc_release(handle, 1);
    hybris_gralloc_deinitialize();
    printf("=== probe 3 done ===\n");
    return 0;
}
