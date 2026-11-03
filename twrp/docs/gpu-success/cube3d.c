#define _GNU_SOURCE
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <unistd.h>

static const char* vsrc = 
"attribute vec3 pos;\n"
"attribute vec3 normal;\n"
"attribute vec2 texcoord;\n"
"uniform mat4 mvp;\n"
"uniform mat4 model;\n"
"varying vec3 v_normal;\n"
"varying vec2 v_texcoord;\n"
"varying vec3 v_pos;\n"
"void main() {\n"
"    gl_Position = mvp * vec4(pos, 1.0);\n"
"    v_normal = mat3(model) * normal;\n"
"    v_texcoord = texcoord;\n"
"    v_pos = (model * vec4(pos, 1.0)).xyz;\n"
"}";

static const char* fsrc = 
"precision mediump float;\n"
"uniform float u_time;\n"
"varying vec3 v_normal;\n"
"varying vec2 v_texcoord;\n"
"varying vec3 v_pos;\n"
"void main() {\n"
"    vec3 light_dir = normalize(vec3(1.0, 1.0, 1.0));\n"
"    vec3 normal = normalize(v_normal);\n"
"    float diffuse = max(dot(normal, light_dir), 0.0);\n"
"    float specular = pow(diffuse, 32.0);\n"
"    vec3 color = vec3(0.2 + 0.3 * diffuse + 0.5 * specular);\n"
"    color *= vec3(0.5 + 0.5 * sin(u_time + v_pos.x * 5.0),\n"
"                  0.5 + 0.5 * sin(u_time + v_pos.y * 5.0),\n"
"                  0.5 + 0.5 * sin(u_time + v_pos.z * 5.0));\n"
"    gl_FragColor = vec4(color, 1.0);\n"
"}";

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

// Cube vertices (position + normal + texcoord)
static const GLfloat cube[] = {
    // Front face
    -1,-1,1, 0,0,1, 0,0, 1,-1,1, 0,0,1, 1,0, 1,1,1, 0,0,1, 1,1,
    -1,1,1, 0,0,1, 0,1, -1,-1,1, 0,0,1, 0,0, 1,1,1, 0,0,1, 1,1,
    // Back face
    -1,-1,-1, 0,0,-1, 0,0, -1,1,-1, 0,0,-1, 0,1, 1,1,-1, 0,0,-1, 1,1,
    1,-1,-1, 0,0,-1, 1,0, -1,-1,-1, 0,0,-1, 0,0, 1,1,-1, 0,0,-1, 1,1,
    // Top face
    -1,1,-1, 0,1,0, 0,0, -1,1,1, 0,1,0, 0,1, 1,1,1, 0,1,0, 1,1,
    1,1,-1, 0,1,0, 1,0, -1,1,-1, 0,1,0, 0,0, 1,1,1, 0,1,0, 1,1,
    // Bottom face
    -1,-1,-1, 0,-1,0, 0,0, 1,-1,-1, 0,-1,0, 1,0, 1,-1,1, 0,-1,0, 1,1,
    -1,-1,1, 0,-1,0, 0,1, -1,-1,-1, 0,-1,0, 0,0, 1,-1,1, 0,-1,0, 1,1,
    // Right face
    1,-1,-1, 1,0,0, 0,0, 1,1,-1, 1,0,0, 0,1, 1,1,1, 1,0,0, 1,1,
    1,-1,1, 1,0,0, 1,0, 1,-1,-1, 1,0,0, 0,0, 1,1,1, 1,0,0, 1,1,
    // Left face
    -1,-1,-1, -1,0,0, 0,0, -1,-1,1, -1,0,0, 1,0, -1,1,1, -1,0,0, 1,1,
    -1,1,-1, -1,0,0, 0,1, -1,-1,-1, -1,0,0, 0,0, -1,1,1, -1,0,0, 1,1,
};

int main() {
    // 1. Open X11 display
    Display* xdisplay = XOpenDisplay(":0");
    if (!xdisplay) { printf("FAIL: XOpenDisplay\n"); return 1; }
    int screen = DefaultScreen(xdisplay);
    Window root = RootWindow(xdisplay, screen);
    int W = 256, H = 256;
    Window window = XCreateSimpleWindow(xdisplay, root, 100, 100, W, H, 1, BlackPixel(xdisplay, screen), WhitePixel(xdisplay, screen));
    XStoreName(xdisplay, window, "Mali-T860 GPU 3D Cube");
    XSelectInput(xdisplay, window, ExposureMask | KeyPressMask);
    XMapWindow(xdisplay, window);
    XFlush(xdisplay);
    printf("X11 window: OK\n");
    XImage* ximage = XCreateImage(xdisplay, DefaultVisual(xdisplay, screen), 24, ZPixmap, 0, NULL, W, H, 32, 0);
    ximage->data = (char*)malloc(W * H * 4);

    // 2. EGL + Mali GPU
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, attrs, &cfg, 1, &n);
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    EGLint pattribs[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    eglMakeCurrent(dpy, surf, surf, ctx);
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));

    // 3. Shaders
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { printf("FAIL shader link\n"); return 5; }
    glUseProgram(prog);
    printf("Shaders: OK (3D cube + lighting)\n");

    // 4. Cube VBO
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
    GLint pos_loc = glGetAttribLocation(prog, "pos");
    GLint normal_loc = glGetAttribLocation(prog, "normal");
    GLint texcoord_loc = glGetAttribLocation(prog, "texcoord");
    GLint mvp_loc = glGetUniformLocation(prog, "mvp");
    GLint model_loc = glGetUniformLocation(prog, "model");
    GLint time_loc = glGetUniformLocation(prog, "u_time");
    glEnableVertexAttribArray(pos_loc);
    glEnableVertexAttribArray(normal_loc);
    glEnableVertexAttribArray(texcoord_loc);
    glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)0);
    glVertexAttribPointer(normal_loc, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
    glVertexAttribPointer(texcoord_loc, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void*)(6 * sizeof(GLfloat)));

    // 5. Enable depth test
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // 6. Render loop（3D 旋转立方体）
    int frames = 300;
    double start = now_ms();
    for (int frame = 0; frame < frames; frame++) {
        float angle = frame * 0.02f;
        float time_val = frame * 0.01f;
        
        // Model matrix (rotation)
        GLfloat model[16] = {
            cos(angle), 0, -sin(angle), 0,
            0, 1, 0, 0,
            sin(angle), 0, cos(angle), 0,
            0, 0, -5, 1
        };
        
        // MVP matrix (perspective projection)
        GLfloat fov = 45.0f * M_PI / 180.0f;
        GLfloat aspect = (GLfloat)W / (GLfloat)H;
        GLfloat near = 0.1f, far = 100.0f;
        GLfloat f = 1.0f / tan(fov / 2.0f);
        GLfloat mvp[16] = {
            f / aspect, 0, 0, 0,
            0, f, 0, 0,
            0, 0, (far + near) / (near - far), -1,
            0, 0, 2 * far * near / (near - far), 0
        };
        
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp);
        glUniformMatrix4fv(model_loc, 1, GL_FALSE, model);
        glUniform1f(time_loc, time_val);
        
        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glFinish();
        
        // Read pixels (GPU → CPU)
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, ximage->data);
        
        // Convert RGBA → BGRA (X11 24-bit)
        unsigned char* data = (unsigned char*)ximage->data;
        for (int i = 0; i < W * H; i++) {
            unsigned char r = data[i*4+0];
            unsigned char g = data[i*4+1];
            unsigned char b = data[i*4+2];
            data[i*4+0] = b;
            data[i*4+1] = g;
            data[i*4+2] = r;
            data[i*4+3] = 255;
        }
        
        // Put image to X11 window
        XPutImage(xdisplay, window, DefaultGC(xdisplay, screen), ximage, 0, 0, 0, 0, W, H);
        XFlush(xdisplay);
        
        if (frame % 30 == 0) {
            double elapsed = now_ms() - start;
            printf("Frame %d: %.2f FPS (%.2f ms/frame)\n", frame, frame * 1000.0 / elapsed, elapsed / frame);
            fflush(stdout);
        }
    }
    double elapsed = now_ms() - start;
    printf("\n%d frames in %.2f ms (%.2f FPS, %.2f ms/frame)\n", frames, elapsed, frames * 1000.0 / elapsed, elapsed / frames);

    // Cleanup
    XDestroyImage(ximage);
    XDestroyWindow(xdisplay, window);
    XCloseDisplay(xdisplay);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
