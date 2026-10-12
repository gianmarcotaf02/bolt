#include "font_atlas.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kFirstChar = 32;    // spazio
constexpr int kLastChar  = 126;   // tilde

// Il blocco latino-1: gli accenti delle lingue vicine, che nei nomi dei file
// italiani si vedono tutti i giorni. Si rasterizza tutto il blocco invece di
// elencare le lettere una per una: sono 96 caratteri, costano una manciata di
// celle, e nessuno deve ricordarsi di aggiungere la "u" accentata.
constexpr uint32_t kFirstLatin1 = 0x00A0;
constexpr uint32_t kLastLatin1  = 0x00FF;

// E i pochi simboli che l'interfaccia disegna: le frecce della barra strumenti, la
// stella dei segnalibri, i puntini di sospensione, i separatori.
constexpr wchar_t kExtraChars[] =
    L"\u2190\u2192\u2191\u2193\u21BB\u2605\u2606"      // frecce, stella piena e vuota
    L"\u2026\u00B7\u2022"                            // puntini, separatori
    L"\u2013\u2014\u2018\u2019\u201C\u201D";       // trattini e virgolette tipografiche

// Transparent gutter around every glyph cell. Without it, a quad sampled with
// GL_LINEAR at anything other than an exact texel centre drags in the edge of the
// neighbouring glyph, which shows up as ghosting along the strokes. The icon and
// thumbnail atlases already keep a gutter; the glyph atlas did not.
constexpr int kGlyphPadding = 2;

struct DibSection
{
    HBITMAP  bitmap  = nullptr;
    HDC      dc      = nullptr;
    void*    pixels  = nullptr;
    uint32_t* pixels32 = nullptr;
    int      width   = 0;
    int      height  = 0;

    ~DibSection()
    {
        if (dc != nullptr)     DeleteDC(dc);
        if (bitmap != nullptr) DeleteObject(bitmap);
    }
};

// Creates a 32-bit top-down DIB section and a memory DC already selecting it.
bool CreateDib(DibSection& dib, int width, int height)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = width;
    info.bmiHeader.biHeight      = -height;   // negative => top-down
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    if (screen == nullptr)
        return false;

    dib.bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &dib.pixels, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (dib.bitmap == nullptr)
        return false;

    dib.dc = CreateCompatibleDC(nullptr);
    if (dib.dc == nullptr)
        return false;

    SelectObject(dib.dc, dib.bitmap);
    dib.pixels32 = static_cast<uint32_t*>(dib.pixels);
    dib.width    = width;
    dib.height   = height;
    return true;
}

// Rounds up to a multiple of 64 so the atlas size stays a comfortable number.
int RoundUp(int value, int multiple)
{
    return ((value + multiple - 1) / multiple) * multiple;
}

} // namespace

FontAtlas::~FontAtlas() = default;

bool FontAtlas::Build(UIRenderer& renderer, const wchar_t* faceName, int pixelHeight, int layer,
                      int textureLayerSize, std::string* error)
{
    if (pixelHeight <= 0)
    {
        if (error != nullptr) *error = "font pixel height must be positive";
        return false;
    }
    if (textureLayerSize <= 0)
    {
        if (error != nullptr) *error = "font atlas needs a positive texture layer size";
        return false;
    }

    glyphs_.clear();
    layer_ = layer;
    textureLayerSize_ = textureLayerSize;

    // --- measure the glyph set first so the atlas can be sized exactly ------
    HDC measuring = CreateCompatibleDC(nullptr);
    if (measuring == nullptr)
    {
        if (error != nullptr) *error = "CreateCompatibleDC failed";
        return false;
    }

    // ClearType rather than plain grayscale antialiasing. At the sizes a file
    // manager uses, grayscale antialiasing produces noticeably thinner and
    // lighter stems than every other window on the desktop, which reads as low
    // quality next to them. ClearType's stem darkening is the difference; the
    // subpixel colouring is discarded below, so no colour fringing survives.
    HFONT font = CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, faceName);
    if (font == nullptr)
    {
        DeleteDC(measuring);
        if (error != nullptr) *error = "CreateFontW failed for the requested face";
        return false;
    }

    HGDIOBJ previous = SelectObject(measuring, font);

    TEXTMETRICW metrics{};
    GetTextMetricsW(measuring, &metrics);
    const int cellHeight = metrics.tmHeight;
    ascent_     = static_cast<float>(metrics.tmAscent);
    lineHeight_ = static_cast<float>(cellHeight);

    // Quali caratteri: ASCII, latino-1, e l'elenco dei simboli.
    std::vector<uint32_t> characters;
    characters.reserve((kLastChar - kFirstChar + 1) + (kLastLatin1 - kFirstLatin1 + 1) + 16);
    for (uint32_t c = kFirstChar; c <= static_cast<uint32_t>(kLastChar); ++c)
        characters.push_back(c);
    for (uint32_t c = kFirstLatin1; c <= kLastLatin1; ++c)
        characters.push_back(c);
    for (const wchar_t c : kExtraChars)
        characters.push_back(static_cast<uint32_t>(c));

    int cellWidth = 1;
    for (const uint32_t codepoint : characters)
    {
        const wchar_t wide = static_cast<wchar_t>(codepoint);
        SIZE size{};
        GetTextExtentPoint32W(measuring, &wide, 1, &size);
        cellWidth = (std::max)(cellWidth, static_cast<int>(size.cx));
    }
    cellWidth += 1;   // one pixel of slack so overhangs are not clipped

    SelectObject(measuring, previous);
    DeleteObject(font);
    DeleteDC(measuring);

    // --- lay the glyph grid out and rasterise it ---------------------------
    const int glyphCount = static_cast<int>(characters.size());
    const int columns    = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(glyphCount))));
    const int rows       = (glyphCount + columns - 1) / columns;

    const int slotWidth  = cellWidth  + kGlyphPadding * 2;
    const int slotHeight = cellHeight + kGlyphPadding * 2;

    const int neededWidth  = columns * slotWidth;
    const int neededHeight = rows * slotHeight;
    const int atlasSize    = RoundUp((std::max)((std::max)(neededWidth, neededHeight), 256), 64);

    if (atlasSize > textureLayerSize)
    {
        if (error != nullptr)
            *error = "font atlas needs a larger texture layer";
        return false;
    }

    DibSection dib;
    if (!CreateDib(dib, atlasSize, atlasSize))
    {
        if (error != nullptr) *error = "CreateDIBSection failed";
        return false;
    }

    // The DIB starts zeroed, which is black with no coverage.
    std::fill(dib.pixels32, dib.pixels32 + static_cast<size_t>(atlasSize) * atlasSize, 0u);

    HFONT drawFont = CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, faceName);
    if (drawFont == nullptr)
    {
        if (error != nullptr) *error = "CreateFontW failed while rasterising";
        return false;
    }

    HGDIOBJ previousDraw = SelectObject(dib.dc, drawFont);
    SetBkMode(dib.dc, TRANSPARENT);
    SetTextColor(dib.dc, RGB(255, 255, 255));

    for (size_t index = 0; index < characters.size(); ++index)
    {
        const int cellX = (static_cast<int>(index) % columns) * slotWidth  + kGlyphPadding;
        const int cellY = (static_cast<int>(index) / columns) * slotHeight + kGlyphPadding;

        RECT cell{cellX, cellY, cellX + cellWidth, cellY + cellHeight};
        const wchar_t character = static_cast<wchar_t>(characters[index]);
        DrawTextW(dib.dc, &character, 1, &cell, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);

        SIZE size{};
        GetTextExtentPoint32W(dib.dc, &character, 1, &size);

        const float normalise = static_cast<float>(textureLayerSize);
        Glyph glyph;
        glyph.u0      = static_cast<float>(cellX) / normalise;
        glyph.v0      = static_cast<float>(cellY) / normalise;
        glyph.u1      = static_cast<float>(cellX + size.cx + 1) / normalise;
        glyph.v1      = static_cast<float>(cellY + cellHeight) / normalise;
        glyph.advance = static_cast<float>(size.cx);
        glyph.width   = static_cast<float>(size.cx);
        glyphs_.emplace(characters[index], glyph);
    }

    SelectObject(dib.dc, previousDraw);
    DeleteObject(drawFont);

    // --- convert coverage to a white RGBA layer ----------------------------
    // GDI does not write the alpha channel, so the coverage lands in the colour
    // channels of the DIB. With ClearType the three channels are the three
    // subpixel coverages, so their mean is the grayscale coverage: averaging is
    // what turns subpixel rendering back into the single alpha the shader wants,
    // without the colour fringes.
    std::vector<uint8_t> rgba(static_cast<size_t>(atlasSize) * atlasSize * 4);
    for (size_t i = 0; i < static_cast<size_t>(atlasSize) * atlasSize; ++i)
    {
        const uint32_t pixel = dib.pixels32[i];
        const uint32_t red   = (pixel >> 16) & 0xFF;
        const uint32_t green = (pixel >> 8) & 0xFF;
        const uint32_t blue  = pixel & 0xFF;
        const uint8_t  coverage = static_cast<uint8_t>((red + green + blue) / 3);
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage;
    }

    renderer.UploadLayerRegion(layer, rgba.data(), atlasSize, atlasSize, 0, 0);
    atlasSize_ = static_cast<float>(atlasSize);
    return true;
}

const FontAtlas::Glyph* FontAtlas::Find(uint32_t codepoint) const
{
    const auto it = glyphs_.find(codepoint);
    return it == glyphs_.end() ? nullptr : &it->second;
}

float FontAtlas::MeasureOne(uint32_t codepoint, float scale) const
{
    const Glyph* glyph = Find(codepoint);
    return ((glyph != nullptr) ? glyph->advance : lineHeight_ * 0.5f) * scale;
}

namespace Utf8
{

size_t Length(const std::string& text, size_t index)
{
    if (index >= text.size())
        return 0;

    const unsigned char lead = static_cast<unsigned char>(text[index]);
    size_t length = 1;
    if ((lead & 0x80u) == 0x00u)      length = 1;
    else if ((lead & 0xE0u) == 0xC0u) length = 2;
    else if ((lead & 0xF0u) == 0xE0u) length = 3;
    else if ((lead & 0xF8u) == 0xF0u) length = 4;
    else                              return 1;   // byte di continuazione orfano

    // Una sequenza tronca si legge per quello che c'e': meglio un carattere
    // sbagliato in fondo a un nome che un indice fuori dalla stringa.
    return (index + length <= text.size()) ? length : (text.size() - index);
}

size_t Next(const std::string& text, size_t index)
{
    return index + Length(text, index);
}

uint32_t At(const std::string& text, size_t index, size_t* length)
{
    const size_t size = Length(text, index);
    if (length != nullptr)
        *length = size;
    if (size == 0)
        return 0;

    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (size == 1)
        return lead;

    uint32_t codepoint = lead & (0xFFu >> (size + 1));
    for (size_t i = 1; i < size; ++i)
        codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[index + i]) & 0x3Fu);
    return codepoint;
}

} // namespace Utf8

float FontAtlas::Measure(const std::string& text, float scale) const
{
    float width = 0.0f;
    for (size_t i = 0; i < text.size(); i = Utf8::Next(text, i))
    {
        const Glyph* glyph = Find(Utf8::At(text, i, nullptr));
        if (glyph != nullptr)
            width += glyph->advance;
        else
            width += lineHeight_ * 0.5f;
    }
    return width * scale;
}

void FontAtlas::Draw(UIRenderer& renderer, float x, float y, const std::string& text, Color color, float scale) const
{
    float penX = x;
    for (size_t i = 0; i < text.size(); i = Utf8::Next(text, i))
    {
        const uint32_t codepoint = Utf8::At(text, i, nullptr);
        const Glyph* glyph = Find(codepoint);
        if (glyph == nullptr)
        {
            penX += lineHeight_ * 0.5f * scale;
            continue;
        }

        // Space and other blank glyphs have no ink; advance the pen only.
        if (codepoint != ' ')
        {
            const float atlas = static_cast<float>(textureLayerSize_);
            // Snap the origin to whole pixels. The pen itself stays fractional so
            // the advances do not drift, but a glyph landing on a half pixel is
            // resampled by the linear filter and loses its sharpness.
            const Rect destination{std::floor(penX + 0.5f), std::floor(y + 0.5f),
                                   (glyph->u1 - glyph->u0) * atlas * scale,
                                   (glyph->v1 - glyph->v0) * atlas * scale};
            renderer.Draw(UIDraw::Image(destination, color, static_cast<float>(layer_),
                                        glyph->u0, glyph->v0, glyph->u1, glyph->v1, /*monoMask=*/false));
        }
        penX += glyph->advance * scale;
    }
}
