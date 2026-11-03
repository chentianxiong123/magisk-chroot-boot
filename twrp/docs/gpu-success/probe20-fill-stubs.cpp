// probe 20: dump window 0x00~0xF0，找所有 NULL 槽，填 stub，找 eglSwapBuffers 崩的函数指针
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ANativeWindowBuffer ANativeWindowBuffer;

#define P(msg) fprintf(stderr, "[P20] %s\n", msg); fflush(stderr);

typedef void (*HWCPresentCallback)(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer);
typedef ANativeWindow* (*HWCNativeWindowCreate_t)(unsigned int width, unsigned int height, unsigned int format, HWCPresentCallback present, void* cb_data);
typedef void (*HWCNativeWindowDestroy_t)(ANativeWindow *window);

static void present_callback(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer) {
    fprintf(stderr, "[P20] present_callback: window=%p buffer=%p\n", window, buffer);
}

// stub 函数（返回 0，啥也不做）
static int stub_swapInterval(ANativeWindow* w, int interval) { fprintf(stderr, "[P20] stub_swapInterval called\n"); return 0; }
static int stub_dequeueBuffer_DEPRECATED(ANativeWindow* w, ANativeWindowBuffer** b) { fprintf(stderr, "[P20] stub_dequeueBuffer_DEPRECATED called\n"); return 0; }
static int stub_lockBuffer_DEPRECATED(ANativeWindow* w, ANativeWindowBuffer* b) { fprintf(stderr, "[P20] stub_lockBuffer_DEPRECATED called\n"); return 0; }
static int stub_queueBuffer_DEPRECATED(ANativeWindow* w, ANativeWindowBuffer* b) { fprintf(stderr, "[P20] stub_queueBuffer_DEPRECATED called\n"); return 0; }
static int stub_query(ANativeWindow const* w, int what, int* v) { fprintf(stderr, "[P20] stub_query called what=%d\n", what); *v = 0; return 0; }
static int stub_perform(ANativeWindow* w, int op, ...) { fprintf(stderr, "[P20] stub_perform called op=%d\n", op); return 0; }
static int stub_cancelBuffer_DEPRECATED(ANativeWindow* w, ANativeWindowBuffer* b) { fprintf(stderr, "[P20] stub_cancelBuffer_DEPRECATED called\n"); return 0; }
static int stub_dequeueBuffer(ANativeWindow* w, ANativeWindowBuffer** b, int* fenceFd) { fprintf(stderr, "[P20] stub_dequeueBuffer called\n"); *fenceFd = -1; return 0; }
static int stub_queueBuffer(ANativeWindow* w, ANativeWindowBuffer* b, int fenceFd) { fprintf(stderr, "[P20] stub_queueBuffer called fenceFd=%d\n", fenceFd); return 0; }
static int stub_cancelBuffer(ANativeWindow* w, ANativeWindowBuffer* b, int fenceFd) { fprintf(stderr, "[P20] stub_cancelBuffer called fenceFd=%d\n", fenceFd); return 0; }

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    fprintf(stderr, "[P20] %dx%d\n", W, H);

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

    // 4. HWCNativeWindowCreate
    ANativeWindow* window = HWCNativeWindowCreate(W, H, 1, present_callback, NULL);
    if (!window) { fprintf(stderr, "FAIL HWCNativeWindowCreate\n"); return 3; }
    fprintf(stderr, "[P20] window=%p\n", window);

    // 5. dump window 0x00~0xF0，找 NULL 槽
    fprintf(stderr, "[P20] window dump 0x00~0xF0 (NULL 槽标 *):\n");
    for (int i = 0; i < 30; i++) {
        void* ptr = *(void**)((char*)window + i*8);
        fprintf(stderr, "[P20] 0x%02x: %p %s\n", i*8, ptr, ptr == NULL ? "*" : "");
    }

    // 6. 手动填 NULL 槽（0x38~0x68，8个槽）
    // 按 ANativeWindow 结构顺序（window.h 402-475）
    // 0x38 flags, 0x3C minSwapInterval, 0x40 maxSwapInterval, 0x44 xdpi, 0x48 ydpi
    // 0x4C~0x5B oem[4]
    // 0x5C setSwapInterval, 0x64 dequeueBuffer_DEPRECATED, 0x6C lockBuffer_DEPRECATED
    // 0x74 queueBuffer_DEPRECATED, 0x7C query, 0x84 perform, 0x8C cancelBuffer_DEPRECATED
    // 0x94 dequeueBuffer, 0x9C queueBuffer, 0xA4 cancelBuffer
    // 但 dump 显示 0x38~0x68 全 NULL（8个槽），0x70~0xa8 OK（8个）
    // 所以 dump 偏移和 window.h 不匹配，0x38~0x68 可能是其他字段
    // 按 dump 填 NULL 槽（0x38~0x68，8个）
    fprintf(stderr, "[P20] filling NULL slots 0x38~0x68 with stubs...\n");
    *(void**)((char*)window + 0x38) = (void*)stub_swapInterval;
    *(void**)((char*)window + 0x40) = (void*)stub_dequeueBuffer_DEPRECATED;
    *(void**)((char*)window + 0x48) = (void*)stub_lockBuffer_DEPRECATED;
    *(void**)((char*)window + 0x50) = (void*)stub_queueBuffer_DEPRECATED;
    *(void**)((char*)window + 0x58) = (void*)stub_cancelBuffer_DEPRECATED;
    *(void**)((char*)window + 0x60) = (void*)stub_query;
    *(void**)((char*)window + 0x68) = (void*)stub_perform;

    fprintf(stderr, "[P20] after fill: 0x38=%p 0x40=%p 0x48=%p 0x50=%p 0x58=%p 0x60=%p 0x68=%p\n",
        *(void**)((char*)window + 0x38),
        *(void**)((char*)window + 0x40),
        *(void**)((char*)window + 0x48),
        *(void**)((char*)window + 0x50),
        *(void**)((char*)window + 0x58),
        *(void**)((char*)window + 0x60),
        *(void**)((char*)window + 0x68));

    // 7. eglCreateWindowSurface
    P("eglCreateWindowSurface (hwcomposerwindow, stubs filled)");
    EGLSurface surface = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)window, NULL);
    P("eglCreateWindowSurface done");
    if (surface == EGL_NO_SURFACE) { fprintf(stderr, "FAIL eglCreateWindowSurface err=0x%x\n", eglGetError()); return 4; }

    // 8. context
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    if (!eglMakeCurrent(dpy, surface, surface, ctx)) { fprintf(stderr, "FAIL eglMakeCurrent err=0x%x\n", eglGetError()); return 5; }
    P("context OK");

    // 9. render one frame
    glClearColor(1.0, 0.0, 0.0, 1.0);
    P("glClearColor done");
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
