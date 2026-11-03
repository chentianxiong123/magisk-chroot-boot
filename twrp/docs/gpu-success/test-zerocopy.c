// 真零拷贝：GPU 渲染到 gralloc buffer → hwcomposer 合成到屏幕
// 路径: gralloc → EGLImage → texture → FBO → GPU render → hwcomposer prepare/set
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// glEGLImageTargetTexture2DOES 声明
extern void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hybris/gralloc/gralloc.h>

static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

// GL shader
static const char* vsrc = "attribute vec2 p; void main(){ gl_Position=vec4(p,0.0,1.0); }";
static const char* fsrc = "precision mediump float; uniform float t; void main(){ gl_FragColor=vec4(abs(sin(t)), abs(sin(t*1.3)), abs(sin(t*0.7)), 1.0); }";

static GLuint mk(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); fprintf(stderr, "shader err: %s\n", log); }
    return s;
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 512;
    int H = argc > 2 ? atoi(argv[2]) : 512;
    int frames = argc > 3 ? atoi(argv[3]) : 60;
    fprintf(stderr, "[ZC] %dx%d %d frames\n", W, H, frames);

    // ===== 1. EGL + pbuffer context =====
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    EGLint pbAttr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLSurface pbuf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, pbuf, pbuf, ctx);
    fprintf(stderr, "[ZC] pbuffer context OK: %dx%d\n", 64, 64);

    // ===== 2. gralloc 分配（HW_RENDER | HW_COMPOSER）=====
    hybris_gralloc_initialize(0);
    buffer_handle_t handle = NULL; uint32_t stride = 0;
    int usage = 0x100 | 0x800 | 0x1000; // HW_RENDER | HW_COMPOSER | HW_FB
    int ret = hybris_gralloc_allocate(W, H, 1, usage, &handle, &stride);
    if (ret < 0) { fprintf(stderr, "[ZC] FAIL allocate\n"); return 1; }
    fprintf(stderr, "[ZC] gralloc allocate OK: stride=%u handle=%p\n", stride, handle);

    // ===== 3. gralloc → EGLImage =====
    p_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    p_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)handle, NULL);
    if (img == EGL_NO_IMAGE_KHR) { fprintf(stderr, "[ZC] FAIL eglCreateImageKHR\n"); return 2; }
    fprintf(stderr, "[ZC] eglCreateImageKHR OK: img=%p\n", img);

    // ===== 4. EGLImage → texture → FBO =====
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);

    GLuint fbo; glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "[ZC] FAIL FBO incomplete 0x%x\n", fboStatus); return 3; }
    fprintf(stderr, "[ZC] FBO OK (GPU render target = gralloc buffer)\n");

    // ===== 5. shader =====
    GLuint v = mk(GL_VERTEX_SHADER, vsrc), f = mk(GL_FRAGMENT_SHADER, fsrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v); glAttachShader(prog, f); glLinkProgram(prog);
    GLint a_p = glGetAttribLocation(prog, "p");
    GLint u_t = glGetUniformLocation(prog, "t");
    glUseProgram(prog);

    // viewport 到 FBO（gralloc buffer 尺寸）
    glViewport(0, 0, W, H);

    // ===== 6. hwcomposer =====
    const hw_module_t *hwcModule = NULL;
    hw_get_module(HWC_HARDWARE_MODULE_ID, &hwcModule);
    hwc_composer_device_1_t *hwc = NULL;
    hwcModule->methods->open(hwcModule, HWC_HARDWARE_COMPOSER, (struct hw_device_t**)&hwc);
    struct hwc_procs procs; memset(&procs, 0, sizeof(procs));
    if (hwc->registerProcs) hwc->registerProcs(hwc, &procs);
    fprintf(stderr, "[ZC] hwcomposer open OK\n");

    // ===== 7. 渲染循环（真零拷贝）=====
    GLfloat quad[] = { -1,-1, 1,-1, 1,1, -1,1 };
    glEnableVertexAttribArray(a_p);
    glVertexAttribPointer(a_p, 2, GL_FLOAT, GL_FALSE, 0, quad);

    // allocate display contents (flexible array)
    hwc_display_contents_1_t *disp = (hwc_display_contents_1_t*)malloc(sizeof(hwc_display_contents_1_t) + sizeof(hwc_layer_1_t));
    hwc_layer_1_t *layer = &disp->hwLayers[0];
    memset(disp, 0, sizeof(hwc_display_contents_1_t) + sizeof(hwc_layer_1_t));

    for (int i = 0; i < frames; i++) {
        float t = i / 60.0f;
        glUniform1f(u_t, t);

        // GPU 渲染到 gralloc-backed FBO（真零拷贝，无 CPU 读回）
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glFinish();

        // hwcomposer prepare + set（gralloc handle → 屏幕）
        layer->compositionType = HWC_FRAMEBUFFER;
        layer->handle = handle;
        layer->blending = HWC_BLENDING_NONE;
        layer->sourceCrop.right = W; layer->sourceCrop.bottom = H;
        layer->displayFrame.right = W; layer->displayFrame.bottom = H;
        layer->acquireFenceFd = -1; layer->releaseFenceFd = -1;
        disp->numHwLayers = 1;
        disp->flags = HWC_GEOMETRY_CHANGED;
        disp->retireFenceFd = -1;

        int r = hwc->prepare(hwc, 1, &disp);
        if (r < 0) { fprintf(stderr, "[ZC] prepare FAIL %d\n", r); break; }
        r = hwc->set(hwc, 1, &disp);
        if (r < 0) { fprintf(stderr, "[ZC] set FAIL %d\n", r); break; }

        if (i % 10 == 0) fprintf(stderr, "[ZC] frame %d rendered+composited\n", i);
    }

    fprintf(stderr, "[ZC] === 真零拷贝完成: GPU→gralloc→hwcomposer→屏幕 ===\n");

    // cleanup
    free(disp);
    hybris_gralloc_release(handle, 1);
    p_eglDestroyImageKHR(dpy, img);
    hybris_gralloc_deinitialize();
    hwc->common.close((struct hw_device_t*)hwc);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, pbuf);
    return 0;
}
