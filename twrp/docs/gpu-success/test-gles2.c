#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>

static const char* vsrc = "attribute vec4 pos; void main() { gl_Position = pos; }";
static const char* fsrc = "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }";

int main() {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    printf("EGL_VENDOR: %s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("EGL_VERSION: %s\n", eglQueryString(dpy, EGL_VERSION));
    EGLConfig cfg; EGLint n;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    eglChooseConfig(dpy, attrs, &cfg, 1, &n);
    printf("Config: %d matches\n", n);
    EGLint cattribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattribs);
    if (ctx == EGL_NO_CONTEXT) { printf("FAIL: eglCreateContext err=0x%x\n", eglGetError()); return 1; }
    printf("Context: OK (GLES2)\n");
    EGLint pattribs[] = { EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pattribs);
    eglMakeCurrent(dpy, surf, surf, ctx);

    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    printf("GLSL_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs);
    GLint ok; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    printf("Vertex shader compile: %s\n", ok == GL_TRUE ? "OK" : "FAIL");
    if (ok != GL_TRUE) { char log[1024]; glGetShaderInfoLog(vs, 1024, NULL, log); printf("VS log: %s\n", log); }
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    printf("Fragment shader compile: %s\n", ok == GL_TRUE ? "OK" : "FAIL");
    if (ok != GL_TRUE) { char log[1024]; glGetShaderInfoLog(fs, 1024, NULL, log); printf("FS log: %s\n", log); }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    printf("Shader link: %s\n", ok == GL_TRUE ? "OK" : "FAIL");

    glUseProgram(prog);
    GLfloat verts[] = { 0,0.5,0, -0.5,-0.5,0, 0.5,-0.5,0 };
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(prog, "pos");
    printf("Attrib loc: %d\n", loc);
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    printf("Draw OK\n");

    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return 0;
}
