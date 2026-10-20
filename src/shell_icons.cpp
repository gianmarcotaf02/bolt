#include "shell_icons.h"

#include "shell_location.h"

#include <shlwapi.h>
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

// Il verso opposto, per le chiavi della cache: una posizione di spazio dei nomi
// viene da una stringa larga, e la chiave e' una stringa stretta.
std::string Utf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed,
                        nullptr, nullptr);
    return out;
}

// Toglie le virgolette da un percorso che ne porta: l'associazione di Windows
// scrive `"C:\...\EXCEL.EXE",0`, e le virgolette fanno parte della stringa.
std::wstring Unquote(const std::wstring& text)
{
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
        return text.substr(1, text.size() - 2);
    return text;
}

// Il ProgID che Windows usa per un'estensione: la scelta **dell'utente** se c'e'
// (`UserChoice`), altrimenti quella di sistema. Serve perche' l'icona va chiesta al
// ProgID, non all'estensione: `.csv` risponde `shell32.dll,0` — il documento
// generico — mentre `Excel.CSV` risponde `xlicons.exe,9`, che e' l'icona di Excel.
// La shell, quando le si chiede l'icona di un'estensione, si ferma un passo prima
// del ProgID, ed e' per questo che un `.csv` usciva anonimo.
std::wstring ProgIdOfExtension(const std::wstring& extension)
{
    const std::wstring userChoice =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\" + extension +
        L"\\UserChoice";
    HKEY  key = nullptr;
    DWORD size = 0;
    DWORD type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, userChoice.c_str(), 0, KEY_QUERY_VALUE, &key) ==
        ERROR_SUCCESS)
    {
        wchar_t progId[256]{};
        size = sizeof(progId);
        const LONG status = RegQueryValueExW(key, L"ProgId", nullptr, &type,
                                             reinterpret_cast<LPBYTE>(progId), &size);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS && type == REG_SZ && progId[0] != L'\0')
            return progId;
    }

    wchar_t progId[256]{};
    size = sizeof(progId);
    if (RegGetValueW(HKEY_CLASSES_ROOT, extension.c_str(), nullptr, RRF_RT_REG_SZ, nullptr,
                     progId, &size) == ERROR_SUCCESS && progId[0] != L'\0')
        return progId;
    return {};
}

// L'icona che Windows usa **davvero** per un'estensione: quella dell'applicazione
// che apre quei file.
//
// `SHGetFileInfoW` non basta: con `SHGFI_USEFILEATTRIBUTES` la shell non tocca il
// disco, e un'icona che vive dentro `EXCEL.EXE` — o dentro `xlicons.exe` — aprirla
// e' un accesso al file system, che in quella modalita' non si fa. Il **tipo** lo
// risolve lo stesso (`Excel.CSV`), l'icona no.
HICON IconForExtension(const std::string& extension, int pixels)
{
    if (extension.empty())
        return nullptr;

    std::string lower;
    lower.reserve(extension.size() + 1);
    lower.push_back('.');
    for (const char c : extension)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    const std::wstring narrow = Widen(lower);
    const std::wstring progId = ProgIdOfExtension(narrow);
    const std::wstring assoc  = progId.empty() ? narrow : progId;

    wchar_t iconPath[1024]{};
    DWORD length = sizeof(iconPath);
    if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_DEFAULTICON, assoc.c_str(), nullptr,
                                 iconPath, &length)))
        return nullptr;
    if (iconPath[0] == L'\0')
        return nullptr;

    // Un'applicazione del pacchetto non nomina un file, nomina una **risorsa**:
    // `@{Microsoft.WindowsNotepad_...?ms-resource://...}`. La si fa risolvere alla
    // shell; se non si risolve, si rinuncia invece di indovinare.
    if (iconPath[0] == L'@')
    {
        wchar_t resolved[1024]{};
        if (FAILED(SHLoadIndirectString(iconPath, resolved, 1024, nullptr)) || resolved[0] == L'\0')
            return nullptr;
        wcscpy_s(iconPath, resolved);
    }

    // `"C:\...\EXCEL.EXE",0` — il numero dopo la virgola e' l'indice dell'icona
    // dentro il file; senza virgola, un `.ico` che ne ha una sola.
    std::wstring file = iconPath;
    int index = 0;
    const size_t comma = file.rfind(L',');
    if (comma != std::wstring::npos)
    {
        index = _wtoi(file.c_str() + comma + 1);
        file.erase(comma);
    }

    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(Unquote(file).c_str(), index, 0, &icon, nullptr,
                                 static_cast<UINT>(pixels > 0 ? pixels : GetSystemMetrics(SM_CXICON)))))
        return nullptr;
    return icon;
}

// Legge un'icona in pixel BGRA a canale **diritto**, che e' cio' che il resto della
// pipeline si aspetta. Le icone a 32bpp portano l'alpha premoltiplicato; quelle
// precedenti a quella convenzione hanno il canale alpha a zero e si appoggiano alla
// maschera AND. Qui si tolgono entrambe le ambiguita': chi disegna non deve saperle.
bool ReadIconPixels(HICON icon, int width, int height, std::vector<uint32_t>* out)
{
    ICONINFO info{};
    if (icon == nullptr || width <= 0 || height <= 0 || GetIconInfo(icon, &info) == FALSE)
        return false;

    std::vector<uint32_t> colour;
    const bool haveColour = ReadBitmap(info.hbmColor, width, height, colour);

    if (haveColour)
    {
        bool hasAlpha = false;
        for (const uint32_t pixel : colour)
        {
            if ((pixel >> 24) != 0) { hasAlpha = true; break; }
        }

        std::vector<uint32_t> mask;
        if (!hasAlpha && info.hbmMask != nullptr)
            ReadBitmap(info.hbmMask, width, height, mask);

        out->assign(colour.size(), 0u);
        for (size_t index = 0; index < colour.size(); ++index)
        {
            const uint32_t bgra = colour[index];

            uint8_t blue  = static_cast<uint8_t>(bgra & 0xFF);
            uint8_t green = static_cast<uint8_t>((bgra >> 8) & 0xFF);
            uint8_t red   = static_cast<uint8_t>((bgra >> 16) & 0xFF);
            uint8_t alpha = static_cast<uint8_t>((bgra >> 24) & 0xFF);

            if (hasAlpha)
            {
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

            (*out)[index] = (static_cast<uint32_t>(alpha) << 24) |
                            (static_cast<uint32_t>(red) << 16) |
                            (static_cast<uint32_t>(green) << 8) |
                            static_cast<uint32_t>(blue);
        }
    }

    if (info.hbmColor != nullptr) DeleteObject(info.hbmColor);
    if (info.hbmMask != nullptr)  DeleteObject(info.hbmMask);
    return haveColour;
}

} // namespace

bool ExtensionIconPixels(const std::string& extension, int size, int* width, int* height,
                         std::vector<uint32_t>* pixels)
{
    // L'icona dell'applicazione che apre il tipo, **alla misura che serve a chi
    // disegna**: la stessa di prima, un'altra dimensione. Non e' un ingrandimento:
    // `SHDefExtractIconW` estrae la risorsa alla misura chiesta, quindi a 128 px si
    // vede un disegno a 128 px.
    HICON icon = IconForExtension(extension, size);
    if (icon == nullptr)
        return false;

    const bool read = ReadIconPixels(icon, size, size, pixels);
    DestroyIcon(icon);

    if (read)
    {
        if (width != nullptr)  *width = size;
        if (height != nullptr) *height = size;
    }
    return read;
}

ShellIconAtlas::~ShellIconAtlas() = default;

bool ShellIconAtlas::Initialize(IRenderer& renderer, int layer, int atlasSize, std::string* error)
{
    renderer_  = &renderer;
    layer_     = layer;
    atlasSize_ = atlasSize;

    // The shell hands back icons at the system large-icon size; the atlas cell
    // is sized to match so no resampling happens on upload.
    const int iconSize = GetSystemMetrics(SM_CXICON);
    iconPixels_ = (iconSize > 0 ? iconSize : 32);
    cellSize_   = iconPixels_ + kCellPadding * 2;
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

const ShellIconAtlas::IconUV* ShellIconAtlas::Rasterize(const std::string& key, LPCWSTR name,
                                                        DWORD attributes, Source source,
                                                        bool linkOverlay)
{
    SHFILEINFOW info{};
    DWORD flags = SHGFI_ICON | SHGFI_LARGEICON;
    switch (source)
    {
        case Source::SyntheticName: flags |= SHGFI_USEFILEATTRIBUTES; break;
        case Source::Pidl:          flags |= SHGFI_PIDL;             break;
        case Source::RealPath:                                       break;
    }
    if (linkOverlay)
        flags |= SHGFI_LINKOVERLAY;

    if (SHGetFileInfoW(name, attributes, &info, sizeof(info), flags) == 0)
        return nullptr;
    if (info.hIcon == nullptr)
        return nullptr;

    const IconUV* uv = UploadIcon(key, info.hIcon);
    DestroyIcon(info.hIcon);
    return uv;
}

// I pixel gia' in mano, BGRA a canale diritto. E' il punto in cui convergono tutte
// le strade — un'icona, una bitmap di menu, una specifica del registro — e l'unico
// che sa come si riempie una celletta.
const ShellIconAtlas::IconUV* ShellIconAtlas::UploadPixels(const std::string& key, int width,
                                                          int height,
                                                          const std::vector<uint32_t>& pixels)
{
    if (renderer_ == nullptr || width <= 0 || height <= 0 ||
        pixels.size() < static_cast<size_t>(width) * height)
        return nullptr;

    const size_t capacity = static_cast<size_t>(columns_) * columns_;
    if (cells_.size() >= capacity)
        return nullptr;   // full: callers keep the last resolved icon as fallback

    const int drawSize = cellSize_ - kCellPadding * 2;

    // L'immagine si porta alla misura della cella, **non** la si centra: le bitmap
    // di menu sono 16 px, la cella ne tiene 32, e un'immagine centrata dentro una
    // cella mappata su un quad da 16 px si vedrebbe a 8 px — meta' misura, sfocata.
    // Con l'ingrandimento nella cella, la misura sullo schermo e' quella giusta.
    // (Per le icone di sistema non cambia niente: `UploadIcon` arriva gia' a 32.)
    {
        // Stage one cell, then upload just that cell. Il margine e' il gutter che
        // impedisce al filtro di pescare il pixel dell'icona accanto.
        std::vector<uint8_t> cell(static_cast<size_t>(cellSize_) * cellSize_ * 4, 0);

        for (int y = 0; y < drawSize; ++y)
        {
            const int sy = (std::min)(height - 1, y * height / drawSize);
            for (int x = 0; x < drawSize; ++x)
            {
                const int sx = (std::min)(width - 1, x * width / drawSize);
                const uint32_t pixel = pixels[static_cast<size_t>(sy) * width + sx];
                const size_t destination =
                    (static_cast<size_t>(y + kCellPadding) * cellSize_ +
                     static_cast<size_t>(x + kCellPadding)) * 4;
                cell[destination + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFF);   // red
                cell[destination + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);    // green
                cell[destination + 2] = static_cast<uint8_t>(pixel & 0xFF);           // blue
                cell[destination + 3] = static_cast<uint8_t>((pixel >> 24) & 0xFF);   // alpha
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

    const auto it = cache_.find(key);
    return it == cache_.end() ? nullptr : &it->second;
}

// L'icona gia' in mano: da qui in giu' non si sa piu' da dove viene — un nome
// finto, un percorso, un PIDL o l'associazione di un'estensione.
const ShellIconAtlas::IconUV* ShellIconAtlas::UploadIcon(const std::string& key, HICON icon)
{
    if (icon == nullptr)
        return nullptr;

    const int width  = cellSize_ - kCellPadding * 2;
    const int height = cellSize_ - kCellPadding * 2;

    std::vector<uint32_t> pixels;
    if (!ReadIconPixels(icon, width, height, &pixels))
        return nullptr;
    // `ReadIconPixels` toglie gia' l'alpha premoltiplicato e la maschera AND: qui i
    // pixel sono BGRA a canale diritto come li vuole `UploadPixels`.
    return UploadPixels(key, width, height, pixels);
}

// Una bitmap di menu: la shell (o un'estensione) la mette in `hbmpItem`, ed e' di
// proprieta' dell'HMENU — nostra solo in prestito, finche' il menu vive. Si legge
// subito, si carica in una celletta e non si tocca piu'.
const ShellIconAtlas::IconUV* ShellIconAtlas::GetForMenuBitmap(const std::string& key,
                                                              HBITMAP bitmap)
{
    if (renderer_ == nullptr || bitmap == nullptr)
        return nullptr;

    const std::string cacheKey = "menu:" + key;
    const auto it = cache_.find(cacheKey);
    if (it != cache_.end())
        return &it->second;

    BITMAP header{};
    if (GetObjectW(bitmap, sizeof(header), &header) == 0 ||
        header.bmWidth <= 0 || header.bmHeight <= 0 || header.bmWidth > 256 ||
        header.bmHeight > 256)
        return nullptr;

    std::vector<uint32_t> pixels;
    if (!ReadBitmap(bitmap, header.bmWidth, header.bmHeight, pixels))
        return nullptr;

    // Le bitmap di menu sono a 32 bit con l'alpha, ma non tutte: quelle vecchie
    // hanno il canale a zero e si appoggiano alla maschera. Un alpha tutto a zero
    // su un'immagine che ha dei pixel non e' trasparenza, e' un formato piu'
    // vecchio: meglio opaca che invisibile.
    bool anyAlpha = false;
    for (const uint32_t pixel : pixels)
    {
        if ((pixel >> 24) != 0)
        {
            anyAlpha = true;
            break;
        }
    }
    if (!anyAlpha)
    {
        for (uint32_t& pixel : pixels)
            pixel |= 0xFF000000u;
    }

    return UploadPixels(cacheKey, header.bmWidth, header.bmHeight, pixels);
}

// `"C:\...pp.exe"` oppure `"C:\...pp.exe",-105`: la specifica d'icona come la
// scrive il registro, che e' l'unico posto in cui un'applicazione dice qual e' la
// sua icona di menu. Un percorso che non porta a un'icona non e' un errore: la voce
// resta senza, come le voci che l'icona non la dichiarano affatto.
const ShellIconAtlas::IconUV* ShellIconAtlas::GetForIconSpec(const std::wstring& spec)
{
    if (renderer_ == nullptr || spec.empty())
        return nullptr;

    std::wstring file = spec;
    int index = 0;
    const size_t comma = file.rfind(L',');
    if (comma != std::wstring::npos)
    {
        index = _wtoi(file.c_str() + comma + 1);
        file.erase(comma);
    }

    const std::string cacheKey = "spec:" + Utf8(spec);
    const auto it = cache_.find(cacheKey);
    if (it != cache_.end())
        return &it->second;

    if (file.size() >= 2 && file.front() == L'"' && file.back() == L'"')
        file = file.substr(1, file.size() - 2);

    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(file.c_str(), index, 0, &icon, nullptr,
                                 static_cast<UINT>(cellSize_ - kCellPadding * 2))) ||
        icon == nullptr)
        return nullptr;

    const IconUV* uv = UploadIcon(cacheKey, icon);
    DestroyIcon(icon);
    return uv;
}

const ShellIconAtlas::IconUV* ShellIconAtlas::GetFor(const std::string& extension, bool isDirectory)
{
    if (isDirectory)
    {
        const auto it = cache_.find("<dir>");
        if (it != cache_.end())
            return &it->second;
        if (const IconUV* uv =
                Rasterize("<dir>", L"folder", FILE_ATTRIBUTE_DIRECTORY, Source::SyntheticName))
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

    // Prima l'icona dell'applicazione che apre questo tipo: e' quella che si vede
    // in Esplora risorse, ed e' l'unica che distingue un `.csv` di Excel da un
    // documento qualunque. Se l'associazione non porta a niente — o l'icona non si
    // riesce a estrarre — si ripiega sul nome finto di prima, che almeno da' il
    // documento generico invece di niente.
    if (HICON icon = IconForExtension(extension, iconPixels_))
    {
        const IconUV* uv = UploadIcon(key, icon);
        DestroyIcon(icon);
        if (uv != nullptr)
            return uv;
    }

    if (const IconUV* uv = Rasterize(key, synthetic.c_str(), FILE_ATTRIBUTE_NORMAL, Source::SyntheticName))
        return uv;

    // Unknown extension: fall back to the generic file icon, warming it if needed.
    const auto generic = cache_.find("<file>");
    if (generic != cache_.end())
        return &generic->second;
    return Rasterize("<file>", L"file", FILE_ATTRIBUTE_NORMAL, Source::SyntheticName);
}

const ShellIconAtlas::IconUV* ShellIconAtlas::GetForObject(const std::wstring& location,
                                                          bool linkOverlay)
{
    if (renderer_ == nullptr || location.empty())
        return nullptr;

    const std::string key = (linkOverlay ? "lnk:" : "obj:") + Utf8(location);
    const auto it = cache_.find(key);
    if (it != cache_.end())
        return &it->second;

    // Un nome di spazio dei nomi comincia per `::` — il cestino, "Questo PC". La
    // shell lo sa leggere, e da li' si arriva al PIDL, che e' l'unico modo di
    // chiederle l'icona **sua**: senza, `SHGetFileInfoW` risponde con quella di
    // una cartella qualunque.
    if (ShellLocation::IsNamespace(location))
    {
        // Il PIDL della posizione: per un nome `::{...}` lo risolve la shell, per un
        // `shell:pidl:` e' gia' li'. Una sola strada, o l'icona di una cartella
        // aperta dal cestino sarebbe quella generica.
        if (PIDLIST_ABSOLUTE pidl = ShellLocation::Parse(location))
        {
            const IconUV* uv =
                Rasterize(key, reinterpret_cast<LPCWSTR>(pidl), 0, Source::Pidl);
            CoTaskMemFree(pidl);
            if (uv != nullptr)
                return uv;
        }
        return nullptr;
    }

    // Un percorso vero, guardato davvero: e' cosi' che si ottiene l'icona di
    // un'unita', che non dipende dall'estensione ma dal tipo di disco.
    return Rasterize(key, location.c_str(), 0, Source::RealPath, linkOverlay);
}

const ShellIconAtlas::IconUV* ShellIconAtlas::GetForPath(const std::wstring& path,
                                                        const std::string& extension,
                                                        bool isDirectory)
{
    // Un `.lnk` non e' il suo nome: chiedere l'icona di "x.lnk" senza guardare il
    // file da' il documento generico, ed e' quello che si vedeva nella lista. Il
    // file vero, guardato, da' l'icona della destinazione con la freccetta.
    if (!isDirectory && (extension == "lnk" || extension == "url"))
    {
        if (const IconUV* uv = GetForObject(path, /*linkOverlay=*/true))
            return uv;
    }
    return GetFor(extension, isDirectory);
}
