// ---------------------------------------------------------------------------
// Bolt — application shell.
//
// Clone didattico di File Pilot, ricostruito da analisi statica del binario.
//
// Milestone 1 proved the rendering architecture. Milestone 2 adds what makes it
// a file manager rather than a mock-up:
//
//   * real shell icons, keyed by extension (shell_icons.h)
//   * a clickable breadcrumb path bar
//   * sortable columns with a click on the header
//   * navigation: double click to enter a folder, Backspace to go up, F5 to
//     refresh, arrow keys and Enter
//   * an inspector showing metadata taken from the directory entry
//   * the real known folders in the sidebar
//
// Everything still flows through one quad batcher and one draw call per frame.
// ---------------------------------------------------------------------------
#include "diag.h"
#include "dir_stats.h"
#include "drag_drop.h"
#include "file_ops.h"
#include "font_atlas.h"
#include "gl_loader.h"
#include "resource.h"
#include "shell_icons.h"
#include "shell_new.h"
#include "shell_preview.h"
#include "shell_sendto.h"
#include "ui_renderer.h"

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
// CMINVOKECOMMANDINFOEX sta in shobjidl.h, non in shlobj.h: serve per eseguire
// una voce del menu della shell.
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef REA_SHADER_DIR
#define REA_SHADER_DIR "shaders"
#endif

namespace {

// --- WGL context creation attributes (not in the Windows SDK) --------------
constexpr int kWglContextMajorVersion = 0x2091;
constexpr int kWglContextMinorVersion = 0x2092;
constexpr int kWglContextProfileMask  = 0x9126;
constexpr int kWglContextCoreProfile  = 0x00000001;

using WglCreateContextAttribsArbFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

// --- texture array layers ---------------------------------------------------
// Every UI texture shares one array, so the layer index is the only thing that
// tells a glyph from an icon from a thumbnail.
constexpr int kGlyphLayer      = 0;
constexpr int kIconLayer       = 1;
constexpr int kPreviewLayer    = 2;
constexpr int kSmallGlyphLayer = 3;

// --- theme -----------------------------------------------------------------
struct Theme
{
    Color windowBg    = Color::FromHex(0x1B1C1F);
    Color panelBg     = Color::FromHex(0x232428);
    Color panelBorder = Color::FromHex(0x32343A);
    Color headerBg    = Color::FromHex(0x1F2024);
    Color rowOdd      = Color::FromHex(0x26272C);
    Color rowSelected = Color::FromHex(0x2F4C7E);
    Color rowHover    = Color::FromHex(0x2C2E34);
    Color text        = Color::FromHex(0xE6E7EA);
    Color textDim     = Color::FromHex(0x9195A0);
    Color accent      = Color::FromHex(0x4C8DFF);
    Color scrollThumb = Color::FromHex(0x4A4D55);
    Color separator   = Color::FromHex(0x2A2C31);
};

// --- model -----------------------------------------------------------------
enum class SortColumn { Name, Type, Modified, Folders, Files, Size };

struct Entry
{
    std::wstring wideName;
    std::string  name;
    std::string  extension;     // lowercase, no leading dot
    std::string  typeText;
    std::string  sizeText;
    std::string  modifiedText;    // compact, for the list column
    std::string  modifiedFull;    // with time, for the inspector
    uint64_t     bytes = 0;
    bool         isDirectory = false;
    FILETIME     modified{};
    bool         readable = true;
};

struct Place
{
    std::string  label;
    std::wstring path;
};

// Rectangles recorded while drawing, so input handling can hit test them without
// duplicating the layout maths. Rebuilt every frame.
struct ClickTarget
{
    enum class Kind
    {
        Column, Breadcrumb, Place, Row, GroupHeader, GroupMode,
        Menu, Tab, TabClose, TabNew,
        WindowMinimize, WindowMaximize, WindowClose,
        MenuRow, MenuHeader,
    };
    Rect         rect;
    Kind         kind = Kind::Row;
    int          index = 0;      // column index or row index
    std::wstring path;           // for breadcrumbs and places
    std::string  label;          // for group headers
};

// --- view model -------------------------------------------------------------
// The list is not the directory. A filter removes entries and a grouping inserts
// headers between them, so what gets drawn and hit tested is a list of rows over
// the entry array, rebuilt only when something that affects it changes.

enum class GroupMode { None, Name, Date, Size, Count, Volume };

// How the list is laid out. Details and List are rows of text; Icons and Tiles
// are a grid of cells built around a thumbnail. Two row layouts and two cell
// sizes cover what Explorer exposes as its eight view commands, because most of
// those are the same layout at a different size.
enum class ViewMode { Details, List, Tiles, Icons };

const char* ViewModeName(ViewMode mode)
{
    switch (mode)
    {
        case ViewMode::List:  return "List";
        case ViewMode::Tiles: return "Tiles";
        case ViewMode::Icons: return "Icons";
        default:              return "Details";
    }
}

// Geometry of the current view. |columns| is only meaningful in a grid.
struct ViewMetrics
{
    bool  grid       = false;
    float rowHeight  = 24.0f;
    float iconSize   = 16.0f;
    float cellWidth  = 0.0f;
    float cellHeight = 0.0f;
    float padding    = 4.0f;
    int   columns    = 1;
};

struct Row
{
    bool header = false;
    int  entry  = -1;   // index into AppState::entries
    int  group  = -1;   // index into AppState::groups
};

struct Group
{
    std::string label;
    int         count = 0;
};

// --- command palette ---------------------------------------------------------
// A command is data, not a callback: the table below is the single place a
// command is declared, and |RunCommand| is the single place it is executed. The
// reference exposes 319 of them with a tooltip each; this is the subset the shell
// can actually carry out, which is the honest place to start.
enum CommandId
{
    CmdNewTab, CmdCloseTab, CmdNextTab, CmdPrevTab,
    CmdGoUp, CmdBack, CmdForward, CmdRefresh, CmdOpenExplorer, CmdOpenConsole, CmdRecalcStats,
    CmdGoTo,
    CmdToggleSidebar, CmdZoomIn, CmdZoomOut, CmdZoomReset, CmdToggleDetails,
    CmdViewDetails, CmdViewList, CmdViewTiles, CmdViewIcons,
    CmdSortName, CmdSortType, CmdSortModified, CmdSortFolders, CmdSortFiles,
    CmdSortSize, CmdSortDirection,
    CmdGroupNone, CmdGroupName, CmdGroupDate, CmdGroupSize, CmdGroupCount, CmdGroupVolume,
    CmdFocusFilter, CmdClearFilter, CmdScrollTop, CmdScrollBottom,
    CmdToggleBookmark, CmdClearBookmarks, CmdClearRecents,
    // Operazioni su file (M7).
    CmdCopy, CmdCut, CmdPaste, CmdDelete, CmdDeletePermanent, CmdRename, CmdNewFolder,
};

// Item ids below this are CommandId values; above it they are indexes into
// AppState::places. The palette has two kinds of row and the id has to say which.
constexpr int kPalettePlaceBase = 1000;

// The reusable part: a modal, filtered, grouped and collapsible list. It knows
// nothing about what an item means — |id| is the caller's business — which is
// what lets the same component serve the command palette and the GoTo window.
// It became a component when the second caller arrived, not before.
struct GroupMenuItem
{
    std::string group;
    std::string label;
    std::string detail;   // shown right aligned in detailed mode
    int         id = 0;   // meaningful only to the caller
};

struct GroupMenuRow
{
    bool header = false;
    int  group  = -1;   // index into GroupMenu::groups
    int  item   = -1;   // index into GroupMenu::items
};

enum class MenuAction { None, Activate, Close };

struct GroupMenu
{
    bool         open      = false;
    bool         detailed  = false;
    bool         dirty     = true;
    std::wstring filter;
    std::string  title;
    std::string  placeholder;
    std::string  hint;   // the footer line, which differs per caller

    std::vector<GroupMenuItem>      items;
    std::vector<GroupMenuRow>       rows;
    std::vector<std::string>        groups;
    std::unordered_set<std::string> collapsed;
    int          selected   = 0;
    int          matchCount = 0;
};

// One panel's worth of state. Switching tabs saves the live state into the tab
// it came from and loads the target tab's, which keeps every other part of the
// program working on a single set of fields instead of having to know that tabs
// exist. Completed preview and statistic results are global caches keyed by path,
// so they survive a switch for free.
struct Tab
{
    std::wstring directory;
    std::vector<Entry> entries;
    std::wstring cursor;
    std::wstring selectionAnchor;
    std::unordered_set<std::wstring> selection;
    std::wstring filter;
    std::string  errorText;
    float        scroll = 0.0f;
    bool         filterActive = false;
    SortColumn   sortColumn = SortColumn::Name;
    bool         sortAscending = true;
    GroupMode    groupMode = GroupMode::None;
    ViewMode     viewMode  = ViewMode::Details;

    // Per-tab navigation history, walked by Alt+Left and Alt+Right.
    std::vector<std::wstring> history;
    int                       historyIndex = -1;
};

// --- menu contestuale: strutture --------------------------------------------
//
// Non e' un GroupMenu, ed e' una scelta e non una svista. Condividono l'idea di
// "una lista di righe etichettate", ma differiscono in tutto il resto: questo si
// ancora al puntatore invece di stare al centro, e' alto quanto il contenuto
// invece di 420 px fissi, ha separatori e non bande, e non ha filtro ne' piede.
// Forzarlo dentro GroupMenu avrebbe voluto dire un campo modalita' e due
// impaginazioni dentro lo stesso componente.

// Id che non sono comandi della palette: il primo apre la selezione, che l'Invio
// gia' fa, il secondo apre la finestra delle proprieta' di Windows.
constexpr int kContextOpen       = -1;
constexpr int kContextProperties = -2;

struct ContextMenuItem
{
    std::string label;
    // CommandId, kContext*, oppure un id della banda della shell: vedi
    // kShellCmdFirst. La banda e' cosa distingue una voce nostra da una che
    // appartiene alla shell e va eseguita da lei.
    int         command   = 0;
    bool        separator = false;
    // Una voce che esiste ma non si puo' eseguire adesso: la shell offre voci
    // spente per una selezione mista o per un file che non sa gestire. Va
    // mostrata spenta, non nascosta, perche' la sua presenza spiega cosa la
    // shell saprebbe fare in un altro contesto.
    bool        disabled  = false;

    // Sottomenu. `children` vuoto non vuol dire "niente sottomenu": la shell lo
    // descrive con un HMENU che si legge **quando si apre**, perche' il suo
    // contenuto non esiste prima di WM_INITMENUPOPUP. Vedi `HasSubmenu`.
    std::vector<ContextMenuItem> children;
    HMENU shellSubmenu  = nullptr;   // il sottomenu della shell, non ancora letto
    int   shellPosition = 0;         // dove sta la voce nel menu della shell
    // Un sottomenu nostro che si riempie alla prima apertura: i modelli di
    // "Nuovo file" stanno nel registro, e leggerli costa qualche centinaio di
    // aperture di chiave che non si devono pagare a ogni tasto destro.
    bool  lazyNewFile   = false;
    // E cosi' "Invia a": le destinazioni stanno in una cartella dell'utente, e
    // leggerle a ogni tasto destro e' lavoro che si fa quando il menu si apre.
    bool  lazySendTo    = false;
};

// Scelta e finestra di un livello del menu. Ce n'e' una per livello aperto.
struct ContextMenuLevel
{
    int selected  = -1;
    int firstItem = 0;
};

struct ContextMenu
{
    bool open = false;
    float x = 0.0f, y = 0.0f;       // dove e' stato aperto, in coordinate cliente
    // I sottomenu aperti, un indice per livello: openPath[i] e' la voce aperta
    // nel livello i. `levels` ne ha sempre uno in piu' di openPath, e non e' mai
    // vuoto: il livello 0 e' il menu stesso.
    std::vector<int>              openPath;
    std::vector<ContextMenuLevel> levels;
    std::string subtitle;
    std::vector<ContextMenuItem> items;      // le voci del livello 0

    // La shell descrive il proprio menu in un HMENU che non mostriamo mai: serve
    // per leggere le voci, e per i sottomenu anche per farsele popolare. Va
    // tenuto vivo finche' l'IContextMenu e' vivo, e distrutto insieme a lui, o
    // l'HMENU resta appeso a ogni tasto destro.
    HMENU         shellMenu = nullptr;
    IContextMenu* shell     = nullptr;
    // Il menu e' stato chiesto per il fondo della cartella invece che per una
    // selezione: serve a filtrare i verbi quando si legge un sottomenu, perche'
    // allora la lettura avviene molto dopo averlo deciso.
    bool shellIsBackground = false;
};

struct AppState
{
    // Il contesto di disegno della finestra, per poter disegnare un fotogramma
    // anche fuori dal ciclo principale (vedi `RenderFrame`).
    HDC deviceContext = nullptr;

    UIRenderer      renderer;
    FontAtlas       font;
    // Testo secondario: intestazioni, etichette, barra di stato. Rasterizzato
    // alla sua dimensione invece che ricampionato dal corpo.
    FontAtlas       smallFont;
    ShellIconAtlas  icons;
    ShellPreviewCache previews;
    DirStatsService   stats;
    Theme             theme;

    HWND          window = nullptr;

    std::vector<Entry> entries;
    std::vector<Place> places;
    std::vector<ClickTarget> clickTargets;

    std::vector<Tab> tabs;
    int              activeTab = 0;
    bool             sidebarVisible = true;
    int              hoveredTab = -1;
    int              hoveredChrome = -1;   // 0 menu, 1 new tab, 2 min, 3 max, 4 close

    std::wstring directory;

    // Selection is kept by file name, not by entry index: sorting, filtering and
    // refreshing all renumber the entry array, and a selection that slides onto a
    // different file when the user sorts by size is worse than no selection.
    std::wstring cursor;                            // the focused entry
    std::wstring selectionAnchor;                   // Shift+click / Shift+arrow origin
    std::unordered_set<std::wstring> selection;     // entry names

    // Rubber band, in client coordinates.
    bool  rectSelecting = false;
    bool  rectAdditive  = false;
    float rectX0 = 0.0f, rectY0 = 0.0f, rectX1 = 0.0f, rectY1 = 0.0f;

    // Trascinamento. La pressione su una riga non e' ancora un trascinamento: lo
    // diventa quando il mouse si muove oltre la soglia di sistema — sotto quella
    // resta un clic, ed e' l'unico modo di distinguere le due intenzioni. Il punto
    // di partenza serve perche' la soglia si misura da li'.
    bool  dragArmed  = false;
    float dragStartX = 0.0f, dragStartY = 0.0f;

    // La destinazione di una caduta in corso: si evidenzia finche' il puntatore ci
    // sta sopra, ed e' l'unico modo di far vedere DOVE finiranno i file prima di
    // lasciarli. |dropTargetIsRow| distingue una riga dal fondo della lista.
    std::wstring dropTarget;
    bool         dropTargetIsRow = false;

    // --- operazioni su file (M7) -------------------------------------------
    //
    // Rinomina sul posto: il campo si disegna sopra la riga, quindi la lista non
    // cambia impaginazione mentre si scrive.
    bool         renameActive = false;
    std::wstring renamePath;        // percorso completo di cio' che si rinomina
    std::wstring renameOriginal;    // nome di partenza, per annullare con Esc
    std::string  renameText;        // il nome in scrittura

    // Vero se l'ultima scrittura in clipboard era un taglio: decide se incollare
    // copia o sposta, ed e' lo stesso bit che Esplora risorse legge.
    bool clipboardCut = false;

    // Messaggio d'esito nella barra di stato. Un'operazione che fallisce in
    // silenzio e' peggio di una che non parte: senza questo, "Sposta" su un file
    // in uso sembrerebbe non aver fatto nulla.
    std::wstring statusMessage;
    double       statusMessageTime = 0.0;

    int   hovered  = -1;
    float scroll   = 0.0f;
    float zoom     = 1.0f;
    float dpiScale = 1.0f;
    int   hoveredPlace = -1;

    SortColumn sortColumn    = SortColumn::Name;
    bool       sortAscending = true;
    ViewMode   viewMode      = ViewMode::Details;

    // View: filter and grouping sit between the directory and the rows.
    std::wstring filter;          // free text; a term may be an extension pattern
    bool         filterActive = false;
    std::vector<Row>   rows;
    std::vector<Group> groups;

    // Two menus, one component.
    GroupMenu palette;                       // Ctrl+P, item id = CommandId
    GroupMenu goTo;                          // Ctrl+G, item id = index in goToTargets
    ContextMenu contextMenu;                 // tasto destro
    std::vector<std::wstring> goToTargets;
    std::vector<std::wstring> recents;       // visited this run: StoreRecents = Session
    std::vector<std::wstring> bookmarks;     // this run; persistence is milestone 9
    std::unordered_set<std::string> collapsedGroups;
    GroupMode    groupMode  = GroupMode::None;
    int          matchCount = 0;   // entries surviving the filter, folding aside
    bool         viewDirty  = true;

    // Navigation history of the active tab, saved into and out of it like the
    // rest of the tab state.
    std::vector<std::wstring> history;
    int                       historyIndex = -1;
    uint64_t     viewStatsRevision = 0;
    double       lastViewRebuild   = 0.0;

    std::string errorText;

    double lastFrameTime = 0.0;
    float  framesPerSecond = 0.0f;
    int    viewportWidth = 0;
    int    viewportHeight = 0;
};

// Helpers defined further down but needed earlier: the selection queries by the
// drawing code, and the parent path by the command palette.
int  FindEntryByName(const AppState& app, const std::wstring& name);
int  CursorIndex(const AppState& app);
bool IsEntrySelected(const AppState& app, const Entry& entry);
std::wstring ParentOf(const std::wstring& path);
double       NowSeconds();

// --- string helpers --------------------------------------------------------
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

std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

// Directory and name joined with exactly one separator, so a drive root such as
// "C:\\" does not become "C:\\\\name".
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

std::string FormatFileTime(const FILETIME& fileTime, bool withTime)
{
    FILETIME local{};
    SYSTEMTIME system{};
    if (FileTimeToLocalFileTime(&fileTime, &local) == FALSE ||
        FileTimeToSystemTime(&local, &system) == FALSE)
        return "-";

    char buffer[64];
    if (withTime)
        std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u %02u:%02u",
                      system.wYear, system.wMonth, system.wDay, system.wHour, system.wMinute);
    else
        std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u",
                      system.wYear, system.wMonth, system.wDay);
    return buffer;
}

uint64_t FileTimeToUint64(const FILETIME& fileTime)
{
    return (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
}

// Truncates |text| with a trailing ellipsis so it cannot spill into the next
// column. The renderer has no scissor rectangle, so fitting has to happen here.
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
    for (const char c : text)
    {
        const FontAtlas::Glyph* glyph = font.Find(c);
        const float advance = (glyph != nullptr) ? glyph->advance * scale : 6.0f * scale;
        if (width + advance + ellipsisWidth > maxWidth)
            break;
        truncated.push_back(c);
        width += advance;
    }
    return truncated + ellipsis;
}

// Shell type name for an extension, cached: "PNG image" reads better than "PNG file".
std::string ShellTypeName(const std::string& extension, bool isDirectory)
{
    static std::unordered_map<std::string, std::string> cache;
    const std::string key = isDirectory ? std::string("<dir>") : extension;

    const auto cached = cache.find(key);
    if (cached != cache.end())
        return cached->second;

    const std::wstring synthetic = isDirectory ? L"folder" : (L"x." + Widen(extension));
    const DWORD attributes = isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;

    SHFILEINFOW info{};
    std::string result;
    if (SHGetFileInfoW(synthetic.c_str(), attributes, &info, sizeof(info),
                       SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) != 0)
        result = ToUtf8(info.szTypeName);

    if (result.empty())
        result = isDirectory ? "File folder" : (extension.empty() ? "File" : extension + " file");

    cache.emplace(key, result);
    return result;
}

// --- directory reading -----------------------------------------------------
std::vector<Entry> ReadDirectory(const std::wstring& directory, std::string* error)
{
    std::vector<Entry> entries;
    error->clear();

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = directory + L"\\*";
    HANDLE search = FindFirstFileExW(pattern.c_str(),
                                     FindExInfoBasic,
                                     &data,
                                     FindExSearchNameMatch,
                                     nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE)
    {
        // Retry without the large-fetch hint: some network providers reject it.
        search = FindFirstFileW(pattern.c_str(), &data);
    }

    if (search == INVALID_HANDLE_VALUE)
    {
        const DWORD code = GetLastError();
        *error = (code == ERROR_ACCESS_DENIED) ? "Access denied." : "Cannot read this folder.";
        return entries;
    }

    do
    {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
            continue;

        Entry entry;
        entry.wideName    = name;
        entry.name        = ToUtf8(name);
        entry.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.bytes       = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        entry.modified    = data.ftLastWriteTime;

        const size_t dot = entry.name.rfind('.');
        if (!entry.isDirectory && dot != std::string::npos && dot + 1 < entry.name.size())
        {
            entry.extension = entry.name.substr(dot + 1);
            std::transform(entry.extension.begin(), entry.extension.end(), entry.extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }

        entry.typeText     = ShellTypeName(entry.extension, entry.isDirectory);
        entry.sizeText     = entry.isDirectory ? "" : HumanBytes(entry.bytes);
        entry.modifiedText = FormatFileTime(entry.modified, /*withTime=*/false);
        entry.modifiedFull = FormatFileTime(entry.modified, /*withTime=*/true);
        entries.push_back(std::move(entry));
    }
    while (FindNextFileW(search, &data) != FALSE);

    FindClose(search);
    return entries;
}

bool SortEntries(AppState& app)
{
    const bool ascending = app.sortAscending;
    const SortColumn column = app.sortColumn;

    // A statistics column needs the service, which costs a lock per comparison.
    // That is acceptable here because sorting happens on demand, not per frame.
    auto statsFor = [&app](const Entry& entry, DirStats* out) -> bool
    {
        if (!entry.isDirectory)
            return false;
        return app.stats.TryGet(JoinPath(app.directory, entry.wideName), out);
    };

    auto statValue = [&statsFor](const Entry& entry, SortColumn which) -> uint64_t
    {
        if (!entry.isDirectory)
            return (which == SortColumn::Size) ? entry.bytes : 0;

        DirStats stats;
        if (!statsFor(entry, &stats))
            return 0;
        switch (which)
        {
            case SortColumn::Folders: return stats.folders;
            case SortColumn::Files:   return stats.files;
            default:                  return stats.logicalBytes;
        }
    };

    std::stable_sort(app.entries.begin(), app.entries.end(),
                     [column, ascending, &statValue](const Entry& left, const Entry& right)
                     {
                         // Folders always lead, matching the FoldersFirst default.
                         if (left.isDirectory != right.isDirectory)
                             return left.isDirectory;

                         int order = 0;
                         switch (column)
                         {
                             case SortColumn::Size:
                             case SortColumn::Folders:
                             case SortColumn::Files:
                             {
                                 const uint64_t a = statValue(left, column);
                                 const uint64_t b = statValue(right, column);
                                 order = (a < b) ? -1 : (a > b) ? 1 : 0;
                                 break;
                             }
                             case SortColumn::Modified:
                             {
                                 const uint64_t a = FileTimeToUint64(left.modified);
                                 const uint64_t b = FileTimeToUint64(right.modified);
                                 order = (a < b) ? -1 : (a > b) ? 1 : 0;
                                 break;
                             }
                             case SortColumn::Type:
                                 order = left.typeText.compare(right.typeText);
                                 break;
                             case SortColumn::Name:
                             default:
                                 order = _wcsicmp(left.wideName.c_str(), right.wideName.c_str());
                                 break;
                         }

                         if (order == 0)
                             order = _wcsicmp(left.wideName.c_str(), right.wideName.c_str());

                         return ascending ? (order < 0) : (order > 0);
                     });
    return true;
}

// --- view: filtering --------------------------------------------------------
std::wstring ToLowerWide(const std::wstring& text)
{
    std::wstring out = text;
    for (wchar_t& c : out)
        c = static_cast<wchar_t>(towlower(c));
    return out;
}

// '*' matches any run, '?' one character. Iterative with backtracking on the
// last star, which is the classic linear-time formulation.
bool GlobMatch(const std::wstring& pattern, const std::wstring& text)
{
    size_t p = 0, t = 0;
    size_t star = std::wstring::npos;
    size_t resume = 0;

    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == L'?' || pattern[p] == text[t]))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == L'*')
        {
            star = p++;
            resume = t;
        }
        else if (star != std::wstring::npos)
        {
            p = star + 1;
            t = ++resume;
        }
        else
        {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == L'*')
        ++p;
    return p == pattern.size();
}

bool TermMatches(const Entry& entry, const std::wstring& term)
{
    // A term that starts with a dot, or contains a wildcard, asks about the
    // type rather than the whole name: ".png" and "*.png" mean the same thing.
    // This is the filter-by-type path of the reference key table
    // (FilterFileTypes / FilterTypes) expressed in the same box as free text.
    if (!term.empty() && term[0] == L'.' && term.find(L'*') == std::wstring::npos)
    {
        if (entry.isDirectory)
            return false;
        return ToLowerWide(Widen(entry.extension)) == term.substr(1);
    }

    const std::wstring name = ToLowerWide(entry.wideName);
    if (term.find(L'*') != std::wstring::npos || term.find(L'?') != std::wstring::npos)
        return GlobMatch(term, name);
    return name.find(term) != std::wstring::npos;
}

// Space separated terms, all of which must match. An empty filter matches all.
bool FilterMatch(const Entry& entry, const std::wstring& filter)
{
    if (filter.empty())
        return true;

    size_t start = 0;
    for (;;)
    {
        size_t space = filter.find(L' ', start);
        if (space == std::wstring::npos)
            space = filter.size();
        if (space > start && !TermMatches(entry, filter.substr(start, space - start)))
            return false;
        if (space >= filter.size())
            break;
        start = space + 1;
    }
    return true;
}

// --- view: grouping ---------------------------------------------------------
uint64_t LocalDayIndex(const FILETIME& fileTime)
{
    FILETIME local{};
    if (FileTimeToLocalFileTime(&fileTime, &local) == FALSE)
        return 0;
    const uint64_t ticks = (static_cast<uint64_t>(local.dwHighDateTime) << 32) | local.dwLowDateTime;
    return ticks / (10000000ULL * 60ULL * 60ULL * 24ULL);
}

uint64_t CurrentDayIndex()
{
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return LocalDayIndex(now);
}

bool VolumeUsage(const std::wstring& path, int* percentFull)
{
    if (path.size() < 2 || path[1] != L':')
        return false;
    const wchar_t root[4] = {path[0], L':', L'\\', L'\0'};
    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(root, &available, &total, &freeBytes) == FALSE || total.QuadPart == 0)
        return false;
    *percentFull = static_cast<int>((total.QuadPart - freeBytes.QuadPart) * 100 / total.QuadPart);
    return true;
}

// The band names come from the reference key table (GroupByLabel_*). The
// cut-offs do not: docs/03 records them as still unknown, so the values here are
// a reconstruction and are deliberately easy to find and change.
void GroupKeyFor(const Entry& entry, GroupMode mode, uint64_t bytes, uint64_t items,
                 bool statsKnown, int percentFull, int* outOrder, std::string* outLabel)
{
    switch (mode)
    {
        case GroupMode::Name:
        {
            const wchar_t c = entry.wideName.empty()
                ? L'\0'
                : static_cast<wchar_t>(towupper(entry.wideName[0]));
            if (c >= L'0' && c <= L'9')      { *outOrder = 0; *outLabel = "09"; }
            else if (c >= L'A' && c <= L'H') { *outOrder = 1; *outLabel = "AH"; }
            else if (c >= L'I' && c <= L'P') { *outOrder = 2; *outLabel = "IP"; }
            else if (c >= L'Q' && c <= L'Z') { *outOrder = 3; *outLabel = "QZ"; }
            else                             { *outOrder = 4; *outLabel = "Other"; }
            break;
        }
        case GroupMode::Date:
        {
            const uint64_t now  = CurrentDayIndex();
            const uint64_t then = LocalDayIndex(entry.modified);
            const uint64_t age  = (now >= then) ? (now - then) : 0;
            if (age == 0)        { *outOrder = 0; *outLabel = "Today"; }
            else if (age == 1)   { *outOrder = 1; *outLabel = "Yesterday"; }
            else if (age <= 6)   { *outOrder = 2; *outLabel = "EarlierThisWeek"; }
            else if (age <= 13)  { *outOrder = 3; *outLabel = "LastWeek"; }
            else if (age <= 30)  { *outOrder = 4; *outLabel = "EarlierThisMonth"; }
            else if (age <= 60)  { *outOrder = 5; *outLabel = "LastMonth"; }
            else if (age <= 364) { *outOrder = 6; *outLabel = "EarlierThisYear"; }
            else if (age <= 729) { *outOrder = 7; *outLabel = "LastYear"; }
            else                 { *outOrder = 8; *outLabel = "Older"; }
            break;
        }
        case GroupMode::Size:
        {
            if (entry.isDirectory && !statsKnown) { *outOrder = 7; *outLabel = "Unspecified"; break; }
            if (bytes == 0)                          { *outOrder = 0; *outLabel = "Empty"; }
            else if (bytes < 16ULL * 1024)           { *outOrder = 1; *outLabel = "Tiny"; }
            else if (bytes < 1024ULL * 1024)         { *outOrder = 2; *outLabel = "Small"; }
            else if (bytes < 16ULL * 1024 * 1024)    { *outOrder = 3; *outLabel = "Medium"; }
            else if (bytes < 128ULL * 1024 * 1024)   { *outOrder = 4; *outLabel = "Large"; }
            else if (bytes < 1024ULL * 1024 * 1024)  { *outOrder = 5; *outLabel = "Huge"; }
            else                                     { *outOrder = 6; *outLabel = "Gigantic"; }
            break;
        }
        case GroupMode::Count:
        {
            if (!entry.isDirectory || !statsKnown) { *outOrder = 8; *outLabel = "Unspecified"; break; }
            if (items == 0)          { *outOrder = 0; *outLabel = "Empty"; }
            else if (items <= 5)     { *outOrder = 1; *outLabel = "Few"; }
            else if (items <= 50)    { *outOrder = 2; *outLabel = "Dozens"; }
            else if (items <= 500)   { *outOrder = 3; *outLabel = "Hundreds"; }
            else if (items <= 5000)  { *outOrder = 4; *outLabel = "Thousands"; }
            else if (items <= 50000) { *outOrder = 5; *outLabel = "TensOfThousands"; }
            else if (items <= 500000){ *outOrder = 6; *outLabel = "HundredsOfThousands"; }
            else                     { *outOrder = 7; *outLabel = "Millions"; }
            break;
        }
        case GroupMode::Volume:
        default:
        {
            if (percentFull < 0) { *outOrder = 11; *outLabel = "Unspecified"; break; }
            const int band = (percentFull >= 100) ? 10 : percentFull / 10;
            *outOrder = band;
            if (band >= 10)
            {
                *outLabel = "100%";
            }
            else
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%d-%d%%", band * 10, band * 10 + 9);
                *outLabel = buffer;
            }
            break;
        }
    }
}

const char* GroupModeName(GroupMode mode)
{
    switch (mode)
    {
        case GroupMode::Name:   return "Name";
        case GroupMode::Date:   return "Date";
        case GroupMode::Size:   return "Size";
        case GroupMode::Count:  return "Count";
        case GroupMode::Volume: return "Volume";
        default:                return "None";
    }
}

void RebuildView(AppState& app)
{
    app.rows.clear();
    app.groups.clear();
    app.matchCount = 0;
    app.viewDirty = false;

    // A grid places cells by index, so a full-width group band would break the
    // flow. Explorer does group in its icon views; doing it here needs a flow
    // layout with band breaks, which is a separate job.
    const bool grid = (app.viewMode == ViewMode::Tiles || app.viewMode == ViewMode::Icons);

    if (app.groupMode == GroupMode::None || grid)
    {
        for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
        {
            if (FilterMatch(app.entries[static_cast<size_t>(i)], app.filter))
                app.rows.push_back(Row{false, i, -1});
        }
        app.matchCount = static_cast<int>(app.rows.size());
        return;
    }

    int percentFull = -1;
    if (app.groupMode == GroupMode::Volume)
    {
        int percent = 0;
        if (VolumeUsage(app.directory, &percent))
            percentFull = percent;
    }

    struct Bucket
    {
        int              order = 0;
        std::string      label;
        std::vector<int> entries;
    };

    std::vector<Bucket> buckets;
    std::unordered_map<int, int> bucketIndex;

    const auto addToBucket = [&buckets, &bucketIndex](int order, const std::string& label, int entryIndex)
    {
        const auto found = bucketIndex.find(order);
        int index = 0;
        if (found == bucketIndex.end())
        {
            index = static_cast<int>(buckets.size());
            bucketIndex[order] = index;
            buckets.push_back(Bucket{order, label, {}});
        }
        else
        {
            index = found->second;
        }
        buckets[static_cast<size_t>(index)].entries.push_back(entryIndex);
    };

    for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
    {
        const Entry& entry = app.entries[static_cast<size_t>(i)];
        if (!FilterMatch(entry, app.filter))
            continue;

        uint64_t bytes = entry.isDirectory ? 0 : entry.bytes;
        uint64_t items = 0;
        bool statsKnown = true;
        if (entry.isDirectory)
        {
            DirStats stats;
            statsKnown = app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats);
            if (statsKnown)
            {
                bytes = stats.logicalBytes;
                items = stats.files + stats.folders;
            }
        }

        int order = 0;
        std::string label;
        GroupKeyFor(entry, app.groupMode, bytes, items, statsKnown, percentFull, &order, &label);
        addToBucket(order, label, i);
        ++app.matchCount;
    }

    std::sort(buckets.begin(), buckets.end(),
              [](const Bucket& a, const Bucket& b) { return a.order < b.order; });

    for (const Bucket& bucket : buckets)
    {
        app.groups.push_back(Group{bucket.label, static_cast<int>(bucket.entries.size())});
        const int groupIndex = static_cast<int>(app.groups.size()) - 1;
        app.rows.push_back(Row{true, -1, groupIndex});

        if (app.collapsedGroups.count(bucket.label) != 0)
            continue;
        for (const int entryIndex : bucket.entries)
            app.rows.push_back(Row{false, entryIndex, -1});
    }
}

// Last path component, which is what a tab is labelled with. A drive root keeps
// its colon and slash so "C:\" does not become an empty label.
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

void UpdateWindowTitle(AppState& app)
{
    if (app.window == nullptr)
        return;
    const std::wstring title = L"Bolt — " +
                               (app.directory.empty() ? L"This PC" : app.directory);
    SetWindowTextW(app.window, title.c_str());
}

// |recordHistory| is false only when the move is itself a history step, or
// walking back would push the place you are leaving onto the stack again.
void LoadDirectory(AppState& app, const std::wstring& directory, bool recordHistory = true)
{
    const std::wstring normalised = directory.empty() ? L"" : directory;
    app.entries = ReadDirectory(normalised, &app.errorText);
    app.directory = normalised;
    app.previews.CancelPending();
    app.stats.CancelQueued();

    // The filter does not follow the user into another folder: a filter left over
    // from a different listing would look like a missing directory. FilterSticky
    // exists in the reference key table; this is the non-sticky default.
    app.filter.clear();
    app.filterActive = false;

    app.scroll = 0.0f;
    app.selection.clear();
    app.cursor.clear();
    app.selectionAnchor.clear();
    app.rectSelecting = false;

    SortEntries(app);
    if (!app.entries.empty())
    {
        app.cursor = app.entries.front().wideName;
        app.selection.insert(app.cursor);
        app.selectionAnchor = app.cursor;
    }
    app.viewDirty = true;

    if (recordHistory)
    {
        // Walking somewhere new discards the forward stack, the way every
        // browser and file manager does.
        if (app.historyIndex + 1 < static_cast<int>(app.history.size()))
            app.history.resize(static_cast<size_t>(app.historyIndex + 1));
        app.history.push_back(normalised);
        app.historyIndex = static_cast<int>(app.history.size()) - 1;
    }

    // StoreRecents has three modes in the reference key table (LocalDisk,
    // Persistent, Session). This is Session: nothing is written to disk yet.
    app.recents.erase(std::remove(app.recents.begin(), app.recents.end(), normalised),
                      app.recents.end());
    app.recents.insert(app.recents.begin(), normalised);
    if (app.recents.size() > 20)
        app.recents.resize(20);

    UpdateWindowTitle(app);
}

// --- tabs -------------------------------------------------------------------
void SaveActiveTab(AppState& app)
{
    if (app.activeTab < 0 || app.activeTab >= static_cast<int>(app.tabs.size()))
        return;

    Tab& tab = app.tabs[static_cast<size_t>(app.activeTab)];
    tab.directory       = app.directory;
    tab.entries         = std::move(app.entries);
    tab.cursor          = app.cursor;
    tab.selectionAnchor = app.selectionAnchor;
    tab.selection       = std::move(app.selection);
    tab.filter          = app.filter;
    tab.errorText       = app.errorText;
    tab.scroll          = app.scroll;
    tab.filterActive    = app.filterActive;
    tab.sortColumn      = app.sortColumn;
    tab.sortAscending   = app.sortAscending;
    tab.groupMode       = app.groupMode;
    tab.viewMode        = app.viewMode;
    tab.history         = app.history;
    tab.historyIndex    = app.historyIndex;
}

void LoadActiveTab(AppState& app)
{
    if (app.activeTab < 0 || app.activeTab >= static_cast<int>(app.tabs.size()))
        return;

    Tab& tab = app.tabs[static_cast<size_t>(app.activeTab)];
    app.directory       = tab.directory;
    app.entries         = std::move(tab.entries);
    app.cursor          = tab.cursor;
    app.selectionAnchor = tab.selectionAnchor;
    app.selection       = std::move(tab.selection);
    app.filter          = tab.filter;
    app.errorText       = tab.errorText;
    app.scroll          = tab.scroll;
    app.filterActive    = tab.filterActive;
    app.sortColumn      = tab.sortColumn;
    app.sortAscending   = tab.sortAscending;
    app.groupMode       = tab.groupMode;
    app.viewMode        = tab.viewMode;
    app.history         = tab.history;
    app.historyIndex    = tab.historyIndex;
    app.rectSelecting   = false;
    app.viewDirty       = true;

    UpdateWindowTitle(app);
}

void NewTab(AppState& app, const std::wstring& directory)
{
    SaveActiveTab(app);

    Tab tab;
    // A new tab inherits the view settings, not the location: the user's idea of
    // "how I look at files" should not reset when a tab is opened.
    tab.sortColumn    = app.sortColumn;
    tab.sortAscending = app.sortAscending;
    tab.groupMode     = app.groupMode;
    tab.directory     = directory;

    app.tabs.push_back(std::move(tab));
    app.activeTab = static_cast<int>(app.tabs.size()) - 1;

    app.entries.clear();
    app.selection.clear();
    app.cursor.clear();
    app.selectionAnchor.clear();
    app.scroll = 0.0f;
    app.filter.clear();
    app.filterActive = false;
    LoadDirectory(app, directory);
}

void ActivateTab(AppState& app, int index)
{
    if (index < 0 || index >= static_cast<int>(app.tabs.size()) || index == app.activeTab)
        return;

    SaveActiveTab(app);
    app.activeTab = index;
    LoadActiveTab(app);
    app.stats.CancelQueued();
    app.previews.CancelPending();
}

void CloseTab(AppState& app, int index)
{
    if (index < 0 || index >= static_cast<int>(app.tabs.size()))
        return;
    if (app.tabs.size() == 1)
        return;   // the last tab is not closable: use the window button instead

    const bool closingActive = (index == app.activeTab);
    app.tabs.erase(app.tabs.begin() + index);

    if (index < app.activeTab)
    {
        // The active tab merely shifted down; the live state still matches it.
        --app.activeTab;
        return;
    }

    if (closingActive)
    {
        if (app.activeTab >= static_cast<int>(app.tabs.size()))
            app.activeTab = static_cast<int>(app.tabs.size()) - 1;
        LoadActiveTab(app);
        app.stats.CancelQueued();
        app.previews.CancelPending();
    }
}

void BuildPlaces(AppState& app)
{
    struct Known { KNOWNFOLDERID id; const char* label; };
    const Known known[] = {
        {FOLDERID_Profile,   "Home"},
        {FOLDERID_Desktop,   "Desktop"},
        {FOLDERID_Documents, "Documents"},
        {FOLDERID_Downloads, "Downloads"},
        {FOLDERID_Pictures,  "Pictures"},
        {FOLDERID_Music,     "Music"},
        {FOLDERID_Videos,    "Videos"},
    };

    for (const Known& item : known)
    {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(item.id, 0, nullptr, &path)) && path != nullptr)
        {
            app.places.push_back(Place{item.label, path});
            CoTaskMemFree(path);
        }
    }
}

// --- layout ----------------------------------------------------------------
struct Layout
{
    Rect titleBar;
    Rect sidebar;
    Rect toolbar;
    Rect header;
    Rect list;
    Rect inspector;
    Rect statusBar;
    Rect filterBar;
    float rowHeight = 0.0f;
    float scale = 1.0f;
};

Layout ComputeLayout(const AppState& app)
{
    const float scale = app.zoom;
    const float width = static_cast<float>(app.viewportWidth);
    const float height = static_cast<float>(app.viewportHeight);

    const float sidebarWidth   = app.sidebarVisible ? 186.0f * scale : 0.0f;
    const float inspectorWidth = 292.0f * scale;
    const float titleHeight    = 34.0f * scale;
    const float toolbarHeight  = 38.0f * scale;
    // A grid view has no columns to head, so it gives the space back to the list.
    const bool gridView = (app.viewMode == ViewMode::Tiles || app.viewMode == ViewMode::Icons);
    const float headerHeight   = gridView ? 0.0f : 26.0f * scale;
    const float statusHeight   = 22.0f * scale;
    const float filterHeight   = 24.0f * scale;
    const float gap            = 6.0f * scale;

    Layout layout;
    layout.scale      = scale;
    layout.rowHeight  = 24.0f * scale;
    layout.titleBar   = Rect{0.0f, 0.0f, width, titleHeight};
    layout.toolbar    = Rect{0.0f, titleHeight, width, toolbarHeight};
    layout.sidebar    = Rect{gap, titleHeight + toolbarHeight + gap, sidebarWidth,
                             height - titleHeight - toolbarHeight - statusHeight - gap * 3};
    layout.header     = Rect{sidebarWidth + gap * 2, titleHeight + toolbarHeight + gap,
                             width - sidebarWidth - inspectorWidth - gap * 4, headerHeight};
    layout.filterBar  = Rect{layout.header.x, height - statusHeight - gap - filterHeight,
                             layout.header.w, filterHeight};
    layout.list       = Rect{layout.header.x, layout.header.bottom(),
                             layout.header.w,
                             layout.filterBar.y - gap - layout.header.bottom()};
    layout.inspector  = Rect{width - inspectorWidth - gap, titleHeight + toolbarHeight + gap,
                             inspectorWidth, layout.list.h + headerHeight + filterHeight + gap};
    layout.statusBar  = Rect{0.0f, height - statusHeight, width, statusHeight};
    return layout;
}

// --- drawing ---------------------------------------------------------------
float TextY(const Layout& layout, const Rect& row, const FontAtlas& font)
{
    const float lineHeight = font.LineHeight() * layout.scale;
    return row.y + (row.h - lineHeight) * 0.5f;
}

// The window draws its own caption. The native one is removed by answering
// WM_NCCALCSIZE; what replaces it is a tab strip, which is the point of the
// milestone, plus the three window buttons and the menu button.
void DrawTitleBar(AppState& app, const Layout& layout)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect bar = layout.titleBar;

    r.FillRect(bar, t.headerBg);
    r.FillRect(Rect{bar.x, bar.bottom() - 1.0f, bar.w, 1.0f}, t.separator);

    const float buttonWidth = 44.0f * scale;
    const float stripLeft   = 44.0f * scale;
    const float stripRight  = bar.right() - buttonWidth * 3.0f;

    // Menu button. The reference opens the application menu here; the menu
    // component arrives with the command palette, so for now it toggles the one
    // thing the shell already has.
    const Rect menu{bar.x + 6.0f * scale, bar.y + 7.0f * scale, 30.0f * scale, bar.h - 14.0f * scale};
    r.FillRect(menu, app.hoveredChrome == 0 ? t.rowHover : t.panelBg, 5.0f * scale);
    for (int i = 0; i < 3; ++i)
        r.FillRect(Rect{menu.x + 8.0f * scale, menu.y + 5.0f * scale + i * 5.0f * scale,
                        14.0f * scale, 1.5f * scale}, t.text);
    {
        ClickTarget target;
        target.rect = menu;
        target.kind = ClickTarget::Kind::Menu;
        app.clickTargets.push_back(target);
    }

    float x = stripLeft;
    for (int i = 0; i < static_cast<int>(app.tabs.size()); ++i)
    {
        const Tab& tab = app.tabs[static_cast<size_t>(i)];
        // The active tab's snapshot is empty while it is live: its entries were
        // moved out and only the live fields are current, so the title has to come
        // from there.
        const std::wstring& source = (i == app.activeTab) ? app.directory : tab.directory;
        const std::string title = ToUtf8(LeafName(source));
        const float width = (std::min)(220.0f * scale,
                                       (std::max)(130.0f * scale,
                                                  app.font.Measure(title, scale) + 66.0f * scale));
        if (x + width > stripRight)
            break;   // no overflow menu yet: extra tabs are simply not drawn

        const Rect tabRect{x, bar.y + 5.0f * scale, width, bar.h - 5.0f * scale};
        const bool active = (i == app.activeTab);

        if (active)
        {
            r.FillRect(tabRect, t.panelBg, 6.0f * scale);
            r.Draw(UIDraw::Border(tabRect, t.panelBorder, 1.0f, 6.0f * scale));
        }
        else if (i == app.hoveredTab)
        {
            r.FillRect(tabRect, t.rowHover, 6.0f * scale);
        }

        if (const ShellIconAtlas::IconUV* uv = app.icons.GetFor("", true))
        {
            const Rect icon{tabRect.x + 10.0f * scale, tabRect.y + 6.0f * scale,
                            16.0f * scale, 16.0f * scale};
            r.Draw(UIDraw::Image(icon, active ? t.text : t.textDim, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }

        app.font.Draw(r, tabRect.x + 32.0f * scale, TextY(layout, tabRect, app.font),
                      FitText(app.font, title, scale, tabRect.w - 56.0f * scale),
                      active ? t.text : t.textDim, scale);

        const Rect close{tabRect.right() - 24.0f * scale, tabRect.y + 5.0f * scale,
                         18.0f * scale, tabRect.h - 10.0f * scale};
        app.smallFont.Draw(r, close.x + 5.0f * scale, TextY(layout, close, app.smallFont), "x",
                      i == app.hoveredTab ? t.text : t.textDim, scale);

        ClickTarget tabTarget;
        tabTarget.rect  = tabRect;
        tabTarget.kind  = ClickTarget::Kind::Tab;
        tabTarget.index = i;
        app.clickTargets.push_back(tabTarget);

        ClickTarget closeTarget;
        closeTarget.rect  = close;
        closeTarget.kind  = ClickTarget::Kind::TabClose;
        closeTarget.index = i;
        app.clickTargets.push_back(closeTarget);

        x += width + 3.0f * scale;
    }

    const Rect add{x, bar.y + 5.0f * scale, 30.0f * scale, bar.h - 5.0f * scale};
    if (add.right() <= stripRight)
    {
        r.FillRect(add, app.hoveredChrome == 1 ? t.rowHover : t.headerBg, 5.0f * scale);
        app.font.Draw(r, add.x + 11.0f * scale, TextY(layout, add, app.font), "+", t.text, scale);

        ClickTarget target;
        target.rect = add;
        target.kind = ClickTarget::Kind::TabNew;
        app.clickTargets.push_back(target);
    }

    const bool maximized = (app.window != nullptr) && IsZoomed(app.window) != FALSE;
    for (int i = 0; i < 3; ++i)
    {
        const Rect button{stripRight + i * buttonWidth, bar.y, buttonWidth, bar.h};
        const bool hot = (app.hoveredChrome == 2 + i);
        if (hot)
            r.FillRect(button, i == 2 ? Color::FromHex(0xC42B1C) : t.rowHover);

        const Color glyph = hot && i == 2 ? Color{1, 1, 1, 1} : t.textDim;
        const float centreX = button.x + buttonWidth * 0.5f;
        const float centreY = button.y + button.h * 0.5f;

        if (i == 0)
        {
            r.FillRect(Rect{centreX - 5.0f * scale, centreY, 10.0f * scale, 1.0f}, glyph);
        }
        else if (i == 1)
        {
            if (maximized)
            {
                r.Draw(UIDraw::Border(Rect{centreX - 4.0f * scale, centreY - 5.0f * scale,
                                           9.0f * scale, 9.0f * scale}, glyph, 1.0f));
                r.Draw(UIDraw::Border(Rect{centreX - 6.0f * scale, centreY - 3.0f * scale,
                                           9.0f * scale, 9.0f * scale}, glyph, 1.0f));
            }
            else
            {
                r.Draw(UIDraw::Border(Rect{centreX - 5.0f * scale, centreY - 5.0f * scale,
                                           10.0f * scale, 10.0f * scale}, glyph, 1.0f));
            }
        }
        else
        {
            app.smallFont.Draw(r, centreX - app.smallFont.Measure("x", scale) * 0.5f,
                          centreY - app.smallFont.LineHeight() * scale * 0.5f, "x", glyph, scale);
        }

        ClickTarget target;
        target.rect = button;
        target.kind = (i == 0) ? ClickTarget::Kind::WindowMinimize
                    : (i == 1) ? ClickTarget::Kind::WindowMaximize
                               : ClickTarget::Kind::WindowClose;
        app.clickTargets.push_back(target);
    }
}

void DrawToolbar(AppState& app, const Layout& layout, const std::wstring& displayPath)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.FillRect(layout.toolbar, t.headerBg);

    // Breadcrumb: split the path into components, each clickable. Everything is
    // measured from the toolbar rectangle, not from the top of the window: the
    // title bar above it is drawn by the application and its height varies with
    // the UI scale.
    float penX = layout.toolbar.x + 12.0f * scale;
    const float baseline = layout.toolbar.y + 11.0f * scale;

    std::wstring accumulated;
    size_t start = 0;
    while (start <= displayPath.size())
    {
        size_t slash = displayPath.find(L'\\', start);
        if (slash == std::wstring::npos)
            slash = displayPath.size();

        std::wstring segment = displayPath.substr(start, slash - start);
        if (accumulated.empty())
            accumulated = segment;
        else if (!segment.empty())
            accumulated += L"\\" + segment;

        const std::string label = ToUtf8(segment);
        const float width = app.font.Measure(label, scale) + 14.0f * scale;

        const Rect chip{penX, baseline - 4.0f * scale, width, 22.0f * scale};
        ClickTarget target;
        target.rect  = chip;
        target.kind  = ClickTarget::Kind::Breadcrumb;
        target.path  = accumulated;
        app.clickTargets.push_back(target);

        if (!segment.empty())
        {
            app.font.Draw(r, penX + 7.0f * scale, baseline + 2.0f * scale, label,
                          slash == displayPath.size() ? t.text : t.textDim, scale);
        }
        penX += width;

        if (slash == displayPath.size())
            break;
        app.font.Draw(r, penX - 4.0f * scale, baseline + 2.0f * scale, ">", t.textDim, scale);
        start = slash + 1;
    }

    // Item count on the right.
    char summary[64];
    std::snprintf(summary, sizeof(summary), "%zu items", app.entries.size());
    const float summaryWidth = app.font.Measure(summary, scale);
    app.font.Draw(r, static_cast<float>(app.viewportWidth) - summaryWidth - 14.0f * scale,
                  layout.toolbar.y + 12.0f * scale, summary, t.textDim, scale);
}

void DrawSidebar(AppState& app, const Layout& layout)
{
    if (!app.sidebarVisible)
        return;

    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.Draw(UIDraw::Fill(layout.sidebar, t.panelBg, 6.0f * scale));
    app.smallFont.Draw(r, layout.sidebar.x + 12.0f * scale, layout.sidebar.y + 9.0f * scale,
                  "QUICK ACCESS", t.textDim, scale);

    float y = layout.sidebar.y + 32.0f * scale;
    for (size_t i = 0; i < app.places.size(); ++i)
    {
        const Place& place = app.places[i];
        const Rect row{layout.sidebar.x + 5.0f * scale, y, layout.sidebar.w - 10.0f * scale, 26.0f * scale};

        const bool active = (place.path == app.directory);
        if (active)
            r.FillRect(row, Color::FromHex(0x2E3138), 4.0f * scale);
        else if (static_cast<int>(i) == app.hoveredPlace)
            r.FillRect(row, t.rowHover, 4.0f * scale);

        const ShellIconAtlas::IconUV* uv = app.icons.GetFor("", true);
        if (uv != nullptr)
        {
            const Rect icon{row.x + 8.0f * scale, row.y + 5.0f * scale, 16.0f * scale, 16.0f * scale};
            r.Draw(UIDraw::Image(icon, active ? t.text : t.textDim, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }
        app.font.Draw(r, row.x + 30.0f * scale, TextY(layout, row, app.font), place.label,
                      active ? t.text : t.textDim, scale);

        ClickTarget target;
        target.rect = row;
        target.kind = ClickTarget::Kind::Place;
        target.index = static_cast<int>(i);
        target.path = place.path;
        app.clickTargets.push_back(target);

        y += 28.0f * scale;
    }
}

struct ColumnDef
{
    const char* label;
    SortColumn  sort;
    float       fraction;   // of the list width, measured from its left edge
};

// Name, then the columns a directory entry already knows, then the three that
// depend on the recursive statistics and arrive progressively.
const ColumnDef kColumns[] = {
    {"Name",     SortColumn::Name,     0.00f},
    {"Type",     SortColumn::Type,     0.38f},
    {"Modified", SortColumn::Modified, 0.57f},
    {"Folders",  SortColumn::Folders,  0.71f},
    {"Files",    SortColumn::Files,    0.80f},
    {"Size",     SortColumn::Size,     0.88f},
};
constexpr int kColumnCount = static_cast<int>(sizeof(kColumns) / sizeof(kColumns[0]));

void DrawHeader(AppState& app, const Layout& layout, float* columnX)
{
    if (app.viewMode == ViewMode::Tiles || app.viewMode == ViewMode::Icons)
        return;   // nothing to head, and the list already has the space

    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.FillRect(layout.header, t.headerBg, 4.0f * scale);

    // A small pulse beside the statistics headers while jobs are running: the
    // numbers are still moving, and that is worth saying out loud.
    const float pulse = 0.25f + 0.75f * static_cast<float>(std::fabs(std::sin(app.lastFrameTime * 3.0)));

    for (int i = 0; i < kColumnCount; ++i)
    {
        const float x = layout.header.x + 10.0f * scale + layout.header.w * kColumns[i].fraction;
        columnX[i] = x;

        const bool sorted = (app.sortColumn == kColumns[i].sort);
        std::string label = kColumns[i].label;
        if (sorted)
            label += app.sortAscending ? " ^" : " v";

        app.smallFont.Draw(r, x, TextY(layout, layout.header, app.smallFont), label,
                      sorted ? t.text : t.textDim, scale);

        if (i >= 3 && app.stats.Busy())
        {
            const float dotX = x + app.smallFont.Measure(label, scale) + 6.0f * scale;
            const float dotY = layout.header.y + layout.header.h * 0.5f - 2.0f * scale;
            r.FillRect(Rect{dotX, dotY, 4.0f * scale, 4.0f * scale},
                       t.accent.WithAlpha(pulse), 2.0f * scale);
        }

        const float width = (i < kColumnCount - 1)
            ? layout.header.w * (kColumns[i + 1].fraction - kColumns[i].fraction)
            : layout.header.w * (1.0f - kColumns[i].fraction);
        ClickTarget target;
        target.rect = Rect{x - 10.0f * scale, layout.header.y, width, layout.header.h};
        target.kind = ClickTarget::Kind::Column;
        target.index = i;
        app.clickTargets.push_back(target);
    }

    r.FillRect(Rect{layout.header.x, layout.header.bottom() - 1.0f, layout.header.w, 1.0f}, t.separator);
}

// The filter box sits under the list, the way the reference application puts it.
// It is the only text entry in the window, so it owns the keyboard whenever the
// filter is active.
void DrawFilterBar(AppState& app, const Layout& layout)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect bar = layout.filterBar;

    r.FillRect(bar, t.headerBg, 4.0f * scale);
    r.Draw(UIDraw::Border(bar, app.filterActive ? t.accent.WithAlpha(0.75f) : t.panelBorder,
                          1.0f, 4.0f * scale));

    const std::string text = app.filter.empty()
        ? ("Filter " + std::to_string(app.entries.size()) + " files...")
        : ToUtf8(app.filter);

    const float textX = bar.x + 10.0f * scale;
    app.font.Draw(r, textX, TextY(layout, bar, app.font), text,
                  app.filter.empty() ? t.textDim : t.text, scale);

    if (app.filterActive)
    {
        const float caretX = textX + app.font.Measure(text, scale) + 1.0f * scale;
        r.FillRect(Rect{caretX, bar.y + 5.0f * scale, 1.0f, bar.h - 10.0f * scale}, t.text);
    }

    // The count is how many entries the filter matched, not how many rows are on
    // screen: folding a group away must not look like the filter changed.
    const std::string matchText = std::to_string(app.matchCount) + " / " +
                                  std::to_string(app.entries.size());

    // A grid cannot carry a full-width group band, so grouping is off there. Say
    // so rather than leaving the control claiming something the view is not doing.
    const bool gridView = (app.viewMode == ViewMode::Tiles || app.viewMode == ViewMode::Icons);
    std::string groupText = std::string("Group: ") + GroupModeName(app.groupMode);
    if (gridView && app.groupMode != GroupMode::None)
        groupText += " (rows only)";
    const float matchWidth = app.smallFont.Measure(matchText, scale);
    const float groupWidth = app.smallFont.Measure(groupText, scale);
    const float rightEdge  = bar.right() - 10.0f * scale;

    app.smallFont.Draw(r, rightEdge - matchWidth, TextY(layout, bar, app.smallFont),
                  matchText, t.textDim, scale);

    const Rect groupRect{rightEdge - matchWidth - groupWidth - 20.0f * scale, bar.y,
                         groupWidth + 12.0f * scale, bar.h};
    app.smallFont.Draw(r, groupRect.x, TextY(layout, bar, app.smallFont), groupText,
                  (gridView && app.groupMode != GroupMode::None) ? t.textDim : t.accent,
                  scale);

    ClickTarget target;
    target.rect = groupRect;
    target.kind = ClickTarget::Kind::GroupMode;
    app.clickTargets.push_back(target);
}

ViewMetrics ComputeViewMetrics(const AppState& app, const Layout& layout)
{
    const float scale = layout.scale;
    // The cell height is derived from the icon and the label rather than fixed,
    // so a two line name can never overlap the picture above it.
    const float labelHeight = app.font.LineHeight() * scale * 2.0f;

    ViewMetrics metrics;
    metrics.padding = 6.0f * scale;

    switch (app.viewMode)
    {
        case ViewMode::List:
            metrics.rowHeight = 20.0f * scale;
            metrics.iconSize  = 14.0f * scale;
            break;

        case ViewMode::Icons:
            metrics.grid       = true;
            metrics.iconSize   = 64.0f * scale;
            metrics.cellWidth  = 104.0f * scale;
            metrics.cellHeight = metrics.iconSize + labelHeight + 14.0f * scale;
            break;

        case ViewMode::Tiles:
            metrics.grid       = true;
            metrics.iconSize   = 128.0f * scale;
            metrics.cellWidth  = 176.0f * scale;
            metrics.cellHeight = metrics.iconSize + labelHeight + 16.0f * scale;
            break;

        case ViewMode::Details:
        default:
            metrics.rowHeight = layout.rowHeight;
            metrics.iconSize  = 16.0f * scale;
            break;
    }

    if (metrics.grid)
    {
        const float usable = layout.list.w - metrics.padding * 2.0f;
        metrics.columns = (std::max)(1, static_cast<int>(usable / metrics.cellWidth));
        // Spread the leftover width so the cells tile the panel exactly.
        metrics.cellWidth = usable / static_cast<float>(metrics.columns);
    }
    return metrics;
}

float ViewContentHeight(const ViewMetrics& metrics, size_t count)
{
    if (!metrics.grid)
        return static_cast<float>(count) * metrics.rowHeight;
    const int rows = static_cast<int>((count + static_cast<size_t>(metrics.columns) - 1) /
                                      static_cast<size_t>(metrics.columns));
    return static_cast<float>(rows) * metrics.cellHeight + metrics.padding * 2.0f;
}

Rect ItemRect(const Layout& layout, const ViewMetrics& metrics, int rowIndex, float scroll)
{
    if (!metrics.grid)
    {
        return Rect{layout.list.x + 1.0f,
                    layout.list.y + static_cast<float>(rowIndex) * metrics.rowHeight - scroll,
                    layout.list.w - 2.0f, metrics.rowHeight};
    }

    const int column = rowIndex % metrics.columns;
    const int row    = rowIndex / metrics.columns;
    return Rect{layout.list.x + metrics.padding + static_cast<float>(column) * metrics.cellWidth,
                layout.list.y + metrics.padding + static_cast<float>(row) * metrics.cellHeight - scroll,
                metrics.cellWidth, metrics.cellHeight};
}

// Splits a name into at most |maxLines| lines that fit, ellipsising the last one.
// Character based: this is a label, and the glyph atlas is ASCII.
std::vector<std::string> WrapName(const FontAtlas& font, const std::string& text, float scale,
                                  float maxWidth, int maxLines)
{
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0.0f || maxLines < 1)
        return lines;

    std::string current;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (font.Measure(current + c, scale) > maxWidth && !current.empty())
        {
            lines.push_back(current);
            if (static_cast<int>(lines.size()) == maxLines - 1)
            {
                current = text.substr(i);   // the rest goes on the last line
                break;
            }
            current.clear();
            if (c == ' ')
                continue;                    // do not start a line with a space
        }
        current.push_back(c);
    }
    if (!current.empty())
        lines.push_back(current);

    if (!lines.empty())
        lines.back() = FitText(font, lines.back(), scale, maxWidth);
    return lines;
}

// One cell of a grid view: thumbnail on top, name underneath, centred.
void DrawEntryCell(AppState& app, const Layout& layout, const ViewMetrics& metrics,
                   const Rect& cell, const Row& view, int cursorIndex)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Entry& entry = app.entries[static_cast<size_t>(view.entry)];

    const Rect card = cell.Inset(metrics.padding * 0.5f, metrics.padding * 0.5f);
    // La selezione vince sul sovrappasso: la riga sotto il mouse resta leggibile
    // anche quando e' selezionata, invece di cambiare colore sotto il puntatore.
    if (IsEntrySelected(app, entry))
        r.FillRect(card, t.rowSelected, 4.0f * scale);
    else if (view.entry == app.hovered)
        r.FillRect(card, t.rowHover, 4.0f * scale);
    if (view.entry == cursorIndex)
        r.Draw(UIDraw::Border(card, t.accent.WithAlpha(0.9f), 1.0f, 4.0f * scale));

    // La destinazione di una caduta: tratteggio, cosi' si distingue sia dal
    // riempimento della selezione sia dalla linea continua del cursore.
    if (!app.dropTarget.empty() && app.dropTargetIsRow &&
        entry.wideName == LeafName(app.dropTarget))
        r.Draw(UIDraw::DashedBorder(card, t.accent, 2.0f * scale, 6.0f * scale, 4.0f * scale,
                                    6.0f * scale));

    ClickTarget target;
    target.rect  = card;
    target.kind  = ClickTarget::Kind::Row;
    target.index = view.entry;
    app.clickTargets.push_back(target);

    const float lineHeight = app.font.LineHeight() * scale;
    const float labelTop   = card.bottom() - lineHeight * 2.0f;
    const float iconArea   = (std::max)(16.0f, labelTop - card.y - 4.0f * scale);
    const float iconSize   = (std::min)(metrics.iconSize, iconArea);
    const Rect icon{card.x + (card.w - iconSize) * 0.5f,
                    card.y + (iconArea - iconSize) * 0.5f,
                    iconSize, iconSize};

    ShellPreviewCache::IconUV preview;
    const ShellPreviewCache::State state =
        app.previews.Query(JoinPath(app.directory, entry.wideName), entry.extension, &preview);

    if (state == ShellPreviewCache::State::Ready)
    {
        r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                             preview.u0, preview.v0, preview.u1, preview.v1));
    }
    else if (const ShellIconAtlas::IconUV* uv = app.icons.GetFor(entry.extension, entry.isDirectory))
    {
        r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                             uv->u0, uv->v0, uv->u1, uv->v1));
    }

    const std::vector<std::string> lines =
        WrapName(app.font, entry.name, scale, card.w - 8.0f * scale, 2);
    float textY = labelTop;
    for (const std::string& line : lines)
    {
        const float width = app.font.Measure(line, scale);
        app.font.Draw(r, card.x + (card.w - width) * 0.5f, textY, line, t.text, scale);
        textY += lineHeight;
    }
}

void DrawList(AppState& app, const Layout& layout, const float* columnX)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const int cursorIndex = CursorIndex(app);
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    const float rowHeight = metrics.rowHeight;

    r.Draw(UIDraw::Fill(layout.list, t.panelBg, 6.0f * scale));

    // Il fondo della lista riceve la caduta nella cartella aperta: si evidenzia
    // l'area, perche' li' sotto non c'e' nessuna riga da tratteggiare. Il
    // tratteggio e' lo stesso della riga, cosi' le due destinazioni si leggono
    // come la stessa cosa detta in due punti diversi.
    if (!app.dropTarget.empty() && !app.dropTargetIsRow)
        r.Draw(UIDraw::DashedBorder(layout.list.Inset(1.0f, 1.0f), t.accent, 2.0f * scale,
                                    6.0f * scale, 4.0f * scale, 6.0f * scale));

    if (!app.errorText.empty())
    {
        app.font.Draw(r, layout.list.x + 16.0f * scale, layout.list.y + 16.0f * scale,
                      app.errorText, t.textDim, scale);
        return;
    }

    // A grid packs several rows of the view into one visual row, so the window of
    // interesting rows is computed in items, not in visual rows.
    int first = 0;
    int last  = static_cast<int>(app.rows.size());
    if (metrics.grid)
    {
        const int firstGridRow = (std::max)(0, static_cast<int>(app.scroll / metrics.cellHeight) - 1);
        first = firstGridRow * metrics.columns;
        last  = (std::min)(last, first + (static_cast<int>(layout.list.h / metrics.cellHeight) + 3) * metrics.columns);
    }
    else
    {
        first = (std::max)(0, static_cast<int>(app.scroll / rowHeight) - 1);
        last  = (std::min)(last, first + static_cast<int>(layout.list.h / rowHeight) + 3);
    }

    for (int rowIndex = first; rowIndex < last; ++rowIndex)
    {
        const Row& view = app.rows[static_cast<size_t>(rowIndex)];
        const Rect row = ItemRect(layout, metrics, rowIndex, app.scroll);

        if (row.y < layout.list.y - row.h || row.y > layout.list.bottom())
            continue;

        // A group band is a row like any other: it scrolls, it is hit tested, and
        // clicking it folds the group away. Folding is remembered by label, so it
        // survives a rebuild of the view.
        if (view.header)
        {
            const Group& group = app.groups[static_cast<size_t>(view.group)];
            const bool collapsed = app.collapsedGroups.count(group.label) != 0;

            r.FillRect(row, t.headerBg);
            const std::string title = (collapsed ? "> " : "v ") + group.label;
            app.smallFont.Draw(r, row.x + 8.0f * scale, TextY(layout, row, app.smallFont),
                          title, t.text, scale);

            const std::string count = std::to_string(group.count) + " items";
            app.smallFont.Draw(r, row.right() - app.smallFont.Measure(count, scale) - 10.0f * scale,
                          TextY(layout, row, app.smallFont), count, t.textDim, scale);

            ClickTarget target;
            target.rect  = row;
            target.kind  = ClickTarget::Kind::GroupHeader;
            target.index = view.group;
            target.label = group.label;
            app.clickTargets.push_back(target);
            continue;
        }

        if (metrics.grid)
        {
            DrawEntryCell(app, layout, metrics, row, view, cursorIndex);
            continue;
        }

        const Entry& entry = app.entries[static_cast<size_t>(view.entry)];
        // L'ordine e' deliberato: selezione, poi sovrappasso, poi righe alternate.
        // Il sovrappasso deve stare PRIMA dell'alternanza, o sarebbe visibile
        // solo sulle righe pari e a chiazze.
        if (IsEntrySelected(app, entry))
            r.FillRect(row, t.rowSelected, 3.0f * scale);
        else if (view.entry == app.hovered)
            r.FillRect(row, t.rowHover, 3.0f * scale);
        else if (rowIndex % 2 == 1)
            r.FillRect(row, t.rowOdd, 3.0f * scale);

        // The selection has a fill; the cursor has an outline. The reference theme
        // keeps them as separate tokens (ContentSelection, OutlineSelection) for
        // exactly this reason: with more than one row selected the user still has
        // to be able to see which one the keyboard is on.
        if (view.entry == cursorIndex)
            r.Draw(UIDraw::Border(row.Inset(1.0f, 1.0f), t.accent.WithAlpha(0.9f), 1.0f, 2.0f * scale));

        // La destinazione di una caduta in corso.
        if (!app.dropTarget.empty() && app.dropTargetIsRow &&
            entry.wideName == LeafName(app.dropTarget))
            r.Draw(UIDraw::DashedBorder(row.Inset(1.0f, 1.0f), t.accent, 2.0f * scale,
                                        6.0f * scale, 4.0f * scale, 6.0f * scale));

        ClickTarget target;
        target.rect = row;
        target.kind = ClickTarget::Kind::Row;
        target.index = view.entry;
        app.clickTargets.push_back(target);

        // A real thumbnail replaces the type icon when one is ready. Asking is
        // free: an unavailable type never reaches the worker, and the request is
        // de-duplicated while a decode is in flight.
        const Rect icon{row.x + 6.0f * scale,
                        row.y + (rowHeight - metrics.iconSize) * 0.5f,
                        metrics.iconSize, metrics.iconSize};
        ShellPreviewCache::IconUV preview;
        const ShellPreviewCache::State previewState =
            app.previews.Query(JoinPath(app.directory, entry.wideName), entry.extension, &preview);

        if (previewState == ShellPreviewCache::State::Ready)
        {
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                                 preview.u0, preview.v0, preview.u1, preview.v1));
        }
        else if (const ShellIconAtlas::IconUV* uv = app.icons.GetFor(entry.extension, entry.isDirectory))
        {
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }

        const float textY = TextY(layout, row, app.font);
        const std::wstring fullPath = JoinPath(app.directory, entry.wideName);

        // Numeric columns are right aligned so the digits line up down the
        // column, and underlined while the number is still growing.
        const auto columnRight = [&](int column) -> float
        {
            const float end = (column < kColumnCount - 1) ? columnX[column + 1]
                                                          : layout.header.right();
            return end - 8.0f * scale;
        };
        const auto drawRight = [&](int column, const std::string& text, Color color, bool growing)
        {
            const float right = columnRight(column);
            const float width = app.font.Measure(text, scale);
            app.font.Draw(r, right - width, textY, text, color, scale);
            if (growing)
                r.FillRect(Rect{right - width, row.bottom() - 5.0f * scale, width, 1.0f},
                           color.WithAlpha(0.6f));
        };

        // Name starts clear of the icon; the header label above it does not need
        // that offset because no icon is drawn there.
        const float nameX   = row.x + 28.0f * scale;
        const float nameMax = columnX[1] - nameX - 12.0f * scale;

        if (app.renameActive && entry.wideName == app.renameOriginal)
        {
            // Il campo si disegna SOPRA la riga invece di sostituirne il
            // contenuto: la lista non cambia impaginazione mentre si scrive,
            // quindi niente si sposta sotto le dita di chi sta digitando.
            const Rect field{nameX - 3.0f * scale, row.y + 1.0f * scale,
                             nameMax + 6.0f * scale, row.h - 2.0f * scale};
            r.Draw(UIDraw::Fill(field, t.windowBg, 2.0f * scale));
            r.Draw(UIDraw::Border(field, t.accent, 1.0f, 2.0f * scale));

            const std::string shown =
                FitText(app.font, app.renameText, scale, field.w - 8.0f * scale);
            app.font.Draw(r, nameX + 1.0f * scale, textY, shown, t.text, scale);

            // Il cursore va dopo il testo VISIBILE, non dopo quello completo: se
            // il nome e' piu' lungo del campo, un cursore sul fondo del campo
            // sembrerebbe fermo mentre si scrive.
            const float caretX =
                nameX + 1.0f * scale + app.font.Measure(shown, scale) + 1.0f * scale;
            r.FillRect(Rect{caretX, row.y + 3.0f * scale, 1.0f * scale,
                            row.h - 6.0f * scale},
                       t.text);
        }
        else
        {
            app.font.Draw(r, nameX, textY, FitText(app.font, entry.name, scale, nameMax), t.text, scale);
        }

        const float typeMax = columnX[2] - columnX[1] - 12.0f * scale;
        app.font.Draw(r, columnX[1], textY, FitText(app.font, entry.typeText, scale, typeMax), t.textDim, scale);
        app.font.Draw(r, columnX[2], textY, entry.modifiedText, t.textDim, scale);

        // Files answer immediately; folders are answered by the statistics
        // service, which fills them in progressively. Requesting here means only
        // rows that are actually on screen get counted.
        if (entry.isDirectory)
        {
            app.stats.Request(fullPath);

            DirStats stats;
            const bool known = app.stats.TryGet(fullPath, &stats);
            const Color numberColor = known ? t.text : t.textDim;
            const bool growing = known && stats.partial;

            drawRight(3, known ? HumanCount(stats.folders) : "--", numberColor, growing);
            drawRight(4, known ? HumanCount(stats.files) : "--", numberColor, growing);
            drawRight(5, known ? HumanBytes(stats.logicalBytes) : "--", numberColor, growing);
        }
        else
        {
            drawRight(3, "--", t.textDim, false);
            drawRight(4, "--", t.textDim, false);
            drawRight(5, entry.sizeText, t.textDim, false);
        }
    }

    // Scrollbar.
    const float contentHeight = ViewContentHeight(metrics, app.rows.size());
    if (contentHeight > layout.list.h)
    {
        const float trackHeight = layout.list.h - 8.0f;
        const float thumbHeight = (std::max)(24.0f * scale, trackHeight * (layout.list.h / contentHeight));
        const float maxScroll = (std::max)(1.0f, contentHeight - layout.list.h);
        const float thumbY = layout.list.y + 4.0f + (trackHeight - thumbHeight) * (app.scroll / maxScroll);

        const Rect track{layout.list.right() - 7.0f, layout.list.y + 4.0f, 4.0f, trackHeight};
        r.FillRect(track, t.separator, 2.0f * scale);
        r.FillRect(Rect{track.x, thumbY, track.w, thumbHeight}, t.scrollThumb, 2.0f * scale);
    }
}

void DrawInspector(AppState& app, const Layout& layout)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.Draw(UIDraw::Shadow(Rect{layout.inspector.x + 2.0f, layout.inspector.y + 3.0f,
                               layout.inspector.w, layout.inspector.h},
                          Color{0.0f, 0.0f, 0.0f, 0.32f}, 10.0f * scale, 8.0f * scale));
    r.Draw(UIDraw::Fill(layout.inspector, t.panelBg, 8.0f * scale));
    r.Draw(UIDraw::Border(layout.inspector, t.panelBorder, 1.0f, 8.0f * scale));

    float y = layout.inspector.y + 13.0f * scale;
    app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y, "INSPECTOR", t.textDim, scale);
    y += 24.0f * scale;

    const int selectedCount = static_cast<int>(app.selection.size());
    const int cursorIndex = CursorIndex(app);

    if (selectedCount == 0 || cursorIndex < 0)
    {
        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y, "Nothing selected", t.textDim, scale);
        return;
    }

    if (selectedCount > 1)
    {
        // The reference inspector has labels for "file singolo o conteggio" and
        // "dimensione o somma": with several entries selected it reports the
        // aggregate. Folder sizes come from the cache only, because asking for a
        // folder that is not on screen would start work the user cannot see.
        uint64_t totalBytes = 0;
        int files = 0;
        int folders = 0;
        for (const std::wstring& name : app.selection)
        {
            const int index = FindEntryByName(app, name);
            if (index < 0)
                continue;
            const Entry& item = app.entries[static_cast<size_t>(index)];
            if (item.isDirectory)
            {
                ++folders;
                DirStats stats;
                if (app.stats.TryGet(JoinPath(app.directory, item.wideName), &stats))
                    totalBytes += stats.logicalBytes;
            }
            else
            {
                ++files;
                totalBytes += item.bytes;
            }
        }

        const std::string title = std::to_string(selectedCount) + " items selected";
        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y, title, t.text, scale);
        y += 26.0f * scale;

        const std::string breakdown =
            std::to_string(files) + " files, " + std::to_string(folders) + " folders";
        const std::pair<const char*, std::string> summary[] = {
            {"Items", breakdown},
            {"Size", HumanBytes(totalBytes)},
        };
        for (const auto& item : summary)
        {
            app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y, item.first, t.textDim, scale);
            app.smallFont.Draw(r, layout.inspector.x + 96.0f * scale, y, item.second, t.text, scale);
            y += 20.0f * scale;
        }
        return;
    }

    const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];

    ShellPreviewCache::IconUV preview;
    const ShellPreviewCache::State previewState =
        app.previews.Query(JoinPath(app.directory, entry.wideName), entry.extension, &preview);

    if (previewState == ShellPreviewCache::State::Ready)
    {
        // A ready thumbnail gets the width of the panel and pushes the name
        // underneath it: this is the one place large enough to be worth looking at.
        const float side = 128.0f * scale;
        const Rect frame{layout.inspector.x + 14.0f * scale, y, side, side};
        r.Draw(UIDraw::Fill(frame, Color::FromHex(0x131417), 6.0f * scale));
        r.Draw(UIDraw::Image(frame.Inset(1.0f, 1.0f), Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                             preview.u0, preview.v0, preview.u1, preview.v1));
        r.Draw(UIDraw::Border(frame, t.panelBorder, 1.0f, 6.0f * scale));
        y += side + 12.0f * scale;

        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y,
                      FitText(app.font, entry.name, scale, layout.inspector.w - 28.0f * scale),
                      t.text, scale);
        y += 26.0f * scale;
    }
    else
    {
        if (const ShellIconAtlas::IconUV* uv = app.icons.GetFor(entry.extension, entry.isDirectory))
        {
            const Rect icon{layout.inspector.x + 14.0f * scale, y, 32.0f * scale, 32.0f * scale};
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }

        // Name too long for the inspector column is truncated the same way.
        const float valueMax = layout.inspector.w - 110.0f * scale;
        app.font.Draw(r, layout.inspector.x + 56.0f * scale, y + 8.0f * scale,
                      FitText(app.font, entry.name, scale, valueMax), t.text, scale);
        y += 46.0f * scale;
    }

    // Folder numbers, if the panel is counting them. Asking here covers the
    // selected row even when it has scrolled out of view.
    DirStats stats;
    bool hasStats = false;
    if (entry.isDirectory)
    {
        const std::wstring full = JoinPath(app.directory, entry.wideName);
        app.stats.Request(full);
        hasStats = app.stats.TryGet(full, &stats);
    }

    struct Row { const char* label; std::string value; bool growing; };
    std::vector<Row> rows;
    rows.push_back({"Type", entry.typeText, false});
    if (entry.isDirectory)
    {
        const bool growing = hasStats && stats.partial;
        rows.push_back({"Size",    hasStats ? HumanBytes(stats.logicalBytes) : "counting...", growing});
        rows.push_back({"Folders", hasStats ? HumanCount(stats.folders) : "--", growing});
        rows.push_back({"Files",   hasStats ? HumanCount(stats.files) : "--", growing});
    }
    else
    {
        rows.push_back({"Size", entry.sizeText, false});
    }
    rows.push_back({"Modified", entry.modifiedFull, false});
    rows.push_back({"Ext", entry.extension.empty() ? "—" : entry.extension, false});

    for (const Row& row : rows)
    {
        app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y, row.label, t.textDim, scale);
        const float valueX = layout.inspector.x + 96.0f * scale;
        app.smallFont.Draw(r, valueX, y, row.value, t.text, scale);
        if (row.growing)
        {
            const float width = app.smallFont.Measure(row.value, scale);
            r.FillRect(Rect{valueX, y + 15.0f * scale, width, 1.0f}, t.textDim);
        }
        y += 20.0f * scale;
    }

    // Qui c'era un riquadro tratteggiato "Drop files here (not wired up yet)",
    // lasciato da M1 quando il tratteggio serviva solo a dare qualcosa da fare al
    // tracciato dello shader. Adesso il tratteggio ha il suo uso vero —
    // l'evidenziazione della destinazione di una caduta, in `DrawList` — e questo
    // segnaposto prometteva qualcosa che nessuna milestone ha mai previsto.
}

void DrawStatusBar(AppState& app, const Layout& layout)
{
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    char status[200];
    std::snprintf(status, sizeof(status),
                  "%zu items, %zu selected   |   %.0f fps   |   %.0f quads   |   %d icons, %d previews   |   %d stats jobs   |   Enter opens, F5 refreshes",
                  app.entries.size(), app.selection.size(), static_cast<double>(app.framesPerSecond),
                  static_cast<double>(app.renderer.QuadCount()),
                  app.icons.IconCount(), app.previews.ReadyCount(),
                  app.stats.ActiveJobs());
    // Un esito recente prende il posto delle statistiche. Chi ha appena premuto
    // un tasto guarda li' per sapere com'e' andata, e le statistiche non
    // rispondono alla sua domanda.
    const bool showingResult = !app.statusMessage.empty() &&
                               NowSeconds() - app.statusMessageTime < 4.0;
    if (showingResult)
    {
        app.smallFont.Draw(r, 12.0f * scale, layout.statusBar.y + 4.0f * scale,
                           ToUtf8(app.statusMessage), t.accent, scale);
        return;
    }

    app.smallFont.Draw(r, 12.0f * scale, layout.statusBar.y + 4.0f * scale, status, t.textDim, scale);
}

// --- command palette: registry ----------------------------------------------
struct CommandDef
{
    const char* group;
    const char* label;
    CommandId   id;
};

// Group names come from the reference key table (the 19 palette groups). Only the
// groups this shell can carry out are present.
const CommandDef kCommandTable[] = {
    {"Tabs",    "New tab",                     CmdNewTab},
    {"Tabs",    "Close current tab",           CmdCloseTab},
    {"Tabs",    "Next tab",                    CmdNextTab},
    {"Tabs",    "Previous tab",                CmdPrevTab},
    {"Folder",  "Go to parent folder",         CmdGoUp},
    {"Folder",  "Go back",                     CmdBack},
    {"Folder",  "Go forward",                  CmdForward},
    {"Folder",  "Refresh",                     CmdRefresh},
    {"Folder",  "Recalculate folder sizes",    CmdRecalcStats},
    {"Folder",  "Open in Explorer",            CmdOpenExplorer},
    {"Folder",  "Open a console here",         CmdOpenConsole},
    {"File",    "Copy",                        CmdCopy},
    {"File",    "Cut",                         CmdCut},
    {"File",    "Paste",                       CmdPaste},
    {"File",    "Rename",                      CmdRename},
    {"File",    "New folder",                  CmdNewFolder},
    {"File",    "Delete (recycle bin)",        CmdDelete},
    {"File",    "Delete permanently",          CmdDeletePermanent},
    {"GoTo",    "Go to folder...",             CmdGoTo},
    {"View",    "View: details",               CmdViewDetails},
    {"View",    "View: list",                  CmdViewList},
    {"View",    "View: tiles",                 CmdViewTiles},
    {"View",    "View: icons",                 CmdViewIcons},
    {"View",    "Toggle sidebar",              CmdToggleSidebar},
    {"View",    "Zoom in",                     CmdZoomIn},
    {"View",    "Zoom out",                    CmdZoomOut},
    {"View",    "Reset zoom",                  CmdZoomReset},
    {"View",    "Toggle command details",      CmdToggleDetails},
    {"Sort",    "Sort by name",                CmdSortName},
    {"Sort",    "Sort by type",                CmdSortType},
    {"Sort",    "Sort by date modified",       CmdSortModified},
    {"Sort",    "Sort by folder count",        CmdSortFolders},
    {"Sort",    "Sort by file count",          CmdSortFiles},
    {"Sort",    "Sort by size",                CmdSortSize},
    {"Sort",    "Reverse sort direction",      CmdSortDirection},
    {"GroupBy", "Group: none",                 CmdGroupNone},
    {"GroupBy", "Group by name",               CmdGroupName},
    {"GroupBy", "Group by date",               CmdGroupDate},
    {"GroupBy", "Group by size",               CmdGroupSize},
    {"GroupBy", "Group by item count",         CmdGroupCount},
    {"GroupBy", "Group by volume usage",       CmdGroupVolume},
    {"Text",    "Focus the filter box",        CmdFocusFilter},
    {"Text",    "Clear the filter",            CmdClearFilter},
    {"Scroll",  "Scroll to top",               CmdScrollTop},
    {"Scroll",  "Scroll to bottom",            CmdScrollBottom},
    {"Bookmarks", "Bookmark this folder",      CmdToggleBookmark},
    {"Bookmarks", "Clear all bookmarks",       CmdClearBookmarks},
    {"Recents", "Clear recent locations",      CmdClearRecents},
};

bool MenuMatch(const std::string& text, const std::wstring& filter)
{
    if (filter.empty())
        return true;

    std::string lower = text;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    size_t start = 0;
    for (;;)
    {
        size_t space = filter.find(L' ', start);
        if (space == std::wstring::npos)
            space = filter.size();
        if (space > start)
        {
            std::string term = ToUtf8(filter.substr(start, space - start));
            for (char& c : term)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find(term) == std::string::npos)
                return false;
        }
        if (space >= filter.size())
            break;
        start = space + 1;
    }
    return true;
}

// --- the group menu component -----------------------------------------------
void GroupMenuRebuild(GroupMenu& menu);

void GroupMenuOpen(GroupMenu& menu, std::string title, std::string placeholder,
                   std::vector<GroupMenuItem> items)
{
    menu.title       = std::move(title);
    menu.placeholder = std::move(placeholder);
    menu.items       = std::move(items);
    menu.filter.clear();
    menu.selected = 0;
    menu.dirty    = true;
    menu.open     = true;
    GroupMenuRebuild(menu);   // so the selection is valid before the first frame
}

void GroupMenuRebuild(GroupMenu& menu)
{
    menu.rows.clear();
    menu.groups.clear();
    menu.matchCount = 0;
    menu.dirty = false;

    // Items arrive already ordered by group, so a group is a run of consecutive
    // items and one pass is enough.
    std::string current;
    int groupIndex = -1;

    for (int i = 0; i < static_cast<int>(menu.items.size()); ++i)
    {
        const GroupMenuItem& item = menu.items[static_cast<size_t>(i)];
        if (!MenuMatch(item.label, menu.filter))
            continue;

        ++menu.matchCount;

        if (item.group != current)
        {
            current = item.group;
            menu.groups.push_back(current);
            groupIndex = static_cast<int>(menu.groups.size()) - 1;
            menu.rows.push_back(GroupMenuRow{true, groupIndex, -1});
        }

        if (menu.collapsed.count(current) != 0)
            continue;
        menu.rows.push_back(GroupMenuRow{false, groupIndex, i});
    }

    // Keep the cursor on a real entry.
    if (menu.selected < 0 || menu.selected >= static_cast<int>(menu.rows.size()) ||
        menu.rows[static_cast<size_t>(menu.selected)].header)
    {
        menu.selected = 0;
        while (menu.selected < static_cast<int>(menu.rows.size()) &&
               menu.rows[static_cast<size_t>(menu.selected)].header)
            ++menu.selected;
    }
}

void GroupMenuMove(GroupMenu& menu, int delta)
{
    const int count = static_cast<int>(menu.rows.size());
    if (count == 0 || delta == 0)
        return;

    int target = menu.selected + delta;
    if (target < 0) target = 0;
    if (target >= count) target = count - 1;

    const int step = (delta > 0) ? 1 : -1;
    while (target >= 0 && target < count && menu.rows[static_cast<size_t>(target)].header)
        target += step;

    if (target >= 0 && target < count)
        menu.selected = target;
}

// First or last entry row, skipping the group band that may sit on the edge.
void GroupMenuSelectEdge(GroupMenu& menu, bool last)
{
    const int count = static_cast<int>(menu.rows.size());
    if (count == 0)
        return;

    int index = last ? count - 1 : 0;
    const int step = last ? -1 : 1;
    while (index >= 0 && index < count && menu.rows[static_cast<size_t>(index)].header)
        index += step;

    if (index >= 0 && index < count)
        menu.selected = index;
}

int GroupMenuSelectedId(const GroupMenu& menu)
{
    const int index = menu.selected;
    if (index < 0 || index >= static_cast<int>(menu.rows.size()))
        return -1;
    const GroupMenuRow& row = menu.rows[static_cast<size_t>(index)];
    if (row.header)
        return -1;
    return menu.items[static_cast<size_t>(row.item)].id;
}

// Keys the component owns. The caller checks its own toggle key before calling.
MenuAction GroupMenuKeyDown(GroupMenu& menu, WPARAM key)
{
    switch (key)
    {
        case VK_ESCAPE: return MenuAction::Close;
        case VK_RETURN: return MenuAction::Activate;
        case VK_UP:     GroupMenuMove(menu, -1); break;
        case VK_DOWN:   GroupMenuMove(menu, 1);  break;
        case VK_NEXT:   GroupMenuMove(menu, 10); break;
        case VK_PRIOR:  GroupMenuMove(menu, -10); break;
        case VK_HOME:   GroupMenuSelectEdge(menu, false); break;
        case VK_END:    GroupMenuSelectEdge(menu, true);  break;
        case VK_BACK:
            if (!menu.filter.empty())
            {
                menu.filter.pop_back();
                menu.dirty = true;
            }
            break;
        default:
            break;
    }
    return MenuAction::None;
}

void GroupMenuChar(GroupMenu& menu, wchar_t character)
{
    if (character >= 0x20 && character != 0x7F)
    {
        menu.filter.push_back(character);
        menu.dirty = true;
    }
}

GroupMenu* OpenMenu(AppState& app)
{
    if (app.palette.open) return &app.palette;
    if (app.goTo.open)    return &app.goTo;
    return nullptr;
}

// --- navigation, recents and bookmarks -------------------------------------- 
void NavigateBack(AppState& app)
{
    if (app.historyIndex <= 0)
        return;
    --app.historyIndex;
    LoadDirectory(app, app.history[static_cast<size_t>(app.historyIndex)], false);
}

void NavigateForward(AppState& app)
{
    if (app.historyIndex + 1 >= static_cast<int>(app.history.size()))
        return;
    ++app.historyIndex;
    LoadDirectory(app, app.history[static_cast<size_t>(app.historyIndex)], false);
}

bool IsBookmarked(const AppState& app)
{
    return std::find(app.bookmarks.begin(), app.bookmarks.end(), app.directory) != app.bookmarks.end();
}

void ToggleBookmark(AppState& app)
{
    if (app.directory.empty())
        return;
    const auto found = std::find(app.bookmarks.begin(), app.bookmarks.end(), app.directory);
    if (found != app.bookmarks.end())
        app.bookmarks.erase(found);
    else
        app.bookmarks.push_back(app.directory);
}

// --- building the two menus -------------------------------------------------
void BuildPaletteMenu(AppState& app)
{
    std::vector<GroupMenuItem> items;
    for (const CommandDef& command : kCommandTable)
    {
        std::string label = command.label;
        // The one command whose wording depends on state. The table stays data.
        if (command.id == CmdToggleBookmark && IsBookmarked(app))
            label = "Remove bookmark from this folder";
        items.push_back(GroupMenuItem{command.group, label, "", command.id});
    }

    // Locations are data, not code: one entry per known folder, so the palette
    // covers the sidebar without a command being declared for each.
    for (int i = 0; i < static_cast<int>(app.places.size()); ++i)
    {
        items.push_back(GroupMenuItem{"Places", app.places[static_cast<size_t>(i)].label, "",
                                      kPalettePlaceBase + i});
    }

    app.goTo.open = false;   // one menu at a time
    GroupMenuOpen(app.palette, "COMMAND PALETTE", "Type to filter commands...", std::move(items));
    app.palette.hint = "Enter runs   Esc closes   Ctrl+P toggles   Ctrl+Shift+P details";
}

void BuildGoToMenu(AppState& app)
{
    std::vector<GroupMenuItem> items;
    app.goToTargets.clear();

    const auto add = [&items, &app](const char* group, const std::wstring& path, const std::string& label)
    {
        const int id = static_cast<int>(app.goToTargets.size());
        items.push_back(GroupMenuItem{group, label, ToUtf8(path), id});
        app.goToTargets.push_back(path);
    };

    // The path itself, deepest last: the breadcrumb, reachable from the keyboard.
    if (!app.directory.empty())
    {
        std::vector<std::wstring> ancestors;
        std::wstring current = app.directory;
        for (;;)
        {
            ancestors.push_back(current);
            const std::wstring parent = ParentOf(current);
            if (parent == current)
                break;
            current = parent;
        }
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
            add("Path", *it, ToUtf8(LeafName(*it)));
    }

    for (const Place& place : app.places)
        add("Places", place.path, place.label);

    for (const std::wstring& path : app.bookmarks)
        add("Bookmarks", path, ToUtf8(LeafName(path)));

    for (const std::wstring& path : app.recents)
        add("Recents", path, ToUtf8(LeafName(path)));

    app.palette.open = false;
    GroupMenuOpen(app.goTo, "GO TO", "Type to filter locations...", std::move(items));
    app.goTo.detailed = true;   // a location without its path is not much use
    app.goTo.hint = "Enter goes   Esc closes   Ctrl+G toggles";
}

// --- operazioni su file (M7) ------------------------------------------------
int          CursorIndex(const AppState& app);
double       NowSeconds();

// Percorsi completi di cio' che e' selezionato **e visibile**, nell'ordine della
// lista.
//
// Filtrare puo' nascondere righe che restano nella selezione: la selezione e'
// per nome e sopravvive al cambio di filtro. Senza questo controllo il menu
// contestuale offriva "Elimina definitivamente" su un file che l'utente non
// vedeva piu', e un'azione su qualcosa che non si vede e' esattamente cio' che
// non deve poter succedere. La regola e' quella che Ctrl+A gia' segue: il filtro
// fa parte della domanda, quindi fa parte della risposta.
std::vector<std::wstring> SelectedPaths(const AppState& app)
{
    std::vector<std::wstring> paths;
    for (const Row& row : app.rows)
    {
        if (row.header)
            continue;
        const Entry& entry = app.entries[static_cast<size_t>(row.entry)];
        if (app.selection.count(entry.wideName) != 0)
            paths.push_back(JoinPath(app.directory, entry.wideName));
    }
    return paths;
}

// Un'operazione che fallisce in silenzio e' peggio di una che non parte: senza
// un messaggio, "Sposta" su un file in uso sembrerebbe non aver fatto nulla.
void ReportStatus(AppState& app, const std::wstring& message)
{
    app.statusMessage     = message;
    app.statusMessageTime = NowSeconds();
}

// Ricarica la cartella mantenendo la selezione sui nomi che esistono ancora.
//
// LoadDirectory svuota la selezione e sceglie la prima riga, ed e' giusto perche'
// cambia cartella. Dopo un incolla o un'eliminazione no: l'utente sta lavorando
// li' e perdere il posto a ogni operazione lo costringe a ricercarlo.
void ReloadKeeping(AppState& app, const std::unordered_set<std::wstring>& keep,
                   const std::wstring& preferCursor, float restoreScroll)
{
    LoadDirectory(app, app.directory, /*recordHistory=*/false);

    const auto present = [&](const std::wstring& name) {
        for (const Entry& entry : app.entries)
            if (entry.wideName == name)
                return true;
        return false;
    };

    app.selection.clear();
    for (const Entry& entry : app.entries)
        if (keep.count(entry.wideName) != 0)
            app.selection.insert(entry.wideName);

    if (!preferCursor.empty() && present(preferCursor))
    {
        app.cursor = preferCursor;
        if (app.selection.empty())
            app.selection.insert(preferCursor);
        app.selectionAnchor = preferCursor;
    }
    else
    {
        // Il primo nome sopravvissuto in ordine di lista, non un elemento
        // qualunque dell'insieme: la selezione e' un insieme non ordinato.
        for (const Entry& entry : app.entries)
        {
            if (app.selection.count(entry.wideName) != 0)
            {
                app.cursor          = entry.wideName;
                app.selectionAnchor = entry.wideName;
                break;
            }
        }
    }

    app.scroll = restoreScroll;
    app.viewDirty = true;

    // La cartella e' cambiata, quindi le statistiche che la riguardano non
    // valgono piu'. Serve invalidare QUESTA cartella, non quella che si sta
    // guardando: i numeri di una sottocartella si vedono dal genitore, quindi e'
    // li' che si noterebbe che sono vecchi.
    app.stats.Invalidate(app.directory);
}

// Nome predefinito di una cartella nuova, come quello di Esplora risorse.
constexpr wchar_t kNewFolderName[] = L"Nuova cartella";

void BeginRename(AppState& app)
{
    const int index = CursorIndex(app);
    if (index < 0)
        return;

    const Entry& entry = app.entries[static_cast<size_t>(index)];
    app.renameActive   = true;
    app.renamePath     = JoinPath(app.directory, entry.wideName);
    app.renameOriginal = entry.wideName;
    app.renameText     = entry.name;

    // La tastiera adesso scrive nel nome, non nel filtro: senza questo, rinominare
    // in "foto" filtrerebbe la lista a ogni lettera.
    app.filterActive = false;
}

void CancelRename(AppState& app)
{
    app.renameActive = false;
    app.renameText.clear();
    app.renamePath.clear();
    app.renameOriginal.clear();
}

void CommitRename(AppState& app)
{
    if (!app.renameActive)
        return;

    const std::wstring newName  = Widen(app.renameText);
    const std::wstring path     = app.renamePath;
    const std::wstring original = app.renameOriginal;
    const float        scroll   = app.scroll;
    CancelRename(app);

    if (newName.empty() || newName == original)
        return;

    std::wstring error;
    std::vector<std::wstring> created;
    const FileOps::Result result = FileOps::Rename(app.window, path, newName, &created, &error);
    if (result == FileOps::Result::Ok)
    {
        std::unordered_set<std::wstring> keep;
        const std::wstring landed = created.empty() ? newName : LeafName(created.front());
        keep.insert(landed);
        ReloadKeeping(app, keep, landed, scroll);
        ReportStatus(app, L"Rinominato in " + landed);
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, L"Rinomina non riuscita: " + error);
    }
}

void DoCopyToClipboard(AppState& app, bool cut)
{
    const std::vector<std::wstring> paths = SelectedPaths(app);
    if (paths.empty())
        return;

    if (!FileOps::SetClipboardFiles(app.window, paths, cut))
    {
        ReportStatus(app, L"Clipboard non disponibile.");
        return;
    }

    app.clipboardCut = cut;
    ReportStatus(app, (cut ? L"Tagliati " : L"Copiati ") +
                          std::to_wstring(paths.size()) + L" elementi");
}

void DoPaste(AppState& app)
{
    std::vector<std::wstring> paths;
    bool cut = false;
    if (!FileOps::ReadClipboardFiles(&paths, &cut))
    {
        ReportStatus(app, L"Niente da incollare.");
        return;
    }

    const float scroll = app.scroll;
    std::wstring error;
    std::vector<std::wstring> created;
    const FileOps::Result result = cut ? FileOps::Move(app.window, paths, app.directory, &created, &error)
                                       : FileOps::Copy(app.window, paths, app.directory, &created, &error);

    if (result == FileOps::Result::Ok)
    {
        // Un taglio si consuma con l'incolla: lasciarlo nella clipboard farebbe
        // spostare gli stessi file una seconda volta al prossimo Ctrl+V.
        if (cut)
            FileOps::ClearClipboard(app.window);

        // Si seleziona cio' che la shell ha DAVVERO creato. Con un conflitto di
        // nome il nome finale non e' quello di partenza, e selezionare quello di
        // partenza metteva il cursore sull'originale: un Canc subito dopo
        // cancellava la cosa sbagliata.
        std::unordered_set<std::wstring> keep;
        std::wstring landed;
        for (const std::wstring& path : created)
        {
            const std::wstring name = LeafName(path);
            keep.insert(name);
            if (landed.empty())
                landed = name;
        }
        if (keep.empty())
        {
            for (const std::wstring& path : paths)
                keep.insert(LeafName(path));
        }

        ReloadKeeping(app, keep, landed, scroll);
        ReportStatus(app, (cut ? L"Spostati " : L"Incollati ") +
                              std::to_wstring(paths.size()) + L" elementi");
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, L"Incolla non riuscito: " + error);
    }
}

void DoDelete(AppState& app, bool permanently)
{
    const std::vector<std::wstring> paths = SelectedPaths(app);
    if (paths.empty())
        return;

    const float scroll = app.scroll;
    std::wstring error;
    const FileOps::Result result = FileOps::Delete(app.window, paths, permanently, &error);

    if (result == FileOps::Result::Ok)
    {
        ReloadKeeping(app, /*keep=*/{}, /*preferCursor=*/{}, scroll);
        ReportStatus(app, (permanently ? L"Eliminati definitivamente " : L"Nel cestino: ") +
                              std::to_wstring(paths.size()) + L" elementi");
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, L"Eliminazione non riuscita: " + error);
    }
}

void DoNewFolder(AppState& app)
{
    const float scroll = app.scroll;
    std::vector<std::wstring> created;
    std::wstring error;
    const FileOps::Result result = FileOps::CreateFolder(
        app.window, app.directory, kNewFolderName, &created, &error);

    if (result == FileOps::Result::Ok)
    {
        const std::wstring name = created.empty() ? std::wstring(kNewFolderName)
                                                  : LeafName(created.front());
        std::unordered_set<std::wstring> keep;
        keep.insert(name);
        ReloadKeeping(app, keep, name, scroll);

        // Creare e rinominare sono un'azione sola: chi preme Ctrl+Shift+N vuole
        // dare un nome, non guardare un'altra "Nuova cartella".
        BeginRename(app);
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, L"Cartella non creata: " + error);
    }
}

// Il contenuto della voce nuova. La shell crea il file e sceglie il nome — e' il
// solo modo di avere il "(2)" dei conflitti senza indovinarlo — ma senza un
// modello crea un file di **zero byte**: e uno zip di zero byte non e' uno zip,
// un .docx di zero byte non si apre. Il contenuto lo dice il registro, e lo
// scriviamo qui.
//
// I modelli di Office, per esempio, sono registrati con un **guscio vuoto** nella
// loro cartella VFS (`WORD.DOCX` e' un file di zero byte su disco): il contenuto
// vero lo produce il provider di Office, che da fuori non si raggiunge. Il file
// nuovo nasce vuoto anche a noi, e allora lo si dice — un .docx che Word rifiuta
// di aprire, senza una parola, e' peggio di un .docx che non c'e'.
void FillNewItemContent(AppState& app, const ShellNew::Template& model,
                        const std::wstring& path)
{
    if (model.source == ShellNew::Template::Source::Empty)
        return;

    if (model.source == ShellNew::Template::Source::TemplateFile)
    {
        // Copia, invece di leggere e riscrivere: il modello puo' essere grande
        // (un .pub sono sessanta kilobyte), e questa e' una copia di file sola.
        if (!CopyFileW(model.templatePath.c_str(), path.c_str(), FALSE))
        {
            ReportStatus(app, L"Il modello di questo tipo non si e' potuto copiare: "
                              L"il file e' stato creato vuoto.");
            return;
        }
    }
    else
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            ReportStatus(app, L"Il file e' stato creato, ma il contenuto del modello no.");
            return;
        }

        DWORD written = 0;
        const bool wrote =
            WriteFile(file, model.data.data(), static_cast<DWORD>(model.data.size()), &written,
                      nullptr) != FALSE &&
            written == model.data.size();
        CloseHandle(file);
        if (!wrote)
        {
            ReportStatus(app, L"Il file e' stato creato, ma il contenuto del modello no.");
            return;
        }
    }

    // Il modello c'era ma era vuoto: il file e' nato vuoto, e chi lo apre lo deve
    // sapere adesso e non quando l'applicazione gli dira' che e' danneggiato.
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        info.nFileSizeHigh == 0 && info.nFileSizeLow == 0)
    {
        ReportStatus(app, L"Il modello di questo tipo e' vuoto: " + LeafName(path) +
                              L" e' stato creato vuoto.");
    }
}

// Crea una voce nuova dal modello |index| del registro, col meccanismo del
// sistema: il nome definitivo lo sceglie la shell ("Nuovo documento di testo.txt",
// e "(2)" se esiste gia'), il contenuto lo mette `FillNewItemContent`.
void CreateNewItem(AppState& app, size_t index)
{
    const std::vector<ShellNew::Template>& templates = ShellNew::Templates();
    if (index >= templates.size())
        return;

    const std::wstring wanted = templates[index].suggestedName;
    const float scroll = app.scroll;
    std::vector<std::wstring> created;
    std::wstring error;
    const FileOps::Result result =
        FileOps::NewItem(app.window, app.directory, wanted, &created, &error);

    if (result != FileOps::Result::Ok)
    {
        if (result == FileOps::Result::Failed)
            ReportStatus(app, L"File non creato: " + error);
        return;
    }

    const std::wstring path = created.empty() ? JoinPath(app.directory, wanted)
                                              : created.front();
    FillNewItemContent(app, templates[index], path);

    const std::wstring name = LeafName(path);
    std::unordered_set<std::wstring> keep;
    keep.insert(name);
    ReloadKeeping(app, keep, name, scroll);

    // Creare e rinominare sono un'azione sola, come per la cartella: chi crea
    // vuole dare un nome, non guardare un altro "Nuovo documento di testo".
    BeginRename(app);
}

// --- menu execution ---------------------------------------------------------
void RunCommand(AppState& app, int command);

void RunPaletteSelection(AppState& app)
{
    const int id = GroupMenuSelectedId(app.palette);
    if (id < 0)
        return;

    if (id >= kPalettePlaceBase)
    {
        const int place = id - kPalettePlaceBase;
        if (place < static_cast<int>(app.places.size()))
            LoadDirectory(app, app.places[static_cast<size_t>(place)].path);
        app.palette.open = false;
        return;
    }

    RunCommand(app, id);
    if (id != CmdToggleDetails)
        app.palette.open = false;
}

void RunGoToSelection(AppState& app)
{
    const int id = GroupMenuSelectedId(app.goTo);
    if (id < 0 || id >= static_cast<int>(app.goToTargets.size()))
        return;
    LoadDirectory(app, app.goToTargets[static_cast<size_t>(id)]);
    app.goTo.open = false;
}

void RunCommand(AppState& app, int command)
{
    const int cursorIndex = CursorIndex(app);

    switch (static_cast<CommandId>(command))
    {
        case CmdNewTab:  NewTab(app, app.directory.empty() ? L"C:\\" : app.directory); break;
        case CmdCloseTab: CloseTab(app, app.activeTab); break;
        case CmdNextTab:
            if (app.tabs.size() > 1) ActivateTab(app, (app.activeTab + 1) % static_cast<int>(app.tabs.size()));
            break;
        case CmdPrevTab:
            if (app.tabs.size() > 1)
            {
                const int count = static_cast<int>(app.tabs.size());
                ActivateTab(app, (app.activeTab + count - 1) % count);
            }
            break;

        case CmdGoUp:      LoadDirectory(app, ParentOf(app.directory)); break;
        case CmdBack:      NavigateBack(app); break;
        case CmdForward:   NavigateForward(app); break;
        case CmdGoTo:      BuildGoToMenu(app); break;
        case CmdRefresh:   app.stats.ClearAll(); LoadDirectory(app, app.directory); break;
        case CmdRecalcStats: app.stats.ClearAll(); break;

        case CmdCopy:      DoCopyToClipboard(app, /*cut=*/false); break;
        case CmdCut:       DoCopyToClipboard(app, /*cut=*/true); break;
        case CmdPaste:     DoPaste(app); break;
        case CmdRename:    BeginRename(app); break;
        case CmdNewFolder: DoNewFolder(app); break;
        case CmdDelete:          DoDelete(app, /*permanently=*/false); break;
        case CmdDeletePermanent: DoDelete(app, /*permanently=*/true); break;

        case CmdOpenExplorer:
            if (cursorIndex >= 0)
            {
                const std::wstring full = JoinPath(app.directory, app.entries[static_cast<size_t>(cursorIndex)].wideName);
                const std::wstring arguments = L"/select,\"" + full + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
            }
            else
            {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", app.directory.c_str(), nullptr, SW_SHOWNORMAL);
            }
            break;

        case CmdOpenConsole:
            ShellExecuteW(nullptr, L"open", L"cmd.exe", nullptr, app.directory.c_str(), SW_SHOWNORMAL);
            break;

        case CmdToggleSidebar: app.sidebarVisible = !app.sidebarVisible; break;
        case CmdZoomIn:    app.zoom = (std::min)(2.0f, app.zoom + 0.1f); break;
        case CmdZoomOut:   app.zoom = (std::max)(0.6f, app.zoom - 0.1f); break;
        case CmdZoomReset: app.zoom = app.dpiScale; break;
        case CmdToggleDetails: app.palette.detailed = !app.palette.detailed; break;
        case CmdViewDetails: app.viewMode = ViewMode::Details; app.viewDirty = true; break;
        case CmdViewList:    app.viewMode = ViewMode::List;    app.viewDirty = true; break;
        case CmdViewTiles:   app.viewMode = ViewMode::Tiles;   app.viewDirty = true; break;
        case CmdViewIcons:   app.viewMode = ViewMode::Icons;   app.viewDirty = true; break;
        case CmdToggleBookmark: ToggleBookmark(app); break;
        case CmdClearBookmarks: app.bookmarks.clear(); break;
        case CmdClearRecents:   app.recents.clear(); break;

        case CmdSortName:     app.sortColumn = SortColumn::Name;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortType:     app.sortColumn = SortColumn::Type;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortModified: app.sortColumn = SortColumn::Modified; SortEntries(app); app.viewDirty = true; break;
        case CmdSortFolders:  app.sortColumn = SortColumn::Folders;  SortEntries(app); app.viewDirty = true; break;
        case CmdSortFiles:    app.sortColumn = SortColumn::Files;    SortEntries(app); app.viewDirty = true; break;
        case CmdSortSize:     app.sortColumn = SortColumn::Size;     SortEntries(app); app.viewDirty = true; break;
        case CmdSortDirection: app.sortAscending = !app.sortAscending; SortEntries(app); app.viewDirty = true; break;

        case CmdGroupNone:   app.groupMode = GroupMode::None;   app.viewDirty = true; break;
        case CmdGroupName:   app.groupMode = GroupMode::Name;   app.viewDirty = true; break;
        case CmdGroupDate:   app.groupMode = GroupMode::Date;   app.viewDirty = true; break;
        case CmdGroupSize:   app.groupMode = GroupMode::Size;   app.viewDirty = true; break;
        case CmdGroupCount:  app.groupMode = GroupMode::Count;  app.viewDirty = true; break;
        case CmdGroupVolume: app.groupMode = GroupMode::Volume; app.viewDirty = true; break;

        case CmdFocusFilter: app.filterActive = true; break;
        case CmdClearFilter:
            app.filter.clear();
            app.filterActive = false;
            app.viewDirty = true;
            break;

        case CmdScrollTop:
            app.scroll = 0.0f;
            break;
        case CmdScrollBottom:
        {
            const Layout layout = ComputeLayout(app);
            const float content = static_cast<float>(app.rows.size()) * layout.rowHeight;
            app.scroll = (std::max)(0.0f, content - layout.list.h);
            break;
        }
    }
}

Rect GroupMenuPanel(const AppState& app, const Layout& layout)
{
    const float scale = layout.scale;
    const float width  = (std::min)(560.0f * scale, static_cast<float>(app.viewportWidth) - 60.0f * scale);
    const float height = (std::min)(420.0f * scale, static_cast<float>(app.viewportHeight) - 90.0f * scale);
    return Rect{(static_cast<float>(app.viewportWidth) - width) * 0.5f, 70.0f * scale, width, height};
}

// --- menu contestuale: geometria, disegno, interazione ----------------------
void ActivateSelection(AppState& app);

// --- i livelli del menu contestuale ----------------------------------------
//
// Un menu non e' una lista: e' un albero di liste. Lo chiamiamo *livello*: 0 e'
// il primo, 1 il sottomenu aperto, e cosi' via. `openPath` tiene gli indici delle
// voci aperte, uno per livello, e `levels` la scelta e la finestra di ogni
// livello. Il livello piu' profondo aperto e' quello su cui si agisce.
const std::vector<ContextMenuItem>* LevelItems(const ContextMenu& menu, int depth)
{
    const std::vector<ContextMenuItem>* items = &menu.items;
    for (int i = 0; i < depth; ++i)
    {
        if (i >= static_cast<int>(menu.openPath.size()))
            return nullptr;
        const int index = menu.openPath[static_cast<size_t>(i)];
        if (index < 0 || index >= static_cast<int>(items->size()))
            return nullptr;
        items = &(*items)[static_cast<size_t>(index)].children;
    }
    return items;
}

int LevelCount(const ContextMenu& menu, int depth)
{
    const std::vector<ContextMenuItem>* items = LevelItems(menu, depth);
    return items == nullptr ? 0 : static_cast<int>(items->size());
}

const ContextMenuItem* LevelItem(const ContextMenu& menu, int depth, int index)
{
    const std::vector<ContextMenuItem>* items = LevelItems(menu, depth);
    if (items == nullptr || index < 0 || index >= static_cast<int>(items->size()))
        return nullptr;
    return &(*items)[static_cast<size_t>(index)];
}

// Come LevelItem, ma per chi deve SCRIVERE nella voce: il contenuto di un
// sottomenu della shell si copia dentro `children` alla prima apertura.
ContextMenuItem* LevelItemMutable(ContextMenu& menu, int depth, int index)
{
    std::vector<ContextMenuItem>* items = &menu.items;
    for (int i = 0; i < depth; ++i)
    {
        if (i >= static_cast<int>(menu.openPath.size()))
            return nullptr;
        const int parent = menu.openPath[static_cast<size_t>(i)];
        if (parent < 0 || parent >= static_cast<int>(items->size()))
            return nullptr;
        items = &(*items)[static_cast<size_t>(parent)].children;
    }
    if (index < 0 || index >= static_cast<int>(items->size()))
        return nullptr;
    return &(*items)[static_cast<size_t>(index)];
}

// La voce ha qualcosa dentro? Vale anche per un sottomenu della shell **non
// ancora letto**: il suo contenuto non esiste prima di aprirlo, e chiederlo per
// tutti a ogni tasto destro costerebbe quanto interrogare la shell dieci volte.
bool HasSubmenu(const ContextMenuItem& item)
{
    return item.shellSubmenu != nullptr || !item.children.empty() || item.lazyNewFile ||
           item.lazySendTo;
}

int ActiveDepth(const ContextMenu& menu)
{
    return static_cast<int>(menu.openPath.size());
}

float ContextMenuWidth(const AppState& app, const Layout& layout, int depth)
{
    const float scale = layout.scale;
    float widest = 0.0f;
    float arrow  = 0.0f;
    const int total = LevelCount(app.contextMenu, depth);
    for (int i = 0; i < total; ++i)
    {
        const ContextMenuItem* item = LevelItem(app.contextMenu, depth, i);
        widest = (std::max)(widest, app.font.Measure(item->label, scale));
        if (HasSubmenu(*item))
            arrow = 18.0f * scale;   // lo spazio del segno di sottomenu
    }

    // La larghezza si DERIVA dal contenuto, con un minimo e un massimo: fissa
    // taglierebbe le voci lunghe come "Apri in Esplora risorse", e larga quanto
    // l'etichetta piu' lunga lascerebbe il sottotitolo a sbattere sul bordo.
    return (std::max)(220.0f * scale, (std::min)(340.0f * scale, widest + 56.0f * scale + arrow));
}

// Quante voci entrano nell'altezza utile, a partire da |firstItem|.
//
// Il taglio cade sempre fra due voci. Una voce mostrata a meta' si sceglie per
// sbaglio, e con i verbi della shell il menu supera la finestra molto prima di
// quanto si creda: venti voci su un monitor normale sono piu' della finestra.
int ContextMenuVisibleCount(const AppState& app, const Layout& layout, int depth, int firstItem)
{
    const float scale     = layout.scale;
    const float separator = 7.0f * scale;
    // 8 px di margine sotto, 6 px di riempimento sopra e sotto: quello che resta
    // e' lo spazio delle voci. Il sottotitolo ce l'ha solo il primo livello.
    const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
    const float room = static_cast<float>(app.viewportHeight) - 8.0f * scale -
                       12.0f * scale - footer;

    const int total = LevelCount(app.contextMenu, depth);
    float used  = 0.0f;
    int   count = 0;
    for (int i = firstItem; i < total; ++i)
    {
        const ContextMenuItem* item = LevelItem(app.contextMenu, depth, i);
        const float height = (item != nullptr && item->separator) ? separator : layout.rowHeight;
        // Almeno una voce si mostra sempre: un menu vuoto non si spiega.
        if (count > 0 && used + height > room)
            break;
        used += height;
        ++count;
    }
    return count;
}

// I due si chiamano a vicenda: il pannello di un sottomenu si ancora alla riga
// della voce che lo apre, e la riga si misura dentro il pannello.
Rect ContextMenuRowRect(const AppState& app, const Layout& layout, int depth, int index);

Rect ContextMenuPanel(const AppState& app, const Layout& layout, int depth)
{
    const float scale     = layout.scale;
    const float separator = 7.0f * scale;
    const float padding   = 6.0f * scale;

    const ContextMenu& menu = app.contextMenu;
    const int first   = menu.levels[static_cast<size_t>(depth)].firstItem;
    const int visible = ContextMenuVisibleCount(app, layout, depth, first);

    float content = 0.0f;
    for (int i = first; i < first + visible; ++i)
    {
        const ContextMenuItem* item = LevelItem(menu, depth, i);
        content += (item != nullptr && item->separator) ? separator : layout.rowHeight;
    }

    const float width = ContextMenuWidth(app, layout, depth);
    // 22 px in fondo per il sottotitolo, che c'e' solo nel primo livello.
    const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
    const float height = content + padding * 2.0f + footer;

    // L'ancora: il primo livello sta dove ha cliccato il puntatore, un sottomenu
    // accanto alla voce che lo apre.
    float anchorX = menu.x;
    float anchorY = menu.y;
    if (depth > 0)
    {
        const Rect parent = ContextMenuPanel(app, layout, depth - 1);
        const int parentIndex = menu.openPath[static_cast<size_t>(depth - 1)];
        const Rect row = ContextMenuRowRect(app, layout, depth - 1, parentIndex);
        anchorX = parent.right() - 4.0f * scale;
        anchorY = row.y - padding;

        // A destra non ci sta? Si apre a sinistra, come i menu di Windows: un
        // sottomenu che esce dalla finestra non si puo' leggere.
        if (anchorX + width > static_cast<float>(app.viewportWidth) - 4.0f * scale)
            anchorX = parent.x - width + 4.0f * scale;
    }

    // Il menu si ancora al puntatore ma non esce dalla finestra: aperto accanto
    // al bordo destro finirebbe per meta' fuori e irraggiungibile.
    const float maxX = static_cast<float>(app.viewportWidth) - width - 4.0f * scale;
    const float maxY = static_cast<float>(app.viewportHeight) - height - 4.0f * scale;
    const float x = (std::max)(4.0f * scale, (std::min)(anchorX, maxX));
    const float y = (std::max)(4.0f * scale, (std::min)(anchorY, maxY));
    return Rect{x, y, width, height};
}

// Ultimo indice disegnato, escluso. Fuori da questo intervallo una voce non
// esiste: non si disegna, non si sovrappassa e non si clicca. Cosi' non serve
// nessun taglio, perche' quello che non si disegna non sborda.
int ContextMenuLastVisible(const AppState& app, const Layout& layout, int depth)
{
    const int first   = app.contextMenu.levels[static_cast<size_t>(depth)].firstItem;
    const int visible = ContextMenuVisibleCount(app, layout, depth, first);
    return (std::min)(LevelCount(app.contextMenu, depth), first + visible);
}

Rect ContextMenuRowRect(const AppState& app, const Layout& layout, int depth, int index)
{
    const Rect panel       = ContextMenuPanel(app, layout, depth);
    const float scale      = layout.scale;
    const float separator  = 7.0f * scale;
    const float padding    = 6.0f * scale;

    const ContextMenu& menu = app.contextMenu;
    float top = panel.y + padding;
    for (int i = menu.levels[static_cast<size_t>(depth)].firstItem; i < index; ++i)
    {
        const ContextMenuItem* item = LevelItem(menu, depth, i);
        top += (item != nullptr && item->separator) ? separator : layout.rowHeight;
    }

    const ContextMenuItem* item = LevelItem(menu, depth, index);
    const float height = (item != nullptr && item->separator) ? separator : layout.rowHeight;
    return Rect{panel.x + 4.0f * scale, top, panel.w - 8.0f * scale, height};
}

// Tiene la finestra del menu dentro i limiti e ci tiene dentro la voce scelta:
// l'Invio esegue quello che si vede, quindi la selezione non deve poter uscire
// dal pannello.
void ContextMenuClampWindow(AppState& app, const Layout& layout, int depth,
                            bool followSelection = true)
{
    ContextMenu& menu = app.contextMenu;
    if (depth < 0 || depth >= static_cast<int>(menu.levels.size()))
        return;

    ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
    const int total = LevelCount(menu, depth);
    if (total == 0)
    {
        level.firstItem = 0;
        return;
    }

    // La rotella scorre la finestra SENZA trascinarsi dietro la scelta: la voce
    // sotto il puntatore e' scelta dal sovrappasso, e obbligarla a restare
    // visibile faceva resistire il menu alla rotella stessa. Per le frecce
    // l'opposto e' vero, e la voce scelta non deve poter uscire dal pannello.
    if (!followSelection)
    {
        const int capacity = (std::max)(1, ContextMenuVisibleCount(app, layout, depth, 0));
        level.firstItem =
            (std::max)(0, (std::min)(level.firstItem, (std::max)(0, total - capacity)));

        if (level.selected >= 0 &&
            (level.selected < level.firstItem ||
             level.selected >= ContextMenuLastVisible(app, layout, depth)))
            level.selected = -1;   // l'Invio esegue quello che si vede, o niente
        return;
    }

    const int capacity = (std::max)(1, ContextMenuVisibleCount(app, layout, depth, 0));
    level.firstItem = (std::max)(0, (std::min)(level.firstItem, (std::max)(0, total - capacity)));

    if (level.selected < 0)
        return;
    if (level.selected < level.firstItem)
    {
        level.firstItem = level.selected;
        return;
    }

    // Si avanza finche' la voce scelta non entra: quante ne entrano dipende da
    // dove si comincia, perche' un separatore non e' alto quanto una voce.
    while (level.firstItem < level.selected &&
           level.firstItem + ContextMenuVisibleCount(app, layout, depth, level.firstItem) <=
               level.selected)
        ++level.firstItem;
    level.firstItem = (std::min)(level.firstItem, level.selected);
}

// --- verbi della shell nel menu contestuale ---------------------------------
//
// La shell sa fare cose che non sono nostre: "Apri con", "Invia a", i verbi
// delle estensioni installate. Non si reimplementa niente: le si chiede
// l'oggetto di interfaccia della selezione, le si fa descrivere il proprio menu
// dentro un HMENU che non mostriamo mai, se ne leggono le voci e se ne esegue una
// quando l'utente la sceglie. Il binario di File Pilot fa esattamente questo —
// `Win_ContextMenuBuild`, `P_GetContextMenu`, `P_ContextMenuInvoke`, e la
// stringa `GCS_VERBW` per leggere i verbi.

// Gli id della shell vivono in una banda riservata: dentro quella banda un id e'
// un indice per IContextMenu, fuori e' un nostro CommandId.
constexpr int kShellCmdFirst = 0x6000;
constexpr int kShellCmdLast  = 0x6FFF;

// Le voci di "Nuovo file" hanno una banda loro, per lo stesso motivo: l'indice e'
// la posizione del modello nell'elenco letto dal registro.
constexpr int kNewItemCmdFirst = 0x7000;
constexpr int kNewItemCmdLast  = 0x70FF;

// Le destinazioni di "Invia a": stessa banda per lo stesso motivo, l'indice e' la
// posizione nell'elenco letto dalla cartella SendTo.
constexpr int kSendToCmdFirst = 0x7100;
constexpr int kSendToCmdLast  = 0x71FF;


// L'atlante dei glifi copre il solo ASCII stampabile, quindi un'etichetta della
// shell con un accento mostrerebbe un buco: si ripiega sull'ASCII, come fa gia'
// "Proprieta'" scritto a mano.
char FoldAccent(wchar_t c)
{
    switch (c)
    {
        case L'\u00e0': case L'\u00e1': case L'\u00e2': case L'\u00e3': case L'\u00e4': case L'\u00e5': return 'a';
        case L'\u00c0': case L'\u00c1': case L'\u00c2': case L'\u00c3': case L'\u00c4': case L'\u00c5': return 'A';
        case L'\u00e7': return 'c';
        case L'\u00c7': return 'C';
        case L'\u00e8': case L'\u00e9': case L'\u00ea': case L'\u00eb': return 'e';
        case L'\u00c8': case L'\u00c9': case L'\u00ca': case L'\u00cb': return 'E';
        case L'\u00ec': case L'\u00ed': case L'\u00ee': case L'\u00ef': return 'i';
        case L'\u00cc': case L'\u00cd': case L'\u00ce': case L'\u00cf': return 'I';
        case L'\u00f1': return 'n';
        case L'\u00d1': return 'N';
        case L'\u00f2': case L'\u00f3': case L'\u00f4': case L'\u00f5': case L'\u00f6': return 'o';
        case L'\u00d2': case L'\u00d3': case L'\u00d4': case L'\u00d5': case L'\u00d6': return 'O';
        case L'\u00f9': case L'\u00fa': case L'\u00fb': case L'\u00fc': return 'u';
        case L'\u00d9': case L'\u00da': case L'\u00db': case L'\u00dc': return 'U';
        case L'\u00fd': case L'\u00ff': return 'y';
        default: break;
    }
    return '\0';
}

std::string ShellLabelToAscii(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];

        // Nelle etichette della shell la e commerciale segnala il tasto di scelta
        // rapida e si raddoppia per scriverla davvero: "Aggiungi ad &&Accesso"
        // significa "Aggiungi ad &Accesso". Il nostro menu non ha tasti di scelta
        // rapida, quindi l'occhiello si toglie invece di finire a schermo.
        if (c == L'&')
        {
            if (i + 1 < text.size() && text[i + 1] == L'&')
            {
                out.push_back('&');
                ++i;
            }
            continue;
        }

        if (c >= 32 && c < 127)
        {
            out.push_back(static_cast<char>(c));
            continue;
        }
        const char folded = FoldAccent(c);
        if (folded != '\0')
            out.push_back(folded);
    }
    return out;
}

bool SameWord(const wchar_t* a, const wchar_t* b)
{
    while (*a != L'\0' && *b != L'\0')
    {
        if (towlower(*a) != towlower(*b))
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

// I verbi che il nostro menu gia' offre: ripeterli accanto ai nostri comandi
// darebbe due "Taglia" e due "Proprieta'" nella stessa lista. Il confronto e' per
// verbo canonico e non per etichetta, perche' l'etichetta e' tradotta e il verbo
// no. "Apri con" (openas) NON e' fra questi: e' proprio uno dei motivi per cui
// la shell viene interrogata.
//
// Sul menu della CARTELLA l'elenco e' piu' corto, e non per svogliatezza: la'
// il nostro menu non ha ne' "Apri" ne' "Proprieta'", quindi quelli della shell
// sono un guadagno e non un doppione. Resta fuori solo cio' che c'e' in tutti e
// due, cioe' l'incolla.
bool IsOwnShellVerb(const wchar_t* verb, bool folderBackground)
{
    if (folderBackground)
        return SameWord(verb, L"paste");

    static const wchar_t* const kOwn[] = {
        L"open", L"cut", L"copy", L"paste", L"delete", L"rename", L"properties",
    };
    for (const wchar_t* own : kOwn)
    {
        if (SameWord(verb, own))
            return true;
    }
    return false;
}

// Rilascia quello che la shell ci ha prestato. Va chiamato a ogni chiusura del
// menu: un IContextMenu tenuto in vita e' una DLL di estensione che non si
// scarica, e un HMENU mai distrutto e' una risorsa utente che cola.
void ShellMenuRelease(ContextMenu& menu)
{
    if (menu.shell != nullptr)
    {
        menu.shell->Release();
        menu.shell = nullptr;
    }
    if (menu.shellMenu != nullptr)
    {
        DestroyMenu(menu.shellMenu);
        menu.shellMenu = nullptr;
    }
}

// I sottomenu che la shell popola **da sé** — "Nuovo", "Invia a", "Dare accesso
// a" — e che da qui non si possono eseguire: la shell consegna i comandi di
// quei sottomenu alla finestra del menu, e noi una finestra di menu non
// l'abbiamo. Offrirli sarebbe offrire voci che non fanno niente.
//
// Il confronto è per verbo canonico. Una voce senza verbo viene trattata come
// della shell: non si può sapere di chi è, e la prudenza costa una voce, non un
// comando che finge di riuscire.
bool IsShellOwnSubmenuVerb(const wchar_t* verb)
{
    if (verb[0] == L'\0')
        return true;

    static const wchar_t* const kShellOwn[] = {
        L"New",      L"Windows.Share", L"Windows.SendTo", L"SendTo",
        L"View",     L"Arrange",       L"Arrangement",    L"Sort",
        L"Group",    L"Layout",        L"Background",     L"Customize",
    };
    for (const wchar_t* own : kShellOwn)
    {
        if (SameWord(verb, own))
            return true;
    }
    return false;
}

// Legge le voci di un menu della shell — separatori compresi — e le traduce nel
// nostro elenco. Vale per il menu della selezione, per quello del fondo della
// cartella, e un livello alla volta per un sottomenu.
void ReadShellMenu(IContextMenu* shell, HMENU handle, bool folderBackground,
                   std::vector<ContextMenuItem>* out, int* outSkipped)
{
        const int count = GetMenuItemCount(handle);
        for (int i = 0; i < count; ++i)
        {
            MENUITEMINFOW basic{};
            basic.cbSize = sizeof(basic);
            basic.fMask  = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_SUBMENU;
            if (!GetMenuItemInfoW(handle, static_cast<UINT>(i), TRUE, &basic))
                continue;

            if ((basic.fType & MFT_SEPARATOR) != 0)
            {
                out->push_back({"", 0, /*separator=*/true});
                continue;
            }

            // Fuori banda la voce non si potrebbe eseguire: meglio non offrirla
            // che offrirla e non farla partire.
            if (basic.wID < kShellCmdFirst || basic.wID >= kShellCmdLast)
                continue;

            const UINT offset = static_cast<UINT>(basic.wID) - kShellCmdFirst;

            wchar_t verb[128]{};
            const bool haveVerb = SUCCEEDED(shell->GetCommandString(
                offset, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), 128));

            if (haveVerb && IsOwnShellVerb(verb, folderBackground))
                continue;

            wchar_t text[256]{};
            MENUITEMINFOW detailed{};
            detailed.cbSize     = sizeof(detailed);
            detailed.fMask      = MIIM_STRING;
            detailed.dwTypeData = text;
            detailed.cch        = 255;
            GetMenuItemInfoW(handle, static_cast<UINT>(i), TRUE, &detailed);

            ContextMenuItem item;
            item.label    = ShellLabelToAscii(text);
            item.command  = static_cast<int>(basic.wID);
            item.disabled = (basic.fState & (MFS_DISABLED | MFS_GRAYED)) != 0;

            // Un sottomenu si legge **quando si apre**, non adesso: il suo
            // contenuto non esiste prima di WM_INITMENUPOPUP, e chiederlo per
            // tutti a ogni tasto destro costerebbe quanto interrogare la shell
            // dieci volte, per voci che magari nessuno apre. `shellPosition` e'
            // dove la voce sta nel menu padre, e la shell vuole saperlo per
            // decidere cosa popolare.
            if (basic.hSubMenu != nullptr)
            {
                // Che sia di un'estensione o della shell decide se la voce si
                // potra' eseguire: vedi IsShellOwnSubmenuVerb.
                if (!haveVerb || IsShellOwnSubmenuVerb(verb))
                {
                    DiagLog("  saltata (sottomenu della shell, non eseguibile): '" +
                            item.label + "'");
                    ++(*outSkipped);
                    continue;
                }
                item.command       = 0;
                item.shellSubmenu  = basic.hSubMenu;
                item.shellPosition = i;
            }

            // Le voci disegnate da chi le ha messe (ownerdraw) non portano il
            // testo nel menu: il ripiego e' il testo di aiuto, che e' meglio del
            // nome tecnico del verbo.
            if (item.label.empty())
            {
                wchar_t help[256]{};
                if (SUCCEEDED(shell->GetCommandString(offset, GCS_HELPTEXTW, nullptr,
                                                      reinterpret_cast<LPSTR>(help), 256)))
                    item.label = ShellLabelToAscii(help);
            }
            if (item.label.empty())
            {
                DiagLog("  saltata (senza etichetta): verbo '" + ShellLabelToAscii(verb) + "'");
                ++(*outSkipped);
                continue;
            }

            DiagLog("  voce: '" + item.label + "'" + (haveVerb ? " verbo '" : " verbo '?") +
                    ShellLabelToAscii(verb) + "'" + (item.disabled ? " (spenta)" : ""));
            out->push_back(std::move(item));
        }

        // La shell apre e chiude con dei separatori suoi, che accanto ai nostri
        // diventerebbero doppi; e togliendo le voci che offriamo gia' possono
        // restarne due di fila.
        std::vector<ContextMenuItem> cleaned;
        for (ContextMenuItem& item : *out)
        {
            if (item.separator && (cleaned.empty() || cleaned.back().separator))
                continue;
            cleaned.push_back(std::move(item));
        }
        while (!cleaned.empty() && cleaned.back().separator)
            cleaned.pop_back();
        *out = std::move(cleaned);
}

// Chiede alla shell il menu della selezione e ne copia le voci utilizzabili.
// L'IContextMenu esce da qui perche' serve ancora al momento dell'esecuzione: la
// shell non accetta un verbo senza l'oggetto da cui e' uscito.
bool BuildShellItems(HWND owner, const std::vector<std::wstring>& paths, bool folderBackground,
                     IContextMenu** outMenu, HMENU* outHandle,
                     std::vector<ContextMenuItem>* out, int* outSkipped)
{
    *outMenu    = nullptr;
    *outHandle  = nullptr;
    *outSkipped = 0;
    if (paths.empty())
        return false;

    std::vector<PIDLIST_ABSOLUTE> pidls;
    IShellFolder* parent = nullptr;
    IContextMenu* shell   = nullptr;

    // Il menu del FONDO della cartella non e' il menu della cartella come
    // elemento, e la shell li tiene distinti. La differenza si vede: il secondo
    // offre Taglia, Copia ed Elimina **sulla cartella stessa**, che a destra di
    // un fondo non se li aspetta nessuno. Il fondo si chiede alla vista, con
    // `CreateViewObject`; la cartella come elemento con `GetUIObjectOf`. Chiedere
    // quello sbagliato non da' un errore: da' un menu plausibile e pericoloso.
    if (folderBackground)
    {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(paths.front().c_str(), nullptr, &pidl, 0, nullptr)))
        {
            IShellFolder* folder = nullptr;
            if (SUCCEEDED(SHBindToObject(nullptr, pidl, nullptr, IID_IShellFolder,
                                         reinterpret_cast<void**>(&folder))))
            {
                folder->CreateViewObject(owner, IID_IContextMenu,
                                         reinterpret_cast<void**>(&shell));
                folder->Release();
            }
            CoTaskMemFree(pidl);
        }
    }
    else
    {
        for (const std::wstring& path : paths)
        {
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)))
                break;

            // La lista mostra una cartella sola, quindi il padre di tutti i percorsi
            // selezionati e' lo stesso: quello del primo.
            if (parent == nullptr &&
                FAILED(SHBindToParent(pidl, IID_IShellFolder, reinterpret_cast<void**>(&parent), nullptr)))
            {
                CoTaskMemFree(pidl);
                break;
            }
            pidls.push_back(pidl);
        }

        if (parent != nullptr && !pidls.empty())
        {
            // GetUIObjectOf vuole i figli relativi alla cartella padre, e l'ultimo id
            // di un pidl assoluto e' esattamente quello.
            std::vector<PCUITEMID_CHILD> children;
            children.reserve(pidls.size());
            for (PIDLIST_ABSOLUTE pidl : pidls)
                children.push_back(ILFindLastID(pidl));

            parent->GetUIObjectOf(owner, static_cast<UINT>(children.size()),
                                  children.data(), IID_IContextMenu, nullptr,
                                  reinterpret_cast<void**>(&shell));
        }
    }

    if (shell != nullptr)
    {
        HMENU handle = CreatePopupMenu();
        if (handle != nullptr &&
            SUCCEEDED(shell->QueryContextMenu(handle, 0, kShellCmdFirst, kShellCmdLast, CMF_NORMAL)))
        {
            ReadShellMenu(shell, handle, folderBackground, out, outSkipped);

            if (!out->empty())
            {
                *outMenu   = shell;
                *outHandle = handle;
                shell  = nullptr;
                handle = nullptr;
            }
        }

        if (handle != nullptr)
            DestroyMenu(handle);
        if (shell != nullptr)
            shell->Release();
    }
    if (parent != nullptr)
        parent->Release();

    for (PIDLIST_ABSOLUTE pidl : pidls)
        CoTaskMemFree(pidl);

    return *outMenu != nullptr;
}

// Aggiunge al menu le voci della shell, se ce ne sono. Il blocco resta separato
// dai nostri comandi: sono di un altro, e mescolarli farebbe sembrare nostro
// quello che non lo e'.
void AppendShellItems(AppState& app, ContextMenu& menu)
{
    std::vector<std::wstring> paths = SelectedPaths(app);

    // Nel vuoto il contesto della shell e' **la cartella aperta**: il menu che si
    // apre col tasto destro sul fondo parla della cartella, non di un file. Sono
    // gli stessi verbi su un altro oggetto, ed e' da li' che arrivano "Apri in
    // Terminale", "Condividi" e la finestra delle proprieta' della cartella.
    const bool folderBackground = paths.empty();
    if (folderBackground)
    {
        if (app.directory.empty())
            return;
        paths.push_back(app.directory);
    }

    const double started = NowSeconds();
    std::vector<ContextMenuItem> items;
    IContextMenu* shell    = nullptr;
    HMENU handle           = nullptr;
    int skipped            = 0;
    BuildShellItems(app.window, paths, folderBackground, &shell, &handle, &items, &skipped);
    const double elapsed = (NowSeconds() - started) * 1000.0;

    DiagLog("--- " + DiagClock() + (folderBackground ? " sfondo," : " selezione,") + " " +
            std::to_string(paths.size()) + " percorsi, " + std::to_string(items.size()) +
            " voci, " + std::to_string(skipped) + " saltate, " + std::to_string(elapsed) + " ms");

    ShellMenuRelease(menu);
    if (shell == nullptr)
        return;

    menu.shell            = shell;
    menu.shellMenu        = handle;
    menu.shellIsBackground = folderBackground;
    for (ContextMenuItem& item : items)
        menu.items.push_back(std::move(item));
    // Il separatore va DOPO il blocco: chi lo innesta ci mette il proprio davanti,
    // e in testa a un menu che comincia con la shell sarebbe stato un separatore
    // orfano.
    menu.items.push_back({"", 0, /*separator=*/true});
}

void ContextMenuClose(AppState& app)
{
    ContextMenu& menu = app.contextMenu;
    menu.open = false;
    // I livelli non si svuotano: restano uno solo, vuoto. Un array vuoto
    // significherebbe che il livello 0 non esiste, e ogni funzione di geometria
    // lo indicizza.
    menu.openPath.clear();
    menu.levels.assign(1, ContextMenuLevel{});
    menu.items.clear();
    menu.shellIsBackground = false;
    ShellMenuRelease(menu);
}

void BuildContextMenu(AppState& app, float x, float y)
{
    ContextMenu& menu = app.contextMenu;
    menu.items.clear();
    menu.x = x;
    menu.y = y;
    menu.openPath.clear();
    menu.levels.assign(1, ContextMenuLevel{});
    menu.shellIsBackground = false;

    const size_t count = app.selection.size();
    menu.subtitle = count == 0 ? std::string("niente selezionato")
                               : std::to_string(count) +
                                     (count == 1 ? " elemento" : " elementi");

    // Il menu si adatta al contesto: nel vuoto restano solo le voci che valgono
    // per la cartella, perche' offrire "Rinomina" senza niente da rinominare e'
    // promettere qualcosa che non si puo' mantenere.
    if (count > 0)
    {
        menu.items.push_back({"Apri", kContextOpen});
        menu.items.push_back({"Apri in Esplora risorse", CmdOpenExplorer});
        menu.items.push_back({"", 0, /*separator=*/true});
        menu.items.push_back({"Taglia", CmdCut});
        menu.items.push_back({"Copia", CmdCopy});

        // "Invia a" qui e non in fondo: e' dove sta in Esplora risorse, subito
        // dopo il blocco Taglia/Copia, e li' la mano lo cerca.
        if (!ShellSendTo::Targets().empty())
        {
            ContextMenuItem sendTo;
            sendTo.label      = "Invia a";
            sendTo.lazySendTo = true;
            menu.items.push_back(std::move(sendTo));
        }
        menu.items.push_back({"", 0, true});
        menu.items.push_back({"Rinomina", CmdRename});
        menu.items.push_back({"Elimina", CmdDelete});
        menu.items.push_back({"Elimina definitivamente", CmdDeletePermanent});
        menu.items.push_back({"", 0, true});
        menu.items.push_back({"Proprieta'", kContextProperties});

        // I verbi della shell stanno dopo i nostri: i nostri sono quelli che si
        // usano davvero, e non devono scendere sotto una lista lunga quanto le
        // estensioni installate su questa macchina.
        menu.items.push_back({"", 0, true});
        AppendShellItems(app, menu);
    }
    else
    {
        // Col tasto destro sul fondo, invece, la shell apre il menu: quello e' il
        // contesto di cio' su cui si e' cliccato.
        AppendShellItems(app, menu);
    }

    menu.items.push_back({count > 0 ? "Incolla qui" : "Incolla", CmdPaste});
    menu.items.push_back({"Nuova cartella", CmdNewFolder});

    // "Nuovo file": i tipi che Windows sa creare, dal registro. Se il registro non
    // ne ha nessuno la voce non si mette, perche' un sottomenu vuoto non si spiega.
    if (!ShellNew::Templates().empty())
    {
        ContextMenuItem newFile;
        newFile.label       = "Nuovo file";
        newFile.lazyNewFile = true;
        menu.items.push_back(std::move(newFile));
    }

    menu.items.push_back({"", 0, true});
    menu.items.push_back({"Aggiorna", CmdRefresh});
    menu.items.push_back({"Apri un terminale qui", CmdOpenConsole});

    menu.open = true;
}

void ContextMenuMove(AppState& app, const Layout& layout, int delta)
{
    ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
    const int count = LevelCount(menu, depth);
    if (count == 0 || delta == 0)
        return;

    // Niente e' selezionato all'apertura, e la prima freccia deve scegliere la
    // PRIMA voce. Partendo da 0 l'incremento del ciclo salterebbe proprio quella
    // e la selezione comincerebbe dalla seconda: l'errore era invisibile con la
    // prima voce separata da un separatore e non con le altre.
    //
    // Le frecce saltano i separatori e le voci spente: fermarcisi sopra non vuol
    // dire niente e l'Invio non avrebbe nulla da eseguire.
    int index = level.selected < 0 ? (delta > 0 ? -1 : count) : level.selected;
    for (int step = 0; step < count; ++step)
    {
        index = (index + delta + count * 2) % count;
        const ContextMenuItem* item = LevelItem(menu, depth, index);
        if (item != nullptr && !item->separator && !item->disabled)
            break;
    }
    level.selected = index;

    // Se la voce scelta sta fuori dal pannello, il menu scivola di quel tanto.
    ContextMenuClampWindow(app, layout, depth);
}

// Porta la scelta alla prima o all'ultima voce eseguibile.
void ContextMenuJump(AppState& app, const Layout& layout, bool toFirst)
{
    ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
    const int count = LevelCount(menu, depth);
    if (count == 0)
        return;

    const int step = toFirst ? 1 : -1;
    int index = toFirst ? -1 : count;
    for (int i = 0; i < count; ++i)
    {
        index = (index + step + count * 2) % count;
        const ContextMenuItem* item = LevelItem(menu, depth, index);
        if (item != nullptr && !item->separator && !item->disabled)
            break;
    }
    level.selected = index;
    ContextMenuClampWindow(app, layout, depth);
}

// Apre la finestra delle proprieta' di Windows sulla selezione: la stessa che
// apre Esplora risorse, quindi schede, permessi, versioni e anteprime sono quelli
// del sistema e non una nostra imitazione. Restituisce quante ne ha aperte.
int ShowProperties(HWND owner, const std::vector<std::wstring>& paths)
{
    int launched = 0;
    for (const std::wstring& path : paths)
    {
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask  = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
        info.hwnd   = owner;
        info.lpVerb = L"properties";
        info.lpFile = path.c_str();
        info.nShow  = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info))
            ++launched;
    }
    return launched;
}

// --- il menu "Nuovo file", che facciamo noi ---------------------------------
//
// La shell il proprio "Nuovo" lo popola in un sottomenu, e i comandi di quel
// sottomenu non si possono eseguire da qui (vedi `IsShellOwnSubmenuVerb`), quindi
// il menu "Nuovo file" se lo costruisce Bolt dalle stesse fonti che usa la shell:
// le chiavi `ShellNew` del registro. Vedi `docs/04-roadmap.md`, sezione M7.

// Riempie il sottomenu coi modelli del registro: un CommandId della banda per
// ciascuno, che e' la sua posizione nell'elenco.
void FillNewFileMenu(ContextMenuItem& item)
{
    const double started = NowSeconds();
    const std::vector<ShellNew::Template>& templates = ShellNew::Templates();
    const double elapsed = (NowSeconds() - started) * 1000.0;

    for (size_t i = 0; i < templates.size(); ++i)
    {
        ContextMenuItem entry;
        entry.label   = ShellLabelToAscii(templates[i].label);
        entry.command = kNewItemCmdFirst + static_cast<int>(i);
        item.children.push_back(std::move(entry));
        DiagLog("    nuovo file: '" + item.children.back().label + "'");
    }

    DiagLog("  modelli di 'Nuovo file': " + std::to_string(templates.size()) + " voci, " +
            std::to_string(elapsed) + " ms");
}

// --- il menu "Invia a", che facciamo noi -------------------------------------
//
// Stessa storia del "Nuovo": la shell il proprio "Invia a" lo popola in un
// sottomenu, e quei comandi non si possono eseguire da fuori. La differenza e' che
// qui la fonte non e' nascosta: "Invia a" e' **una cartella vera**
// (`%APPDATA%\Microsoft\Windows\SendTo`), quindi si legge e si esegue a mano.
// Vedi `src/shell_sendto.h`.

// Riempie il sottomenu con le destinazioni: un CommandId della banda per ciascuna,
// che e' la sua posizione nell'elenco letto adesso.
void FillSendToMenu(ContextMenuItem& item)
{
    const double started = NowSeconds();
    const std::vector<ShellSendTo::Target> targets = ShellSendTo::Targets();
    const double elapsed = (NowSeconds() - started) * 1000.0;

    for (size_t i = 0; i < targets.size(); ++i)
    {
        ContextMenuItem entry;
        entry.label   = ShellLabelToAscii(targets[i].label);
        entry.command = kSendToCmdFirst + static_cast<int>(i);
        item.children.push_back(std::move(entry));
    }

    // Deliberatamente **senza le etichette**: l'elenco delle destinazioni dice
    // quali programmi ha installato chi usa questo computer, e nel registro
    // diagnostico non ci finisce. Vedi `AGENTS.md` §0.
    DiagLog("  destinazioni di 'Invia a': " + std::to_string(targets.size()) + " voci, " +
            std::to_string(elapsed) + " ms");
}

// Lascia cadere la selezione su una voce della cartella SendTo.
//
// Non si avvia con `ShellExecuteEx`, e la ragione e' nel registro: la voce
// "cartella compressa" della cartella SendTo **non e' un programma**, e' un file
// `.ZFSendToTarget` che contiene un CLSID, e il suo handler e' registrato come
// `DropHandler` — il registro lo dice con le parole "Drag Files onto this icon to
// compress them". Avviarla non le da' niente da comprimere: `ShellExecuteEx`
// restituisce TRUE e non succede niente, che e' la peggiore delle risposte.
//
// Quindi si fa quello che farebbe il mouse: si prende il bersaglio di caduta della
// voce e gli si lascia cadere sopra la selezione. Vale per tutte le voci della
// cartella, compresi i collegamenti ai programmi, che si aprono con i file
// ricevuti — e' quello che fa la shell quando ci si trascina sopra qualcosa.
bool DropOnSendToTarget(HWND owner, const std::wstring& path, IDataObject* data)
{
    if (data == nullptr)
        return false;

    PIDLIST_ABSOLUTE item = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr)) || item == nullptr)
        return false;

    PIDLIST_ABSOLUTE parent = ILClone(item);
    bool dropped = false;

    IShellFolder* folder = nullptr;
    if (parent != nullptr)
    {
        ILRemoveLastID(parent);
        if (SUCCEEDED(SHBindToObject(nullptr, parent, nullptr, IID_IShellFolder,
                                    reinterpret_cast<void**>(&folder))) &&
            folder != nullptr)
        {
            PCUITEMID_CHILD child = ILFindLastID(item);
            IDropTarget* target = nullptr;
            if (SUCCEEDED(folder->GetUIObjectOf(owner, 1, &child, IID_IDropTarget, nullptr,
                                                reinterpret_cast<void**>(&target))) &&
                target != nullptr)
            {
                // `DROPEFFECT_COPY`: "invia a" manda una copia, non sposta — e'
                // quello che fa la shell, ed e' anche quello che serve alla
                // cartella compressa, che legge i file senza portarli via.
                POINTL point{};
                DWORD  effect = DROPEFFECT_COPY;
                if (SUCCEEDED(target->DragEnter(data, MK_LBUTTON, point, &effect)))
                {
                    effect = DROPEFFECT_COPY;
                    dropped = SUCCEEDED(target->Drop(data, MK_LBUTTON, point, &effect));
                    DiagLog("  caduta su voce: effetto restituito " + std::to_string(effect));
                }
                target->Release();
            }
            folder->Release();
        }
        CoTaskMemFree(parent);
    }
    CoTaskMemFree(item);
    return dropped;
}

// Manda la selezione a una destinazione di "Invia a".
void SendToTargets(AppState& app, size_t index, const std::vector<std::wstring>& paths)
{
    const std::vector<ShellSendTo::Target> targets = ShellSendTo::Targets();
    if (index >= targets.size())
        return;
    if (paths.empty())
        return;   // mandare "niente" da qualche parte non e' un'azione

    IDataObject* data = DragDrop::DataObjectFor(paths);
    const bool dropped = DropOnSendToTarget(app.window, targets[index].path, data);
    if (data != nullptr)
        data->Release();

    if (!dropped)
    {
        // Nel registro va l'indice, **non l'etichetta**: le destinazioni dicono
        // quali programmi ha installato chi usa il computer. Vedi `AGENTS.md` §0.
        DiagLog("  invio a voce " + std::to_string(index) + ": non riuscito");
        ReportStatus(app, L"\"" + targets[index].label + L"\" non ha accettato i file.");
        return;
    }

    DiagLog("  invio a voce " + std::to_string(index) + ": riuscito");

    // "Avviato", non "fatto": la destinazione puo' copiare, comprimere o aprire
    // una finestra sua, e il suo esito non e' nostro da dichiarare.
    ReportStatus(app, L"Avviato: " + targets[index].label);
}

// Chiede alla shell il contenuto di un sottomenu e lo copia nel nostro albero.
//
// Un sottomenu della shell **non ha contenuto** finche' non arriva
// WM_INITMENUPOPUP: leggerlo prima vuol dire leggere un menu vuoto e concludere
// che la voce non ha niente dentro. E il messaggio va mandato alla voce giusta,
// perche' la shell decide COSA popolare in base alla posizione della voce nel
// menu padre — "Invia a" di un file non e' quello di una cartella.
void FillShellSubmenu(AppState& app, ContextMenuItem& item)
{
    if (item.shellSubmenu == nullptr || !item.children.empty())
        return;
    if (app.contextMenu.shell == nullptr)
        return;

    IContextMenu2* menu2 = nullptr;
    if (SUCCEEDED(app.contextMenu.shell->QueryInterface(IID_IContextMenu2,
                                                       reinterpret_cast<void**>(&menu2))))
    {
        menu2->HandleMenuMsg(WM_INITMENUPOPUP,
                             reinterpret_cast<WPARAM>(item.shellSubmenu),
                             static_cast<LPARAM>(item.shellPosition));
        menu2->Release();
    }

    int skipped = 0;
    ReadShellMenu(app.contextMenu.shell, item.shellSubmenu,
                  app.contextMenu.shellIsBackground, &item.children, &skipped);
    DiagLog("    sottomenu di '" + item.label + "': " + std::to_string(item.children.size()) +
            " voci, " + std::to_string(skipped) + " saltate");
}

// Tiene i livelli da 0 a |depth| compreso e butta via quelli sotto.
void CloseSubmenusBelow(ContextMenu& menu, int depth)
{
    const int wanted = depth + 1;
    if (static_cast<int>(menu.levels.size()) <= wanted)
        return;
    menu.openPath.resize(static_cast<size_t>(depth));
    menu.levels.resize(static_cast<size_t>(wanted));
}

// Apre il sottomenu della voce |index| del livello |depth|. Restituisce falso se
// quella voce un sottomenu non ce l'ha, o se e' rimasto vuoto.
bool OpenSubmenu(AppState& app, const Layout& layout, int depth, int index)
{
    ContextMenu& menu = app.contextMenu;
    ContextMenuItem* item = LevelItemMutable(menu, depth, index);
    if (item == nullptr || item->disabled || !HasSubmenu(*item))
        return false;

    // Gia' aperto esattamente cosi': non si rifa' niente. I movimenti del mouse
    // arrivano a decine al secondo, e riaprire il sottomenu a ogni movimento
    // azzererebbe la scelta che ci si e' appena spostati a fare.
    if (static_cast<int>(menu.openPath.size()) > depth &&
        menu.openPath[static_cast<size_t>(depth)] == index)
        return true;

    if (item->lazyNewFile && item->children.empty())
        FillNewFileMenu(*item);
    if (item->lazySendTo && item->children.empty())
        FillSendToMenu(*item);

    FillShellSubmenu(app, *item);
    if (item->children.empty())
    {
        // Un sottomenu che resta vuoto non si apre: si vedrebbe un pannello vuoto
        // e non si capirebbe se e' colpa nostra o della shell.
        DiagLog("  sottomenu vuoto: '" + item->label + "'");
        return false;
    }

    menu.openPath.resize(static_cast<size_t>(depth) + 1);
    menu.openPath[static_cast<size_t>(depth)] = index;
    menu.levels.resize(static_cast<size_t>(depth) + 2);

    // La scelta cade sulla prima voce eseguibile: subito dopo si sta li' con le
    // frecce, e un sottomenu aperto senza niente scelto non dice dove si e'.
    ContextMenuLevel& level = menu.levels.back();
    level.selected  = -1;
    level.firstItem = 0;

    const int newDepth = static_cast<int>(menu.openPath.size());
    const int count = LevelCount(menu, newDepth);
    for (int i = 0; i < count; ++i)
    {
        const ContextMenuItem* child = LevelItem(menu, newDepth, i);
        if (child != nullptr && !child->separator && !child->disabled)
        {
            level.selected = i;
            break;
        }
    }
    ContextMenuClampWindow(app, layout, newDepth);
    return true;
}

// Esc e freccia sinistra: si risale di un livello, e solo dal primo livello si
// chiude il menu. E' l'unico modo di uscire da un sottomenu senza eseguire
// niente, ed e' quello che fanno tutti i menu.
void ContextMenuRaiseLevel(AppState& app)
{
    const int depth = ActiveDepth(app.contextMenu);
    if (depth == 0)
        ContextMenuClose(app);
    else
        CloseSubmenusBelow(app.contextMenu, depth - 1);
}

void ContextMenuActivate(AppState& app)
{
    ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    const ContextMenuItem* chosen = LevelItem(menu, depth,
                                              menu.levels[static_cast<size_t>(depth)].selected);
    if (chosen == nullptr || chosen->separator || chosen->disabled)
        return;

    // Una voce con un sottomenu non esegue niente: lo apre, e ci si lavora
    // dentro. E' l'Invio che scende, come nei menu di Windows.
    if (HasSubmenu(*chosen))
    {
        OpenSubmenu(app, ComputeLayout(app), depth,
                    menu.levels[static_cast<size_t>(depth)].selected);
        return;
    }

    // Una voce di "Nuovo file": la posizione nella banda e' l'indice del modello
    // letto dal registro. Il menu si chiude come per ogni altra voce: senza questo
    // resterebbe aperto sopra la rinomina che parte subito dopo.
    if (chosen->command >= kNewItemCmdFirst && chosen->command < kNewItemCmdLast)
    {
        const size_t model = static_cast<size_t>(chosen->command - kNewItemCmdFirst);
        ContextMenuClose(app);
        CreateNewItem(app, model);
        return;
    }

    // Una destinazione di "Invia a": stessa forma — la posizione nella banda e'
    // l'indice nell'elenco letto dalla cartella SendTo.
    if (chosen->command >= kSendToCmdFirst && chosen->command < kSendToCmdLast)
    {
        const size_t target = static_cast<size_t>(chosen->command - kSendToCmdFirst);
        const std::vector<std::wstring> paths = SelectedPaths(app);
        ContextMenuClose(app);
        SendToTargets(app, target, paths);
        return;
    }

    const ContextMenuItem item = *chosen;
    const std::vector<std::wstring> paths = SelectedPaths(app);

    // Un id nella banda riservata appartiene alla shell, e l'IContextMenu serve
    // ancora al momento dell'esecuzione: lo si prende prima di chiudere, perche'
    // chiudere lo rilascia.
    IContextMenu* shell  = app.contextMenu.shell;
    const int     offset = item.command - kShellCmdFirst;
    if (shell != nullptr)
        shell->AddRef();
    ContextMenuClose(app);

    if (item.command >= kShellCmdFirst)
    {
        if (shell == nullptr)
        {
            ReportStatus(app, L"Comando della shell non piu' disponibile.");
            return;
        }

        CMINVOKECOMMANDINFOEX info{};
        info.cbSize  = sizeof(info);
        info.fMask   = CMIC_MASK_UNICODE | CMIC_MASK_FLAG_NO_UI;
        info.hwnd    = app.window;
        info.lpVerb  = MAKEINTRESOURCEA(static_cast<WORD>(offset));
        info.lpVerbW = MAKEINTRESOURCEW(static_cast<WORD>(offset));
        info.nShow   = SW_SHOWNORMAL;

        const HRESULT hr = shell->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
        shell->Release();

        // Un'azione che fallisce in silenzio e' peggio di una che non parte.
        if (FAILED(hr))
            ReportStatus(app, L"Il comando della shell non e' partito.");
        return;
    }

    if (item.command == kContextOpen)
    {
        ActivateSelection(app);
        return;
    }
    if (item.command == kContextProperties)
    {
        // Un'azione che non fa niente in silenzio e' peggio di una che non parte:
        // se non si apre niente, l'utente deve sapere perche'.
        if (paths.empty())
            ReportStatus(app, L"Niente su cui aprire le proprieta'.");
        else if (ShowProperties(app.window, paths) == 0)
            ReportStatus(app, L"Proprieta' non disponibili per la selezione.");
        return;
    }

    // Tutto il resto e' un comando che esiste gia': il menu contestuale non
    // aggiunge capacita', rende raggiungibile dove serve quella che c'e'.
    RunCommand(app, item.command);
}

// Disegna un livello del menu. Il livello 0 e' il menu, gli altri i sottomenu:
// stesso pannello, senza sottotitolo, e col segno che dice quali voci ne hanno
// uno dentro.
void ContextMenuDrawLevel(AppState& app, const Layout& layout, int depth)
{
    ContextMenu& menu = app.contextMenu;
    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect panel = ContextMenuPanel(app, layout, depth);
    const ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];

    r.Draw(UIDraw::Shadow(Rect{panel.x + 2.0f * scale, panel.y + 5.0f * scale, panel.w, panel.h},
                          Color{0.0f, 0.0f, 0.0f, 0.5f}, 20.0f * scale, 8.0f * scale));
    r.Draw(UIDraw::Fill(panel, t.panelBg, 8.0f * scale));
    r.Draw(UIDraw::Border(panel, t.panelBorder, 1.0f, 8.0f * scale));

    // Solo le voci dentro la finestra del menu: quelle fuori non si disegnano, e
    // quello che non si disegna non sborda fuori dal pannello.
    for (int i = level.firstItem; i < ContextMenuLastVisible(app, layout, depth); ++i)
    {
        const ContextMenuItem* item = LevelItem(menu, depth, i);
        if (item == nullptr)
            continue;
        const Rect row = ContextMenuRowRect(app, layout, depth, i);

        if (item->separator)
        {
            r.FillRect(Rect{row.x + 6.0f * scale, row.y + row.h * 0.5f,
                            row.w - 12.0f * scale, 1.0f},
                       t.separator);
            continue;
        }

        if (i == level.selected && !item->disabled)
            r.FillRect(row, t.rowHover, 4.0f * scale);

        app.font.Draw(r, row.x + 10.0f * scale, TextY(layout, row, app.font), item->label,
                      item->disabled ? t.textDim : t.text, scale);

        // Il segno che la voce ne contiene altre. L'atlante dei glifi e' ASCII,
        // quindi e' un '>' e non una punta: si legge uguale, e non chiede un
        // carattere che l'atlante non ha.
        if (HasSubmenu(*item))
            app.font.Draw(r, row.right() - 18.0f * scale, TextY(layout, row, app.font), ">",
                          t.textDim, scale);
    }

    // La barra dice che sopra o sotto c'e' dell'altro: un menu che scorre senza
    // dirlo sembra un menu a cui mancano delle voci.
    const int total   = LevelCount(menu, depth);
    const int visible = ContextMenuLastVisible(app, layout, depth) - level.firstItem;
    if (visible < total)
    {
        const float footer = (depth == 0) ? 22.0f * scale : 0.0f;
        const float trackY = panel.y + 6.0f * scale;
        const float trackH = panel.h - 12.0f * scale - footer;
        const float thumbH = trackH * static_cast<float>(visible) / static_cast<float>(total);
        const float travel = trackH - thumbH;
        const float offset = static_cast<float>(level.firstItem) /
                             static_cast<float>((std::max)(1, total - visible));
        r.FillRect(Rect{panel.right() - 7.0f * scale, trackY, 3.0f * scale, trackH}, t.separator);
        r.FillRect(Rect{panel.right() - 7.0f * scale, trackY + travel * offset,
                        3.0f * scale, thumbH},
                   t.textDim);
    }

    // Il sottotitolo e' del primo livello soltanto: un sottomenu non conta niente.
    if (depth == 0)
        app.smallFont.Draw(r, panel.x + 14.0f * scale, panel.bottom() - 17.0f * scale,
                           menu.subtitle, t.textDim, scale);
}

void ContextMenuDraw(AppState& app, const Layout& layout)
{
    if (!app.contextMenu.open)
        return;

    // Dal primo livello in giu': ogni pannello disegna sopra il precedente, che e'
    // l'ordine in cui stanno sullo schermo.
    for (int depth = 0; depth <= ActiveDepth(app.contextMenu); ++depth)
        ContextMenuDrawLevel(app, layout, depth);
}

// Vero se il clic e' stato consumato dal menu.
bool ContextMenuClick(AppState& app, const Layout& layout, float x, float y)
{
    if (!app.contextMenu.open)
        return false;

    ContextMenu& menu = app.contextMenu;

    // Dal livello piu' profondo al primo: il pannello aperto per ultimo sta sopra
    // gli altri, quindi e' lui che riceve il clic.
    for (int depth = ActiveDepth(menu); depth >= 0; --depth)
    {
        if (!ContextMenuPanel(app, layout, depth).Contains(x, y))
            continue;

        ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
        for (int i = level.firstItem; i < ContextMenuLastVisible(app, layout, depth); ++i)
        {
            if (!ContextMenuRowRect(app, layout, depth, i).Contains(x, y))
                continue;
            level.selected = i;
            ContextMenuActivate(app);
            return true;
        }
        return true;   // dentro il pannello, ma non su una voce
    }

    ContextMenuClose(app);
    return true;   // un clic fuori chiude, e non deve arrivare alla lista
}

// Freccia destra: scende nel sottomenu della voce scelta. Se non ce l'ha non fa
// niente — una voce semplice non e' un errore.
void ContextMenuDescend(AppState& app, const Layout& layout)
{
    const ContextMenu& menu = app.contextMenu;
    const int depth = ActiveDepth(menu);
    const int index = menu.levels[static_cast<size_t>(depth)].selected;
    if (index < 0)
        return;
    OpenSubmenu(app, layout, depth, index);
}

// Con il menu aperto il puntatore sceglie la voce, non la riga sotto: la lista e'
// coperta, e sovrappassarla sarebbe un secondo stato che l'utente non vede.
//
// Il puntatore sceglie anche il LIVELLO: entrando in un pannello piu' profondo si
// agisce su quello, e su una voce che contiene un sottomenu il sottomenu si apre
// da solo. Col mouse e' cosi' che si usano i menu, ed e' anche l'unico modo di
// accorgersi che quella voce ne aveva uno.
bool ContextMenuHover(AppState& app, const Layout& layout, float x, float y)
{
    ContextMenu& menu = app.contextMenu;
    if (!menu.open)
        return false;

    for (int depth = ActiveDepth(menu); depth >= 0; --depth)
    {
        if (!ContextMenuPanel(app, layout, depth).Contains(x, y))
            continue;

        ContextMenuLevel& level = menu.levels[static_cast<size_t>(depth)];
        for (int i = level.firstItem; i < ContextMenuLastVisible(app, layout, depth); ++i)
        {
            const ContextMenuItem* item = LevelItem(menu, depth, i);
            if (item == nullptr || item->separator || item->disabled ||
                !ContextMenuRowRect(app, layout, depth, i).Contains(x, y))
                continue;

            level.selected = i;
            if (HasSubmenu(*item))
                OpenSubmenu(app, layout, depth, i);
            else
                CloseSubmenusBelow(menu, depth);   // questa voce non ne contiene altri
            return true;
        }

        // Dentro il pannello ma non su una voce: la scelta di QUESTO livello cade,
        // i livelli sotto restano dove sono.
        level.selected = -1;
        return true;
    }

    // Fuori da ogni pannello non si tocca niente: la scelta della tastiera non
    // deve cadere perche' il puntatore e' andato altrove.
    return true;
}

void GroupMenuDraw(AppState& app, const Layout& layout, GroupMenu& menu)
{
    if (!menu.open)
        return;

    UIRenderer& r = app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect panel = GroupMenuPanel(app, layout);

    r.FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                    static_cast<float>(app.viewportHeight)}, Color{0.0f, 0.0f, 0.0f, 0.45f});

    r.Draw(UIDraw::Shadow(Rect{panel.x + 3.0f * scale, panel.y + 6.0f * scale, panel.w, panel.h},
                          Color{0.0f, 0.0f, 0.0f, 0.45f}, 22.0f * scale, 10.0f * scale));
    r.Draw(UIDraw::Fill(panel, t.panelBg, 10.0f * scale));
    r.Draw(UIDraw::Border(panel, t.panelBorder, 1.0f, 10.0f * scale));

    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.y + 12.0f * scale,
                  menu.title, t.textDim, scale);

    const std::string query = menu.filter.empty() ? menu.placeholder : ToUtf8(menu.filter);
    app.font.Draw(r, panel.x + 16.0f * scale, panel.y + 32.0f * scale, query,
                  menu.filter.empty() ? t.textDim : t.text, scale);
    if (!menu.filter.empty())
    {
        const float caretX = panel.x + 16.0f * scale + app.font.Measure(query, scale) + 1.0f * scale;
        r.FillRect(Rect{caretX, panel.y + 32.0f * scale, 1.0f, app.font.LineHeight() * scale}, t.text);
    }

    const float listTop    = panel.y + 58.0f * scale;
    const float listBottom = panel.bottom() - 26.0f * scale;
    const float rowHeight  = layout.rowHeight;
    r.FillRect(Rect{panel.x + 1.0f, listTop - 4.0f * scale, panel.w - 2.0f, 1.0f}, t.separator);

    const int visibleRows = (std::max)(1, static_cast<int>((listBottom - listTop) / rowHeight));
    int firstVisible = 0;
    if (menu.selected >= firstVisible + visibleRows)
        firstVisible = menu.selected - visibleRows + 1;
    if (menu.selected < firstVisible)
        firstVisible = menu.selected;

    for (int i = firstVisible; i < static_cast<int>(menu.rows.size()); ++i)
    {
        const float y = listTop + (i - firstVisible) * rowHeight;
        if (y + rowHeight > listBottom)
            break;

        const GroupMenuRow& row = menu.rows[static_cast<size_t>(i)];
        const Rect rowRect{panel.x + 6.0f * scale, y, panel.w - 12.0f * scale, rowHeight};

        if (row.header)
        {
            const std::string& name = menu.groups[static_cast<size_t>(row.group)];
            const bool collapsed = menu.collapsed.count(name) != 0;
            r.FillRect(rowRect, t.headerBg, 3.0f * scale);
            app.smallFont.Draw(r, rowRect.x + 8.0f * scale, TextY(layout, rowRect, app.smallFont),
                          (collapsed ? "> " : "v ") + name, t.text, scale);

            ClickTarget target;
            target.rect  = rowRect;
            target.kind  = ClickTarget::Kind::MenuHeader;
            target.label = name;
            app.clickTargets.push_back(target);
            continue;
        }

        const GroupMenuItem& item = menu.items[static_cast<size_t>(row.item)];
        if (i == menu.selected)
            r.FillRect(rowRect, t.rowSelected, 3.0f * scale);

        app.font.Draw(r, rowRect.x + 14.0f * scale, TextY(layout, rowRect, app.font),
                      item.label, t.text, scale);

        if (menu.detailed)
        {
            const std::string& detail = item.detail.empty() ? item.group : item.detail;
            const float width = app.smallFont.Measure(detail, scale);
            app.smallFont.Draw(r, rowRect.right() - width - 12.0f * scale, TextY(layout, rowRect, app.smallFont),
                          detail, t.textDim, scale);
        }

        ClickTarget target;
        target.rect  = rowRect;
        target.kind  = ClickTarget::Kind::MenuRow;
        target.index = row.item;
        app.clickTargets.push_back(target);
    }

    const std::string footer = std::to_string(menu.matchCount) + " of " +
                               std::to_string(menu.items.size()) + "   " + menu.hint;
    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.bottom() - 20.0f * scale,
                  footer, t.textDim, scale);
}

void DrawFrame(AppState& app, const Layout& layout)
{
    app.clickTargets.clear();

    float columnX[kColumnCount] = {};

    app.renderer.FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                               static_cast<float>(app.viewportHeight)}, app.theme.windowBg);

    DrawTitleBar(app, layout);
    DrawToolbar(app, layout, app.directory.empty() ? L"This PC" : app.directory);
    DrawSidebar(app, layout);
    DrawHeader(app, layout, columnX);
    DrawList(app, layout, columnX);

    // The rubber band is drawn over the rows it is about to select, and only over
    // the list: it is clamped to that region so dragging past the edge does not
    // look like it is selecting the sidebar.
    if (app.rectSelecting)
    {
        const float x0 = (std::min)(app.rectX0, app.rectX1);
        const float y0 = (std::min)(app.rectY0, app.rectY1);
        const Rect band{x0, y0, std::fabs(app.rectX1 - app.rectX0), std::fabs(app.rectY1 - app.rectY0)};
        app.renderer.FillRect(band, app.theme.accent.WithAlpha(0.18f), 2.0f * layout.scale);
        app.renderer.BorderRect(band, app.theme.accent.WithAlpha(0.7f), 1.0f, 2.0f * layout.scale);
    }

    DrawFilterBar(app, layout);
    DrawInspector(app, layout);
    DrawStatusBar(app, layout);

    // Last, so they cover everything and their click targets are the first ones
    // HitTest finds (it walks the list backwards). At most one is open.
    GroupMenuDraw(app, layout, app.palette);
    GroupMenuDraw(app, layout, app.goTo);
    ContextMenuDraw(app, layout);
}

// Un fotogramma, disegnato adesso.
//
// Il ciclo principale disegna appena puo', ma durante un trascinamento che nasce
// da qui il ciclo e' fermo dentro `DoDragDrop`: la finestra resta com'e' e
// l'evidenziazione della destinazione non comparirebbe mai. Il ciclo modale di OLE
// i messaggi li consegna — e' l'unico motivo per cui la finestra resta viva — e
// quindi basta disegnare quando la destinazione cambia (vedi `DragDrop::Highlight`).
//
// E' l'unico posto in cui si disegna fuori dal ciclo, e ci si arriva solo da un
// callback di OLE, cioe' mai dentro un fotogramma gia' in corso.
void RenderFrame(AppState& app)
{
    if (app.deviceContext == nullptr || app.viewportWidth <= 0 || app.viewportHeight <= 0)
        return;

    glViewport(0, 0, app.viewportWidth, app.viewportHeight);
    glClearColor(0.106f, 0.110f, 0.122f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Le anteprime pronte si caricano prima del fotogramma che le disegna: e' il
    // solo punto in cui l'atlante viene toccato, e gira sul thread che possiede il
    // contesto OpenGL.
    app.previews.Pump();

    app.renderer.Begin(app.viewportWidth, app.viewportHeight);
    app.renderer.ReserveQuads(app.entries.size() * 8 + 512);
    DrawFrame(app, ComputeLayout(app));
    app.renderer.End();

    SwapBuffers(app.deviceContext);
}

// --- input -----------------------------------------------------------------
const ClickTarget* HitTest(const AppState& app, float x, float y, ClickTarget::Kind kind)
{
    // Reverse order: later targets are drawn on top.
    for (auto it = app.clickTargets.rbegin(); it != app.clickTargets.rend(); ++it)
    {
        if (it->kind == kind && it->rect.Contains(x, y))
            return &*it;
    }
    return nullptr;
}

// A click inside an open group menu. The caller turns the action into whatever
// the menu means: running a command, or going to a location.
MenuAction GroupMenuClick(const AppState& app, const Layout& layout, GroupMenu& menu, float x, float y)
{
    if (!menu.open)
        return MenuAction::None;

    if (const ClickTarget* header = HitTest(app, x, y, ClickTarget::Kind::MenuHeader))
    {
        if (menu.collapsed.count(header->label) != 0)
            menu.collapsed.erase(header->label);
        else
            menu.collapsed.insert(header->label);
        menu.dirty = true;
        return MenuAction::None;
    }

    if (const ClickTarget* row = HitTest(app, x, y, ClickTarget::Kind::MenuRow))
    {
        for (int i = 0; i < static_cast<int>(menu.rows.size()); ++i)
        {
            const GroupMenuRow& candidate = menu.rows[static_cast<size_t>(i)];
            if (!candidate.header && candidate.item == row->index)
            {
                menu.selected = i;
                break;
            }
        }
        return MenuAction::Activate;
    }

    // Inside the panel but not on a row: swallow the click. Outside it, close.
    return GroupMenuPanel(app, layout).Contains(x, y) ? MenuAction::None : MenuAction::Close;
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

// --- selection --------------------------------------------------------------
int FindEntryByName(const AppState& app, const std::wstring& name)
{
    for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
    {
        if (app.entries[static_cast<size_t>(i)].wideName == name)
            return i;
    }
    return -1;
}

int CursorIndex(const AppState& app)
{
    return app.cursor.empty() ? -1 : FindEntryByName(app, app.cursor);
}

bool IsEntrySelected(const AppState& app, const Entry& entry)
{
    return app.selection.count(entry.wideName) != 0;
}

void SelectOnly(AppState& app, int entryIndex)
{
    app.selection.clear();
    if (entryIndex < 0)
        return;
    const std::wstring& name = app.entries[static_cast<size_t>(entryIndex)].wideName;
    app.selection.insert(name);
    app.cursor          = name;
    app.selectionAnchor = name;
}

void ToggleSelection(AppState& app, int entryIndex)
{
    if (entryIndex < 0)
        return;
    const std::wstring& name = app.entries[static_cast<size_t>(entryIndex)].wideName;
    if (app.selection.count(name) != 0)
        app.selection.erase(name);
    else
        app.selection.insert(name);
    app.cursor          = name;
    app.selectionAnchor = name;
}

// Row index of an entry in the current view, or -1 when the view hides it.
int RowOfEntry(const AppState& app, int entryIndex)
{
    for (int i = 0; i < static_cast<int>(app.rows.size()); ++i)
    {
        if (!app.rows[static_cast<size_t>(i)].header && app.rows[static_cast<size_t>(i)].entry == entryIndex)
            return i;
    }
    return -1;
}

// Adds every entry row between two row indices, inclusive. Group bands between
// them are stepped over rather than treated as boundaries.
void SelectRowRange(AppState& app, int rowFrom, int rowTo)
{
    if (rowFrom < 0 || rowTo < 0)
        return;
    const int low  = (std::min)(rowFrom, rowTo);
    const int high = (std::max)(rowFrom, rowTo);
    for (int i = low; i <= high && i < static_cast<int>(app.rows.size()); ++i)
    {
        const Row& row = app.rows[static_cast<size_t>(i)];
        if (!row.header)
            app.selection.insert(app.entries[static_cast<size_t>(row.entry)].wideName);
    }
}

bool RectsIntersect(const Rect& rect, float x0, float y0, float x1, float y1)
{
    return !(rect.right() < x0 || rect.x > x1 || rect.bottom() < y0 || rect.y > y1);
}

// Ends a rubber band drag. Hit testing uses the row rectangles recorded by the
// last frame, which is at most one frame stale and only matters if the user
// releases in the same instant the list scrolls.
void FinishRectSelection(AppState& app)
{
    if (!app.rectSelecting)
        return;
    app.rectSelecting = false;

    if (!app.rectAdditive)
        app.selection.clear();

    const float x0 = (std::min)(app.rectX0, app.rectX1);
    const float x1 = (std::max)(app.rectX0, app.rectX1);
    const float y0 = (std::min)(app.rectY0, app.rectY1);
    const float y1 = (std::max)(app.rectY0, app.rectY1);

    std::wstring last;
    for (const ClickTarget& target : app.clickTargets)
    {
        if (target.kind != ClickTarget::Kind::Row)
            continue;
        if (!RectsIntersect(target.rect, x0, y0, x1, y1))
            continue;
        const std::wstring& name = app.entries[static_cast<size_t>(target.index)].wideName;
        app.selection.insert(name);
        last = name;
    }

    if (!last.empty())
    {
        app.cursor          = last;
        app.selectionAnchor = last;
    }
}

void ActivateSelection(AppState& app)
{
    const int cursorIndex = CursorIndex(app);
    if (cursorIndex < 0)
        return;

    const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
    if (entry.isDirectory)
    {
        LoadDirectory(app, JoinPath(app.directory, entry.wideName));
    }
    else
    {
        const std::wstring full = JoinPath(app.directory, entry.wideName);
        ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void ScrollBy(AppState& app, const Layout& layout, float delta)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    const float content = ViewContentHeight(metrics, app.rows.size());
    const float maxScroll = (std::max)(0.0f, content - layout.list.h);
    app.scroll = (std::min)(maxScroll, (std::max)(0.0f, app.scroll + delta));
}

// Moves the cursor by |delta| entry rows, skipping group bands. Returns without
// moving when there is no further entry in that direction. |extend| is the Shift
// case: the selection becomes the range from the anchor to the new cursor.
void MoveSelection(AppState& app, int delta, bool extend)
{
    const int count = static_cast<int>(app.rows.size());
    if (count == 0 || delta == 0)
        return;

    const int cursorIndex = CursorIndex(app);
    const int position = (cursorIndex < 0) ? -1 : RowOfEntry(app, cursorIndex);

    int target = (position < 0) ? (delta > 0 ? 0 : count - 1) : position + delta;
    const int step = (delta > 0) ? 1 : -1;
    while (target >= 0 && target < count && app.rows[static_cast<size_t>(target)].header)
        target += step;

    if (target < 0 || target >= count)
        return;

    const std::wstring name = app.entries[static_cast<size_t>(app.rows[static_cast<size_t>(target)].entry)].wideName;

    if (extend && !app.selectionAnchor.empty())
    {
        const int anchorRow = RowOfEntry(app, FindEntryByName(app, app.selectionAnchor));
        if (anchorRow >= 0)
        {
            // Rebuilt from the anchor each time, so dragging the cursor back
            // towards the anchor shrinks the range instead of only growing it.
            app.selection.clear();
            SelectRowRange(app, anchorRow, target);
        }
        else
        {
            app.selection.insert(name);
        }
    }
    else
    {
        app.selection.clear();
        app.selection.insert(name);
        app.selectionAnchor = name;
    }

    app.cursor = name;
}

// Keyboard navigation must not be able to walk the selection off the visible
// area; with grouping and filtering the row index of an entry is not its index
// in the directory any more.
void EnsureSelectionVisible(AppState& app, const Layout& layout)
{
    const int cursorIndex = CursorIndex(app);
    if (cursorIndex < 0)
        return;
    const int rowIndex = RowOfEntry(app, cursorIndex);
    if (rowIndex < 0)
        return;

    // ItemRect already accounts for the grid flow and the current scroll, so the
    // correction is just how far the item sticks out.
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    const Rect rect = ItemRect(layout, metrics, rowIndex, app.scroll);
    if (rect.y < layout.list.y)
        app.scroll -= layout.list.y - rect.y;
    else if (rect.bottom() > layout.list.bottom())
        app.scroll += rect.bottom() - layout.list.bottom();
    app.scroll = (std::max)(0.0f, app.scroll);
}

// Apre il menu contestuale da tastiera, ancorato alla riga del cursore. Il tasto
// Menu — o Shift+F10, come in Esplora risorse — deve fare la stessa cosa del
// tasto destro sul file scelto: senza mouse il menu resterebbe irraggiungibile.
// Nel binario originale questa chiave esiste, `HK_ContextMenuOpen`.
void OpenContextMenuAtCursor(AppState& app, const Layout& layout)
{
    const int cursorIndex = CursorIndex(app);
    const int rowIndex    = (cursorIndex < 0) ? -1 : RowOfEntry(app, cursorIndex);
    if (rowIndex < 0)
        return;

    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    const Rect item = ItemRect(layout, metrics, rowIndex, app.scroll);
    BuildContextMenu(app, item.x + 40.0f * layout.scale, item.y + item.h * 0.5f);
}

// --- window plumbing -------------------------------------------------------
AppState* StateFromWindow(HWND window)
{
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    AppState* app = StateFromWindow(window);

    switch (message)
    {
        case WM_NCCALCSIZE:
            // wParam arriva a FALSE, non a TRUE. La condizione `wparam == TRUE`
            // non entrava mai, il caso cadeva su DefWindowProc, e DefWindowProc
            // ricalcola il rettangolo cliente DALLO STILE — che contiene
            // WS_CAPTION — quindi rimetteva la didascalia di sistema, disegnata
            // sopra il nostro contenuto. Da qui le due barre.
            //
            // L'area non-cliente va tolta in entrambi i casi.
            if (wparam == TRUE)
            {
                // A finestra ingrandita il sistema gonfia comunque la finestra
                // del bordo di ridimensionamento: il contenuto va rientrato di
                // altrettanto o finirebbe sotto il bordo dello schermo.
                if (IsZoomed(window))
                {
                    NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                    const int frameX = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    const int frameY = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left   += frameX;
                    params->rgrc[0].right  -= frameX;
                    params->rgrc[0].top    += frameY;
                    params->rgrc[0].bottom -= frameY;
                }
            }
            return 0;

        case WM_NCHITTEST:
        {
            if (app == nullptr)
                break;

            POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window, &screen);
            const float x = static_cast<float>(screen.x);
            const float y = static_cast<float>(screen.y);
            const Layout layout = ComputeLayout(*app);

            if (IsZoomed(window) == FALSE)
            {
                const float border = 6.0f * layout.scale;
                const float width  = static_cast<float>(app->viewportWidth);
                const float height = static_cast<float>(app->viewportHeight);
                const bool left = x < border, right = x >= width - border;
                const bool top  = y < border, bottom = y >= height - border;

                if (top && left)      return HTTOPLEFT;
                if (top && right)     return HTTOPRIGHT;
                if (bottom && left)   return HTBOTTOMLEFT;
                if (bottom && right)  return HTBOTTOMRIGHT;
                if (left)             return HTLEFT;
                if (right)            return HTRIGHT;
                if (top)              return HTTOP;
                if (bottom)           return HTBOTTOM;
            }

            if (y < layout.titleBar.bottom())
            {
                // Buttons and tabs must stay clickable, so they report as client
                // area; the rest of the strip is the drag handle, which also gives
                // double-click to maximise and snap for free.
                for (const ClickTarget& target : app->clickTargets)
                {
                    switch (target.kind)
                    {
                        case ClickTarget::Kind::Menu:
                        case ClickTarget::Kind::Tab:
                        case ClickTarget::Kind::TabClose:
                        case ClickTarget::Kind::TabNew:
                        case ClickTarget::Kind::WindowMinimize:
                        case ClickTarget::Kind::WindowMaximize:
                        case ClickTarget::Kind::WindowClose:
                            if (target.rect.Contains(x, y))
                                return HTCLIENT;
                            break;
                        default:
                            break;
                    }
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_MOUSEWHEEL:
            if (app != nullptr)
            {
                const Layout layout = ComputeLayout(*app);

                // Con il menu aperto la rotella scorre il menu: e' l'unica cosa
                // su cui si sta agendo, e la lista sotto e' coperta. Scorre il
                // livello su cui si sta agendo, che e' il piu' profondo aperto.
                if (app->contextMenu.open)
                {
                    const int depth = ActiveDepth(app->contextMenu);
                    const int steps = static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                    app->contextMenu.levels[static_cast<size_t>(depth)].firstItem -= steps;
                    ContextMenuClampWindow(*app, layout, depth, /*followSelection=*/false);
                    return 0;
                }

                const ViewMetrics metrics = ComputeViewMetrics(*app, layout);
                const float stride = metrics.grid ? metrics.cellHeight : metrics.rowHeight;
                const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                ScrollBy(*app, layout, -steps * 3.0f * stride);
            }
            return 0;

        case WM_MOUSELEAVE:
            if (app != nullptr)
            {
                app->hovered       = -1;
                app->hoveredPlace  = -1;
                app->hoveredTab    = -1;
                app->hoveredChrome = -1;
            }
            return 0;

        case WM_RBUTTONUP:
        {
            if (app == nullptr)
                break;

            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));

            // Aprire il menu su una riga non selezionata sposta prima la
            // selezione li': il menu descrive cio' su cui si e' cliccato, non
            // quello che era selezionato prima.
            if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
            {
                if (app->selection.count(app->entries[static_cast<size_t>(row->index)].wideName) == 0)
                    SelectOnly(*app, row->index);
            }
            // Sul fondo della lista non c'e' niente su cui agire, quindi la
            // selezione cade e il menu diventa quello della **cartella** — come
            // in Esplora risorse. Il cursore resta: e' il fuoco della tastiera,
            // non una scelta, e cosi' `Menu` continua a funzionare anche qui.
            // Fuori dalla lista (barra laterale, inspector) non si tocca niente:
            // li' un "fondo" non esiste.
            else if (ComputeLayout(*app).list.Contains(x, y))
            {
                app->selection.clear();
            }

            BuildContextMenu(*app, x, y);
            return 0;
        }

        case WM_MOUSEMOVE:
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));

                // Uscendo dalla finestra il mouse non manda altri WM_MOUSEMOVE,
                // quindi il riquadro di sovrappasso resterebbe appeso sull'ultima
                // riga toccata. TrackMouseEvent fa arrivare un WM_MOUSELEAVE, e va
                // richiesto a ogni movimento perche' la richiesta vale una volta
                // sola.
                TRACKMOUSEEVENT track{};
                track.cbSize    = sizeof(track);
                track.dwFlags   = TME_LEAVE;
                track.hwndTrack = window;
                TrackMouseEvent(&track);

                if (app->rectSelecting)
                {
                    app->rectX1 = x;
                    app->rectY1 = y;
                    return 0;
                }

                // Trascinamento armato e mouse che si allontana: si parte. La
                // soglia e' quella di sistema (`SM_CXDRAG`), perche' una mano che
                // trema su un clic non deve spostare dei file.
                if (app->dragArmed && (GetKeyState(VK_LBUTTON) & 0x8000) != 0)
                {
                    const float farX = std::fabs(x - app->dragStartX);
                    const float farY = std::fabs(y - app->dragStartY);
                    const float thresholdX = static_cast<float>(GetSystemMetrics(SM_CXDRAG));
                    const float thresholdY = static_cast<float>(GetSystemMetrics(SM_CYDRAG));
                    if (farX > thresholdX || farY > thresholdY)
                    {
                        app->dragArmed = false;   // si parte una volta sola

                        const std::vector<std::wstring> paths = SelectedPaths(*app);
                        DWORD effect = DROPEFFECT_NONE;
                        DragDrop::DragOut(window, paths, &effect);

                        // Spostati davvero: gli originali non ci sono piu' e la
                        // vista li sta ancora mostrando.
                        if ((effect & DROPEFFECT_MOVE) != 0 && !paths.empty())
                        {
                            ReloadKeeping(*app, /*keep=*/{}, /*preferCursor=*/{}, app->scroll);
                            ReportStatus(*app, L"Spostati " + std::to_wstring(paths.size()) +
                                                   L" elementi");
                        }
                        return 0;
                    }
                }

                // Con il menu contestuale aperto il puntatore sceglie la voce, non
                // la riga sotto: la lista e' coperta, e sovrappassarla sarebbe un
                // secondo stato che l'utente non vede.
                if (app->contextMenu.open)
                {
                    const Layout layout = ComputeLayout(*app);
                    ContextMenuHover(*app, layout, x, y);
                    app->hovered = -1;
                    return 0;
                }

                app->hovered = -1;
                app->hoveredPlace = -1;
                app->hoveredTab = -1;
                app->hoveredChrome = -1;

                if (HitTest(*app, x, y, ClickTarget::Kind::Menu))              app->hoveredChrome = 0;
                else if (HitTest(*app, x, y, ClickTarget::Kind::TabNew))       app->hoveredChrome = 1;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMinimize)) app->hoveredChrome = 2;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMaximize)) app->hoveredChrome = 3;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowClose))    app->hoveredChrome = 4;

                if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::Tab))
                    app->hoveredTab = tab->index;
                else if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                    app->hovered = row->index;
                else if (const ClickTarget* place = HitTest(*app, x, y, ClickTarget::Kind::Place))
                    app->hoveredPlace = place->index;
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));

                const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                // Il menu contestuale e' modale quanto la palette: finche' e'
                // aperto prende il puntatore e la tastiera, e la lista sotto non
                // risponde.
                if (app->contextMenu.open)
                {
                    const Layout layout = ComputeLayout(*app);
                    if (ContextMenuClick(*app, layout, x, y))
                        return 0;
                }

                // An open menu is modal: its clicks are handled first and nothing
                // underneath is reachable.
                if (GroupMenu* menu = OpenMenu(*app))
                {
                    const MenuAction action = GroupMenuClick(*app, ComputeLayout(*app), *menu, x, y);
                    if (action == MenuAction::Close)
                        menu->open = false;
                    else if (action == MenuAction::Activate)
                    {
                        if (menu == &app->palette)
                            RunPaletteSelection(*app);
                        else
                            RunGoToSelection(*app);
                    }
                    return 0;
                }

                if (const ClickTarget* closeTab = HitTest(*app, x, y, ClickTarget::Kind::TabClose))
                {
                    CloseTab(*app, closeTab->index);
                }
                else if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::Tab))
                {
                    ActivateTab(*app, tab->index);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::TabNew))
                {
                    NewTab(*app, app->directory.empty() ? L"C:\\" : app->directory);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::Menu))
                {
                    app->sidebarVisible = !app->sidebarVisible;
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMinimize))
                {
                    SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMaximize))
                {
                    SendMessageW(window, WM_SYSCOMMAND, IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE, 0);
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowClose))
                {
                    PostMessageW(window, WM_CLOSE, 0, 0);
                }
                else if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                {
                    // La pressione su una riga arma un trascinamento: partira' solo
                    // se il mouse si muove oltre la soglia, e fino ad allora resta
                    // il clic che e' sempre stato.
                    app->dragArmed  = true;
                    app->dragStartX = x;
                    app->dragStartY = y;

                    if (ctrl)
                    {
                        ToggleSelection(*app, row->index);
                    }
                    else if (shift)
                    {
                        // Extend from the anchor to the clicked row, across group
                        // bands, matching what the keyboard does.
                        const int anchorEntry = FindEntryByName(*app, app->selectionAnchor);
                        SelectRowRange(*app, RowOfEntry(*app, anchorEntry), RowOfEntry(*app, row->index));
                        app->cursor = app->entries[static_cast<size_t>(row->index)].wideName;
                    }
                    else
                    {
                        SelectOnly(*app, row->index);
                    }
                }
                else if (const ClickTarget* group = HitTest(*app, x, y, ClickTarget::Kind::GroupHeader))
                {
                    // Fold state is keyed by label, so it survives a rebuild of
                    // the view when the statistics change.
                    if (app->collapsedGroups.count(group->label) != 0)
                        app->collapsedGroups.erase(group->label);
                    else
                        app->collapsedGroups.insert(group->label);
                    app->viewDirty = true;
                }
                else if (HitTest(*app, x, y, ClickTarget::Kind::GroupMode) != nullptr)
                {
                    app->groupMode = static_cast<GroupMode>((static_cast<int>(app->groupMode) + 1) % 6);
                    app->viewDirty = true;
                }
                else if (const ClickTarget* header = HitTest(*app, x, y, ClickTarget::Kind::Column))
                {
                    const SortColumn column = kColumns[header->index].sort;
                    if (column == app->sortColumn)
                        app->sortAscending = !app->sortAscending;
                    else
                    {
                        app->sortColumn = column;
                        app->sortAscending = true;
                    }
                    SortEntries(*app);
                    app->viewDirty = true;
                }
                else if (const ClickTarget* crumb = HitTest(*app, x, y, ClickTarget::Kind::Breadcrumb))
                {
                    LoadDirectory(*app, crumb->path);
                }
                else if (const ClickTarget* place = HitTest(*app, x, y, ClickTarget::Kind::Place))
                {
                    LoadDirectory(*app, place->path);
                }
                else
                {
                    // Nothing under the cursor: an empty area of the list starts a
                    // rubber band. Everywhere else the press is simply ignored.
                    const Layout layout = ComputeLayout(*app);
                    if (layout.list.Contains(x, y))
                    {
                        app->rectSelecting = true;
                        // Shift adds to the selection here too, the way it does on a
                        // click: a band is a range gesture, and Shift is the range
                        // modifier.
                        app->rectAdditive  = ctrl || shift;
                        app->rectX0 = app->rectX1 = x;
                        app->rectY0 = app->rectY1 = y;
                        if (!ctrl)
                        {
                            app->selection.clear();
                            app->cursor.clear();
                            app->selectionAnchor.clear();
                        }
                    }
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (app != nullptr)
            {
                app->dragArmed = false;
                FinishRectSelection(*app);
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                {
                    SelectOnly(*app, row->index);
                    ActivateSelection(*app);
                }
            }
            return 0;

        case WM_KEYDOWN:
            if (app != nullptr)
            {
                const Layout layout = ComputeLayout(*app);

                // An open menu is modal, so it takes the keyboard first. Returning
                // here does not stop TranslateMessage from producing WM_CHAR, which
                // is where printable characters are routed to the menu filter.
                if (GroupMenu* menu = OpenMenu(*app))
                {
                    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                    // Each menu's own toggle key closes it again.
                    const bool toggling = control &&
                        ((menu == &app->palette && wparam == 'P') ||
                         (menu == &app->goTo    && wparam == 'G'));
                    if (toggling)
                    {
                        menu->open = false;
                        return 0;
                    }

                    const MenuAction action = GroupMenuKeyDown(*menu, wparam);
                    if (action == MenuAction::Close)
                        menu->open = false;
                    else if (action == MenuAction::Activate)
                    {
                        if (menu == &app->palette)
                            RunPaletteSelection(*app);
                        else
                            RunGoToSelection(*app);
                    }
                    return 0;
                }

                // Il menu contestuale prende la tastiera prima di tutto il resto,
                // rinomina compresa: finche' e' aperto e' lui la cosa su cui si
                // sta agendo.
                if (app->contextMenu.open)
                {
                    switch (wparam)
                    {
                        // Esc e freccia sinistra risalgono di un livello: dal
                        // primo livello chiudono il menu. E' l'unico modo di
                        // uscire da un sottomenu senza eseguire niente.
                        case VK_ESCAPE:
                        case VK_LEFT:   ContextMenuRaiseLevel(*app);       break;
                        case VK_RIGHT:  ContextMenuDescend(*app, layout);    break;
                        case VK_RETURN: ContextMenuActivate(*app); break;
                        case VK_UP:     ContextMenuMove(*app, layout, -1); break;
                        case VK_DOWN:   ContextMenuMove(*app, layout, 1);  break;
                        // In un menu che scorre, PagSu e PagGiu' scorrono di una
                        // schermata e Home/Fine vanno agli estremi: e' quello che
                        // fanno in ogni menu, e con venti voci la differenza si
                        // sente.
                        case VK_NEXT:   ContextMenuMove(*app, layout, 5);  break;
                        case VK_PRIOR:  ContextMenuMove(*app, layout, -5); break;
                        case VK_HOME:
                        case VK_END:
                            ContextMenuJump(*app, layout, wparam == VK_HOME);
                            break;
                        default: break;
                    }
                    return 0;
                }

                // La rinomina sul posto prende la tastiera prima di tutto il
                // resto. Deve stare qui e non dentro lo switch: con la rinomina
                // attiva, Backspace deve cancellare una lettera del nome e non del
                // filtro, ed Esc deve annullare il nome e non svuotare il filtro.
                if (app->renameActive)
                {
                    switch (wparam)
                    {
                        case VK_RETURN: CommitRename(*app); break;
                        case VK_ESCAPE: CancelRename(*app); break;
                        case VK_BACK:
                            if (!app->renameText.empty())
                                app->renameText.pop_back();
                            break;
                        default:
                            break;
                    }
                    return 0;
                }

                switch (wparam)
                {
                    case VK_ESCAPE:
                        // Escape unwinds the filter and stops there. Closing the
                        // window is the ✕ button or Alt+F4: an application that
                        // exits on a bare Escape is a trap, and this one did.
                        if (!app->filter.empty() || app->filterActive)
                        {
                            app->filter.clear();
                            app->filterActive = false;
                            app->viewDirty = true;
                        }
                        break;
                    case VK_DOWN:
                    case VK_UP:
                    case VK_NEXT:
                    case VK_PRIOR:
                    {
                        const bool extend = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        const ViewMetrics metrics = ComputeViewMetrics(*app, layout);
                        const float stride = metrics.grid ? metrics.cellHeight : metrics.rowHeight;
                        const int step = metrics.grid ? metrics.columns : 1;
                        const int page = (std::max)(1, static_cast<int>(layout.list.h / stride)) * step;

                        int delta = step;
                        if (wparam == VK_UP)         delta = -step;
                        else if (wparam == VK_NEXT)  delta = page;
                        else if (wparam == VK_PRIOR) delta = -page;

                        MoveSelection(*app, delta, extend);
                        EnsureSelectionVisible(*app, layout);
                        break;
                    }
                    case VK_LEFT:
                    case VK_RIGHT:
                    {
                        // Only a grid has a horizontal neighbour to move to.
                        const ViewMetrics metrics = ComputeViewMetrics(*app, layout);
                        if (!metrics.grid)
                            break;
                        const bool extend = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        MoveSelection(*app, wparam == VK_LEFT ? -1 : 1, extend);
                        EnsureSelectionVisible(*app, layout);
                        break;
                    }
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            app->viewMode = (wparam == '1') ? ViewMode::Details
                                          : (wparam == '2') ? ViewMode::List
                                          : (wparam == '3') ? ViewMode::Tiles
                                                            : ViewMode::Icons;
                            app->viewDirty = true;
                        }
                        break;
                    case VK_RETURN:
                        ActivateSelection(*app);
                        break;
                    case VK_BACK:
                        // Backspace edits the filter while it is active, and only
                        // navigates to the parent when there is nothing to erase.
                        if (app->filterActive && !app->filter.empty())
                        {
                            app->filter.pop_back();
                            app->viewDirty = true;
                        }
                        else
                        {
                            LoadDirectory(*app, ParentOf(app->directory));
                        }
                        break;
                    case VK_F5:
                        // A refresh means the tree may have changed: drop the
                        // cached folder statistics so they are counted again.
                        app->stats.ClearAll();
                        LoadDirectory(*app, app->directory);
                        break;
                    case VK_APPS:
                        // Il tasto Menu: aprire il menu contestuale e' l'unica
                        // cosa che possa significare.
                        OpenContextMenuAtCursor(*app, layout);
                        break;
                    case VK_HOME:
                    {
                        app->scroll = 0.0f;
                        for (const Row& row : app->rows)
                        {
                            if (!row.header)
                            {
                                SelectOnly(*app, row.entry);
                                break;
                            }
                        }
                        break;
                    }
                    case VK_OEM_PLUS:
                    case VK_ADD:
                        // Zoom moved to Ctrl+plus so that a plain '+' can be typed
                        // into the filter, which is what CharacterKeyAction's
                        // TypeInFilter mode requires.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            app->zoom = (std::min)(2.0f, app->zoom + 0.1f);
                        break;
                    case VK_OEM_MINUS:
                    case VK_SUBTRACT:
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            app->zoom = (std::max)(0.6f, app->zoom - 0.1f);
                        break;

                    case 'A':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            // Selects what is on screen: the filter is part of the
                            // question, so it is part of the answer.
                            app->selection.clear();
                            for (const Row& row : app->rows)
                            {
                                if (!row.header)
                                    app->selection.insert(app->entries[static_cast<size_t>(row.entry)].wideName);
                            }
                        }
                        break;
                    case 'C':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            DoCopyToClipboard(*app, /*cut=*/false);
                        break;
                    case 'X':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            DoCopyToClipboard(*app, /*cut=*/true);
                        break;
                    case 'V':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            DoPaste(*app);
                        break;
                    case 'N':
                        // Ctrl+Shift+N, come Esplora risorse. Ctrl+N da solo resta
                        // libero: non c'e' ancora una finestra nuova da aprire.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                            (GetKeyState(VK_SHIFT) & 0x8000) != 0)
                            DoNewFolder(*app);
                        break;
                    case VK_F2:
                        BeginRename(*app);
                        break;
                    case VK_DELETE:
                    {
                        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        DoDelete(*app, shift);
                        break;
                    }
                    case 'T':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            NewTab(*app, app->directory.empty() ? L"C:\\" : app->directory);
                        break;
                    case 'P':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            BuildPaletteMenu(*app);
                            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                                app->palette.detailed = true;   // Ctrl+Shift+P
                        }
                        break;
                    case 'G':
                        // GoTo takes the obvious binding; cycling the grouping
                        // moves to Ctrl+Shift+G.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                            {
                                app->groupMode = static_cast<GroupMode>((static_cast<int>(app->groupMode) + 1) % 6);
                                app->viewDirty = true;
                            }
                            else
                            {
                                BuildGoToMenu(*app);
                            }
                        }
                        break;
                    case 'W':
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                            CloseTab(*app, app->activeTab);
                        break;
                    case VK_TAB:
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        {
                            const int count = static_cast<int>(app->tabs.size());
                            if (count > 1)
                                ActivateTab(*app, (app->activeTab + 1) % count);
                        }
                        break;
                    default:
                        break;
                }
            }
            return 0;

        case WM_CHAR:
            if (app != nullptr)
            {
                // CharacterKeyAction defaults to typing into the filter rather
                // than triggering a command, so a printable character over the
                // list starts filtering. Backspace is handled in WM_KEYDOWN: not
                // handling it here avoids erasing two characters per press.
                const wchar_t character = static_cast<wchar_t>(wparam);
                if (character >= 0x20 && character != 0x7F)
                {
                    if (app->renameActive)
                    {
                        // I caratteri non validi in un nome di file li rifiuta
                        // IFileOperation con un messaggio: meglio non anticiparli,
                        // cosi' l'errore lo spiega il sistema.
                        const std::wstring wide(1, character);
                        app->renameText += ToUtf8(wide);
                    }
                    else if (GroupMenu* menu = OpenMenu(*app))
                    {
                        GroupMenuChar(*menu, character);
                    }
                    else
                    {
                        app->filter.push_back(character);
                        app->filterActive = true;
                        app->viewDirty = true;
                        app->scroll = 0.0f;
                    }
                }
            }
            return 0;

        case WM_SYSKEYDOWN:
            // Shift+F10 e' un tasto di sistema come Alt+freccia: F10 da solo
            // attiva il menu della finestra, quindi non passa da WM_KEYDOWN.
            if (app != nullptr && wparam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                OpenContextMenuAtCursor(*app, ComputeLayout(*app));
                return 0;
            }

            // Alt+Left and Alt+Right arrive as system keys, not as ordinary ones,
            // because the window has a system menu.
            if (app != nullptr && wparam == VK_LEFT)
            {
                NavigateBack(*app);
                return 0;
            }
            if (app != nullptr && wparam == VK_RIGHT)
            {
                NavigateForward(*app);
                return 0;
            }
            break;

        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;

        case WM_SIZE:
            if (app != nullptr)
            {
                app->viewportWidth  = LOWORD(lparam);
                app->viewportHeight = HIWORD(lparam);
            }
            return 0;

        case WM_DESTROY:
            // La finestra sta per morire: la registrazione come destinazione di
            // caduta va tolta adesso, o OLE resta con un puntatore a una finestra
            // che non c'e' piu'.
            DragDrop::Unregister(window);

            // Uscendo con il menu aperto la shell ci ha prestato un oggetto che
            // nessuno rilascerebbe piu'.
            if (app != nullptr)
                ContextMenuClose(*app);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

bool CreateOpenGLContext(HWND window, HDC* outDc, HGLRC* outContext)
{
    HDC dc = GetDC(window);
    if (dc == nullptr)
        return false;

    PIXELFORMATDESCRIPTOR descriptor{};
    descriptor.nSize      = sizeof(descriptor);
    descriptor.nVersion   = 1;
    descriptor.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cAlphaBits = 8;
    descriptor.cDepthBits = 24;
    descriptor.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(dc, &descriptor);
    if (format == 0 || SetPixelFormat(dc, format, &descriptor) == FALSE)
    {
        ReleaseDC(window, dc);
        return false;
    }

    HGLRC temporary = wglCreateContext(dc);
    if (temporary == nullptr)
    {
        ReleaseDC(window, dc);
        return false;
    }
    wglMakeCurrent(dc, temporary);

    HGLRC context = nullptr;
    auto createAttribs = reinterpret_cast<WglCreateContextAttribsArbFn>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (createAttribs != nullptr)
    {
        const int attributes[] = {
            kWglContextMajorVersion, 3,
            kWglContextMinorVersion, 3,
            kWglContextProfileMask,  kWglContextCoreProfile,
            0, 0};
        context = createAttribs(dc, nullptr, attributes);
    }

    if (context == nullptr)
        context = wglCreateContext(dc);

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(temporary);

    if (context == nullptr || wglMakeCurrent(dc, context) == FALSE)
    {
        ReleaseDC(window, dc);
        return false;
    }

    *outDc      = dc;
    *outContext = context;
    return true;
}

void EnableHighDpi()
{
    using SetDpiAwarenessFn = BOOL(WINAPI*)(void*);
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32 != nullptr)
    {
        auto setContext = reinterpret_cast<SetDpiAwarenessFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext != nullptr && setContext(reinterpret_cast<void*>(-4)) != FALSE)
            return;   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        FreeLibrary(user32);
    }
    SetProcessDPIAware();
}

// Writes a startup failure next to the executable. A MessageBox is useless when
// the program is launched by a script or a capture loop, and this message is the
// only diagnostic available before the window exists.
void WriteStartupLog(const std::string& message)
{
    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        return;

    std::wstring file = modulePath;
    const size_t slash = file.find_last_of(L'\\');
    file = (slash == std::wstring::npos) ? L"startup-error.log" : file.substr(0, slash + 1) + L"startup-error.log";

    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(handle, message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    CloseHandle(handle);
}

void ShowError(const std::string& message)
{
    WriteStartupLog(message);
    const std::string text = "Bolt\n\n" + message;
    MessageBoxA(nullptr, text.c_str(), "Startup failed", MB_ICONERROR | MB_OK);
}

double NowSeconds()
{
    LARGE_INTEGER frequency{}, counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

} // namespace

// --- trascinamento: la parte che sa la vista -------------------------------
//
// Il modulo OLE (`drag_drop.cpp`) non sa cosa sia una riga o una cartella aperta:
// sa leggere un oggetto dati e consegnare una caduta. La destinazione la decide
// chi conosce l'elenco, cioe' queste tre funzioni.

namespace DragDrop
{

std::wstring TargetAt(HWND window, POINT client, bool* ontoRow)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr)
        return std::wstring();

    const float x = static_cast<float>(client.x);
    const float y = static_cast<float>(client.y);

    if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
    {
        const Entry& entry = app->entries[static_cast<size_t>(row->index)];
        if (!entry.isDirectory)
        {
            // Sopra un file la destinazione e' la cartella che lo contiene, come
            // in Esplora risorse: li' un file non riceve niente, ma la lista si'.
            if (ontoRow != nullptr)
                *ontoRow = false;
            return app->directory;
        }
        if (ontoRow != nullptr)
            *ontoRow = true;
        // Una cartella va aperta per lasciarci qualcosa dentro: la riga mostra il
        // nome, la destinazione e' il percorso.
        return JoinPath(app->directory, entry.wideName);
    }

    const Layout layout = ComputeLayout(*app);
    if (layout.list.Contains(x, y))
    {
        if (ontoRow != nullptr)
            *ontoRow = false;
        return app->directory;
    }
    return std::wstring();   // fuori dalla lista non si lascia cadere niente
}

void Highlight(HWND window, const std::wstring& target, bool ontoRow)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr)
        return;

    const std::wstring previous = app->dropTarget;
    const bool previousRow      = app->dropTargetIsRow;

    app->dropTarget      = target;
    app->dropTargetIsRow = ontoRow && !target.empty();

    // L'evidenziazione va vista ADESSO: se il trascinamento e' nato da qui, il
    // ciclo principale e' fermo dentro `DoDragDrop`, e senza questo disegno la
    // riga non si accenderebbe mai. Solo quando cambia: `DragOver` arriva a ogni
    // movimento del mouse, e un fotogramma per movimento sarebbe lavoro sprecato.
    if (app->dropTarget != previous || app->dropTargetIsRow != previousRow)
        RenderFrame(*app);
}

void Drop(HWND window, const std::vector<std::wstring>& sources, const std::wstring& target,
          bool move)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr || sources.empty())
        return;

    const float scroll = app->scroll;
    std::wstring error;
    std::vector<std::wstring> created;
    const FileOps::Result result =
        move ? FileOps::Move(window, sources, target, &created, &error)
             : FileOps::Copy(window, sources, target, &created, &error);

    if (result == FileOps::Result::Cancelled)
        return;

    if (result == FileOps::Result::Failed)
    {
        ReportStatus(*app, (move ? L"Spostamento non riuscito: " : L"Copia non riuscita: ") + error);
        return;
    }

    // La vista si rifa' sempre: uno spostamento toglie gli originali da qui, e una
    // copia puo' aver aggiunto qualcosa se la destinazione e' la cartella aperta.
    // Si seleziona solo cio' che e' arrivato *qui*, perche' quello che e' finito
    // altrove non si vede e non si potrebbe evidenziare.
    std::unordered_set<std::wstring> keep;
    std::wstring landed;
    if (_wcsicmp(target.c_str(), app->directory.c_str()) == 0)
    {
        for (const std::wstring& path : created)
        {
            const std::wstring name = LeafName(path);
            keep.insert(name);
            if (landed.empty())
                landed = name;
        }
    }
    ReloadKeeping(*app, keep, landed, scroll);

    ReportStatus(*app, (move ? L"Spostati " : L"Copiati ") + std::to_wstring(sources.size()) +
                           L" elementi in " + LeafName(target));
}

} // namespace DragDrop

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    EnableHighDpi();
    // `OleInitialize` e non `CoInitializeEx`: il trascinamento fra finestre lo
    // richiede, e fa anche quello che faceva l'altra. L'appartamento deve essere
    // quello singolo, ed e' quello che usavamo gia'.
    OleInitialize(nullptr);

    // Optional starting directory, the same convenience `explorer` offers:
    //   Bolt.exe D:\some\folder
    std::wstring startDirectory;
    {
        int argumentCount = 0;
        if (LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount))
        {
            if (argumentCount > 1)
            {
                const DWORD attributes = GetFileAttributesW(arguments[1]);
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    startDirectory = arguments[1];
            }
            LocalFree(arguments);
        }
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    windowClass.lpfnWndProc   = WindowProc;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    // L'icona arriva dalle risorse dell'eseguibile, cosi' la barra delle
    // applicazioni, Alt+Tab ed Esplora risorse mostrano la stessa immagine.
    // LR_SHARED: l'icona e' di proprieta' del sistema e non va distrutta.
    windowClass.hIcon         = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BOLT),
                                                              IMAGE_ICON, 32, 32, LR_SHARED));
    windowClass.hIconSm       = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BOLT),
                                                              IMAGE_ICON, 16, 16, LR_SHARED));
    windowClass.lpszClassName = L"BoltWindow";
    if (RegisterClassExW(&windowClass) == 0)
    {
        ShowError("RegisterClassExW failed.");
        return 1;
    }

    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Bolt",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1240, 760,
                                  nullptr, nullptr, instance, nullptr);
    if (window == nullptr)
    {
        ShowError("CreateWindowExW failed.");
        return 1;
    }

    // L'icona registrata con la classe il sistema la usa solo come ripiego: una
    // finestra dichiara le proprie, ed e' quella dichiarata che legge la barra
    // delle applicazioni. Senza questo WM_GETICON risponde 0.
    SendMessageW(window, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(windowClass.hIcon));
    SendMessageW(window, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(windowClass.hIconSm));

    // Togliere l'area non-cliente toglie anche quello che DWM disegnava attorno:
    // ombra e angoli arrotondati, cioe' quello che rende una finestra
    // riconoscibile come finestra su Windows 11. Senza, il bordo e' un salto
    // netto di colore e l'applicazione sembra piatta accanto alle altre.
    //
    // Estendere il riquadro di DWM di un pixel per lato glieli restituisce: DWM
    // disegna il riquadro DENTRO l'area cliente, quindi la didascalia non torna.
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(window, &margins);

    // Il riquadro di DWM segue il tema di sistema, che puo' essere chiaro mentre
    // l'applicazione e' scura: una linea bianca su un bordo scuro si vede.
    const BOOL darkFrame = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame,
                          sizeof(darkFrame));

    // Gli angoli arrotondati di Windows 11 valgono per le finestre con
    // didascalia; questa non ne ha, quindi vanno chiesti.
    const DWORD cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference,
                          sizeof(cornerPreference));

    HDC   dc      = nullptr;
    HGLRC context = nullptr;
    if (!CreateOpenGLContext(window, &dc, &context))
    {
        ShowError("Could not create an OpenGL context.");
        return 1;
    }

    const char* missingFunction = nullptr;
    if (!LoadOpenGLFunctions(&missingFunction))
    {
        ShowError(std::string("Missing OpenGL entry point: ") + (missingFunction ? missingFunction : "?"));
        return 1;
    }

    AppState app;
    std::string error;
    if (!app.renderer.Initialize(REA_SHADER_DIR, &error))
    {
        ShowError(error);
        return 1;
    }

    // One atlas size for every layer, so the glyph, icon and thumbnail UVs
    // cannot diverge. Layer 0 holds the body glyphs, layer 1 the shell icons,
    // layer 2 the file thumbnails, layer 3 the secondary glyphs.
    constexpr int kTextureLayerSize = 1024;
    constexpr int kPreviewCellSize  = 128;
    constexpr int kPreviewCapacity  = 64;

    if (!app.renderer.CreateTextureArray(/*layers=*/4, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    if (!app.font.Build(app.renderer, L"Segoe UI", 15, kGlyphLayer, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    // The secondary size is rasterised rather than scaled down. Drawing the body
    // atlas at 0.8-0.95 resamples glyphs that were hinted for 15 px, which is
    // exactly where small text loses its crispness; a second body costs one more
    // atlas layer and makes the whole secondary tier sharp.
    if (!app.smallFont.Build(app.renderer, L"Segoe UI", 13, kSmallGlyphLayer,
                             kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    if (!app.icons.Initialize(app.renderer, kIconLayer, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    if (!app.previews.Initialize(app.renderer, kPreviewLayer, kTextureLayerSize,
                                 kPreviewCellSize, kPreviewCapacity, &error))
    {
        ShowError(error);
        return 1;
    }

    // Folder statistics run on their own worker pool, sized by the volume
    // profile of whatever is being counted.
    app.stats.Start();

    RECT client{};
    GetClientRect(window, &client);
    app.viewportWidth  = client.right;
    app.viewportHeight = client.bottom;

    // Scale the UI to the monitor DPI, then let the user zoom from there.
    const UINT dpi = GetDpiForWindow(window);
    app.dpiScale = (dpi > 0) ? static_cast<float>(dpi) / 96.0f : 1.0f;
    app.zoom = app.dpiScale;

    BuildPlaces(app);
    app.window = window;

    const std::wstring initialDirectory = startDirectory.empty()
        ? (app.places.empty() ? L"C:\\" : app.places[0].path)
        : startDirectory;

    app.tabs.push_back(Tab{});
    app.activeTab = 0;
    LoadDirectory(app, initialDirectory);

    app.deviceContext = dc;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    // Da qui in poi la finestra accetta file trascinati da fuori. Se il sistema
    // non vuole registrarla lo si dice: un trascinamento che non arriva sembra un
    // difetto dell'applicazione, ed e' un limite del sistema.
    if (!DragDrop::Register(window))
        ReportStatus(app, L"Trascinamento in entrata non disponibile su questo sistema.");

    ShowWindow(window, SW_SHOW);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    app.lastFrameTime = NowSeconds();
    bool running = true;

    while (running)
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running)
            break;

        const double now = NowSeconds();
        const double delta = now - app.lastFrameTime;
        app.lastFrameTime = now;
        if (delta > 0.0)
            app.framesPerSecond = static_cast<float>(0.9 * app.framesPerSecond + 0.1 * (1.0 / delta));

        if (app.viewportWidth <= 0 || app.viewportHeight <= 0)
            continue;

        // The view is rebuilt only when something that affects it changes: the
        // filter, the grouping, the sort, the folder. A grouping that depends on
        // the statistics is refreshed while they arrive, but throttled, so one
        // publish per work slice does not become one rebuild per frame.
        if (GroupMenu* menu = OpenMenu(app))
        {
            if (menu->dirty)
                GroupMenuRebuild(*menu);
        }

        const uint64_t statsRevision = app.stats.Revision();
        if (app.viewDirty ||
            (app.groupMode != GroupMode::None && statsRevision != app.viewStatsRevision &&
             now - app.lastViewRebuild > 0.25))
        {
            RebuildView(app);
            app.viewStatsRevision = statsRevision;
            app.lastViewRebuild   = now;
        }

        RenderFrame(app);
    }

    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    app.stats.Stop();
    OleUninitialize();
    app.previews.Shutdown();
    app.renderer.Shutdown();
    wglMakeCurrent(nullptr, nullptr);
    if (context != nullptr)
        wglDeleteContext(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    CoUninitialize();
    return 0;
}
