// ---------------------------------------------------------------------------
// Minimal OpenGL 3.3 Core loader.
//
// Windows ships only OpenGL 1.1 in opengl32.dll, so everything newer has to be
// resolved at runtime through wglGetProcAddress. Rather than pulling in GLEW or
// GLAD this header declares exactly the entry points this project uses. That
// keeps the build dependency-free and makes the surface the renderer relies on
// explicit and reviewable.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <GL/gl.h>
#include <cstddef>

// --- constants missing from the Windows 1.1 headers -------------------------
#define GL_ARRAY_BUFFER                0x8892
#define GL_ELEMENT_ARRAY_BUFFER        0x8893
#define GL_STATIC_DRAW                 0x88E4
#define GL_DYNAMIC_DRAW                0x88E8
#define GL_STREAM_DRAW                 0x88E0
#define GL_FRAGMENT_SHADER             0x8B30
#define GL_VERTEX_SHADER               0x8B31
#define GL_COMPILE_STATUS              0x8B81
#define GL_LINK_STATUS                 0x8B82
#define GL_INFO_LOG_LENGTH             0x8B84
#define GL_TEXTURE_2D_ARRAY            0x8C1A
#define GL_TEXTURE0                    0x84C0
#define GL_CLAMP_TO_EDGE               0x812F
#define GL_TEXTURE_MAX_LEVEL           0x813D
#define GL_LINEAR_MIPMAP_LINEAR        0x2703
#define GL_RGBA8                       0x8058
#define GL_TEXTURE_MIN_FILTER          0x2801
#define GL_TEXTURE_MAG_FILTER          0x2800
#define GL_TEXTURE_WRAP_S              0x2802
#define GL_TEXTURE_WRAP_T              0x2803
#define GL_MULTISAMPLE                 0x809D
#define GL_BGRA                        0x80E1

// --- additional scalar types -----------------------------------------------
typedef char          GLchar;
typedef ptrdiff_t     GLsizeiptr;
typedef ptrdiff_t     GLintptr;

// --- entry point signatures ------------------------------------------------
// The Windows SDK ships GL/gl.h (OpenGL 1.1) and GLU.h only; GL/glext.h is not
// part of the SDK, so these signatures are declared here. The guard allows a
// project to include a Khronos glext.h instead without redefinition errors.
#ifndef REA_GL_EXT_TYPEDEFS
#define REA_GL_EXT_TYPEDEFS

typedef GLuint (APIENTRY* PFNGLCREATESHADERPROC)(GLenum type);
typedef void   (APIENTRY* PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void   (APIENTRY* PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void   (APIENTRY* PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void   (APIENTRY* PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY* PFNGLDELETESHADERPROC)(GLuint shader);
typedef GLuint (APIENTRY* PFNGLCREATEPROGRAMPROC)(void);
typedef void   (APIENTRY* PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void   (APIENTRY* PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void   (APIENTRY* PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void   (APIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
typedef void   (APIENTRY* PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void   (APIENTRY* PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint  (APIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void   (APIENTRY* PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void   (APIENTRY* PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void   (APIENTRY* PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
typedef void   (APIENTRY* PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void   (APIENTRY* PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void   (APIENTRY* PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void   (APIENTRY* PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void   (APIENTRY* PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void   (APIENTRY* PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void   (APIENTRY* PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef void   (APIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void   (APIENTRY* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
typedef void   (APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void   (APIENTRY* PFNGLTEXIMAGE3DPROC)(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void* pixels);
typedef void   (APIENTRY* PFNGLTEXSUBIMAGE3DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels);
typedef void   (APIENTRY* PFNGLGENERATEMIPMAPPROC)(GLenum target);
typedef void   (APIENTRY* PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);

#endif // REA_GL_EXT_TYPEDEFS

// --- entry points, resolved at startup -------------------------------------
extern PFNGLCREATESHADERPROC        glCreateShader;
extern PFNGLSHADERSOURCEPROC        glShaderSource;
extern PFNGLCOMPILESHADERPROC       glCompileShader;
extern PFNGLGETSHADERIVPROC         glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC    glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC        glDeleteShader;
extern PFNGLCREATEPROGRAMPROC       glCreateProgram;
extern PFNGLATTACHSHADERPROC        glAttachShader;
extern PFNGLLINKPROGRAMPROC         glLinkProgram;
extern PFNGLGETPROGRAMIVPROC        glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC   glGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC          glUseProgram;
extern PFNGLDELETEPROGRAMPROC       glDeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC  glGetUniformLocation;
extern PFNGLUNIFORM1FPROC           glUniform1f;
extern PFNGLUNIFORM1IPROC           glUniform1i;
extern PFNGLUNIFORMMATRIX4FVPROC    glUniformMatrix4fv;
extern PFNGLGENVERTEXARRAYSPROC     glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC     glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC  glDeleteVertexArrays;
extern PFNGLGENBUFFERSPROC          glGenBuffers;
extern PFNGLBINDBUFFERPROC          glBindBuffer;
extern PFNGLBUFFERDATAPROC          glBufferData;
extern PFNGLDELETEBUFFERSPROC       glDeleteBuffers;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLACTIVETEXTUREPROC       glActiveTexture;
extern PFNGLTEXIMAGE3DPROC          glTexImage3D;
extern PFNGLTEXSUBIMAGE3DPROC       glTexSubImage3D;
extern PFNGLGENERATEMIPMAPPROC      glGenerateMipmap;
extern PFNGLBUFFERSUBDATAPROC       glBufferSubData;

// Resolves every entry point above. Returns false and fills |missing| with the
// first name that could not be resolved.
bool LoadOpenGLFunctions(const char** missing);
