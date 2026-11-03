// probe 16: HWCNativeWindowCreate + eglCreateWindowSurface（真零拷贝）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ANativeWindowBuffer ANativeWindowBuffer;

#define P(msg) fprintf(stderr, "[P16] %s\n", msg); fflush(stderr);

// HWCPresentCallback 签名（hwcomposer_window.h）
typedef void (*HWCPresentCallback)(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer);

// dlsym 找 hwcomposerwindow.so 的 HWCNativeWindowCreate/Destroy
typedef ANativeWindow* (*HWCNativeWindowCreate_t)(unsigned int width, unsigned int height, unsigned int format, HWCPresentCallback present, void* cb_data);
typedef void (*HWCNativeWindowDestroy_t)(ANativeWindow *window);

// present 回调（hwcomposer 渲染完成时调用）
static void present_callback(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer) {
    fprintf(stderr, "[P16] present_callback: window=%p buffer=%p\n", window, buffer);
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    fprintf(stderr, "[P16] %dx%d\n", W, H);

    // 1. EGL init
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    P("EGL init done");

    // 2. dlopen hwcomposerwindow.so + platformcommon.so
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
    P("HWCNativeWindowCreate (present=present_callback)");
    ANativeWindow* window = HWCNativeWindowCreate(W, H, 1, present_callback, NULL);
    if (!window) { fprintf(stderr, "FAIL HWCNativeWindowCreate\n"); return 4; }
    fprintf(stderr, "[P16] window=%p\n", window);

    // 5. dump window 函数指针（0x00~0xB0）
    fprintf(stderr, "[P16] window dump:\n");
    for (int i = 0; i < 22; i++) {
        fprintf(stderr, "[P16] 0x%02x: %p\n", i*8, *(void**)((char*)window + i*8));
    }

    // 6. eglCreateWindowSurface (hwcomposerwindow, 用 present 回调创建的 window)
    P("eglCreateWindowSurface (hwcomposerwindow)");
    EGLSurface surface = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)window, NULL);
    P("eglCreateWindowSurface done");
    if (surface == EGL_NO_SURFACE) { fprintf(stderr, "FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 5; }

    // 7. context
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    if (!eglMakeCurrent(dpy, surface, surface, ctx)) { fprintf(stderr, "FAIL eglMakeCurrent err=0x%x\n", eglGetError()); return 6; }
    P("context OK");

    // 8. render one frame
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
