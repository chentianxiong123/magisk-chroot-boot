// probe 4: 找能 lock（CPU 映射）+ 能 render（GPU 访问）的 usage 组合
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <hybris/gralloc/gralloc.h>

#include <GLES2/gl2ext.h>
extern void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

// gralloc usage flags (numeric, avoid macro conflicts)
#define HW_RENDER     0x100
#define HW_TEXTURE    0x200
#define HW_COMPOSER   0x800
#define HW_FB         0x1000
#define SW_READ_RARELY   0x10
#define SW_WRITE_RARELY  0x20

int main() {
    printf("=== probe 4: usage combos ===\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    EGLint pbAttr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface pbuf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, pbuf, pbuf, ctx);
    p_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    p_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    hybris_gralloc_initialize(0);

    // usage combos to try
    struct UsageTest {
        int usage;
        const char* name;
    };
    struct UsageTest tests[] = {
        { HW_RENDER | HW_COMPOSER, "HW_RENDER|HW_COMPOSER" },
        { HW_RENDER | HW_TEXTURE, "HW_RENDER|HW_TEXTURE" },
        { HW_RENDER | HW_TEXTURE | HW_COMPOSER, "HW_RENDER|HW_TEXTURE|HW_COMPOSER" },
        { HW_RENDER | SW_READ_RARELY, "HW_RENDER|SW_READ_RARELY" },
        { HW_RENDER | SW_WRITE_RARELY, "HW_RENDER|SW_WRITE_RARELY" },
        { HW_RENDER | HW_COMPOSER | SW_READ_RARELY, "HW_RENDER|HW_COMPOSER|SW_READ_RARELY" },
        { HW_RENDER | HW_COMPOSER | SW_WRITE_RARELY, "HW_RENDER|HW_COMPOSER|SW_WRITE_RARELY" },
        { HW_RENDER | HW_TEXTURE | SW_WRITE_RARELY, "HW_RENDER|HW_TEXTURE|SW_WRITE_RARELY" },
        { HW_TEXTURE, "HW_TEXTURE only" },
        { HW_COMPOSER, "HW_COMPOSER only" },
    };

    for (int i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        printf("\n=== %s (0x%x) ===\n", tests[i].name, tests[i].usage);
        buffer_handle_t h = NULL; uint32_t s = 0;
        int ret = hybris_gralloc_allocate(512, 512, 1, tests[i].usage, &h, &s);
        printf("  allocate: ret=%d stride=%u\n", ret, s);
        if (ret < 0) continue;

        // try lock
        void* vaddr = NULL;
        ret = hybris_gralloc_lock(h, tests[i].usage, 0, 0, 512, 512, &vaddr);
        printf("  lock: ret=%d vaddr=%p\n", ret, vaddr);
        int can_lock = (vaddr != NULL);
        if (vaddr) hybris_gralloc_unlock(h);

        // try EGLImage + bind texture
        EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)h, NULL);
        printf("  eglCreateImageKHR: img=%p\n", img);
        int can_image = (img != EGL_NO_IMAGE_KHR);
        int can_bind = 0;

        if (can_image) {
            printf("  *** calling glEGLImageTargetTexture2DOES... ***\n"); fflush(stdout);
            GLuint tex; glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
            GLenum err = glGetError();
            printf("  bind OK, GL error: 0x%x\n", err);
            can_bind = (err == GL_NO_ERROR);
            glDeleteTextures(1, &tex);
            p_eglDestroyImageKHR(dpy, img);
        }

        printf("  RESULT: lock=%s image=%s bind=%s\n", can_lock ? "OK" : "NO", can_image ? "OK" : "NO", can_bind ? "OK" : "NO");
        if (can_lock && can_image && can_bind) {
            printf("  *** PERFECT: zero-copy render target ***\n");
        }

        hybris_gralloc_release(h, 1);
    }

    hybris_gralloc_deinitialize();
    printf("=== probe 4 done ===\n");
    return 0;
}
