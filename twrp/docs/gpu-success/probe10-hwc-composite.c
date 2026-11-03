// probe 10: hwcomposer HAL 直接合成 gralloc buffer（绕过 GPU 渲染，验证 hwcomposer 合成能力）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hybris/gralloc/gralloc.h>

#define P(msg) fprintf(stderr, "[P10] %s\n", msg); fflush(stderr);

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1080;
    int H = argc > 2 ? atoi(argv[2]) : 1920;
    int frames = argc > 3 ? atoi(argv[3]) : 30;
    fprintf(stderr, "[P10] %dx%d %d frames\n", W, H, frames);

    // 1. hwcomposer open
    const hw_module_t *module = NULL;
    int ret = hw_get_module(HWC_HARDWARE_MODULE_ID, &module);
    P("hw_get_module done");
    if (ret < 0) { fprintf(stderr, "FAIL hw_get_module err=%d\n", ret); return 1; }
    fprintf(stderr, "[P10] module version=%d\n", module->module_api_version);

    hwc_composer_device_1_t *hwc = NULL;
    ret = module->methods->open(module, HWC_HARDWARE_COMPOSER, (struct hw_device_t**)&hwc);
    P("open done");
    if (ret < 0) { fprintf(stderr, "FAIL open err=%d\n", ret); return 2; }
    fprintf(stderr, "[P10] hwc version=%d\n", hwc->common.version);

    // 2. registerProcs (hwcomposer 需要)
    struct hwc_procs procs;
    memset(&procs, 0, sizeof(procs));
    if (hwc->registerProcs) {
        hwc->registerProcs(hwc, &procs);
        P("registerProcs done");
    }

    // 3. gralloc allocate (CPU 填充 + HW_COMPOSER)
    hybris_gralloc_initialize(0);
    buffer_handle_t handle = NULL; uint32_t stride = 0;
    // GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_SW_WRITE_RARELY (CPU 可写)
    int usage = 0x800 | 0x20;
    ret = hybris_gralloc_allocate(W, H, 1, usage, &handle, &stride);
    P("gralloc_allocate done");
    if (ret < 0) { fprintf(stderr, "FAIL allocate err=%d\n", ret); return 3; }
    fprintf(stderr, "[P10] stride=%u handle=%p\n", stride, handle);

    // 4. lock + fill (CPU 填充 buffer)
    void* vaddr = NULL;
    ret = hybris_gralloc_lock(handle, usage, 0, 0, W, H, &vaddr);
    P("gralloc_lock done");
    if (ret < 0) { fprintf(stderr, "FAIL lock err=%d\n", ret); return 4; }
    fprintf(stderr, "[P10] vaddr=%p (non-NULL=%s)\n", vaddr, vaddr ? "YES" : "NO");

    if (vaddr) {
        // fill with pattern (CPU)
        unsigned int* pixels = (unsigned int*)vaddr;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                unsigned char r = (x * 255) / W;
                unsigned char g = (y * 255) / H;
                unsigned char b = 128;
                pixels[y * stride + x] = (255 << 24) | (b << 16) | (g << 8) | r;
            }
        }
        P("fill buffer OK (CPU)");
    }

    hybris_gralloc_unlock(handle);
    P("unlock done");

    // 5. hwcomposer prepare + set
    hwc_display_contents_1_t *disp = (hwc_display_contents_1_t*)malloc(sizeof(hwc_display_contents_1_t) + sizeof(hwc_layer_1_t));
    hwc_layer_1_t *layer = &disp->hwLayers[0];
    memset(disp, 0, sizeof(hwc_display_contents_1_t) + sizeof(hwc_layer_1_t));

    layer->compositionType = HWC_FRAMEBUFFER;
    layer->handle = handle;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop.right = W;
    layer->sourceCrop.bottom = H;
    layer->displayFrame.right = W;
    layer->displayFrame.bottom = H;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;

    disp->numHwLayers = 1;
    disp->flags = HWC_GEOMETRY_CHANGED;
    disp->retireFenceFd = -1;

    P("calling prepare");
    ret = hwc->prepare(hwc, 1, &disp);
    P("prepare done");
    fprintf(stderr, "[P10] prepare ret=%d\n", ret);
    if (ret < 0) goto fail;

    P("calling set");
    ret = hwc->set(hwc, 1, &disp);
    P("set done");
    fprintf(stderr, "[P10] set ret=%d\n", ret);
    if (ret < 0) goto fail;

    // 6. render loop (measure FPS)
    P("render loop start");
    for (int i = 0; i < frames; i++) {
        // modify buffer (CPU)
        if (vaddr) {
            unsigned int* pixels = (unsigned int*)vaddr;
            hybris_gralloc_lock(handle, usage, 0, 0, W, H, &vaddr);
            unsigned char r = (i * 255) / frames;
            for (int j = 0; j < stride * H; j++) {
                pixels[j] = (255 << 24) | (128 << 16) | (r << 8) | r;
            }
            hybris_gralloc_unlock(handle);
        }

        layer->acquireFenceFd = -1;
        layer->releaseFenceFd = -1;
        disp->flags = 0; // no geometry change

        ret = hwc->prepare(hwc, 1, &disp);
        ret = hwc->set(hwc, 1, &disp);
        if (ret < 0) { fprintf(stderr, "[P10] set FAIL at frame %d\n", i); break; }

        if (i % 10 == 0) fprintf(stderr, "[P10] frame %d composited\n", i);
    }
    P("render loop done");

    fprintf(stderr, "[P10] === hwcomposer HAL 合成 OK ===\n");

fail:
    free(disp);
    hybris_gralloc_release(handle, 1);
    hybris_gralloc_deinitialize();
    hwc->common.close((struct hw_device_t*)hwc);
    return 0;
}
