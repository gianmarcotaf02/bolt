// ---------------------------------------------------------------------------
// Shell thumbnail cache.
//
// Icons answer the question "what kind of file is this"; previews answer "what
// is inside this file". IShellItemImageFactory is the supported way to ask
// Windows for the latter: the shell picks whichever thumbnail provider is
// registered for the type (the imaging codec for photos, the media foundation
// frame grabber for video, the office preview handler for documents) and hands
// back a bitmap already sized for the request.
//
// Two decisions worth stating:
//
//  * Decoding runs on a worker thread. A cold JPEG on a network share can take
//    tens of milliseconds, which would be a visible stall in the frame loop.
//    The worker produces straight-alpha RGBA into a cell-sized buffer and leaves
//    it in a queue; the render thread performs the GL upload, because a texture
//    upload must happen on the thread that owns the context.
//
//  * The atlas has a fixed number of cells and a least-recently-used eviction
//    policy, the same shape as the reference application's AssetCacheSize bound.
//    Scrolling a large folder must not grow GPU memory without limit.
//
// The mechanism is original work; IShellItemImageFactory is a documented shell
// API and the reference application uses the same interface for its thumbnails.
// ---------------------------------------------------------------------------
#pragma once

#include "ui_renderer.h"

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ShellPreviewCache
{
public:
    struct IconUV
    {
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    };

    enum class State
    {
        Ready,       // |outUV| is valid
        Pending,     // queued or decoding; draw the icon for now
        Unavailable, // no provider, decode failed, or the type is never previewed
    };

    ShellPreviewCache() = default;
    ~ShellPreviewCache();

    ShellPreviewCache(const ShellPreviewCache&) = delete;
    ShellPreviewCache& operator=(const ShellPreviewCache&) = delete;

    // Allocates no GPU memory by itself: |layer| must be one of the layers the
    // renderer's texture array already has. |cellSize| is the square cell side
    // in pixels, |cacheCapacity| the number of live previews before eviction.
    bool Initialize(IRenderer& renderer, int layer, int atlasSize, int cellSize,
                    int cacheCapacity, std::string* error);

    // Stops the worker and joins it. Safe to call twice.
    void Shutdown();

    // Render thread, once per frame before drawing: uploads any preview that
    // finished decoding since the last call.
    void Pump();

    // Drops every request that has not been uploaded yet. Called when the
    // directory changes so leaving a large folder does not keep the worker busy
    // decoding thumbnails that are no longer on screen. A decode already in
    // flight finishes and its result is discarded.
    void CancelPending();

    // Render thread. |path| is a full path. Calling this for the same path twice
    // is free and never queues a second decode.
    State Query(const std::wstring& path, const std::string& extension, IconUV* outUV);

    // L'icona **alla misura della cella**, per le viste a icone grandi. Non e' una
    // miniatura — dentro un `.txt` non c'e' niente da guardare — ma nemmeno
    // l'icona dell'atlante ingrandita: quella e' rasterizzata a 32 px, e a 128 si
    // vede. Qui la misura la da' la shell.
    //
    // Si chiede per **tipo** e non per file, quindi duecento `.txt` sono una cella
    // sola; |path| e' il primo file di quel tipo che si incontra e serve solo a
    // dare alla shell qualcosa da guardare.
    State QueryIcon(const std::string& extension, bool isDirectory, const std::wstring& path,
                    IconUV* outUV);

    // True when the type is worth asking the shell about. Text and executables
    // are never previewed, which keeps the queue short while scrolling.
    static bool IsPreviewable(const std::string& extension);

    int ReadyCount() const { return static_cast<int>(cache_.size()); }
    int Capacity() const { return capacity_; }

private:
    void WorkerMain();
    bool RasterizeCell(const Request& request, std::vector<uint8_t>& rgba);
    int  AcquireCell();
    void InsertReady(const std::wstring& key, const std::vector<uint8_t>& rgba);

    IRenderer* renderer_ = nullptr;
    int layer_     = 0;
    int atlasSize_ = 0;
    int cellSize_  = 0;
    int padding_   = 2;
    int columns_   = 0;
    int capacity_  = 0;

    struct Entry
    {
        IconUV uv;
        int    cell = 0;
        std::list<std::wstring>::iterator lru;
    };

    // Render thread only.
    std::unordered_map<std::wstring, Entry> cache_;
    std::list<std::wstring>                 lru_;       // front is most recent
    std::vector<int>                        freeCells_;
    int                                     nextCell_ = 0;

    // Shared with the worker, guarded by |mutex_|.
    struct Result
    {
        std::wstring          key;
        std::vector<uint8_t>  rgba;
    };

    // Una richiesta al worker. |iconOnly| distingue le due domande: una miniatura
    // ("cosa c'e' dentro questo file") da un'icona ("di che tipo e'"), che sono
    // due modi diversi di chiedere la stessa cosa alla shell.
    struct Request
    {
        std::wstring key;
        std::wstring path;
        std::string  extension;
        bool         isDirectory = false;
        bool         iconOnly = false;
    };

    std::mutex                             mutex_;
    std::condition_variable                wake_;
    std::deque<Request>                    requests_;
    std::unordered_set<std::wstring>       inFlight_;   // requested but not yet uploaded
    std::deque<Result>                     results_;
    std::unordered_set<std::wstring>       failed_;
    bool                                   stopping_ = false;
    std::thread                            worker_;
};
