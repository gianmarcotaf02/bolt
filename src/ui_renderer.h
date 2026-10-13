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
// The renderer, behind an interface
//
// L'originale espone `GraphicsBackend` con `Direct3D` e `OpenGL` e importa
// `D3DCompiler_47`: due backend, scelti a runtime. Qui l'interfaccia e' il
// confine, e sotto ci stanno due implementazioni — `UIRenderer` (OpenGL) e
// `D3D11Renderer` (Direct3D 11). Chi disegna non sa quale dei due sta usando:
// parla di quadrilateri, non di `glDrawElements` o `DrawIndexed`.
//
// Cosa **non** e' nell'interfaccia, ed e' voluto: i vertici. Il formato di un
// vertice e' la cosa che i due backend hanno in comune, e resta quello — lo
// stesso array di float, letto con le stesse semantiche. Cambia solo chi lo
// consuma.
class IRenderer
{
public:
    virtual ~IRenderer() = default;

    // Nome del backend, per registri e titoli: "OpenGL" o "Direct3D 11".
    virtual const char* Name() const = 0;

    virtual bool Initialize(std::string* error) = 0;
    virtual void Shutdown() = 0;

    virtual bool CreateTextureArray(int layers, int size, std::string* error) = 0;
    virtual void UploadLayerRegion(int layer, const uint8_t* rgba, int width, int height,
                                   int offsetX, int offsetY) = 0;

    // Un fotogramma: si pulisce, si disegna a lotti, si presenta.
    virtual void BeginFrame(int width, int height) = 0;
    virtual void Present() = 0;

    virtual void Begin(int viewportWidth, int viewportHeight) = 0;
    virtual void Draw(const UIDraw& primitive) = 0;
    virtual void FillRect(Rect rect, Color color, float radius = 0.0f) = 0;
    virtual void BorderRect(Rect rect, Color color, float width, float radius = 0.0f) = 0;
    virtual void End() = 0;
    virtual size_t QuadCount() const = 0;
    virtual void ReserveQuads(size_t quads) = 0;

    // Il ritaglio e' stato di disegno, non del singolo quadrilatero: si accende
    // attorno a un lotto e si spegne subito dopo. Un rettangolo con larghezza o
    // altezza non positiva significa "nessun ritaglio".
    virtual void SetScissor(Rect rect) = 0;
    virtual void ClearScissor() = 0;
};

// ---------------------------------------------------------------------------
// Batcher
// ---------------------------------------------------------------------------

class UIRenderer final : public IRenderer
{
public:
    UIRenderer() = default;
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    const char* Name() const override { return "OpenGL"; }

    // Compiles the shader program and creates the vertex array. |shaderDir| must
    // contain ui.vert and ui.frag. On failure returns false and fills |error|.
    bool Initialize(const std::string& shaderDir, std::string* error);
    bool Initialize(std::string* error) override;
    void Shutdown() override;

    // Must be called before any texture-array upload; |layers| x |size| square
    // RGBA8 layers are allocated with a mip chain, linear filtering and clamped
    // wrapping. The mip chain matters because icons and thumbnails are drawn
    // several times smaller than their atlas cell: without it, minification
    // samples only four texels and the result is blocky.
    bool CreateTextureArray(int layers, int size, std::string* error) override;

    // Uploads one region of one layer. The mip chain is regenerated once per
    // frame at most, before the draw, so that every level stays complete.
    void UploadLayerRegion(int layer, const uint8_t* rgba, int width, int height, int offsetX, int offsetY) override;

    void BeginFrame(int width, int height) override;
    void Present() override;

    // Starts a frame for the given viewport size in pixels.
    void Begin(int viewportWidth, int viewportHeight) override;

    // Queues one primitive. Quads are drawn in submission order.
    void Draw(const UIDraw& primitive) override;

    // Convenience wrappers for the common cases.
    void FillRect(Rect rect, Color color, float radius = 0.0f) override;
    void BorderRect(Rect rect, Color color, float width, float radius = 0.0f) override;

    // Uploads and draws the queued geometry in a single draw call.
    void End() override;

    // Number of quads submitted in the current frame (vertices are cleared by
    // Begin, so this never accumulates across frames).
    size_t QuadCount() const override;
    GLuint TextureArray() const { return texture_; }

    // Ensures the vertex buffer can hold |quads| more quads without reallocating.
    void ReserveQuads(size_t quads) override;

    void SetScissor(Rect rect) override;
    void ClearScissor() override;

    // La finestra e il suo contesto: il backend OpenGL disegna su un `HDC` e
    // presenta con `SwapBuffers`, quindi li riceve da chi ha creato la finestra.
    void Attach(HWND window, HDC dc, HGLRC context)
    {
        window_ = window;
        dc_     = dc;
        context_ = context;
    }

private:
    void AppendQuad(const UIDraw& primitive);

    HWND  window_  = nullptr;
    HDC   dc_      = nullptr;
    HGLRC context_ = nullptr;

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
