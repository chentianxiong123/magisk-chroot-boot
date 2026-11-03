// probe 18: 验证 patched hwcomposerwindow.so 的 Window 构造是否设了函数指针
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ANativeWindowBuffer ANativeWindowBuffer;

#define P(msg) fprintf(stderr, "[P18] %s\n", msg); fflush(stderr);

typedef void (*HWCPresentCallback)(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer);
typedef ANativeWindow* (*HWCNativeWindowCreate_t)(unsigned int width, unsigned int height, unsigned int format, HWCPresentCallback present, void* cb_data);

static void present_callback(void* cb_data, ANativeWindow* window, ANativeWindowBuffer* buffer) {
    fprintf(stderr, "[P18] present_callback: window=%p buffer=%p\n", window, buffer);
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    fprintf(stderr, "[P18] %dx%d\n", W, H);

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
    fprintf(stderr, "[P18] window=%p\n", window);

    // 4. dump window 函数指针（0x00~0xB0）
    fprintf(stderr, "[P18] window dump:\n");
    for (int i = 0; i < 22; i++) {
        fprintf(stderr, "[P18] 0x%02x: %p\n", i*8, *(void**)((char*)window + i*8));
    }

    // 5. 检查 0x38~0x68 是否还是 NULL
    int nullCount = 0;
    for (int i = 7; i <= 13; i++) {  // 0x38~0x68 = index 7~13
        if (*(void**)((char*)window + i*8) == NULL) nullCount++;
    }
    fprintf(stderr, "[P18] 0x38~0x68 NULL count: %d/7 (%s)\n", nullCount, nullCount == 0 ? "PATCHED OK" : "PATCH FAILED");

    P("done");
    return 0;
}
