// probe 11: hwcomposer prepare 带 framebuffer target layer
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hybris/gralloc/gralloc.h>

#define P(msg) fprintf(stderr, "[P11] %s\n", msg); fflush(stderr);

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1080;
    int H = argc > 2 ? atoi(argv[2]) : 1920;
    P("start");

    const hw_module_t *module = NULL;
    int ret = hw_get_module(HWC_HARDWARE_MODULE_ID, &module);
    P("hw_get_module done");
    if (ret < 0) { fprintf(stderr, "FAIL hw_get_module err=%d\n", ret); return 1; }

    hwc_composer_device_1_t *hwc = NULL;
    ret = module->methods->open(module, HWC_HARDWARE_COMPOSER, (struct hw_device_t**)&hwc);
    P("open done");
    if (ret < 0) { fprintf(stderr, "FAIL open err=%d\n", ret); return 2; }
    fprintf(stderr, "[P11] hwc version=%d\n", hwc->common.version);

    struct hwc_procs procs;
    memset(&procs, 0, sizeof(procs));
    if (hwc->registerProcs) hwc->registerProcs(hwc, &procs);
    P("registerProcs done");

    hybris_gralloc_initialize(0);
    buffer_handle_t handle = NULL; uint32_t stride = 0;
    int usage = 0x800 | 0x20; // HW_COMPOSER | SW_WRITE_RARELY
    ret = hybris_gralloc_allocate(W, H, 1, usage, &handle, &stride);
    P("gralloc_allocate done");
    if (ret < 0) { fprintf(stderr, "FAIL allocate err=%d\n", ret); return 3; }

    void* vaddr = NULL;
    ret = hybris_gralloc_lock(handle, usage, 0, 0, W, H, &vaddr);
    P("gralloc_lock done");
    if (vaddr) {
        unsigned int* pixels = (unsigned int*)vaddr;
        for (int j = 0; j < stride * H; j++) pixels[j] = 0xFF0000FF; // red
        hybris_gralloc_unlock(handle);
    }
    P("fill done");

    // ===== 2 layers: framebuffer + framebuffer_target =====
    // hwcomposer 1.1+ needs HWC_FRAMEBUFFER_TARGET layer
    hwc_display_contents_1_t *disp = (hwc_display_contents_1_t*)malloc(sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t));
    memset(disp, 0, sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t));
    hwc_layer_1_t *layers = &disp->hwLayers[0];

    // layer 0: framebuffer (overlay source)
    layers[0].compositionType = HWC_FRAMEBUFFER;
    layers[0].handle = handle;
    layers[0].blending = HWC_BLENDING_NONE;
    layers[0].sourceCrop.right = W; layers[0].sourceCrop.bottom = H;
    layers[0].displayFrame.right = W; layers[0].displayFrame.bottom = H;
    layers[0].acquireFenceFd = -1; layers[0].releaseFenceFd = -1;

    // layer 1: framebuffer target (GPU render target)
    layers[1].compositionType = HWC_FRAMEBUFFER_TARGET;
    layers[1].handle = handle; // same buffer (simplified)
    layers[1].blending = HWC_BLENDING_NONE;
    layers[1].sourceCrop.right = W; layers[1].sourceCrop.bottom = H;
    layers[1].displayFrame.right = W; layers[1].displayFrame.bottom = H;
    layers[1].acquireFenceFd = -1; layers[1].releaseFenceFd = -1;

    disp->numHwLayers = 2;
    disp->flags = HWC_GEOMETRY_CHANGED;
    disp->retireFenceFd = -1;

    P("calling prepare (2 layers: FB + FB_TARGET)");
    ret = hwc->prepare(hwc, 1, &disp);
    P("prepare done");
    fprintf(stderr, "[P11] prepare ret=%d\n", ret);
    if (ret < 0) goto fail;

    fprintf(stderr, "[P11] layer 0 compositionType after prepare: %d (HWC_FRAMEBUFFER=%d, HWC_OVERLAY=%d)\n", 
            layers[0].compositionType, HWC_FRAMEBUFFER, HWC_OVERLAY);
    fprintf(stderr, "[P11] layer 1 compositionType after prepare: %d\n", layers[1].compositionType);

    P("calling set");
    ret = hwc->set(hwc, 1, &disp);
    P("set done");
    fprintf(stderr, "[P11] set ret=%d\n", ret);
    if (ret < 0) goto fail;

    P("sleep 100ms");
    usleep(100000);

    fprintf(stderr, "[P11] === hwcomposer prepare/set OK (2 layers) ===\n");

fail:
    free(disp);
    hybris_gralloc_release(handle, 1);
    hybris_gralloc_deinitialize();
    hwc->common.close((struct hw_device_t*)hwc);
    return 0;
}
