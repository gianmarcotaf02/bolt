// ---------------------------------------------------------------------------
// Shell icon atlas.
//
// File and folder icons come from the shell, not from the application: that is
// what makes a file manager look like Windows rather than like a rectangle
// simulator. SHGetFileInfoW resolves an icon for an extension or an attribute
// set, and the result is rasterised into one layer of the renderer's texture
// array, laid out as a grid of cells.
//
// Two decisions worth stating:
//
//  * Icons are keyed by extension (or by "directory"), not per file. The shell
//    returns the same icon for every .txt file, so caching per extension keeps
//    the atlas small and the lookup free. SHGFI_USEFILEATTRIBUTES means the call
//    never touches the disk.
//
//  * The atlas is rasterised at 32x32 and drawn at roughly half that, so it
//    still looks correct when the UI is zoomed in.
//
//  * There are two ways of asking, and they are not interchangeable. `GetFor`
//    names an **extension**, the shell answers without touching the disk, and
//    the cache stays small however long the list is. `GetForObject` names a
//    **shell object** — a drive, a known folder, the recycle bin — and the shell
//    looks at it: it is the only way to get the icon a drive really has, and it
//    is meant for the places where the set is small and closed, because this
//    atlas has no eviction and one icon per file would fill it.
// ---------------------------------------------------------------------------
#pragma once

#include "ui_renderer.h"

#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

class ShellIconAtlas
{
public:
    struct IconUV
    {
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    };

    ShellIconAtlas() = default;
    ~ShellIconAtlas();

    ShellIconAtlas(const ShellIconAtlas&) = delete;
    ShellIconAtlas& operator=(const ShellIconAtlas&) = delete;

    // Allocates the atlas layer and warms the cache with the icons for folders
    // and for entries without an extension.
    bool Initialize(IRenderer& renderer, int layer, int atlasSize, std::string* error);

    // Returns the atlas rectangle for one entry. |extension| is matched case
    // insensitively and may be empty. Never returns null: unknown extensions fall
    // back to the generic file icon.
    const IconUV* GetFor(const std::string& extension, bool isDirectory);

    // The icon of one shell object, looked at rather than guessed from a name:
    // a drive root ("D:\\"), a known folder, "This PC", the recycle bin — either a
    // real path or a namespace name (`::{...}`). Null when the shell has no icon
    // for it, so the caller keeps its own fallback.
    const IconUV* GetForObject(const std::wstring& location);

    int IconCount() const { return static_cast<int>(cells_.size()); }

    // Quanti pixel misura il disegno dentro una celletta. E' la misura oltre la
    // quale un'icona va chiesta **grande** invece di essere ingrandita: ingrandire
    // questo valore si vede, ed e' il punto in cui la vista a icone grandi si
    // sgranava. Derivato dalla cella, non scelto a occhio.
    int IconPixels() const { return cellSize_ - kCellPadding * 2; }

private:
    // How the icon is asked for, which decides the flags: a synthetic name plus
    // attributes (no disk access), a real path, or a PIDL.
    enum class Source { SyntheticName, RealPath, Pidl };

    // Rasterises one icon and uploads it into the next free cell. |name| is a
    // path, or a synthetic name, or a PIDL — never a string that was built from a
    // PIDL, because a PIDL has NULs inside it. Returns null when the atlas is
    // full or the shell has no icon for the key.
    const IconUV* Rasterize(const std::string& key, LPCWSTR name, DWORD attributes, Source source);

    // L'icona gia' in mano, ovunque sia stata presa: la converte in RGBA e la
    // carica nella prima celletta libera. Non distrugge |icon|.
    const IconUV* UploadIcon(const std::string& key, HICON icon);

    IRenderer* renderer_ = nullptr;
    int layer_ = 0;
    int atlasSize_ = 0;
    int cellSize_ = 0;
    int columns_ = 0;

    std::unordered_map<std::string, IconUV> cache_;
    std::vector<std::string>                cells_;   // key per occupied cell
};
