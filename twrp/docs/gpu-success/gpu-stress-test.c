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
"attribute vec3 color;\n"
"uniform mat4 mvp;\n"
"uniform float u_time;\n"
"varying vec3 v_color;\n"
"varying vec3 v_pos;\n"
"void main() {\n"
"    gl_Position = mvp * vec4(pos, 1.0);\n"
"    v_color = color;\n"
"    v_pos = pos;\n"
"}";

static const char* fsrc = 
"precision mediump float;\n"
"uniform float u_time;\n"
"varying vec3 v_color;\n"
"varying vec3 v_pos;\n"
"void main() {\n"
"    vec3 light_dir = normalize(vec3(1.0, 1.0, 1.0));\n"
"    vec3 normal = normalize(vec3(0.5, 0.5, 1.0));\n"
"    float diffuse = max(dot(normal, light_dir), 0.0);\n"
"    float specular = pow(diffuse, 16.0);\n"
"    vec3 color = v_color * (0.3 + 0.5 * diffuse + 0.3 * specular);\n"
"    color *= 0.8 + 0.2 * sin(u_time + v_pos.x * 3.0 + v_pos.y * 3.0 + v_pos.z * 3.0);\n"
"    gl_FragColor = vec4(color, 1.0);\n"
"}";

// Cube vertices (position + color)
static const GLfloat cube_template[] = {
    -0.5,-0.5, 0.5, 1,0,0,  0.5,-0.5, 0.5, 1,0,0,  0.5, 0.5, 0.5, 1,0,0,
    -0.5,-0.5, 0.5, 1,0,0,  0.5, 0.5, 0.5, 1,0,0, -0.5, 0.5, 0.5, 1,0,0,
    -0.5,-0.5,-0.5, 0,1,0, -0.5, 0.5,-0.5, 0,1,0,  0.5, 0.5,-0.5, 0,1,0,
    -0.5,-0.5,-0.5, 0,1,0,  0.5, 0.5,-0.5, 0,1,0,  0.5,-0.5,-0.5, 0,1,0,
    -0.5, 0.5,-0.5, 0,0,1, -0.5, 0.5, 0.5, 0,0,1,  0.5, 0.5, 0.5, 0,0,1,
    -0.5, 0.5,-0.5, 0,0,1,  0.5, 0.5, 0.5, 0,0,1,  0.5, 0.5,-0.5, 0,0,1,
    -0.5,-0.5,-0.5, 1,1,0,  0.5,-0.5,-0.5, 1,1,0,  0.5,-0.5, 0.5, 1,1,0,
    -0.5,-0.5,-0.5, 1,1,0,  0.5,-0.5, 0.5, 1,1,0, -0.5,-0.5, 0.5, 1,1,0,
     0.5,-0.5,-0.5, 0,1,1,  0.5, 0.5,-0.5, 0,1,1,  0.5, 0.5, 0.5, 0,1,1,
     0.5,-0.5,-0.5, 0,1,1,  0.5, 0.5, 0.5, 0,1,1,  0.5,-0.5, 0.5, 0,1,1,
    -0.5,-0.5,-0.5, 1,0,1, -0.5,-0.5, 0.5, 1,0,1, -0.5, 0.5, 0.5, 1,0,1,
    -0.5,-0.5,-0.5, 1,0,1, -0.5, 0.5, 0.5, 1,0,1, -0.5, 0.5,-0.5, 1,0,1,
};

#define NUM_CUBES 50
#define NUM_PARTICLES 200

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main() {
    // 1. Open X11 display
    Display* xdisplay = XOpenDisplay(":0");
    if (!xdisplay) { printf("FAIL: XOpenDisplay\n"); return 1; }
    int screen = DefaultScreen(xdisplay);
    Window root = RootWindow(xdisplay, screen);
    int W = 256, H = 256;
    Window window = XCreateSimpleWindow(xdisplay, root, 100, 100, W, H, 1, BlackPixel(xdisplay, screen), WhitePixel(xdisplay, screen));
    XStoreName(xdisplay, window, "Mali-T860 GPU Stress Test (50 Cubes + 200 Particles)");
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

    // 3. Shaders（加 GL_OES_standard_derivatives 扩展）
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) { 
        printf("FAIL shader link\n");
        char log[1024];
        glGetProgramInfoLog(prog, 1024, NULL, log);
        printf("Shader log: %s\n", log);
        return 5; 
    }
    glUseProgram(prog);
    printf("Shaders: OK (50 Cubes + 200 Particles + lighting)\n");

    // 4. Cube VBO
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_template), cube_template, GL_STATIC_DRAW);
    GLint pos_loc = glGetAttribLocation(prog, "pos");
    GLint color_loc = glGetAttribLocation(prog, "color");
    GLint mvp_loc = glGetUniformLocation(prog, "mvp");
    GLint time_loc = glGetUniformLocation(prog, "u_time");
    glEnableVertexAttribArray(pos_loc);
    glEnableVertexAttribArray(color_loc);
    glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)0);
    glVertexAttribPointer(color_loc, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));

    // 5. Enable depth test + cull face
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // 6. Render loop（多立方体 + 粒子）
    int frames = 300;
    double start = now_ms();
    for (int frame = 0; frame < frames; frame++) {
        float time_val = frame * 0.01f;
        float angle = frame * 0.02f;
        
        // Model matrix (camera rotation)
        GLfloat model[16] = {
            cos(angle), 0, -sin(angle), 0,
            0, 1, 0, 0,
            sin(angle), 0, cos(angle), 0,
            0, 0, -10, 1
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
        glUniform1f(time_loc, time_val);
        
        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Draw 50 cubes（每个不同位置 + 旋转）
        for (int i = 0; i < NUM_CUBES; i++) {
            float cx = sin(i * 0.5f + time_val) * 3.0f;
            float cy = cos(i * 0.3f + time_val) * 3.0f;
            float cz = sin(i * 0.2f + time_val) * 3.0f;
            float rot = i * 0.1f + time_val;
            
            // Cube model matrix（绕 Y 轴旋转 + 位移）
            GLfloat cube_model[16] = {
                cos(rot), 0, -sin(rot), 0,
                0, 1, 0, 0,
                sin(rot), 0, cos(rot), 0,
                cx, cy, cz + model[14], 1
            };
            
            // MVP = projection * cube_model
            GLfloat cube_mvp[16];
            for (int j = 0; j < 4; j++) {
                for (int k = 0; k < 4; k++) {
                    cube_mvp[j*4+k] = 0;
                    for (int l = 0; l < 4; l++) {
                        cube_mvp[j*4+k] += mvp[j*4+l] * cube_model[l*4+k];
                    }
                }
            }
            
            glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, cube_mvp);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
        
        // Draw 200 particles（点精灵）
        GLfloat particles[NUM_PARTICLES * 3];
        for (int i = 0; i < NUM_PARTICLES; i++) {
            float angle = i * 0.1f + time_val * 2.0f;
            float radius = 2.0f + sin(i * 0.5f + time_val) * 1.5f;
            particles[i*3+0] = cos(angle) * radius;
            particles[i*3+1] = sin(angle) * radius;
            particles[i*3+2] = sin(i * 0.3f + time_val) * 3.0f;
        }
        
        GLuint particle_vbo; glGenBuffers(1, &particle_vbo); glBindBuffer(GL_ARRAY_BUFFER, particle_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(particles), particles, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(pos_loc);
        glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glDrawArrays(GL_POINTS, 0, NUM_PARTICLES);
        glDeleteBuffers(1, &particle_vbo);
        
        // Rebind cube VBO
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(pos_loc);
        glEnableVertexAttribArray(color_loc);
        glVertexAttribPointer(pos_loc, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)0);
        glVertexAttribPointer(color_loc, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
        
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
    printf("Total vertices rendered: %d cubes * 36 verts + %d particles = %d\n", 
           NUM_CUBES, NUM_PARTICLES, NUM_CUBES * 36 + NUM_PARTICLES);

    // Cleanup
    XDestroyImage(ximage);
    XDestroyWindow(xdisplay, window);
    XCloseDisplay(xdisplay);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
