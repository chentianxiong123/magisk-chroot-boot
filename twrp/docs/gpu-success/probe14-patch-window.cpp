// probe 14: 修 hwcomposerwindow window 对象 NULL 函数指针（手动设）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

typedef struct ANativeWindow ANativeWindow;

#define P(msg) fprintf(stderr, "[P14] %s\n", msg); fflush(stderr);

// dlsym 找 hwcomposerwindow.so 的 HWCNativeWindowCreate
typedef ANativeWindow* (*HWCNativeWindowCreate_t)(unsigned int width, unsigned int height, unsigned int format, void* present, void* cb_data);
typedef void (*HWCNativeWindowDestroy_t)(ANativeWindow *window);

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    fprintf(stderr, "[P14] %dx%d\n", W, H);

    // 1. EGL init
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    P("EGL init done");

    // 2. dlopen hwcomposerwindow.so
    void* hwcHandle = dlopen("/usr/lib/libhybris/libhybris-hwcomposerwindow.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hwcHandle) { fprintf(stderr, "FAIL dlopen hwcomposerwindow.so: %s\n", dlerror()); return 1; }
    P("dlopen hwcomposerwindow.so done");

    // 3. dlsym HWCNativeWindowCreate
    HWCNativeWindowCreate_t HWCNativeWindowCreate = (HWCNativeWindowCreate_t)dlsym(hwcHandle, "HWCNativeWindowCreate");
    HWCNativeWindowDestroy_t HWCNativeWindowDestroy = (HWCNativeWindowDestroy_t)dlsym(hwcHandle, "HWCNativeWindowDestroy");
    if (!HWCNativeWindowCreate) { fprintf(stderr, "FAIL dlsym HWCNativeWindowCreate: %s\n", dlerror()); return 2; }
    P("dlsym HWCNativeWindowCreate done");

    // 4. HWCNativeWindowCreate (present=NULL 先不设)
    P("HWCNativeWindowCreate");
    ANativeWindow* window = HWCNativeWindowCreate(W, H, 1, NULL, NULL); // format=1 RGBA
    if (!window) { fprintf(stderr, "FAIL HWCNativeWindowCreate\n"); return 3; }
    fprintf(stderr, "[P14] window=%p\n", window);

    // 5. dump window 函数指针（0x00~0xB0）
    fprintf(stderr, "[P14] window dump:\n");
    for (int i = 0; i < 22; i++) {
        fprintf(stderr, "[P14] 0x%02x: %p\n", i*8, *(void**)((char*)window + i*8));
    }

    // 6. dlopen platformcommon.so，找 BaseNativeWindow 静态 thunk 函数
    void* platformHandle = dlopen("/usr/lib/libhybris/libhybris-eglplatformcommon.so", RTLD_NOW | RTLD_GLOBAL);
    if (!platformHandle) { fprintf(stderr, "FAIL dlopen platformcommon.so: %s\n", dlerror()); return 4; }
    P("dlopen platformcommon.so done");

    // dlsym BaseNativeWindow 静态函数（这些是 ANativeWindow 函数指针指向的）
    void* setSwapInterval = dlsym(platformHandle, "_ZN16BaseNativeWindow14_setSwapIntervalEP13ANativeWindowi");
    void* lockBuffer_DEPRECATED = dlsym(platformHandle, "_ZN16BaseNativeWindow19_lockBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
    void* dequeueBuffer_DEPRECATED = dlsym(platformHandle, "_ZN16BaseNativeWindow21_dequeueBuffer_DEPRECATEDEP13ANativeWindowPP19ANativeWindowBuffer");
    void* queueBuffer_DEPRECATED = dlsym(platformHandle, "_ZN16BaseNativeWindow19_queueBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
    void* cancelBuffer_DEPRECATED = dlsym(platformHandle, "_ZN16BaseNativeWindow21_cancelBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
    void* query = dlsym(platformHandle, "_ZN16BaseNativeWindow7_queryEPK13ANativeWindowiPi");
    void* perform = dlsym(platformHandle, "_ZN16BaseNativeWindow8_performEP13ANativeWindowijPv");

    fprintf(stderr, "[P14] dlsym: setSwapInterval=%p lockBuffer=%p dequeueBuffer=%p queueBuffer=%p cancelBuffer=%p query=%p perform=%p\n",
        setSwapInterval, lockBuffer_DEPRECATED, dequeueBuffer_DEPRECATED, queueBuffer_DEPRECATED, cancelBuffer_DEPRECATED, query, perform);

    // 7. 设函数指针（如果找到了）
    if (setSwapInterval) *(void**)((char*)window + 0x38) = setSwapInterval;
    if (lockBuffer_DEPRECATED) *(void**)((char*)window + 0x40) = lockBuffer_DEPRECATED;
    if (dequeueBuffer_DEPRECATED) *(void**)((char*)window + 0x48) = dequeueBuffer_DEPRECATED;
    if (queueBuffer_DEPRECATED) *(void**)((char*)window + 0x50) = queueBuffer_DEPRECATED;
    if (cancelBuffer_DEPRECATED) *(void**)((char*)window + 0x58) = cancelBuffer_DEPRECATED;
    if (query) *(void**)((char*)window + 0x60) = query;
    if (perform) *(void**)((char*)window + 0x68) = perform;

    fprintf(stderr, "[P14] after: 0x38=%p 0x40=%p 0x48=%p 0x50=%p 0x58=%p 0x60=%p 0x68=%p\n",
        *(void**)((char*)window + 0x38),
        *(void**)((char*)window + 0x40),
        *(void**)((char*)window + 0x48),
        *(void**)((char*)window + 0x50),
        *(void**)((char*)window + 0x58),
        *(void**)((char*)window + 0x60),
        *(void**)((char*)window + 0x68));

    // 8. eglCreateWindowSurface (hwcomposerwindow, 用修好的 window)
    P("eglCreateWindowSurface (hwcomposerwindow, patched window)");
    EGLSurface surface = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)window, NULL);
    P("eglCreateWindowSurface done");
    if (surface == EGL_NO_SURFACE) { fprintf(stderr, "FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 5; }

    // 9. context
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    if (!eglMakeCurrent(dpy, surface, surface, ctx)) { fprintf(stderr, "FAIL eglMakeCurrent err=0x%x\n", eglGetError()); return 6; }
    P("context OK");

    // 10. render one frame
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(dpy, surface);
    P("rendered 1 frame (GPU→hwcomposer→screen)");

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surface);
    HWCNativeWindowDestroy(window);

    P("ALL OK (GPU→hwcomposer→screen zero-copy)");
    return 0;
}
