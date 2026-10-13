// ---------------------------------------------------------------------------
// Glyph atlas built with GDI and uploaded as one texture-array layer.
//
// Text rendering is the one place where a from-scratch implementation is not
// worth the detour at this stage: GDI already produces correct, hinted,
// antialiased glyph coverage for any installed font. The atlas therefore
// rasterises its character set once, uploads the coverage as an RGBA layer, and
// every later frame draws text as one textured quad per glyph.
//
// **Il testo e' UTF-8, i caratteri si contano uno alla volta.** Un nome di file
// italiano ha gli accenti ("perché.txt", "relazione più recente.txt") e in UTF-8
// sono due byte: trattarli come due caratteri faceva sparire la lettera e lasciare
// un buco. L'insieme rasterizzato e' quindi ASCII piu' il blocco latino-1 piu' i
// pochi simboli che l'interfaccia disegna (le frecce della barra strumenti, la
// stella dei segnalibri, i puntini di sospensione). Vedi `docs/02` §7.29.
//
// The reference application ships its own rasteriser instead
// (WinFontRasterizer.cpp appears in its string table). Replacing this with a
// hand-written one is a later milestone; see docs/04-roadmap.md.
// ---------------------------------------------------------------------------
#pragma once

#include "ui_renderer.h"

#include <string>
#include <unordered_map>

class FontAtlas
{
public:
    struct Glyph
    {
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;   // atlas coordinates
        float advance = 0.0f;                                // in atlas pixels
        float width = 0.0f;                                  // ink width in atlas pixels
    };

    FontAtlas() = default;
    ~FontAtlas();

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    // Rasterises the character set at |pixelHeight| using |faceName| and uploads
    // the result into |layer| of the renderer's texture array.
    //
    // |textureLayerSize| is the side of the texture array in pixels. The glyph
    // bitmap itself is only as large as the glyph grid needs, but UVs must be
    // normalised against the layer, not against the bitmap: mixing the two makes
    // every glyph sample the wrong part of the atlas.
    bool Build(IRenderer& renderer, const wchar_t* faceName, int pixelHeight, int layer,
               int textureLayerSize, std::string* error);

    // Il glifo di un carattere, cercato per punto di codice e non per byte.
    const Glyph* Find(uint32_t codepoint) const;

    float LineHeight() const { return lineHeight_; }
    float Ascent() const { return ascent_; }

    // Width of |text| at |scale|, in UI pixels.
    float Measure(const std::string& text, float scale) const;

    // La larghezza di un solo carattere: chi tronca un testo ha bisogno di
    // misurarne uno alla volta, senza spezzare le sequenze.
    float MeasureOne(uint32_t codepoint, float scale) const;

    // Queues one quad per glyph. |x|, |y| name the top-left corner of the line box.
    void Draw(IRenderer& renderer, float x, float y, const std::string& text, Color color, float scale) const;

private:
    std::unordered_map<uint32_t, Glyph> glyphs_;
    std::string                     faceName_;
    int                             layer_ = 0;
    int                             textureLayerSize_ = 0;
    float                           lineHeight_ = 0.0f;
    float                           ascent_ = 0.0f;
    float                           atlasSize_ = 0.0f;
};

// --- testo in UTF-8 ---------------------------------------------------------
//
// Il testo che arriva qui viene da fuori: nomi di file, etichette della shell,
// stringhe nostre. Girare sui byte e' sbagliato appena compare una lettera
// accentata, e un nome di file accentato non e' un caso di laboratorio.
namespace Utf8
{
// Lunghezza in byte del carattere che comincia a |index|. Zero se |index| e'
// fuori dalla stringa.
size_t Length(const std::string& text, size_t index);

// Passo successivo: l'indice del carattere dopo quello che comincia a |index|.
size_t Next(const std::string& text, size_t index);

// Punto di codice che comincia a |index|; |length| riceve quanti byte occupa.
uint32_t At(const std::string& text, size_t index, size_t* length);
} // namespace Utf8
