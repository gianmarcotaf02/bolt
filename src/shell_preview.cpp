#include "shell_preview.h"

#include <shobjidl.h>

#include <algorithm>
#include <cctype>

namespace {

// Types the shell can produce a real thumbnail for. Anything else is answered
// with "unavailable" without ever waking the worker, so a folder full of source
// files costs nothing.
bool IsPreviewableExtension(const std::string& extension)
{
    static const char* kExtensions[] = {
        // images
        "jpg", "jpeg", "jpe", "jfif", "png", "gif", "bmp", "webp", "tif", "tiff",
        "heic", "heif", "avif", "ico",
        // photographs and design files with a registered handler
        "raw", "cr2", "cr3", "nef", "arw", "dng", "psd",
        // video
        "mp4", "m4v", "mov", "avi", "wmv", "mkv", "webm", "mpg", "mpeg", "3gp", "flv",
        // documents whose preview handler is part of a default install
        "pdf", "docx", "xlsx", "pptx",
    };

    for (const char* candidate : kExtensions)
    {
        if (extension == candidate)
            return true;
    }
    return false;
}

struct DibReader
{
    HDC       dc     = nullptr;
    HBITMAP   bitmap = nullptr;

    ~DibReader()
    {
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (dc != nullptr)     DeleteDC(dc);
    }
};

// Copies a bitmap into a top-down BGRA buffer at its native size. |width| and
// |height| come from GetObjectW, so no resampling happens here: GetImage already
// returned the bitmap at the size that was asked for.
bool ReadBitmapPixels(HBITMAP bitmap, int width, int height, std::vector<uint32_t>& out)
{
    if (width <= 0 || height <= 0)
        return false;

    out.assign(static_cast<size_t>(width) * height, 0u);

    BITMAPINFO info{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = width;
    info.bmiHeader.biHeight      = -height;   // negative => top-down
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
        return false;

    const int copied = GetDIBits(dc, bitmap, 0, static_cast<UINT>(height),
                                 out.data(), &info, DIB_RGB_COLORS);
    DeleteDC(dc);
    return copied != 0;
}

// Thumbnail providers do not agree on the alpha convention. Three cases occur:
// no alpha channel at all (a device-dependent bitmap), straight alpha, and
// premultiplied alpha. The rest of the pipeline assumes straight alpha, so the
// buffer is normalised here before it can reach the shader.
void NormaliseAlpha(std::vector<uint32_t>& pixels)
{
    int maxAlpha = 0;
    for (const uint32_t pixel : pixels)
    {
        const int alpha = static_cast<int>(pixel >> 24);
        if (alpha > maxAlpha)
            maxAlpha = alpha;
    }

    if (maxAlpha == 0)
    {
        // No alpha channel: treat every pixel as opaque.
        for (uint32_t& pixel : pixels)
            pixel |= 0xFF000000u;
        return;
    }

    // Premultiplied data never has a colour channel larger than alpha; straight
    // data almost always does somewhere in the image (a bright highlight edge).
    bool premultiplied = true;
    for (const uint32_t pixel : pixels)
    {
        const int alpha = static_cast<int>(pixel >> 24);
        if (alpha == 0)
            continue;
        const int red   = static_cast<int>((pixel >> 16) & 0xFF);
        const int green = static_cast<int>((pixel >> 8) & 0xFF);
        const int blue  = static_cast<int>(pixel & 0xFF);
        if (red > alpha || green > alpha || blue > alpha)
        {
            premultiplied = false;
            break;
        }
    }

    if (!premultiplied)
        return;

    for (uint32_t& pixel : pixels)
    {
        const int alpha = static_cast<int>(pixel >> 24);
        if (alpha == 0)
        {
            pixel = 0;
            continue;
        }
        const int red   = (std::min)(255, static_cast<int>((pixel >> 16) & 0xFF) * 255 / alpha);
        const int green = (std::min)(255, static_cast<int>((pixel >> 8) & 0xFF) * 255 / alpha);
        const int blue  = (std::min)(255, static_cast<int>(pixel & 0xFF) * 255 / alpha);
        pixel = (static_cast<uint32_t>(alpha) << 24) | (static_cast<uint32_t>(red) << 16) |
                (static_cast<uint32_t>(green) << 8) | static_cast<uint32_t>(blue);
    }
}

} // namespace

ShellPreviewCache::~ShellPreviewCache()
{
    Shutdown();
}

bool ShellPreviewCache::Initialize(UIRenderer& renderer, int layer, int atlasSize, int cellSize,
                                   int cacheCapacity, std::string* error)
{
    if (cellSize <= padding_ * 2 || atlasSize < cellSize)
    {
        if (error != nullptr) *error = "preview atlas too small for one cell";
        return false;
    }

    renderer_  = &renderer;
    layer_     = layer;
    atlasSize_ = atlasSize;
    cellSize_  = cellSize;
    columns_   = atlasSize_ / cellSize_;
    capacity_  = (std::min)(cacheCapacity, columns_ * columns_);
    if (capacity_ < 1)
        capacity_ = 1;

    cache_.clear();
    lru_.clear();
    freeCells_.clear();
    nextCell_ = 0;

    stopping_ = false;
    worker_ = std::thread(&ShellPreviewCache::WorkerMain, this);
    return true;
}

void ShellPreviewCache::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!worker_.joinable())
            return;
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

void ShellPreviewCache::WorkerMain()
{
    // The shell imaging APIs need an apartment. MTA is the right choice on a
    // background thread: no window, no message pump, and the factory is created
    // and released entirely inside this loop so nothing is marshalled across.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    for (;;)
    {
        std::pair<std::wstring, std::wstring> request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !requests_.empty(); });
            if (stopping_ && requests_.empty())
                break;
            request = std::move(requests_.front());
            requests_.pop_front();
        }

        std::vector<uint8_t> rgba;
        const bool decoded = RasterizeCell(request.second, rgba);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (decoded)
            {
                // |inFlight_| is cleared by Pump, after the upload, so a second
                // query arriving in between cannot queue a duplicate decode.
                results_.push_back(Result{request.first, std::move(rgba)});
            }
            else
            {
                inFlight_.erase(request.first);
                failed_.insert(request.first);
            }
        }
    }

    if (SUCCEEDED(com))
        CoUninitialize();
}

bool ShellPreviewCache::RasterizeCell(const std::wstring& path, std::vector<uint8_t>& rgba)
{
    IShellItemImageFactory* factory = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr,
                                             IID_PPV_ARGS(&factory));
    if (FAILED(hr) || factory == nullptr)
        return false;

    const int inner = cellSize_ - padding_ * 2;
    const SIZE size{inner, inner};
    // THUMBNAILONLY keeps the shell from handing back the type icon as a
    // "preview": a file with no provider must fail here so the caller falls back
    // to the icon atlas instead of duplicating it.
    const SIIGBF flags = static_cast<SIIGBF>(SIIGBF_RESIZETOFIT | SIIGBF_THUMBNAILONLY);

    HBITMAP bitmap = nullptr;
    hr = factory->GetImage(size, flags, &bitmap);
    factory->Release();

    if (FAILED(hr) || bitmap == nullptr)
        return false;

    BITMAP header{};
    if (GetObjectW(bitmap, sizeof(header), &header) == 0 || header.bmWidth <= 0 || header.bmHeight <= 0)
    {
        DeleteObject(bitmap);
        return false;
    }

    std::vector<uint32_t> pixels;
    const bool read = ReadBitmapPixels(bitmap, header.bmWidth, header.bmHeight, pixels);
    const int sourceWidth  = header.bmWidth;
    const int sourceHeight = header.bmHeight;
    DeleteObject(bitmap);

    if (!read)
        return false;

    NormaliseAlpha(pixels);

    // GetImage honours RESIZETOFIT, so the source is already at or below the
    // cell. A nearest-neighbour copy into the centred cell is therefore close to
    // a 1:1 blit; anything larger only happens when the provider returns a small
    // image, in which case no scaling is wanted either.
    const float scale = (std::min)(static_cast<float>(inner) / sourceWidth,
                                   static_cast<float>(inner) / sourceHeight);
    const int drawWidth  = (std::max)(1, static_cast<int>(sourceWidth * scale));
    const int drawHeight = (std::max)(1, static_cast<int>(sourceHeight * scale));
    const int originX    = padding_ + (inner - drawWidth) / 2;
    const int originY    = padding_ + (inner - drawHeight) / 2;

    rgba.assign(static_cast<size_t>(cellSize_) * cellSize_ * 4, 0);

    for (int y = 0; y < drawHeight; ++y)
    {
        const int sourceY = (std::min)(sourceHeight - 1, static_cast<int>((y + 0.5f) / scale));
        for (int x = 0; x < drawWidth; ++x)
        {
            const int sourceX = (std::min)(sourceWidth - 1, static_cast<int>((x + 0.5f) / scale));
            const uint32_t pixel = pixels[static_cast<size_t>(sourceY) * sourceWidth + sourceX];

            const size_t destination =
                (static_cast<size_t>(originY + y) * cellSize_ + (originX + x)) * 4;
            rgba[destination + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFF);   // red
            rgba[destination + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);    // green
            rgba[destination + 2] = static_cast<uint8_t>(pixel & 0xFF);           // blue
            rgba[destination + 3] = static_cast<uint8_t>((pixel >> 24) & 0xFF);   // alpha
        }
    }

    return true;
}

void ShellPreviewCache::Pump()
{
    std::deque<Result> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(results_);
    }

    for (const Result& result : ready)
    {
        // A key still marked in flight is one the UI is still waiting for. If it
        // was cancelled while decoding, the result is stale and is thrown away
        // instead of taking up an atlas cell.
        bool stillWanted = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stillWanted = inFlight_.erase(result.key) != 0;
        }
        if (stillWanted)
            InsertReady(result.key, result.rgba);
    }
}

void ShellPreviewCache::CancelPending()
{
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.clear();
    inFlight_.clear();
}

int ShellPreviewCache::AcquireCell()
{
    if (!freeCells_.empty())
    {
        const int cell = freeCells_.back();
        freeCells_.pop_back();
        return cell;
    }

    if (static_cast<int>(cache_.size()) < capacity_ && nextCell_ < columns_ * columns_)
        return nextCell_++;

    if (lru_.empty())
        return -1;

    // Evict the least recently used preview and reuse its cell. Its pixels stay
    // in the atlas but nothing maps to them any more, so they are never sampled.
    const std::wstring victim = lru_.back();
    lru_.pop_back();
    const auto it = cache_.find(victim);
    if (it == cache_.end())
        return -1;
    const int cell = it->second.cell;
    cache_.erase(it);
    return cell;
}

void ShellPreviewCache::InsertReady(const std::wstring& key, const std::vector<uint8_t>& rgba)
{
    if (renderer_ == nullptr || rgba.size() != static_cast<size_t>(cellSize_) * cellSize_ * 4)
        return;
    if (cache_.find(key) != cache_.end())
        return;   // already resident; the duplicate decode is simply dropped

    const int cell = AcquireCell();
    if (cell < 0)
        return;

    const int cellX = (cell % columns_) * cellSize_;
    const int cellY = (cell / columns_) * cellSize_;

    renderer_->UploadLayerRegion(layer_, rgba.data(), cellSize_, cellSize_, cellX, cellY);

    const float atlas = static_cast<float>(atlasSize_);
    IconUV uv;
    uv.u0 = static_cast<float>(cellX + padding_) / atlas;
    uv.v0 = static_cast<float>(cellY + padding_) / atlas;
    uv.u1 = static_cast<float>(cellX + cellSize_ - padding_) / atlas;
    uv.v1 = static_cast<float>(cellY + cellSize_ - padding_) / atlas;

    lru_.push_front(key);
    cache_.emplace(key, Entry{uv, cell, lru_.begin()});
}

bool ShellPreviewCache::IsPreviewable(const std::string& extension)
{
    return IsPreviewableExtension(extension);
}

ShellPreviewCache::State ShellPreviewCache::Query(const std::wstring& path, const std::string& extension,
                                                  IconUV* outUV)
{
    if (renderer_ == nullptr || !IsPreviewableExtension(extension))
        return State::Unavailable;

    // cache_ and the LRU list are touched only from the render thread, but the
    // in-flight and failure sets live behind the mutex; take it once and do both.
    std::lock_guard<std::mutex> lock(mutex_);

    const auto cached = cache_.find(path);
    if (cached != cache_.end())
    {
        lru_.erase(cached->second.lru);
        lru_.push_front(path);
        cached->second.lru = lru_.begin();
        if (outUV != nullptr)
            *outUV = cached->second.uv;
        return State::Ready;
    }

    if (failed_.count(path) != 0)
        return State::Unavailable;
    if (inFlight_.count(path) != 0)
        return State::Pending;

    inFlight_.insert(path);
    requests_.emplace_back(path, path);
    wake_.notify_one();
    return State::Pending;
}
