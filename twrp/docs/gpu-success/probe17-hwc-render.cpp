// probe 17: eglCreateWindowSurface 成功后，渲染前手动设 window 函数指针
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ANativeWindowBuffer ANativeWindowBuffer;

#define P(msg) fprintf(stderr, "[P17] %s\n", msg); fflush(stderr);

typedef void (*HWCPresentCallback)(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer);
typedef ANativeWindow* (*HWCNativeWindowCreate_t)(unsigned int width, unsigned int height, unsigned int format, HWCPresentCallback present, void* cb_data);
typedef void (*HWCNativeWindowDestroy_t)(ANativeWindow *window);

static void present_callback(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer) {
    fprintf(stderr, "[P17] present_callback: window=%p buffer=%p\n", window, buffer);
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    fprintf(stderr, "[P17] %dx%d\n", W, H);

    // 1. EGL init
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    P("EGL init done");

    // 2. dlopen
    void* hwcHandle = dlopen("/usr/lib/libhybris/libhybris-hwcomposerwindow.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hwcHandle) { fprintf(stderr, "FAIL dlopen hwcomposerwindow.so: %s\n", dlerror()); return 1; }
    P("dlopen hwcomposerwindow.so done");

    void* platformHandle = dlopen("/usr/lib/libhybris/libhybris-eglplatformcommon.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!platformHandle) { fprintf(stderr, "FAIL dlopen platformcommon.so: %s\n", dlerror()); return 2; }
    P("dlopen platformcommon.so done");

    // 3. dlsym HWCNativeWindowCreate
    HWCNativeWindowCreate_t HWCNativeWindowCreate = (HWCNativeWindowCreate_t)dlsym(hwcHandle, "HWCNativeWindowCreate");
    HWCNativeWindowDestroy_t HWCNativeWindowDestroy = (HWCNativeWindowDestroy_t)dlsym(hwcHandle, "HWCNativeWindowDestroy");
    if (!HWCNativeWindowCreate) { fprintf(stderr, "FAIL dlsym HWCNativeWindowCreate: %s\n", dlerror()); return 3; }
    P("dlsym HWCNativeWindowCreate done");

    // 4. HWCNativeWindowCreate (present 非 NULL)
    ANativeWindow* window = HWCNativeWindowCreate(W, H, 1, present_callback, NULL);
    if (!window) { fprintf(stderr, "FAIL HWCNativeWindowCreate\n"); return 4; }
    fprintf(stderr, "[P17] window=%p\n", window);

    // 5. dlsym BaseNativeWindow 静态 thunk 函数（platformcommon.so）
    void* setSwapInterval = dlsym(platformHandle, "_ZN16BaseNativeWindow14_setSwapIntervalEP13ANativeWindowi");
    void* lockBuffer_DEPRECATED = dlsym(platformHandle, "_ZN16BaseNativeWindow19_lockBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
    void* dequeueBuffer_DEPRECATED = dlsym(platformHandle, "_ZN16BaseNativeWindow21_dequeueBuffer_DEPRECATEDEP13ANativeWindowPP19ANativeWindowBuffer");
    void* queueBuffer_DEPRECATED = dlsym(platformHandle, "_ZN16BaseNativeWindow19_queueBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
    void* cancelBuffer_DEPRECATED = dlsym(platformHandle, "_ZN16BaseNativeWindow21_cancelBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
    void* query = dlsym(platformHandle, "_ZN16BaseNativeWindow7_queryEPK13ANativeWindowiPi");
    void* perform = dlsym(platformHandle, "_ZN16BaseNativeWindow8_performEP13ANativeWindowijPv");

    fprintf(stderr, "[P17] dlsym: setSwapInterval=%p lockBuffer=%p dequeueBuffer=%p queueBuffer=%p cancelBuffer=%p query=%p perform=%p\n",
        setSwapInterval, lockBuffer_DEPRECATED, dequeueBuffer_DEPRECATED, queueBuffer_DEPRECATED, cancelBuffer_DEPRECATED, query, perform);

    // 6. eglCreateWindowSurface（先建 surface，window 函数指针后补）
    P("eglCreateWindowSurface (hwcomposerwindow)");
    EGLSurface surface = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)window, NULL);
    P("eglCreateWindowSurface done");
    if (surface == EGL_NO_SURFACE) { fprintf(stderr, "FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 5; }

    // 7. 补 window 函数指针（渲染前设，eglCreateWindowSurface 后）
    if (setSwapInterval) *(void**)((char*)window + 0x38) = setSwapInterval;
    if (lockBuffer_DEPRECATED) *(void**)((char*)window + 0x40) = lockBuffer_DEPRECATED;
    if (dequeueBuffer_DEPRECATED) *(void**)((char*)window + 0x48) = dequeueBuffer_DEPRECATED;
    if (queueBuffer_DEPRECATED) *(void**)((char*)window + 0x50) = queueBuffer_DEPRECATED;
    if (cancelBuffer_DEPRECATED) *(void**)((char*)window + 0x58) = cancelBuffer_DEPRECATED;
    if (query) *(void**)((char*)window + 0x60) = query;
    if (perform) *(void**)((char*)window + 0x68) = perform;

    fprintf(stderr, "[P17] after patch: 0x38=%p 0x40=%p 0x48=%p 0x50=%p 0x58=%p 0x60=%p 0x68=%p\n",
        *(void**)((char*)window + 0x38),
        *(void**)((char*)window + 0x40),
        *(void**)((char*)window + 0x48),
        *(void**)((char*)window + 0x50),
        *(void**)((char*)window + 0x58),
        *(void**)((char*)window + 0x60),
        *(void**)((char*)window + 0x68));

    // 8. context
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    if (!eglMakeCurrent(dpy, surface, surface, ctx)) { fprintf(stderr, "FAIL eglMakeCurrent err=0x%x\n", eglGetError()); return 6; }
    P("context OK");

    // 9. render one frame
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    P("glClear done");
    eglSwapBuffers(dpy, surface);
    P("eglSwapBuffers done");
    P("rendered 1 frame (GPU→hwcomposer→screen)");

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surface);
    HWCNativeWindowDestroy(window);

    P("ALL OK (GPU→hwcomposer→screen zero-copy)");
    return 0;
}
