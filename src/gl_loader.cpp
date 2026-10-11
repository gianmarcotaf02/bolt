#include "gl_loader.h"

#include <cstring>

PFNGLCREATESHADERPROC        glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC        glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC       glCompileShader = nullptr;
PFNGLGETSHADERIVPROC         glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC    glGetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC        glDeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC       glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC        glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC         glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC        glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC   glGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC          glUseProgram = nullptr;
PFNGLDELETEPROGRAMPROC       glDeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC  glGetUniformLocation = nullptr;
PFNGLUNIFORM1FPROC           glUniform1f = nullptr;
PFNGLUNIFORM1IPROC           glUniform1i = nullptr;
PFNGLUNIFORMMATRIX4FVPROC    glUniformMatrix4fv = nullptr;
PFNGLGENVERTEXARRAYSPROC     glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC     glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC  glDeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC          glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC          glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC          glBufferData = nullptr;
PFNGLDELETEBUFFERSPROC       glDeleteBuffers = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLACTIVETEXTUREPROC       glActiveTexture = nullptr;
PFNGLTEXIMAGE3DPROC          glTexImage3D = nullptr;
PFNGLTEXSUBIMAGE3DPROC       glTexSubImage3D = nullptr;
PFNGLGENERATEMIPMAPPROC      glGenerateMipmap = nullptr;
PFNGLBUFFERSUBDATAPROC       glBufferSubData = nullptr;

namespace {

// wglGetProcAddress returns null for functions that the driver exports through
// opengl32.dll directly, so fall back to GetProcAddress on that module.
void* Resolve(const char* name)
{
    void* address = reinterpret_cast<void*>(wglGetProcAddress(name));
    if (address == nullptr || address == reinterpret_cast<void*>(1)
        || address == reinterpret_cast<void*>(2) || address == reinterpret_cast<void*>(3)
        || address == reinterpret_cast<void*>(-1))
    {
        HMODULE module = LoadLibraryA("opengl32.dll");
        if (module != nullptr)
            address = reinterpret_cast<void*>(GetProcAddress(module, name));
    }
    return address;
}

// Assigns one resolved pointer, reporting an unresolved name instead of
// crashing on a null call later. NOLINTNEXTLINE(bugprone-macro-parentheses)
#define REA_RESOLVE(name)                                         \
    do {                                                          \
        void* resolved = Resolve(#name);                          \
        if (resolved == nullptr) { *missing = #name; return false; } \
        name = reinterpret_cast<decltype(name)>(resolved);         \
    } while (false)

} // namespace

bool LoadOpenGLFunctions(const char** missing)
{
    static const char* unused = nullptr;
    if (missing == nullptr)
        missing = &unused;
    *missing = nullptr;

    REA_RESOLVE(glCreateShader);
    REA_RESOLVE(glShaderSource);
    REA_RESOLVE(glCompileShader);
    REA_RESOLVE(glGetShaderiv);
    REA_RESOLVE(glGetShaderInfoLog);
    REA_RESOLVE(glDeleteShader);
    REA_RESOLVE(glCreateProgram);
    REA_RESOLVE(glAttachShader);
    REA_RESOLVE(glLinkProgram);
    REA_RESOLVE(glGetProgramiv);
    REA_RESOLVE(glGetProgramInfoLog);
    REA_RESOLVE(glUseProgram);
    REA_RESOLVE(glDeleteProgram);
    REA_RESOLVE(glGetUniformLocation);
    REA_RESOLVE(glUniform1f);
    REA_RESOLVE(glUniform1i);
    REA_RESOLVE(glUniformMatrix4fv);
    REA_RESOLVE(glGenVertexArrays);
    REA_RESOLVE(glBindVertexArray);
    REA_RESOLVE(glDeleteVertexArrays);
    REA_RESOLVE(glGenBuffers);
    REA_RESOLVE(glBindBuffer);
    REA_RESOLVE(glBufferData);
    REA_RESOLVE(glDeleteBuffers);
    REA_RESOLVE(glEnableVertexAttribArray);
    REA_RESOLVE(glVertexAttribPointer);
    REA_RESOLVE(glActiveTexture);
    REA_RESOLVE(glTexImage3D);
    REA_RESOLVE(glTexSubImage3D);
    REA_RESOLVE(glGenerateMipmap);
    REA_RESOLVE(glBufferSubData);

    return true;
}

#undef REA_RESOLVE
