// 探测：零拷贝路径可行性（不实际渲染，只探测 API 可用性）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <hybris/gralloc/gralloc.h>

// native_handle_t from header

int main() {
    printf("=== probe zero-copy path feasibility ===\n");

    // 0. EGL init
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { printf("FAIL eglGetDisplay\n"); return 1; }
    if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize\n"); return 1; }
    printf("EGL init OK: dpy=%p\n", dpy);

    // 1. 探测 eglCreateImageKHR 能否拿到（被 OVERRIDE_MY 包装，但应该能 eglGetProcAddress）
    PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    printf("eglCreateImageKHR: %p (via eglGetProcAddress)\n", p_eglCreateImageKHR);

    PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    printf("eglDestroyImageKHR: %p\n", p_eglDestroyImageKHR);

    // 2. 探测 glEGLImageTargetTexture2DOES / RenderbufferStorageOES
    void* p_glEGLImageTargetTexture2DOES = eglGetProcAddress("glEGLImageTargetTexture2DOES");
    printf("glEGLImageTargetTexture2DOES: %p\n", p_glEGLImageTargetTexture2DOES);
    void* p_glEGLImageTargetRenderbufferStorageOES = eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
    printf("glEGLImageTargetRenderbufferStorageOES: %p\n", p_glEGLImageTargetRenderbufferStorageOES);

    // 3. 探测 eglCreatePbufferFromClientBuffer（直通）
    printf("eglCreatePbufferFromClientBuffer: %p (direct link)\n", (void*)eglCreatePbufferFromClientBuffer);

    // 4. EGLImage extension 支持？
    const char* exts = eglQueryString(dpy, EGL_EXTENSIONS);
    if (exts) {
        printf("EGL_KHR_image_base: %s\n", strstr(exts, "EGL_KHR_image_base") ? "YES" : "NO");
        printf("EGL_KHR_image: %s\n", strstr(exts, "EGL_KHR_image ") ? "YES" : "NO");
        printf("EGL_ANDROID_image_native_buffer: %s\n", strstr(exts, "EGL_ANDROID_image_native_buffer") ? "YES" : "NO");
        printf("EGL_ANDROID_native_fence: %s\n", strstr(exts, "EGL_ANDROID_native_fence") ? "YES" : "NO");
    }

    // 5. gralloc 分配（用 GRALLOC_USAGE_HW_RENDER | HW_COMPOSER）
    hybris_gralloc_initialize(0);
    buffer_handle_t handle = NULL;
    uint32_t stride = 0;
    int W = 512, H = 512;
    int usage = 0x100 | 0x800 | 0x1000; // HW_RENDER | HW_COMPOSER | HW_FB (approx)
    int ret = hybris_gralloc_allocate(W, H, 1, usage, &handle, &stride);
    printf("hybris_gralloc_allocate: ret=%d stride=%u handle=%p\n", ret, stride, handle);
    if (ret < 0) { printf("FAIL allocate\n"); return 2; }

    // 6. 构造 ANativeWindowBuffer 包 gralloc handle
    // ANativeWindowBuffer { ANativeWindowBuffer_t (deprecated), int width, height, stride, format, usage, buffer_handle_t handle, void* reserved[2] }
    // 简化：直接传 handle 当 client buffer，看 eglCreateImageKHR 接不接受
    printf("\n=== probe: eglCreateImageKHR with raw native_handle_t ===\n");
    if (p_eglCreateImageKHR) {
        // 需要 config
        EGLConfig config;
        EGLint numConfigs;
        EGLint cfgAttrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
        eglChooseConfig(dpy, cfgAttrs, &config, 1, &numConfigs);
        printf("config=%p numConfigs=%d\n", config, numConfigs);

        // attrib: EGL_NATIVE_BUFFER_ANDROID 需要 ANativeWindowBuffer*，但我们传 native_handle_t
        // 试 EGL_ANDROID_image_native_buffer target
        EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)handle, NULL);
        printf("eglCreateImageKHR(raw handle): img=%p\n", img);
        if (img != EGL_NO_IMAGE_KHR && p_eglDestroyImageKHR) {
            p_eglDestroyImageKHR(dpy, img);
        }
    }

    hybris_gralloc_release(handle, 1);
    hybris_gralloc_deinitialize();
    printf("=== probe done ===\n");
    return 0;
}
