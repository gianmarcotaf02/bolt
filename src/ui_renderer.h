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

// Le quattro direzioni di una freccia. Non sono "alto/basso": una freccia punta
// dove porta, e chi la disegna lo dice con questa parola invece che con un numero.
enum class ArrowDirection
{
    Right,
    Left,
    Up,
    Down,
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

    // Un triangolo invece di un rettangolo, iscritto nel rettangolo. Serve alle
    // linguette che aprono e chiudono una sezione: erano i caratteri `v` e `>`
    // disegnati col font, e un carattere non e' un elemento — cambia forma col
    // carattere scelto, non si allinea con niente e a dieci pixel e' un grumo.
    // Puntato in giu' quando la sezione e' aperta, a destra quando e' chiusa.
    bool  triangle     = false;
    bool  triangleDown = false;

    // Un piu' e una croce, per i pulsanti: erano i caratteri `+` e `x`, e valgono le
    // stesse ragioni del triangolo. Lo spessore della barra viaggia in |radius|,
    // perche' per queste due forme i raggi delle punte non vogliono dire niente.
    bool  plus  = false;
    bool  cross = false;

    // I pulsanti di navigazione, che erano i caratteri `←` `→` `↑` `↻` `★`: le
    // stesse ragioni, e in piu' il fatto che quattro caratteri di famiglie diverse
    // — frecce lineari, un cerchio con la punta, una stella — non si somigliano per
    // spessore ne' per altezza ottica, e cambiano disegno col carattere scelto.
    // Lo spessore viaggia in |radius|, come per il piu'.
    bool  arrow     = false;
    bool  arrowUp   = false;
    bool  arrowLeft = false;
    bool  refresh   = false;
    bool  star      = false;
    // La puntina delle cartelle fissate nella barra laterale: la stella resta il segno
    // dei segnalibri sulla barra strumenti, ma li' una cartella fissata si legge come
    // nell'originale (`docs/04`, «La puntina dei fissaggi»).
    bool  pin       = false;

    static UIDraw Fill(Rect rect, Color color, float radius = 0.0f);
    static UIDraw Fill(Rect rect, Color color, Corners radius);
    static UIDraw Border(Rect rect, Color color, float width, float radius = 0.0f, Corners corners = {});
    static UIDraw Shadow(Rect rect, Color color, float blur, float radius = 0.0f);
    static UIDraw DashedBorder(Rect rect, Color color, float width, float on, float off, float radius = 0.0f);
    static UIDraw Image(Rect rect, Color tint, float layer, float u0, float v0, float u1, float v1, bool monoMask = false);
    static UIDraw Triangle(Rect rect, Color color, bool pointsDown = false);
    static UIDraw Plus(Rect rect, Color color, float thickness);
    static UIDraw Cross(Rect rect, Color color, float thickness);
    static UIDraw Arrow(Rect rect, Color color, float thickness, ArrowDirection direction);
    static UIDraw Refresh(Rect rect, Color color, float thickness);
    static UIDraw Star(Rect rect, Color color, float thickness, bool filled);
    // La puntina dei fissaggi: piena, senza spessore. Il contorno non serve — una
    // puntina vuota non vuol dire niente, e a undici pixel sarebbe illeggibile.
    static UIDraw Pin(Rect rect, Color color);
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

    // Quante chiamate di disegno ha fatto questo fotogramma. `QuadCount` da' i
    // quadrilateri, questo il loro prezzo in chiamate: sono due numeri diversi e
    // il secondo e' quello che un ritaglio di stato moltiplicava. Si azzera in
    // `BeginFrame`, non in `Begin`: `Begin` apre un lotto e ce ne possono essere
    // piu' d'uno per fotogramma.
    virtual size_t DrawCalls() const = 0;

    // Il ritaglio e' **del quadrilatero**, non dello stato del disegno: il
    // rettangolo si scrive nel vertice di ogni primitiva accodata da qui alla
    // prossima chiamata, e a tagliare e' il pixel shader. Quindi `End` non va
    // chiamato attorno a un ritaglio: i quadrilateri di tutta l'interfaccia
    // restano in un lotto solo. Era il debito dichiarato in `AGENTS.md` §8 — cinque
    // disegni per fotogramma, quattro dei quali erano il prezzo di due ritagli.
    // Un rettangolo con larghezza o altezza non positiva vale "nessun ritaglio",
    // cioe' il fotogramma intero.
    virtual void SetClip(Rect rect) = 0;
    virtual void ClearClip() = 0;

    // Due moltiplicatori che valgono per tutto cio' che si disegna da qui alla
    // prossima chiamata. Sono lo stato che rende possibili due opzioni senza
    // toccare cento chiamate di disegno:
    //
    //   `SetCornerScale` — `RoundCorners`. Moltiplica i **raggi degli angoli**
    //   (0 = spigoli vivi). Le forme che usano quel numero come spessore — `+`,
    //   `x`, frecce, stella — non si toccano: li' il raggio non e' un angolo, e
    //   azzerarlo farebbe sparire il segno.
    //
    //   `SetOpacity` — `InactivePanelContentOpacity`. Moltiplica l'alpha di ogni
    //   primitiva, testo compreso (il glifo arriva come maschera, quindi l'alpha
    //   del colore e' la sua copertura). 1 = pieno.
    virtual void SetCornerScale(float scale) = 0;
    virtual void SetOpacity(float opacity) = 0;
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

    void SetClip(Rect rect) override;
    void ClearClip() override;
    void SetCornerScale(float scale) override;
    void SetOpacity(float opacity) override;

    size_t DrawCalls() const override
    {
        // Il lotto in corso non e' ancora stato disegnato, e il conto deve dire
        // quanti disegni **fa** il fotogramma: si conta anche lui. Senza, il numero
        // letto dalla barra di stato — che si disegna dentro l'ultimo lotto —
        // sarebbe sempre uno di meno.
        return drawCalls_ + (vertices_.empty() ? 0 : 1);
    }

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

    // Stato di disegno per blocco: vedi `IRenderer::SetCornerScale`/`SetOpacity`.
    float cornerScale_ = 1.0f;
    float opacity_     = 1.0f;

    // Il ritaglio attivo: finisce nel vertice di ogni quadrilatero accodato.
    Rect clip_{};

    // Disegni emessi nel fotogramma corrente: vedi `IRenderer::DrawCalls`.
    size_t drawCalls_ = 0;

    // 4 vertices per quad, laid out to match the attribute locations declared in
    // the vertex shader.
    std::vector<float> vertices_;
    size_t             vertexCapacityBytes_ = 0;
};
