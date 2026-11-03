// probe 15: 为什么 HWCNativeWindowCreate 返回 NULL？
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <hybris/gralloc/gralloc.h>

#define P(msg) fprintf(stderr, "[P15] %s\n", msg); fflush(stderr);

typedef void* (*HWCNativeWindowCreate_t)(unsigned int width, unsigned int height, unsigned int format, void* present, void* cb_data);

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    fprintf(stderr, "[P15] %dx%d\n", W, H);

    // 1. dlopen hwcomposerwindow.so
    void* hwcHandle = dlopen("/usr/lib/libhybris/libhybris-hwcomposerwindow.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hwcHandle) { fprintf(stderr, "FAIL dlopen hwcomposerwindow.so: %s\n", dlerror()); return 1; }
    P("dlopen hwcomposerwindow.so done");

    // 2. dlsym HWCNativeWindowCreate
    HWCNativeWindowCreate_t HWCNativeWindowCreate = (HWCNativeWindowCreate_t)dlsym(hwcHandle, "HWCNativeWindowCreate");
    if (!HWCNativeWindowCreate) { fprintf(stderr, "FAIL dlsym HWCNativeWindowCreate: %s\n", dlerror()); return 2; }
    P("dlsym HWCNativeWindowCreate done");

    // 3. gralloc init（HWCNativeWindowCreate 内部可能需要 gralloc）
    P("hybris_gralloc_initialize");
    hybris_gralloc_initialize(0);
    P("hybris_gralloc_initialize done");

    // 4. 试 gralloc 分配（检查 gralloc 能否分配）
    P("test hybris_gralloc_allocate");
    buffer_handle_t handle = NULL;
    uint32_t stride = 0;
    int ret = hybris_gralloc_allocate(W, H, 1, 0x800 | 0x1000, &handle, &stride); // HW_COMPOSER | HW_FB
    fprintf(stderr, "[P15] hybris_gralloc_allocate: ret=%d handle=%p stride=%u\n", ret, handle, stride);
    if (ret < 0) { fprintf(stderr, "FAIL gralloc allocate\n"); return 3; }
    hybris_gralloc_release(handle, 1);
    P("gralloc allocate test OK");

    // 5. HWCNativeWindowCreate (present=NULL)
    P("HWCNativeWindowCreate (present=NULL)");
    void* window = HWCNativeWindowCreate(W, H, 1, NULL, NULL);
    fprintf(stderr, "[P15] window=%p (NULL=%s)\n", window, window ? "NO" : "YES");
    if (!window) {
        fprintf(stderr, "FAIL HWCNativeWindowCreate returned NULL\n");
        // 试 present 非 NULL（HWCNativeWindowCreate 检查 present，NULL 返回 0）
        fprintf(stderr, "[P15] HWCNativeWindowCreate 检查 present!=NULL，present=NULL 返回 0\n");
        fprintf(stderr, "[P15] 需要 present 非 NULL（HWCPresentCallback）\n");
        return 4;
    }
    P("HWCNativeWindowCreate OK");

    hybris_gralloc_deinitialize();
    return 0;
}
