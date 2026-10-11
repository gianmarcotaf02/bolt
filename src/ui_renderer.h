// ---------------------------------------------------------------------------
// UI quad batcher.
//
// The whole interface is drawn by submitting axis-aligned quads to a single
// shader program. A quad carries its own geometry, colour, corner radii, stroke
// width, blur and texture coordinates, so the renderer needs no state changes
// between primitives: one buffer, one draw call per frame.
//
// This is the same architecture the reference application uses. See
// docs/02-architettura-ricostruita.md for how that was established.
// ---------------------------------------------------------------------------
#pragma once

#include "gl_loader.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Small value types
// ---------------------------------------------------------------------------

struct Color
{
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    static Color FromHex(uint32_t rgb, float alpha = 1.0f)
    {
        return Color{static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                     static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                     static_cast<float>(rgb & 0xFF) / 255.0f,
                     alpha};
    }

    Color WithAlpha(float alpha) const { return Color{r, g, b, alpha}; }
};

// Axis-aligned rectangle in UI pixel space: origin top-left, +Y downwards.
struct Rect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool  Contains(float px, float py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    Rect Inset(float dx, float dy) const { return Rect{x + dx, y + dy, w - dx * 2.0f, h - dy * 2.0f}; }
};

// Corner radii in the conventional order: top-left, top-right, bottom-right,
// bottom-left.
struct Corners
{
    float tl = 0.0f, tr = 0.0f, br = 0.0f, bl = 0.0f;

    static Corners All(float radius) { return Corners{radius, radius, radius, radius}; }
};

// ---------------------------------------------------------------------------
// One drawable primitive
// ---------------------------------------------------------------------------

struct UIDraw
{
    Rect    rect{};
    Color   color{};
    Corners radius{};

    // Per-edge brightness multipliers: 1 everywhere means a flat fill. Used for
    // subtle top-to-bottom shading and for alternating list rows.
    float shadeLeft = 1.0f, shadeRight = 1.0f, shadeBottom = 1.0f, shadeTop = 1.0f;

    float strokeWidth = 0.0f;   // > 0 draws a border instead of a fill
    float softness    = 0.0f;   // > 0 on a fill produces a blurred shadow

    float dashOn  = 0.0f;       // dashed borders when dashOn + dashOff > 0
    float dashOff = 0.0f;

    // Texture array sampling. Ignored unless |textured| is set.
    float layer = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    bool  textured = false;
    bool  mono     = false;     // use the red channel as an alpha mask

    static UIDraw Fill(Rect rect, Color color, float radius = 0.0f);
    static UIDraw Fill(Rect rect, Color color, Corners radius);
    static UIDraw Border(Rect rect, Color color, float width, float radius = 0.0f, Corners corners = {});
    static UIDraw Shadow(Rect rect, Color color, float blur, float radius = 0.0f);
    static UIDraw DashedBorder(Rect rect, Color color, float width, float on, float off, float radius = 0.0f);
    static UIDraw Image(Rect rect, Color tint, float layer, float u0, float v0, float u1, float v1, bool monoMask = false);
};

// ---------------------------------------------------------------------------
// Batcher
// ---------------------------------------------------------------------------

class UIRenderer
{
public:
    UIRenderer() = default;
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    // Compiles the shader program and creates the vertex array. |shaderDir| must
    // contain ui.vert and ui.frag. On failure returns false and fills |error|.
    bool Initialize(const std::string& shaderDir, std::string* error);
    void Shutdown();

    // Must be called before any texture-array upload; |layers| x |size| square
    // RGBA8 layers are allocated with a mip chain, linear filtering and clamped
    // wrapping. The mip chain matters because icons and thumbnails are drawn
    // several times smaller than their atlas cell: without it, minification
    // samples only four texels and the result is blocky.
    bool CreateTextureArray(int layers, int size, std::string* error);

    // Uploads one region of one layer. The mip chain is regenerated once per
    // frame at most, before the draw, so that every level stays complete.
    void UploadLayerRegion(int layer, const uint8_t* rgba, int width, int height, int offsetX, int offsetY);

    // Starts a frame for the given viewport size in pixels.
    void Begin(int viewportWidth, int viewportHeight);

    // Queues one primitive. Quads are drawn in submission order.
    void Draw(const UIDraw& primitive);

    // Convenience wrappers for the common cases.
    void FillRect(Rect rect, Color color, float radius = 0.0f);
    void BorderRect(Rect rect, Color color, float width, float radius = 0.0f);

    // Uploads and draws the queued geometry in a single draw call.
    void End();

    // Number of quads submitted in the current frame (vertices are cleared by
    // Begin, so this never accumulates across frames).
    size_t QuadCount() const;
    GLuint TextureArray() const { return texture_; }

    // Ensures the vertex buffer can hold |quads| more quads without reallocating.
    void ReserveQuads(size_t quads);

private:
    void AppendQuad(const UIDraw& primitive);

    GLuint program_ = 0;
    GLuint vao_     = 0;
    GLuint vbo_     = 0;   // vertex data, one quad per four vertices
    GLuint ebo_     = 0;   // index data, one quad per six indices
    GLuint texture_ = 0;

    GLint uniformProjection_    = -1;
    GLint uniformTexture_       = -1;
    GLint uniformViewportHeight_ = -1;

    int viewportWidth_  = 0;
    int viewportHeight_ = 0;
    bool mipmapsDirty_  = false;

    // 4 vertices per quad, laid out to match the attribute locations declared in
    // the vertex shader.
    std::vector<float> vertices_;
    size_t             vertexCapacityBytes_ = 0;
};
