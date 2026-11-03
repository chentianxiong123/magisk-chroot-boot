// probe 12: 真 ION 路径（绕过 gralloc，直接 ioctl 分配 ION buffer → Mali import）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <errno.h>
#include <cutils/native_handle.h>

// ION ioctl (from kernel ion.h)
#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE  _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_MAP   _IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)
#define ION_IOC_SHARE _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_IMPORT _IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data)

struct ion_allocation_data {
    size_t len;
    size_t align;
    unsigned int heap_id_mask;
    unsigned int flags;
    int handle;
};

struct ion_fd_data {
    int handle;
    int fd;
};

struct ion_handle_data {
    int handle;
};

// ION heap IDs (MTK)
#define ION_HEAP_SYSTEM_MASK   (1 << 0)
#define ION_HEAP_MM_MASK       (1 << 1)  // MTK multimedia heap (GPU accessible)
#define ION_HEAP_MTK_MM_MASK   (1 << 3)  // MTK specific

extern void glEGLImageTargetTexture2DOES(GLenum target, GLeglImageOES image);
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;

int main() {
    printf("=== probe 12: direct ION alloc + Mali import ===\n");

    // 1. open /dev/ion
    int ion_fd = open("/dev/ion", O_RDONLY);
    printf("open /dev/ion: fd=%d\n", ion_fd);
    if (ion_fd < 0) { printf("FAIL open ion\n"); return 1; }

    // 2. alloc ION buffer (1MB, MM heap for GPU)
    struct ion_allocation_data alloc;
    memset(&alloc, 0, sizeof(alloc));
    alloc.len = 1024 * 1024;
    alloc.align = 4096;
    alloc.heap_id_mask = ION_HEAP_MM_MASK | ION_HEAP_SYSTEM_MASK; // try MM + system
    alloc.flags = 0; // uncached or cached?
    int ret = ioctl(ion_fd, ION_IOC_ALLOC, &alloc);
    printf("ION_IOC_ALLOC: ret=%d handle=%d\n", ret, alloc.handle);
    if (ret < 0) {
        // try system heap only
        alloc.heap_id_mask = ION_HEAP_SYSTEM_MASK;
        ret = ioctl(ion_fd, ION_IOC_ALLOC, &alloc);
        printf("ION_IOC_ALLOC (system): ret=%d handle=%d\n", ret, alloc.handle);
        if (ret < 0) { printf("FAIL alloc\n"); return 2; }
    }

    // 3. get fd (share)
    struct ion_fd_data fd_data;
    memset(&fd_data, 0, sizeof(fd_data));
    fd_data.handle = alloc.handle;
    ret = ioctl(ion_fd, ION_IOC_SHARE, &fd_data);
    printf("ION_IOC_SHARE: ret=%d fd=%d\n", ret, fd_data.fd);
    if (ret < 0) { printf("FAIL share\n"); return 3; }
    int buf_fd = fd_data.fd;

    // 4. mmap (CPU access)
    void* vaddr = mmap(NULL, alloc.len, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
    printf("mmap: vaddr=%p (MAP_FAILED=%p)\n", vaddr, MAP_FAILED);
    if (vaddr == MAP_FAILED) {
        printf("mmap FAILED, errno=%d\n", errno);
    } else {
        // fill
        memset(vaddr, 0x80, alloc.len);
        printf("fill buffer OK\n");
    }

    // 5. construct native_handle_t wrapping the fd
    // native_handle_t { int version; int numFds; int numInts; int data[] }
    // data[0] = fd, then numInts ints (offset, size, flags etc)
    int numFds = 1;
    int numInts = 3; // offset, size, flags (simplified)
    int data_size = sizeof(native_handle_t) + (numFds + numInts) * sizeof(int);
    native_handle_t* nh = (native_handle_t*)malloc(data_size);
    nh->version = sizeof(native_handle_t);
    nh->numFds = numFds;
    nh->numInts = numInts;
    int* data = nh->data;
    data[0] = buf_fd; // fd
    data[1] = 0; // offset
    data[2] = alloc.len; // size
    data[3] = 0; // flags
    printf("native_handle_t: version=%d numFds=%d numInts=%d data[0]=%d\n", nh->version, nh->numFds, nh->numInts, data[0]);

    // 6. EGL init (surfaceless)
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint nc;
    EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, ca, &cfg, 1, &nc);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, (EGLint[]){ EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE });
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    printf("surfaceless context OK\n");

    p_eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    p_eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    // 7. eglCreateImageKHR with raw native_handle_t (not gralloc, our own)
    printf("\n*** eglCreateImageKHR(raw ION native_handle) ***\n"); fflush(stdout);
    EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)nh, NULL);
    printf("img=%p (EGL_NO_IMAGE_KHR=%p)\n", img, EGL_NO_IMAGE_KHR);
    if (img == EGL_NO_IMAGE_KHR) {
        printf("FAIL create image\n");
        goto done;
    }

    // 8. bind as texture
    printf("*** glEGLImageTargetTexture2DOES (will Mali import?) ***\n"); fflush(stdout);
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
    printf("survived bind\n");
    GLenum err = glGetError();
    printf("GL error: 0x%x\n", err);

    // 9. FBO
    GLuint fbo; glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("FBO status: 0x%x (%s)\n", fboStatus, fboStatus == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE");

    if (fboStatus == GL_FRAMEBUFFER_COMPLETE) {
        printf("\n*** ZERO-COPY RENDER TARGET OK (GPU renders to ION buffer) ***\n");
        // render
        glClearColor(1.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        printf("GPU rendered to ION buffer OK\n");

        // verify (CPU read back from mmap)
        if (vaddr != MAP_FAILED) {
            unsigned int* px = (unsigned int*)vaddr;
            printf("pixel[0]=0x%08x (expect 0xFF0000FF red)\n", px[0]);
        }
    }

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    p_eglDestroyImageKHR(dpy, img);

done:
    free(nh);
    if (vaddr != MAP_FAILED) munmap(vaddr, alloc.len);
    close(buf_fd);
    // free ION handle
    struct ion_handle_data free_data;
    memset(&free_data, 0, sizeof(free_data));
    free_data.handle = alloc.handle;
    ioctl(ion_fd, ION_IOC_FREE, &free_data);
    close(ion_fd);
    printf("=== probe 12 done ===\n");
    return 0;
}
