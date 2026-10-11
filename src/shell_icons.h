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
    bool Initialize(UIRenderer& renderer, int layer, int atlasSize, std::string* error);

    // Returns the atlas rectangle for one entry. |extension| is matched case
    // insensitively and may be empty. Never returns null: unknown extensions fall
    // back to the generic file icon.
    const IconUV* GetFor(const std::string& extension, bool isDirectory);

    int IconCount() const { return static_cast<int>(cells_.size()); }

private:
    // Rasterises one icon identified by a synthetic name plus attributes and
    // uploads it into the next free cell. Returns null when the atlas is full or
    // the shell has no icon for the key.
    const IconUV* Rasterize(const std::string& key, const std::wstring& syntheticName, DWORD attributes);

    UIRenderer* renderer_ = nullptr;
    int layer_ = 0;
    int atlasSize_ = 0;
    int cellSize_ = 0;
    int columns_ = 0;

    std::unordered_map<std::string, IconUV> cache_;
    std::vector<std::string>                cells_;   // key per occupied cell
};
