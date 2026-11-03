// probe 6: 构造 ANativeWindowBuffer + 试 eglCreatePbufferFromClientBuffer
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <hybris/gralloc/gralloc.h>
#include <system/window.h> // ANativeWindowBuffer

extern void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

int main() {
    printf("=== probe 6: ANativeWindowBuffer + PbufferFromClientBuffer ===\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    // surfaceless
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    printf("surfaceless context OK\n");

    p_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    p_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    hybris_gralloc_initialize(0);
    int W = 256, H = 256;
    buffer_handle_t h = NULL; uint32_t s = 0;
    int usage = 0x100 | 0x800; // RENDER|COMPOSER
    int ret = hybris_gralloc_allocate(W, H, 1, usage, &h, &s);
    printf("allocate: ret=%d stride=%u handle=%p\n", ret, s, h);
    if (ret < 0) return 1;

    // ===== 构造 ANativeWindowBuffer =====
    // ANativeWindowBuffer { ANativeWindowBuffer_t (deprecated, 0 bytes), int width, height, stride, format, usage, buffer_handle_t handle, void* reserved[2] }
    struct ANativeWindowBuffer anb;
    memset(&anb, 0, sizeof(anb));
    anb.width = W;
    anb.height = H;
    anb.stride = s;
    anb.format = 1; // RGBA_8888
    anb.usage = usage;
    anb.handle = h;
    printf("ANativeWindowBuffer: %dx%d stride=%u handle=%p sizeof=%zu\n", anb.width, anb.height, anb.stride, anb.handle, sizeof(anb));

    // ===== path A: eglCreateImageKHR with ANativeWindowBuffer =====
    printf("\n=== path A: eglCreateImageKHR(ANativeWindowBuffer) ===\n");
    EGLImageKHR imgA = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)&anb, NULL);
    printf("imgA=%p\n", imgA);
    if (imgA != EGL_NO_IMAGE_KHR) {
        printf("calling glEGLImageTargetTexture2DOES (ANativeWindowBuffer-backed)...\n"); fflush(stdout);
        GLuint tex; glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)imgA);
        printf("survived bind\n");
        GLenum err = glGetError();
        printf("GL error: 0x%x\n", err);
        glDeleteTextures(1, &tex);
        p_eglDestroyImageKHR(dpy, imgA);
    }

    // ===== path B: eglCreatePbufferFromClientBuffer =====
    printf("\n=== path B: eglCreatePbufferFromClientBuffer(ANativeWindowBuffer) ===\n");
    EGLint pbAttr[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface surf = eglCreatePbufferFromClientBuffer(dpy, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)&anb, cfg, pbAttr);
    printf("pbuffer surface=%p (EGL_NO_SURFACE=%p)\n", surf, EGL_NO_SURFACE);
    if (surf != EGL_NO_SURFACE) {
        printf("eglMakeCurrent(pbuffer surface)...\n"); fflush(stdout);
        eglMakeCurrent(dpy, surf, surf, ctx);
        if (eglGetError() == EGL_SUCCESS) {
            printf("*** ZERO-COPY RENDER SURFACE OK (GPU can render to gralloc buffer) ***\n");
            // try draw
            glClearColor(1.0, 0.0, 0.0, 1.0);
            glClear(GL_COLOR_BUFFER_BIT);
            glFinish();
            printf("GPU rendered to gralloc buffer OK\n");
        }
        eglDestroySurface(dpy, surf);
    }

    hybris_gralloc_release(h, 1);
    hybris_gralloc_deinitialize();
    printf("=== probe 6 done ===\n");
    return 0;
}
