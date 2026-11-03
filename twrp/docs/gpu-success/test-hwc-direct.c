#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hybris/gralloc/gralloc.h>

#define P(msg) fprintf(stderr, "[THD] %s\n", msg); fflush(stderr);

int main() {
    P("start");
    const hw_module_t *module = NULL;
    int ret = hw_get_module(HWC_HARDWARE_MODULE_ID, &module);
    P("hw_get_module done");
    if (ret < 0) { fprintf(stderr, "FAIL hw_get_module err=%d\n", ret); return 1; }
    fprintf(stderr, "[THD] module version=%d\n", module->module_api_version);

    hwc_composer_device_1_t *hwc = NULL;
    ret = module->methods->open(module, HWC_HARDWARE_COMPOSER, (struct hw_device_t**)&hwc);
    P("open done");
    if (ret < 0) { fprintf(stderr, "FAIL open err=%d\n", ret); return 2; }
    fprintf(stderr, "[THD] hwc version=%d\n", hwc->common.version);

    // registerProcs (hwcomposer 需要)
    struct hwc_procs procs;
    memset(&procs, 0, sizeof(procs));
    if (hwc->registerProcs) {
        hwc->registerProcs(hwc, &procs);
        P("registerProcs done");
    }

    hybris_gralloc_initialize(0);
    P("gralloc init done");

    buffer_handle_t handle = NULL;
    uint32_t stride = 0;
    int W = 1080, H = 1920;
    int usage = GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_HW_RENDER;
    ret = hybris_gralloc_allocate(W, H, 1, usage, &handle, &stride);
    P("gralloc_allocate done");
    if (ret < 0) { fprintf(stderr, "FAIL allocate err=%d\n", ret); return 3; }
    fprintf(stderr, "[THD] stride=%u handle=%p\n", stride, handle);

    void* vaddr = NULL;
    ret = hybris_gralloc_lock(handle, usage, 0, 0, W, H, &vaddr);
    P("gralloc_lock done");
    if (ret < 0) { fprintf(stderr, "FAIL lock err=%d\n", ret); return 4; }
    fprintf(stderr, "[THD] vaddr=%p\n", vaddr);

    unsigned int* pixels = (unsigned int*)vaddr;
    for (int i = 0; i < stride * H; i++) {
        pixels[i] = 0xFF804020; // orange-ish
    }
    P("fill done");

    hybris_gralloc_unlock(handle);
    P("unlock done");

    hwc_display_contents_1_t *display = malloc(sizeof(hwc_display_contents_1_t) + sizeof(hwc_layer_1_t));
    hwc_layer_1_t *layer = &display->hwLayers[0];
    memset(display, 0, sizeof(hwc_display_contents_1_t) + sizeof(hwc_layer_1_t));

    layer->compositionType = HWC_FRAMEBUFFER;
    layer->handle = handle;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop.right = W;
    layer->sourceCrop.bottom = H;
    layer->displayFrame.right = W;
    layer->displayFrame.bottom = H;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;

    display->numHwLayers = 1;
    display->flags = HWC_GEOMETRY_CHANGED;
    display->retireFenceFd = -1;

    P("calling prepare");
    ret = hwc->prepare(hwc, 1, &display);
    P("prepare done");
    fprintf(stderr, "[THD] prepare ret=%d\n", ret);

    P("calling set");
    ret = hwc->set(hwc, 1, &display);
    P("set done");
    fprintf(stderr, "[THD] set ret=%d\n", ret);

    // wait a frame
    usleep(100000);
    P("sleep done");

    hybris_gralloc_release(handle, 1);
    hybris_gralloc_deinitialize();
    hwc->common.close((struct hw_device_t*)hwc);

    P("ALL OK");
    return 0;
}
