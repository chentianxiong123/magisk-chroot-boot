// bridge_server.cpp — GPU 桥接服务（bionic，跑在 chroot /system）
// Debian 应用经 unix socket 发帧 → SurfaceComposerClient → SF → hwcomposer → 屏幕
//
// 手工声明 libgui/libui 的 C++ ABI（符号名从 libgui.so 确认）
// 编译（NDK，链接设备 libs）:
//   clang++ bridge_server.cpp -o bridge_server \
//     -I<ndk>/sysroot/usr/include -I<ndk>/sysroot/usr/include/aarch64-linux-android \
//     -L/tmp/android-libs -lgui -lui -lbinder -lutils -lcutils -llog -landroid \
//     -Wl,-rpath,/system/lib64 -Wl,-z,max-page-size=4096

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <pthread.h>
#include <android/native_window.h>

// ============ Android ABI 手工声明 ============
namespace android {
    // 基础
    class String8 {
    public:
        String8();
        String8(const char* s);
        ~String8();
        // 实际是 RefBase 指针，这里按 libgui ABI 用 void* 占位
        void* mData;
    };
    template<typename T> class sp {
    public:
        T* p;
        sp() : p(nullptr) {}
        sp(T* x) : p(x) {}
        sp(const sp& o) : p(o.p) {}
        ~sp() {}
        T* get() const { return p; }
        T* operator->() const { return p; }
    };
    class IBinder { public: virtual ~IBinder(); };
    class Surface;
    class SurfaceControl;
    class SurfaceComposerClient;

    // Surface 方法（libgui）
    class Surface {
    public:
        // ANativeWindow* getNativeWindow() const;
        // 实际 Surface 里有 ANativeWindow，通过 _ZN7android7Surface15getNativeWindowEv
    };

    // SurfaceControl
    class SurfaceControl {
    public:
        sp<Surface> getSurface() const;
        void setLayer(unsigned int layer);
        void show();
        void hide();
    };

    // SurfaceComposerClient
    class SurfaceComposerClient {
    public:
        SurfaceComposerClient();
        sp<SurfaceControl> createSurface(const String8& name,
            unsigned int w, unsigned int h, int format, unsigned int flags);
        static int getBuiltInDisplay(int);
        static void openGlobalTransaction();
        static void closeGlobalTransaction(bool sync);
        void setLayer(const sp<IBinder>&, unsigned int);
        void setLayerStack(const sp<IBinder>&, unsigned int);
    };
}

// ============ 帧接收 + 显示 ============
int main(int argc, char** argv) {
    const char* sock = argc > 1 ? argv[1] : "/data/local/tmp/bridge.sock";
    int W = argc > 2 ? atoi(argv[2]) : 1080;
    int H = argc > 3 ? atoi(argv[3]) : 1920;
    printf("[bridge] start sock=%s %dx%d\n", sock, W, H); fflush(stdout);

    // 建 surface（照 bootanimation）
    android::SurfaceComposerClient client;
    android::String8 name("bridge_surface");
    android::sp<android::SurfaceControl> sc =
        client.createSurface(name, W, H, 1 /*RGBA_8888*/, 0);
    if (!sc.get()) { printf("[bridge] createSurface FAIL\n"); return 1; }
    printf("[bridge] createSurface OK\n"); fflush(stdout);

    android::SurfaceComposerClient::openGlobalTransaction();
    sc->setLayer(0x40000000);  // 顶层
    android::SurfaceComposerClient::closeGlobalTransaction(true);
    printf("[bridge] surface configured\n"); fflush(stdout);

    android::sp<android::Surface> surf = sc->getSurface();
    if (!surf.get()) { printf("[bridge] getSurface FAIL\n"); return 2; }
    printf("[bridge] getSurface OK\n"); fflush(stdout);

    ANativeWindow* win = (ANativeWindow*)surf.get();
    printf("[bridge] window=%p 就绪，等待帧...\n", (void*)win); fflush(stdout);

    // 监听 socket（完整帧接收逻辑需 libgui 的 ANativeWindow 接口，此处骨架就绪）
    printf("[bridge] 桥接服务骨架就绪（帧接收待补）\n");
    return 0;
}
