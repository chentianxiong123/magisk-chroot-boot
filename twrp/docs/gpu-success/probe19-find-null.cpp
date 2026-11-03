// probe 19: 验证 window 对象 0x38~0x68 是什么（NULL 还是 vtable），找 eglSwapBuffers 崩的函数指针
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ANativeWindowBuffer ANativeWindowBuffer;

#define P(msg) fprintf(stderr, "[P19] %s\n", msg); fflush(stderr);

typedef void (*HWCPresentCallback)(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer);
typedef ANativeWindow* (*HWCNativeWindowCreate_t)(unsigned int width, unsigned int height, unsigned int format, HWCPresentCallback present, void* cb_data);

static void present_callback(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer) {
    fprintf(stderr, "[P19] present_callback: window=%p buffer=%p\n", window, buffer);
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    fprintf(stderr, "[P19] %dx%d\n", W, H);

    // 1. dlopen hwcomposerwindow.so
    void* hwcHandle = dlopen("/usr/lib/libhybris/libhybris-hwcomposerwindow.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hwcHandle) { fprintf(stderr, "FAIL dlopen hwcomposerwindow.so: %s\n", dlerror()); return 1; }
    P("dlopen hwcomposerwindow.so done");

    // 2. dlsym HWCNativeWindowCreate
    HWCNativeWindowCreate_t HWCNativeWindowCreate = (HWCNativeWindowCreate_t)dlsym(hwcHandle, "HWCNativeWindowCreate");
    if (!HWCNativeWindowCreate) { fprintf(stderr, "FAIL dlsym HWCNativeWindowCreate: %s\n", dlerror()); return 2; }
    P("dlsym HWCNativeWindowCreate done");

    // 3. HWCNativeWindowCreate
    ANativeWindow* window = HWCNativeWindowCreate(W, H, 1, present_callback, NULL);
    if (!window) { fprintf(stderr, "FAIL HWCNativeWindowCreate\n"); return 3; }
    fprintf(stderr, "[P19] window=%p\n", window);

    // 4. dump window 全部 0x00~0xF0（240 bytes = sizeof(ANativeWindow)）
    fprintf(stderr, "[P19] window dump 0x00~0xF0:\n");
    for (int i = 0; i < 30; i++) {
        fprintf(stderr, "[P19] 0x%02x: %p\n", i*8, *(void**)((char*)window + i*8));
    }

    // 5. 找符号（dladdr 每个 0x38~0x68 的函数指针）
    Dl_info info;
    fprintf(stderr, "[P19] symbol lookup 0x38~0x68:\n");
    for (int i = 7; i <= 13; i++) {
        void* ptr = *(void**)((char*)window + i*8);
        if (ptr && dladdr(ptr, &info)) {
            fprintf(stderr, "[P19] 0x%02x: %p = %s (in %s)\n", i*8, ptr, info.dli_sname ? info.dli_sname : "??", info.dli_fname ? info.dli_fname : "??");
        } else {
            fprintf(stderr, "[P19] 0x%02x: %p = (no symbol)\n", i*8, ptr);
        }
    }

    // 6. 找符号（dladdr 0x70~0xa8）
    fprintf(stderr, "[P19] symbol lookup 0x70~0xa8:\n");
    for (int i = 14; i <= 21; i++) {
        void* ptr = *(void**)((char*)window + i*8);
        if (ptr && dladdr(ptr, &info)) {
            fprintf(stderr, "[P19] 0x%02x: %p = %s (in %s)\n", i*8, ptr, info.dli_sname ? info.dli_sname : "??", info.dli_fname ? info.dli_fname : "??");
        } else {
            fprintf(stderr, "[P19] 0x%02x: %p = (no symbol)\n", i*8, ptr);
        }
    }

    P("done");
    return 0;
}
