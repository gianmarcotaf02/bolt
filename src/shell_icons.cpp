#include "shell_icons.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace {

constexpr int kCellPadding = 2;   // transparent gutter so filtering cannot bleed

struct DibSection
{
    HBITMAP   bitmap = nullptr;
    HDC       dc     = nullptr;
    uint32_t* pixels = nullptr;
    int       width  = 0;
    int       height = 0;

    ~DibSection()
    {
        if (dc != nullptr)     DeleteDC(dc);
        if (bitmap != nullptr) DeleteObject(bitmap);
    }
};

// Creates a top-down 32-bit DIB section with a memory DC already selecting it.
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

    void* bits = nullptr;
    dib.bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (dib.bitmap == nullptr)
        return false;

    dib.dc = CreateCompatibleDC(nullptr);
    if (dib.dc == nullptr)
        return false;

    SelectObject(dib.dc, dib.bitmap);
    dib.pixels = static_cast<uint32_t*>(bits);
    dib.width  = width;
    dib.height = height;
    return true;
}

// Reads a bitmap into a 32bpp top-down buffer of the given size.
bool ReadBitmap(HBITMAP bitmap, int width, int height, std::vector<uint32_t>& out)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = width;
    info.bmiHeader.biHeight      = -height;
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;

    out.assign(static_cast<size_t>(width) * height, 0u);

    HDC screen = GetDC(nullptr);
    if (screen == nullptr)
        return false;

    const int copied = GetDIBits(screen, bitmap, 0, static_cast<UINT>(height),
                                 out.data(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    return copied != 0;
}

std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

} // namespace

ShellIconAtlas::~ShellIconAtlas() = default;

bool ShellIconAtlas::Initialize(IRenderer& renderer, int layer, int atlasSize, std::string* error)
{
    renderer_  = &renderer;
    layer_     = layer;
    atlasSize_ = atlasSize;

    // The shell hands back icons at the system large-icon size; the atlas cell
    // is sized to match so no resampling happens on upload.
    const int iconSize = GetSystemMetrics(SM_CXICON);
    cellSize_ = (iconSize > 0 ? iconSize : 32) + kCellPadding * 2;
    columns_  = atlasSize_ / cellSize_;

    if (columns_ <= 0)
    {
        if (error != nullptr) *error = "icon atlas too small for one cell";
        return false;
    }

    cache_.clear();
    cells_.clear();

    // Warm the two entries every listing needs.
    GetFor("", true);
    GetFor("", false);

    return true;
}

const ShellIconAtlas::IconUV* ShellIconAtlas::Rasterize(const std::string& key,
                                                        const std::wstring& syntheticName,
                                                        DWORD attributes, bool mono)
{
    if (renderer_ == nullptr)
        return nullptr;

    const size_t capacity = static_cast<size_t>(columns_) * columns_;
    if (cells_.size() >= capacity)
        return nullptr;   // full: callers keep the last resolved icon as fallback

    SHFILEINFOW info{};
    const DWORD flags = SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES;
    if (SHGetFileInfoW(syntheticName.c_str(), attributes, &info, sizeof(info), flags) == 0)
        return nullptr;
    if (info.hIcon == nullptr)
        return nullptr;

    ICONINFO iconInfo{};
    if (GetIconInfo(info.hIcon, &iconInfo))
    {
        const int width  = cellSize_ - kCellPadding * 2;
        const int height = cellSize_ - kCellPadding * 2;

        std::vector<uint32_t> colour;
        const bool haveColour = ReadBitmap(iconInfo.hbmColor, width, height, colour);

        if (haveColour)
        {
            // A 32bpp icon bitmap carries premultiplied alpha. Detect whether the
            // icon actually uses it: icons predating the alpha convention have an
            // all-zero alpha channel and rely on the AND mask instead.
            bool hasAlpha = false;
            for (const uint32_t pixel : colour)
            {
                if ((pixel >> 24) != 0) { hasAlpha = true; break; }
            }

            std::vector<uint32_t> mask;
            if (!hasAlpha && iconInfo.hbmMask != nullptr)
                ReadBitmap(iconInfo.hbmMask, width, height, mask);

            // Stage one cell, then upload just that cell.
            std::vector<uint8_t> cell(static_cast<size_t>(cellSize_) * cellSize_ * 4, 0);

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const size_t index = static_cast<size_t>(y) * width + x;
                    const uint32_t bgra = colour[index];

                    uint8_t blue  = static_cast<uint8_t>(bgra & 0xFF);
                    uint8_t green = static_cast<uint8_t>((bgra >> 8) & 0xFF);
                    uint8_t red   = static_cast<uint8_t>((bgra >> 16) & 0xFF);
                    uint8_t alpha = static_cast<uint8_t>((bgra >> 24) & 0xFF);

                    if (hasAlpha)
                    {
                        // Undo the premultiplication so the shader can blend with
                        // straight alpha, as the rest of the pipeline assumes.
                        if (alpha == 0)
                        {
                            red = green = blue = 0;
                        }
                        else if (alpha != 255)
                        {
                            red   = static_cast<uint8_t>((std::min)(255, red   * 255 / alpha));
                            green = static_cast<uint8_t>((std::min)(255, green * 255 / alpha));
                            blue  = static_cast<uint8_t>((std::min)(255, blue  * 255 / alpha));
                        }
                    }
                    else if (!mask.empty())
                    {
                        // AND-mask convention: 0 shows the icon, 1 is transparent.
                        alpha = (mask[index] == 0) ? 255 : 0;
                    }
                    else
                    {
                        alpha = 255;
                    }

                    const size_t destination =
                        (static_cast<size_t>(y + kCellPadding) * cellSize_ + (x + kCellPadding)) * 4;

                    // Mono form: the coverage becomes the picture, in every
                    // channel. The shader reads the red one as the coverage and
                    // multiplies it by the tint the caller passed (ui.frag,
                    // FLAG_MONO), so a mask drawn white is a mask drawn in the
                    // theme colour.
                    if (mono)
                        red = green = blue = alpha;

                    cell[destination + 0] = red;
                    cell[destination + 1] = green;
                    cell[destination + 2] = blue;
                    cell[destination + 3] = alpha;
                }
            }

            const int cellIndex = static_cast<int>(cells_.size());
            const int cellX = (cellIndex % columns_) * cellSize_;
            const int cellY = (cellIndex / columns_) * cellSize_;

            renderer_->UploadLayerRegion(layer_, cell.data(), cellSize_, cellSize_, cellX, cellY);

            const float atlas = static_cast<float>(atlasSize_);
            IconUV uv;
            uv.u0 = static_cast<float>(cellX + kCellPadding) / atlas;
            uv.v0 = static_cast<float>(cellY + kCellPadding) / atlas;
            uv.u1 = static_cast<float>(cellX + cellSize_ - kCellPadding) / atlas;
            uv.v1 = static_cast<float>(cellY + cellSize_ - kCellPadding) / atlas;

            cells_.push_back(key);
            cache_.emplace(key, uv);
        }

        if (iconInfo.hbmColor != nullptr) DeleteObject(iconInfo.hbmColor);
        if (iconInfo.hbmMask != nullptr)  DeleteObject(iconInfo.hbmMask);
    }

    DestroyIcon(info.hIcon);

    const auto it = cache_.find(key);
    return it == cache_.end() ? nullptr : &it->second;
}

const ShellIconAtlas::IconUV* ShellIconAtlas::GetFor(const std::string& extension, bool isDirectory)
{
    if (isDirectory)
    {
        const auto it = cache_.find("<dir>");
        if (it != cache_.end())
            return &it->second;
        if (const IconUV* uv = Rasterize("<dir>", L"folder", FILE_ATTRIBUTE_DIRECTORY))
            return uv;
        return nullptr;
    }

    std::string key = "<file>";
    std::wstring synthetic = L"file";
    if (!extension.empty())
    {
        key.clear();
        for (const char c : extension)
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        key.insert(key.begin(), '.');
        synthetic = L"x" + Widen(key);
    }

    const auto it = cache_.find(key);
    if (it != cache_.end())
        return &it->second;

    if (const IconUV* uv = Rasterize(key, synthetic, FILE_ATTRIBUTE_NORMAL))
        return uv;

    // Unknown extension: fall back to the generic file icon, warming it if needed.
    const auto generic = cache_.find("<file>");
    if (generic != cache_.end())
        return &generic->second;
    return Rasterize("<file>", L"file", FILE_ATTRIBUTE_NORMAL);
}
