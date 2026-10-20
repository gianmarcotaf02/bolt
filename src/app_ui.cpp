#include "app_ui.h"

#include <algorithm>
#include <unordered_map>

#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>
#include <cwchar>

#include "shell_location.h"
#include "home.h"

namespace Bolt
{

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}


float PanelAppear(double openedAt, bool animations)
{
    // 160 ms: abbastanza per vedersi, poco da aspettare. Oltre, un pannello che si
    // apre comincia a sembrare lento invece che animato.
    constexpr double kDuration = 0.16;
    if (!animations || openedAt <= 0.0)
        return 1.0f;

    const double elapsed = NowSeconds() - openedAt;
    if (elapsed <= 0.0)
        return 0.0f;
    if (elapsed >= kDuration)
        return 1.0f;

    // Uscita rapida, non lineare: la dissolvenza si sente finita prima di esserlo.
    const double t = elapsed / kDuration;
    return static_cast<float>(1.0 - (1.0 - t) * (1.0 - t));
}

std::wstring IOPath(const std::wstring& path, bool longPaths)
{
    if (!longPaths || path.empty())
        return path;

    const std::wstring prefixed = L"\\\\?\\";
    if (path.rfind(prefixed, 0) == 0)   // gia' prefissato: non si raddoppia
        return path;

    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
        return L"\\\\?\\UNC" + path.substr(1);   // \\server\share -> \\?\UNC\server\share

    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        return prefixed + path;

    return path;   // relativo, o spazio dei nomi: il prefisso non si applica
}

std::wstring JoinPath(const std::wstring& directory, const std::wstring& name)
{
    if (directory.empty())
        return name;
    if (directory.back() == L'\\')
        return directory + name;
    return directory + L"\\" + name;
}


std::string HumanBytes(uint64_t bytes)
{
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), (unit == 0) ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return buffer;
}


// Compact count formatting for the Folders and Files columns, in the style the
// reference application uses: 89, 2.4K, 18.4K, 131K, 109.3K.
std::string HumanCount(uint64_t value)
{
    if (value < 1000)
        return std::to_string(value);

    static const char* suffixes[] = {"K", "M", "B"};
    double scaled = static_cast<double>(value) / 1000.0;
    int suffix = 0;
    while (scaled >= 1000.0 && suffix < 2)
    {
        scaled /= 1000.0;
        ++suffix;
    }

    // Round to one decimal, then drop the decimal when it carries nothing: a
    // count of 131 000 reads better as "131K" than as "131.0K".
    const double tenths = std::floor(scaled * 10.0 + 0.5) / 10.0;
    char buffer[32];
    if (tenths == std::floor(tenths))
        std::snprintf(buffer, sizeof(buffer), "%.0f%s", tenths, suffixes[suffix]);
    else
        std::snprintf(buffer, sizeof(buffer), "%.1f%s", tenths, suffixes[suffix]);
    return buffer;
}


// Le date si scrivono **europee**, sempre: `gg/mm/aaaa`, e con l'ora
// `gg/mm/aaaa hh:mm`.
//
// Non e' una stringa da tradurre. La lingua cambia le parole, non il modo di
// leggere un numero: un formato che seguisse la lingua cambierebbe anche la
// larghezza delle colonne a ogni cambio, e due catture non sarebbero piu'
// confrontabili al pixel — che e' il modo in cui questo progetto verifica le
// cose. Prima era `aaaa-mm-gg`, che ordina bene e si legge male: `2026-10-20` va
// guardato due volte per non scambiarlo per il ventesimo giorno del decimo mese.
// La stessa forma vale in tutte e due le lingue, e la lunghezza (dieci caratteri,
// sedici con l'ora) e' quella di prima, quindi niente si stringe.
std::string FormatFileTime(const FILETIME& fileTime, bool withTime)
{
    FILETIME local{};
    SYSTEMTIME system{};
    if (FileTimeToLocalFileTime(&fileTime, &local) == FALSE ||
        FileTimeToSystemTime(&local, &system) == FALSE)
        return "-";

    char buffer[64];
    if (withTime)
        std::snprintf(buffer, sizeof(buffer), "%02u/%02u/%04u %02u:%02u",
                      system.wDay, system.wMonth, system.wYear, system.wHour, system.wMinute);
    else
        std::snprintf(buffer, sizeof(buffer), "%02u/%02u/%04u",
                      system.wDay, system.wMonth, system.wYear);
    return buffer;
}


uint64_t FileTimeToUint64(const FILETIME& fileTime)
{
    return (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
}


// Truncates |text| with a trailing ellipsis so it cannot spill into the next
// column. Un ritaglio vero adesso c'e' (`IRenderer::SetClip`), ma i caratteri non
// si tagliano a meta': un glifo spezzato dal bordo e' illeggibile, mentre il
// puntino di sospensione dice che c'e' dell'altro. Quindi il testo si accorcia qui
// e il ritaglio resta per le forme.
std::string FitText(const FontAtlas& font, const std::string& text, float scale, float maxWidth)
{
    if (maxWidth <= 0.0f || text.empty())
        return {};
    if (font.Measure(text, scale) <= maxWidth)
        return text;

    const std::string ellipsis = "...";
    const float ellipsisWidth = font.Measure(ellipsis, scale);

    std::string truncated;
    float width = 0.0f;
    for (size_t i = 0; i < text.size(); i = Utf8::Next(text, i))
    {
        // Il taglio cade fra un carattere e l'altro, mai dentro: troncare a meta'
        // una sequenza UTF-8 lascia un byte che il disegno legge come un altro
        // carattere, e in fondo al nome compare un simbolo che non c'e'.
        const size_t length = Utf8::Length(text, i);
        const float advance = font.MeasureOne(Utf8::At(text, i, nullptr), scale);
        if (width + advance + ellipsisWidth > maxWidth)
            break;
        truncated.append(text, i, length);
        width += advance;
    }
    return truncated + ellipsis;
}


std::string FitTextTail(const FontAtlas& font, const std::string& text, float scale, float maxWidth)
{
    if (maxWidth <= 0.0f || text.empty() || font.Measure(text, scale) <= maxWidth)
        return text;

    const std::string ellipsis = "...";
    const float ellipsisWidth = font.Measure(ellipsis, scale);

    // Si parte dal fondo e si cammina all'indietro, un carattere alla volta: cosi'
    // il taglio cade fra due caratteri e non a meta' di una sequenza UTF-8.
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); i = Utf8::Next(text, i))
        starts.push_back(i);

    float width = 0.0f;
    size_t keepFrom = text.size();
    for (size_t i = starts.size(); i > 0; --i)
    {
        const size_t index = starts[i - 1];
        const float advance = font.MeasureOne(Utf8::At(text, index, nullptr), scale);
        if (width + advance + ellipsisWidth > maxWidth)
            break;
        width += advance;
        keepFrom = index;
    }
    return ellipsis + text.substr(keepFrom);
}


// Shell type name for an extension, cached: "PNG image" reads better than "PNG file".
//
// Per una **cartella** la risposta non si chiede alla shell: quella di una cartella e'
// sempre la stessa, e la shell la scrive nella lingua di **Windows**, non in quella
// scelta nel programma. Su un Windows italiano con l'interfaccia in inglese la colonna
// Tipo diceva "Cartella di file" in mezzo a intestazioni inglesi. Per i file la
// domanda resta alla shell, che e' l'unica a sapere che un `.png` e' un'immagine.
std::string ShellTypeName(const std::string& extension, bool isDirectory)
{
    if (isDirectory)
        return Tr("Cartella di file", "File folder");

    static std::unordered_map<std::string, std::string> cache;
    const std::string key = extension;

    const auto cached = cache.find(key);
    if (cached != cache.end())
        return cached->second;

    const std::wstring synthetic = L"x." + Widen(extension);

    SHFILEINFOW info{};
    std::string result;
    if (SHGetFileInfoW(synthetic.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                       SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) != 0)
        result = ToUtf8(info.szTypeName);

    if (result.empty())
        result = extension.empty() ? Tr("File", "File") : extension + Tr(" file", " file");

    cache.emplace(key, result);
    return result;
}


std::wstring LeafName(const std::wstring& path)
{
    if (path.empty())
        return L"This PC";
    std::wstring trimmed = path;
    while (trimmed.size() > 3 && trimmed.back() == L'\\')
        trimmed.pop_back();
    const size_t slash = trimmed.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return trimmed;
    const std::wstring leaf = trimmed.substr(slash + 1);
    return leaf.empty() ? trimmed : leaf;
}


// --- string helpers --------------------------------------------------------
std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}


std::wstring ParentOf(const std::wstring& path)
{
    if (path.size() <= 3)
        return path;   // already a drive root such as C: followed by one slash
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash < 2)
        return path;
    std::wstring parent = path.substr(0, slash);
    if (parent.size() == 2 && parent[1] == L':')
        parent += L"\\";
    return parent;
}

std::wstring FriendlyName(const std::wstring& path)
{
    if (path.empty())
        return path;

    // La barra laterale ridisegna a ogni fotogramma, e `DisplayName` parla con la
    // shell: senza questa memoria si pagherebbe una chiamata per riga per fotogramma.
    static std::unordered_map<std::wstring, std::wstring> cache;
    const auto found = cache.find(path);
    if (found != cache.end())
        return found->second;

    std::wstring name = ShellLocation::DisplayName(path);
    if (name.empty())
        name = LeafName(path);

    if (cache.size() < 2048)
        cache.emplace(path, name);
    return name;
}

std::wstring LocationLabel(const AppState& app, const std::wstring& location)
{
    if (location.empty())
        return L"";
    // La Home ha un nome suo e non viene dalla shell: chiederglielo vorrebbe dire
    // passarle una posizione che non conosce.
    if (IsHome(location))
        return L"Home";
    // Il nome lo da' la shell, e lo traduce **lei**: "Questo PC" in italiano,
    // "This PC" in inglese. Prima qui si teneva l'inglese perche' e' il nome del
    // riferimento; ma il riferimento e' un prodotto in inglese, e chi usa questo
    // sceglie la lingua dalla palette: un percorso che dice "This PC" accanto a una
    // barra laterale che dice "Questo PC" e' due nomi per la stessa cosa.
    if (!app.thisPcPath.empty() && location == app.thisPcPath)
    {
        const std::wstring name = ShellLocation::DisplayName(location);
        return name.empty() ? L"This PC" : name;
    }
    if (!ShellLocation::IsNamespace(location))
        return location;
    const std::wstring name = ShellLocation::DisplayName(location);
    return name.empty() ? location : name;
}

double NowSeconds()
{
    LARGE_INTEGER frequency{}, counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

float TextY(const Layout& layout, const Rect& row, const FontAtlas& font)
{
    const float lineHeight = font.LineHeight() * layout.scale;
    return row.y + (row.h - lineHeight) * 0.5f;
}


UIDraw DisclosureArrow(const Rect& box, Color color, bool pointsDown)
{
    // La base sta sulla dimensione che si vede, la profondita' sull'altra: la
    // linguetta chiusa punta a destra, quindi la sua base e' l'altezza del
    // rettangolo e la sua profondita' la larghezza. Girata, e' il contrario — ed e'
    // per questo che il rettangolo si costruisce qui e non lo passa chi chiama.
    const float base  = (std::min)(box.w, box.h);
    const float depth = base * 2.0f / 3.0f;

    Rect shape = box;
    if (pointsDown)
    {
        shape.w = base;
        shape.h = depth;
    }
    else
    {
        shape.w = depth;
        shape.h = base;
    }
    shape.x = box.x + (box.w - shape.w) * 0.5f;
    shape.y = box.y + (box.h - shape.h) * 0.5f;

    return UIDraw::Triangle(shape, color, pointsDown);
}

// L'atlante dei glifi si rifa' solo quando una delle tre misure cambia davvero:
// rasterizzarlo a ogni fotogramma sarebbe un lavoro che nessuno vede.
void ApplyFonts(AppState& app)
{
    std::string error;
    const int smallSize = (std::max)(AppState::kUiFontSizeMin,
                                     app.uiFontSize - AppState::kSmallFontDelta);
    app.font.Build(*app.renderer, app.fontFace.c_str(), app.uiFontSize, kGlyphLayer,
                   kTextureLayerSize, &error);
    app.smallFont.Build(*app.renderer, app.fontFace.c_str(), smallSize, kSmallGlyphLayer,
                        kTextureLayerSize, &error);
    app.monoFontOk = app.monoFont.Build(*app.renderer, app.monoFace.c_str(), app.monoFontSize,
                                        kMonoLayer, kTextureLayerSize, &error);
}

} // namespace Bolt
