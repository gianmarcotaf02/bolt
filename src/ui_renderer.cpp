#include "ui_renderer.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

// Floats per vertex and vertices per quad. The layout here must stay in step
// with the attribute locations declared in shaders/ui.vert.
constexpr size_t kFloatsPerVertex = 30;
constexpr size_t kVerticesPerQuad = 4;
constexpr size_t kIndicesPerQuad  = 6;

// Attribute location -> (component count, offset in floats).
struct Attrib { GLuint location; GLint components; size_t offsetFloats; };
constexpr Attrib kLayout[] = {
    {0,  2,  0},   // aCornerBL
    {1,  2,  2},   // aCornerBR
    {2,  2,  4},   // aCornerTL
    {3,  2,  6},   // aCornerTR
    {4,  2,  8},   // aUVMin
    {5,  2, 10},   // aUVMax
    {6,  1, 12},   // aLayer
    {7,  4, 13},   // aColor
    {8,  4, 17},   // aGradient
    {9,  4, 21},   // aRadius
    {10, 1, 25},   // aStrokeWidth
    {11, 1, 26},   // aSoftness
    {12, 1, 27},   // aDashOn
    {13, 1, 28},   // aDashOff
    {14, 1, 29},   // aFlags
};

bool ReadTextFile(const std::string& path, std::string* out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    *out = buffer.str();
    return true;
}

GLuint CompileShader(GLenum stage, const std::string& source, const char* label, std::string* error)
{
    GLuint shader = glCreateShader(stage);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        if (error != nullptr)
            *error = std::string(label) + " failed to compile:\n" + log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Orthographic projection mapping UI pixel space (origin top-left, +Y down) onto
// clip space. Column-major, as glUniformMatrix4fv expects.
void BuildProjection(float width, float height, float* out)
{
    for (int i = 0; i < 16; ++i)
        out[i] = 0.0f;

    out[0]  = (width  > 0.0f) ? 2.0f / width  : 0.0f;
    out[5]  = (height > 0.0f) ? -2.0f / height : 0.0f;
    out[10] = -1.0f;
    out[12] = -1.0f;
    out[13] = 1.0f;
    out[15] = 1.0f;
}

} // namespace

// ---------------------------------------------------------------------------
// UIDraw factories
// ---------------------------------------------------------------------------

UIDraw UIDraw::Fill(Rect rect, Color color, float radius)
{
    UIDraw d;
    d.rect   = rect;
    d.color  = color;
    d.radius = Corners::All(radius);
    return d;
}

UIDraw UIDraw::Fill(Rect rect, Color color, Corners radius)
{
    UIDraw d;
    d.rect   = rect;
    d.color  = color;
    d.radius = radius;
    return d;
}

UIDraw UIDraw::Border(Rect rect, Color color, float width, float radius, Corners corners)
{
    UIDraw d;
    d.rect        = rect;
    d.color       = color;
    d.strokeWidth = width;
    d.radius      = (corners.tl == 0.0f && corners.tr == 0.0f && corners.br == 0.0f && corners.bl == 0.0f)
                        ? Corners::All(radius)
                        : corners;
    return d;
}

UIDraw UIDraw::Shadow(Rect rect, Color color, float blur, float radius)
{
    UIDraw d;
    d.rect     = rect;
    d.color    = color;
    d.softness = blur;
    d.radius   = Corners::All(radius);
    return d;
}

UIDraw UIDraw::DashedBorder(Rect rect, Color color, float width, float on, float off, float radius)
{
    UIDraw d;
    d.rect        = rect;
    d.color       = color;
    d.strokeWidth = width;
    d.radius      = Corners::All(radius);
    d.dashOn      = on;
    d.dashOff     = off;
    return d;
}

UIDraw UIDraw::Image(Rect rect, Color tint, float layer, float u0, float v0, float u1, float v1, bool monoMask)
{
    UIDraw d;
    d.rect     = rect;
    d.color    = tint;
    d.layer    = layer;
    d.u0 = u0; d.v0 = v0; d.u1 = u1; d.v1 = v1;
    d.textured = true;
    d.mono     = monoMask;
    return d;
}

// ---------------------------------------------------------------------------
// UIRenderer
// ---------------------------------------------------------------------------

UIRenderer::~UIRenderer()
{
    // GL objects are released in Shutdown(); nothing to do here because the
    // context may already be gone.
}

bool UIRenderer::Initialize(const std::string& shaderDir, std::string* error)
{
    std::string vertexSource, fragmentSource;
    if (!ReadTextFile(shaderDir + "/ui.vert", &vertexSource))
    {
        if (error != nullptr) *error = "cannot read " + shaderDir + "/ui.vert";
        return false;
    }
    if (!ReadTextFile(shaderDir + "/ui.frag", &fragmentSource))
    {
        if (error != nullptr) *error = "cannot read " + shaderDir + "/ui.frag";
        return false;
    }

    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource, "ui.vert", error);
    if (vertexShader == 0)
        return false;

    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource, "ui.frag", error);
    if (fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertexShader);
    glAttachShader(program_, fragmentShader);
    glLinkProgram(program_);

    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (linked == GL_FALSE)
    {
        GLint length = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
        glGetProgramInfoLog(program_, length, nullptr, log.data());
        if (error != nullptr) *error = "shader program failed to link:\n" + log;
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    uniformProjection_     = glGetUniformLocation(program_, "uProjection");
    uniformTexture_        = glGetUniformLocation(program_, "uTexture");
    uniformViewportHeight_ = glGetUniformLocation(program_, "uViewportHeight");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    for (const Attrib& attrib : kLayout)
    {
        glEnableVertexAttribArray(attrib.location);
        glVertexAttribPointer(attrib.location,
                              attrib.components,
                              GL_FLOAT,
                              GL_FALSE,
                              static_cast<GLsizei>(kFloatsPerVertex * sizeof(float)),
                              reinterpret_cast<const void*>(attrib.offsetFloats * sizeof(float)));
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBindVertexArray(0);

    return true;
}

// L'entrata dell'interfaccia: il percorso degli shader lo conosce il backend, non
// chi lo costruisce. `REA_SHADER_DIR` e' una definizione di compilazione (vedi
// `CMakeLists.txt`), quindi vale anche qui.
#ifndef REA_SHADER_DIR
#define REA_SHADER_DIR "shaders"
#endif

bool UIRenderer::Initialize(std::string* error)
{
    return Initialize(REA_SHADER_DIR, error);
}

void UIRenderer::BeginFrame(int width, int height)
{
    viewportWidth_  = width;
    viewportHeight_ = height;
    if (dc_ == nullptr)
        return;
    glViewport(0, 0, width, height);
    // Lo stesso colore di sfondo di prima, quello del tema scuro: e' il colore che
    // si vede per un istante prima che il primo fotogramma sia disegnato.
    glClearColor(0.106f, 0.110f, 0.122f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void UIRenderer::Present()
{
    if (dc_ != nullptr)
        SwapBuffers(dc_);
}

void UIRenderer::SetScissor(Rect rect)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f)
    {
        ClearScissor();
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    // `glScissor` conta dal basso a sinistra; lo spazio dell'interfaccia da in
    // alto a sinistra. La conversione sta qui, una volta sola.
    glScissor(static_cast<GLint>(rect.x),
              static_cast<GLint>(static_cast<float>(viewportHeight_) - rect.bottom()),
              static_cast<GLsizei>(rect.w), static_cast<GLsizei>(rect.h));
}

void UIRenderer::ClearScissor()
{
    glDisable(GL_SCISSOR_TEST);
}

void UIRenderer::Shutdown()
{
    if (texture_ != 0) { glDeleteTextures(1, &texture_); texture_ = 0; }
    if (ebo_ != 0)     { glDeleteBuffers(1, &ebo_);     ebo_ = 0; }
    if (vbo_ != 0)     { glDeleteBuffers(1, &vbo_);     vbo_ = 0; }
    if (vao_ != 0)     { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_ != 0) { glDeleteProgram(program_);     program_ = 0; }

    vertices_.clear();
    vertices_.shrink_to_fit();
    vertexCapacityBytes_ = 0;
}

bool UIRenderer::CreateTextureArray(int layers, int size, std::string* error)
{
    if (layers <= 0 || size <= 0)
    {
        if (error != nullptr) *error = "texture array needs positive layer and size counts";
        return false;
    }

    if (texture_ != 0)
        glDeleteTextures(1, &texture_);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);

    // A full mip chain, built lazily: the icon and thumbnail atlases are sampled
    // far below 1:1 in the list, and point sampling there is what made them look
    // grainy.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Contents are supplied later through UploadLayerRegion, one region at a time.
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, size, size, layers,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    mipmapsDirty_ = true;   // the chain does not exist until it is generated
    return true;
}

void UIRenderer::UploadLayerRegion(int layer, const uint8_t* rgba, int width, int height, int offsetX, int offsetY)
{
    if (texture_ == 0 || rgba == nullptr || width <= 0 || height <= 0)
        return;

    glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
    // The row length is padded to the 4-byte default alignment: RGBA8 rows are
    // always a multiple of 4 bytes, so no unpack setting is required.
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                    offsetX, offsetY, layer,
                    width, height, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    mipmapsDirty_ = true;
}

size_t UIRenderer::QuadCount() const
{
    // One quad is kVerticesPerQuad vertices of kFloatsPerVertex floats each, so
    // dividing the float count by four would over-report by that full stride.
    return vertices_.size() / (kFloatsPerVertex * kVerticesPerQuad);
}

void UIRenderer::Begin(int viewportWidth, int viewportHeight)
{
    viewportWidth_  = viewportWidth;
    viewportHeight_ = viewportHeight;
    vertices_.clear();
}

void UIRenderer::ReserveQuads(size_t quads)
{
    vertices_.reserve(quads * kVerticesPerQuad * kFloatsPerVertex);
}

void UIRenderer::Draw(const UIDraw& primitive)
{
    if (primitive.rect.w <= 0.0f || primitive.rect.h <= 0.0f)
        return;
    AppendQuad(primitive);
}

void UIRenderer::FillRect(Rect rect, Color color, float radius)
{
    Draw(UIDraw::Fill(rect, color, radius));
}

void UIRenderer::BorderRect(Rect rect, Color color, float width, float radius)
{
    Draw(UIDraw::Border(rect, color, width, radius));
}

void UIRenderer::AppendQuad(const UIDraw& p)
{
    const Rect  r = p.rect;
    const float flags = (p.textured ? 1.0f : 0.0f) + (p.mono ? 2.0f : 0.0f);

    for (size_t vertex = 0; vertex < kVerticesPerQuad; ++vertex)
    {
        float v[kFloatsPerVertex];

        v[0] = r.x;          v[1] = r.bottom();   // bottom-left
        v[2] = r.right();    v[3] = r.bottom();   // bottom-right
        v[4] = r.x;          v[5] = r.y;          // top-left
        v[6] = r.right();    v[7] = r.y;          // top-right

        v[8]  = p.u0;  v[9]  = p.v0;
        v[10] = p.u1;  v[11] = p.v1;
        v[12] = p.layer;

        v[13] = p.color.r; v[14] = p.color.g; v[15] = p.color.b; v[16] = p.color.a;

        v[17] = p.shadeLeft; v[18] = p.shadeRight; v[19] = p.shadeBottom; v[20] = p.shadeTop;

        v[21] = p.radius.tl; v[22] = p.radius.tr; v[23] = p.radius.br; v[24] = p.radius.bl;

        v[25] = p.strokeWidth;
        v[26] = p.softness;
        v[27] = p.dashOn;
        v[28] = p.dashOff;
        v[29] = flags;

        vertices_.insert(vertices_.end(), v, v + kFloatsPerVertex);
    }
}

void UIRenderer::End()
{
    if (vertices_.empty() || program_ == 0)
        return;

    const size_t quadCount = vertices_.size() / (kFloatsPerVertex * kVerticesPerQuad);

    // Index buffer: four vertices per quad, expanded into two triangles that
    // share the diagonal. Referencing the same four vertices avoids the strip
    // stitching problem and keeps the vertex data 33% smaller than six
    // independent vertices would be. The pattern for N quads is a prefix of the
    // pattern for M > N quads, so the buffer only ever grows.
    static std::vector<uint32_t> indices;
    if (indices.size() < quadCount * kIndicesPerQuad)
    {
        indices.reserve(quadCount * kIndicesPerQuad * 2);
        while (indices.size() < quadCount * kIndicesPerQuad)
        {
            const uint32_t base = static_cast<uint32_t>(indices.size() / kIndicesPerQuad) * 4u;
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }
    }

    const GLsizeiptr vertexBytes = static_cast<GLsizeiptr>(vertices_.size() * sizeof(float));
    const GLsizeiptr indexBytes  = static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t));

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (vertexBytes > static_cast<GLsizeiptr>(vertexCapacityBytes_))
    {
        glBufferData(GL_ARRAY_BUFFER, vertexBytes, vertices_.data(), GL_STREAM_DRAW);
        vertexCapacityBytes_ = static_cast<size_t>(vertexBytes);
    }
    else
    {
        // Same storage, new contents. Orphaning the buffer this way lets the
        // driver hand back fresh memory instead of stalling on the in-flight frame.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexCapacityBytes_), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertexBytes, vertices_.data());
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexBytes, indices.data(), GL_STATIC_DRAW);

    float projection[16];
    BuildProjection(static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_), projection);

    glUseProgram(program_);
    glUniformMatrix4fv(uniformProjection_, 1, GL_FALSE, projection);
    glUniform1f(uniformViewportHeight_, static_cast<float>(viewportHeight_));
    glUniform1i(uniformTexture_, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);

    // Once per frame, and only when something was uploaded: every level of every
    // layer has to exist before the sampler may use the chain.
    if (mipmapsDirty_)
    {
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        mipmapsDirty_ = false;
    }

    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(quadCount * kIndicesPerQuad),
                   GL_UNSIGNED_INT,
                   nullptr);

    glBindVertexArray(0);
    glUseProgram(0);
}
