// patch-hwcomposerwindow.cpp: 修 BaseNativeWindow 构造不设函数指针的 bug
// 在 HWComposerNativeWindow 构造末尾强制重设 ANativeWindow 函数指针

#include <hybris/gralloc/gralloc.h>
#include <system/window.h>
#include <hardware/hwcomposer.h>
#include <hardware/gralloc.h>
#include <pthread.h>
#include <vector>
#include <mutex>

// 引用 platformcommon.so 提供的静态 thunk 函数（BaseNativeWindow 构造设的 incRef/decRef 同源）
// 这些符号在 libhybris-eglplatformcommon.so 里
extern "C" {
    // BaseNativeWindow 的静态 thunk 函数（platformcommon.so 导出）
    int _ZN16BaseNativeWindow14_setSwapIntervalEP13ANativeWindowi(struct ANativeWindow* window, int interval);
    int _ZN16BaseNativeWindow21_dequeueBuffer_DEPRECATEDEP13ANativeWindowPP19ANativeWindowBuffer(struct ANativeWindow* window, struct ANativeWindowBuffer** buffer);
    int _ZN16BaseNativeWindow19_lockBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer(struct ANativeWindow* window, struct ANativeWindowBuffer* buffer);
    int _ZN16BaseNativeWindow19_queueBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer(struct ANativeWindow* window, struct ANativeWindowBuffer* buffer);
    int _ZN16BaseNativeWindow21_cancelBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer(struct ANativeWindow* window, struct ANativeWindowBuffer* buffer);
    const char* _ZN16BaseNativeWindow7_queryEPK13ANativeWindowiPPi(struct ANativeWindow* window, int what, int* value);
    int _ZN16BaseNativeWindow8_performEP13ANativeWindowijPv(struct ANativeWindow* window, int what, ...);
    int _ZN16BaseNativeWindow10_queryEPK13ANativeWindowiPi(struct ANativeWindow const* window, int what, int* value);
}

// 简化：直接用弱符号（找不到就用 null，不崩）
#define WEAK_SYM(name) extern "C" __attribute__((weak)) int name(...);

class HWComposerNativeWindowPatch {
public:
    static void fixFunctionPointers(struct ANativeWindow* window) {
        if (!window) return;
        
        fprintf(stderr, "[PATCH] fixFunctionPointers: window=%p\n", window);
        
        // 0x38: setSwapInterval (offset 56)
        // 0x40: lockBuffer_DEPRECATED (offset 64)
        // 0x48: dequeueBuffer_DEPRECATED (offset 72)
        // 0x50: queueBuffer_DEPRECATED (offset 80)
        // 0x58: cancelBuffer_DEPRECATED (offset 88)
        // 0x60: query (offset 96)
        // 0x68: perform (offset 104)
        
        // dump 当前状态
        fprintf(stderr, "[PATCH] before: 0x28=%p 0x30=%p 0x38=%p 0x40=%p 0x48=%p 0x50=%p 0x58=%p 0x60=%p 0x68=%p\n",
            *(void**)((char*)window + 0x28),
            *(void**)((char*)window + 0x30),
            *(void**)((char*)window + 0x38),
            *(void**)((char*)window + 0x40),
            *(void**)((char*)window + 0x48),
            *(void**)((char*)window + 0x50),
            *(void**)((char*)window + 0x58),
            *(void**)((char*)window + 0x60),
            *(void**)((char*)window + 0x68));
        
        // 找 platformcommon.so 里的 BaseNativeWindow 静态函数
        // 用 dlsym 找（但它们可能是 internal 符号，不导出）
        // 直接从 platformcommon.so 的已知地址找（incRef 在 0x7fb7a62540，decRef 在 0x7fb7a6298c）
        // 但这些是每次加载不同地址，不能硬编码
        
        // 更好的方法：直接用 dlsym 找 BaseNativeWindow 的静态函数
        void* handle = dlopen("/usr/lib/libhybris/libhybris-eglplatformcommon.so.1", RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            // 这些符号可能不导出（是 internal），但试一下
            void* setSwapInterval = dlsym(handle, "_ZN16BaseNativeWindow14_setSwapIntervalEP13ANativeWindowi");
            void* lockBuffer_DEPRECATED = dlsym(handle, "_ZN16BaseNativeWindow19_lockBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
            void* dequeueBuffer_DEPRECATED = dlsym(handle, "_ZN16BaseNativeWindow21_dequeueBuffer_DEPRECATEDEP13ANativeWindowPP19ANativeWindowBuffer");
            void* queueBuffer_DEPRECATED = dlsym(handle, "_ZN16BaseNativeWindow19_queueBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
            void* cancelBuffer_DEPRECATED = dlsym(handle, "_ZN16BaseNativeWindow21_cancelBuffer_DEPRECATEDEP13ANativeWindowP19ANativeWindowBuffer");
            void* query = dlsym(handle, "_ZN16BaseNativeWindow7_queryEPK13ANativeWindowiPi");
            void* perform = dlsym(handle, "_ZN16BaseNativeWindow8_performEP13ANativeWindowijPv");
            
            fprintf(stderr, "[PATCH] dlsym: setSwapInterval=%p lockBuffer=%p dequeueBuffer=%p queueBuffer=%p cancelBuffer=%p query=%p perform=%p\n",
                setSwapInterval, lockBuffer_DEPRECATED, dequeueBuffer_DEPRECATED, queueBuffer_DEPRECATED, cancelBuffer_DEPRECATED, query, perform);
            
            // 设置函数指针（如果找到了）
            if (setSwapInterval) *(void**)((char*)window + 0x38) = setSwapInterval;
            if (lockBuffer_DEPRECATED) *(void**)((char*)window + 0x40) = lockBuffer_DEPRECATED;
            if (dequeueBuffer_DEPRECATED) *(void**)((char*)window + 0x48) = dequeueBuffer_DEPRECATED;
            if (queueBuffer_DEPRECATED) *(void**)((char*)window + 0x50) = queueBuffer_DEPRECATED;
            if (cancelBuffer_DEPRECATED) *(void**)((char*)window + 0x58) = cancelBuffer_DEPRECATED;
            if (query) *(void**)((char*)window + 0x60) = query;
            if (perform) *(void**)((char*)window + 0x68) = perform;
            
            fprintf(stderr, "[PATCH] after: 0x38=%p 0x40=%p 0x48=%p 0x50=%p 0x58=%p 0x60=%p 0x68=%p\n",
                *(void**)((char*)window + 0x38),
                *(void**)((char*)window + 0x40),
                *(void**)((char*)window + 0x48),
                *(void**)((char*)window + 0x50),
                *(void**)((char*)window + 0x58),
                *(void**)((char*)window + 0x60),
                *(void**)((char*)window + 0x68));
            
            dlclose(handle);
        } else {
            fprintf(stderr, "[PATCH] FAIL dlopen platformcommon.so\n");
        }
    }
};
