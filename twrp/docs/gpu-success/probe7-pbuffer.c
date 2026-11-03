// probe 7: 只试 path B（eglCreatePbufferFromClientBuffer），绕过崩溃的 texture 路径
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <hybris/gralloc/gralloc.h>
#include <system/window.h>

static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

int main() {
    printf("=== probe 7: eglCreatePbufferFromClientBuffer only ===\n");

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    printf("surfaceless context OK\n");

    hybris_gralloc_initialize(0);
    int W = 256, H = 256;
    buffer_handle_t h = NULL; uint32_t s = 0;
    int usage = 0x100 | 0x800; // RENDER|COMPOSER
    int ret = hybris_gralloc_allocate(W, H, 1, usage, &h, &s);
    printf("allocate: ret=%d stride=%u handle=%p\n", ret, s, h);
    if (ret < 0) return 1;

    struct ANativeWindowBuffer anb;
    memset(&anb, 0, sizeof(anb));
    anb.width = W; anb.height = H; anb.stride = s; anb.format = 1; anb.usage = usage; anb.handle = h;

    printf("\n=== path B: eglCreatePbufferFromClientBuffer(ANativeWindowBuffer) ===\n");
    EGLint pbAttr[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface surf = eglCreatePbufferFromClientBuffer(dpy, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)&anb, cfg, pbAttr);
    printf("surface=%p (EGL_NO_SURFACE=%p)\n", surf, (void*)EGL_NO_SURFACE);
    if (surf == EGL_NO_SURFACE) {
        printf("FAIL create pbuffer from client buffer, EGL error: 0x%x\n", eglGetError());
        // try raw handle
        printf("\ntry raw native_handle_t instead...\n");
        surf = eglCreatePbufferFromClientBuffer(dpy, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)h, cfg, pbAttr);
        printf("surface=%p\n", surf);
        if (surf == EGL_NO_SURFACE) {
            printf("FAIL raw handle too\n");
            goto done;
        }
    }

    printf("eglMakeCurrent(surface)...\n"); fflush(stdout);
    eglMakeCurrent(dpy, surf, surf, ctx);
    if (eglGetError() == EGL_SUCCESS) {
        printf("*** ZERO-COPY RENDER SURFACE OK ***\n");
        glClearColor(1.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        printf("GPU rendered to gralloc buffer OK\n");

        // verify by locking and checking pixel
        void* vaddr = NULL;
        ret = hybris_gralloc_lock(h, 0x100 | 0x10, 0, 0, W, H, &vaddr);
        printf("lock for verify: ret=%d vaddr=%p\n", ret, vaddr);
        if (vaddr) {
            unsigned int* px = (unsigned int*)vaddr;
            printf("pixel[0]=0x%08x (expect 0xFF0000FF red)\n", px[0]);
            printf("pixel[mid]=0x%08x\n", px[(H/2)*s + W/2]);
            hybris_gralloc_unlock(h);
        }
    } else {
        printf("FAIL make current, EGL error: 0x%x\n", eglGetError());
    }

    eglDestroySurface(dpy, surf);

done:
    hybris_gralloc_release(h, 1);
    hybris_gralloc_deinitialize();
    printf("=== probe 7 done ===\n");
    return 0;
}
