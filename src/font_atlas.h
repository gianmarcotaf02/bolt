// ---------------------------------------------------------------------------
// Glyph atlas built with GDI and uploaded as one texture-array layer.
//
// Text rendering is the one place where a from-scratch implementation is not
// worth the detour at this stage: GDI already produces correct, hinted,
// antialiased glyph coverage for any installed font. The atlas therefore
// rasterises the printable ASCII range once, uploads the coverage as an RGBA
// layer, and every later frame draws text as one textured quad per glyph.
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

    // Rasterises the printable ASCII range at |pixelHeight| using |faceName| and
    // uploads the result into |layer| of the renderer's texture array.
    //
    // |textureLayerSize| is the side of the texture array in pixels. The glyph
    // bitmap itself is only as large as the glyph grid needs, but UVs must be
    // normalised against the layer, not against the bitmap: mixing the two makes
    // every glyph sample the wrong part of the atlas.
    bool Build(UIRenderer& renderer, const wchar_t* faceName, int pixelHeight, int layer,
               int textureLayerSize, std::string* error);

    const Glyph* Find(char c) const;

    float LineHeight() const { return lineHeight_; }
    float Ascent() const { return ascent_; }

    // Width of |text| at |scale|, in UI pixels.
    float Measure(const std::string& text, float scale) const;

    // Queues one quad per glyph. |x|, |y| name the top-left corner of the line box.
    void Draw(UIRenderer& renderer, float x, float y, const std::string& text, Color color, float scale) const;

private:
    std::unordered_map<char, Glyph> glyphs_;
    std::string                     faceName_;
    int                             layer_ = 0;
    int                             textureLayerSize_ = 0;
    float                           lineHeight_ = 0.0f;
    float                           ascent_ = 0.0f;
    float                           atlasSize_ = 0.0f;
};
