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
#include "dir_watch.h"

// Il messaggio che il sorvegliante manda quando una cartella cambia. Chi lo riceve
// prende i cambiamenti con `Take`: il messaggio dice "guarda", non "e' successo
// questo", perche' fra il momento in cui parte e quello in cui arriva possono
// essercene altri.
constexpr UINT kDirectoryChanged = WM_APP + 1;
#include "drag_drop.h"
#include "file_ops.h"
#include "font_atlas.h"
#include "home.h"
#include "input_targets.h"
#include "language.h"
#include "navigation.h"
#include "recents.h"
#include "gl_loader.h"
#include "resource.h"
#include "shell_icons.h"
#include "settings.h"
#include "shell_location.h"
#include "shell_new.h"
#include "shell_preview.h"
#include "shell_sendto.h"
#include "ui_renderer.h"
#include "d3d11_renderer.h"

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>   // PathIsDirectoryW: una voce del GoTo puo' essere un file
// CMINVOKECOMMANDINFOEX sta in shobjidl.h, non in shlobj.h: serve per eseguire
// una voce del menu della shell.
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <cwctype>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef REA_SHADER_DIR
#define REA_SHADER_DIR "shaders"
#endif

#include "app_state.h"
#include "app_ui.h"
#include "theme.h"
#include "view_list.h"
#include "view_inspector.h"
#include "view_menu.h"
#include "view_sidebar.h"
#include "view_statusbar.h"
#include "tabs.h"
#include "view_titlebar.h"
#include "view_options.h"
#include "shell_menu.h"
#include "view_context_menu.h"
#include "view_update.h"
#include "input.h"
#include "app_actions.h"
#include "commands.h"

using namespace Bolt;

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

using Bolt::NowSeconds;



// --- l'albero dei tab -------------------------------------------------------
//
// I tab sono un albero, non una fila: un tab puo' avere figli, e un figlio si
// disegna **sotto** il suo genitore. La fila si ricava dall'albero a ogni
// fotogramma invece di essere l'ordine in cui i tab sono nati, cosi' chiudere o
// spostare non deve aggiustare l'indice di tutti gli altri — che e' il modo in cui
// un albero diventa un groviglio.
// `HitTest` arriva da `input_targets.h`.

// I fissaggi della shell: "Accesso rapido" di Esplora risorse, che e' una cartella
// dello spazio dei nomi come le altre. Si legge adesso e si rilegge a ogni
// aggiornamento, perche' l'utente puo' averci appena messo qualcosa.
//
// Di suo questo elenco puo' contenere cartelle che l'utente ha scelto, quindi e'
// fatto di nomi *suoi*: nelle prove si fotografa la barra laterale **sotto** questa
// sezione. Vedi `docs/05`.
void BuildQuickAccess(AppState& app)
{
    app.quickAccess.clear();

    const std::wstring location = ShellLocation::QuickAccessName();
    std::string error;
    if (!location.empty())
    {
        const std::vector<ShellLocation::Item> items = ShellLocation::Read(location, &error);
        for (const ShellLocation::Item& item : items)
        {
            if (item.location.empty())
                continue;
            Place place;
            place.label = ToUtf8(item.name);
            place.path  = item.location;
            place.icon  = item.location;   // il fissaggio e' un oggetto, non una cartella
            app.quickAccess.push_back(std::move(place));
        }
    }

    // Nel registro va il motivo, non i nomi: i fissaggi della shell sono cartelle
    // scelte dall'utente. Vedi `AGENTS.md` §0.
    DiagLog("--- " + DiagClock() + " accesso rapido: " +
            (location.empty() ? std::string("la posizione non si apre")
                              : std::to_string(app.quickAccess.size()) + " voci" +
                                    (error.empty() ? "" : ", errore: " + error)));
}

// I volumi, con quanto sono pieni. La shell il nome di un volume lo sa scrivere —
// "Windows (C:)" — e la capacita' la da' il sistema: sono due chiamate, e si fanno
// quando si aggiorna la vista, non a ogni fotogramma.
void RefreshVolumes(AppState& app)
{
    app.volumes.clear();

    wchar_t letters[512]{};
    if (GetLogicalDriveStringsW(511, letters) == 0)
        return;

    for (const wchar_t* letter = letters; *letter != L'\0'; letter += wcslen(letter) + 1)
    {
        const UINT type = GetDriveTypeW(letter);
        // Un lettore senza disco non ha niente da mostrare, e una barra vuota
        // accanto a un'icona racconta una cosa che non c'e'.
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_REMOTE)
            continue;

        Place place;
        place.path = letter;
        place.icon = letter;   // l'unita' stessa: l'icona la sceglie la shell per quel disco


        SHFILEINFOW info{};
        if (SHGetFileInfoW(letter, 0, &info, sizeof(info), SHGFI_DISPLAYNAME) != 0 &&
            info.szDisplayName[0] != L'\0')
            place.label = ToUtf8(info.szDisplayName);
        else
            place.label = ToUtf8(letter);

        ULARGE_INTEGER available{};
        ULARGE_INTEGER total{};
        if (GetDiskFreeSpaceExW(letter, &available, &total, nullptr) &&
            total.QuadPart > 0)
        {
            place.usedFraction = 1.0f -
                static_cast<float>(static_cast<double>(available.QuadPart) /
                                   static_cast<double>(total.QuadPart));
            place.freeText = HumanBytes(available.QuadPart);
        }
        app.volumes.push_back(std::move(place));
    }
}

void BuildPlaces(AppState& app)
{
    BuildQuickAccess(app);
    RefreshVolumes(app);

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
        if (FAILED(SHGetKnownFolderPath(item.id, 0, nullptr, &path)) || path == nullptr)
            continue;

        Place place;
        place.path = path;
        CoTaskMemFree(path);

        // Il nome e l'icona di una cartella nota li sa la shell, ed e' lei che li
        // traduce: "Documenti", "Scaricati", "Immagini". Chiederli a lei invece di
        // scriverli qui vuol dire essere d'accordo con Esplora risorse in ogni
        // lingua, comprese quelle che non sappiamo.
        place.icon = ShellLocation::ParsingNameOf(item.id);
        const std::wstring shown = ShellLocation::DisplayName(place.icon);
        place.label = shown.empty() ? item.label : ToUtf8(shown);

        app.places.push_back(std::move(place));
    }

    // Il cestino va **in fondo**, e non per ordine alfabetico: non e' una cartella in
    // cui si tiene roba, e' dove si va a prendere quello che si e' buttato. Si apre
    // come una cartella qualunque, perche' una cartella dello spazio dei nomi e' una
    // cartella.
    {
        const std::wstring bin = ShellLocation::ParsingNameOf(FOLDERID_RecycleBinFolder);
        app.recycleBinPath = bin;
        if (!bin.empty())
        {
            const std::wstring name = ShellLocation::DisplayName(bin);
            Place place;
            place.label = ToUtf8(name.empty() ? std::wstring(L"Recycle Bin") : name);
            place.path  = bin;
            place.icon  = bin;   // e l'icona e' quella sua, non una cartella
            app.places.push_back(std::move(place));
        }
    }

    // "Questo PC" sta **prima** di tutto: e' dove si scelgono le unita', e da li' si
    // entra in un disco. E' una cartella di spazio dei nomi, non un percorso; il
    // nome e l'icona sono quelli che la shell da' a quella cartella — un computer,
    // non una cartella — cosi' la barra laterale e Esplora risorse dicono la stessa
    // cosa.
    app.thisPcPath = ShellLocation::ParsingNameOf(FOLDERID_ComputerFolder);
    if (!app.thisPcPath.empty())
    {
        Place place;
        const std::wstring shown = ShellLocation::DisplayName(app.thisPcPath);
        place.label = ToUtf8(shown.empty() ? std::wstring(L"This PC") : shown);
        place.path  = app.thisPcPath;
        place.icon  = app.thisPcPath;
        app.places.insert(app.places.begin(), std::move(place));
    }
}

// --- drawing ---------------------------------------------------------------
// The window draws its own caption. The native one is removed by answering
// WM_NCCALCSIZE; what replaces it is a tab strip, which is the point of the
// milestone, plus the three window buttons and the menu button.
// ---------------------------------------------------------------------------
// La ricerca globale: avvio, un passo, fine.
//
// Non c'e' un thread. `AdvanceSearch` legge qualche decina di cartelle e torna: la
// chiama il ciclo dei messaggi (un `SetTimer` acceso solo mentre si cerca), cosi' la
// finestra ridisegna e risponde fra un passo e l'altro. Un albero grande si legge in
// un secondo o due invece che in un fotogramma solo, e "ferma" e' una bandierina.
//
// Il limite e' **dichiarato**: oltre un tetto di cartelle lette la ricerca si ferma e
// lo dice. Una ricerca che continua per dieci minuti su `C:\` sembra un blocco.
constexpr int    kSearchFoldersPerStep = 24;
constexpr size_t kSearchFolderCap      = 4000;
constexpr size_t kSearchResultCap      = 5000;

// Quante cartelle ha in coda, per la riga di stato.
std::wstring MatchParent(const std::wstring& root, const std::wstring& folder)
{
    // Il genitore **relativo alla radice**, che e' quello che si scrive accanto al
    // nome: `immagini_vecchie`. E' la stessa forma di `Toggle Subfolders`.
    if (folder.size() <= root.size())
        return std::wstring();
    std::wstring relative = folder.substr(root.size());
    while (!relative.empty() && (relative.front() == L'\\' || relative.front() == L'/'))
        relative.erase(relative.begin());
    return relative;
}


// Il passo: legge |kSearchFoldersPerStep| cartelle e accumula. Quando la coda e' vuota
// i risultati diventano la lista.
void AdvanceSearch(AppState& app)
{
    if (!app.search.active)
        return;

    int budget = kSearchFoldersPerStep;
    while (budget-- > 0 && !app.search.pending.empty())
    {
        if (app.search.visited >= kSearchFolderCap ||
            app.search.found.size() >= kSearchResultCap)
        {
            app.search.truncated = true;
            app.search.pending.clear();
            break;
        }

        const std::wstring folder = app.search.pending.back();
        app.search.pending.pop_back();
        ++app.search.visited;

        std::string error;
        std::vector<Entry> entries = ReadDirectory(folder, &error, app.longPaths, app.showHiddenFiles, app.showSystemFiles);
        if (!error.empty())
            continue;   // una cartella che non si legge non ferma le altre

        for (Entry& entry : entries)
        {
            if (entry.isDirectory && entry.location.empty())
                app.search.pending.push_back(JoinPath(folder, entry.wideName));

            if (FoldCase(entry.wideName).find(app.search.query) == std::wstring::npos)
                continue;
            if (app.search.found.size() >= kSearchResultCap)
            {
                app.search.truncated = true;
                break;
            }
            entry.fullPath   = JoinPath(folder, entry.wideName);
            entry.parentHint = ToUtf8(MatchParent(app.search.root, folder));
            app.search.found.push_back(std::move(entry));
        }
    }

    DiagLog("--- ricerca: passo, lette " + std::to_string(app.search.visited) +
            ", in coda " + std::to_string(app.search.pending.size()) +
            ", trovati " + std::to_string(app.search.found.size()));
    const bool done = app.search.pending.empty() || app.search.truncated;
    if (!done)
    {
        // I risultati si vedono **mentre arrivano**: la lista si aggiorna a ogni passo,
        // e chi cerca la cosa giusta la vede comparire senza aspettare la fine.
        const float keepScroll = app.scroll;
        app.entries = app.search.found;
        SortEntries(app);
        app.scroll     = keepScroll;
        app.viewDirty  = true;
        InvalidateRect(app.window, nullptr, FALSE);
        return;
    }

    app.search.active   = false;
    app.search.finished = true;

    app.entries = app.search.found;
    SortEntries(app);
    app.cursor.clear();
    app.selection.clear();
    app.scroll    = 0.0f;
    app.viewDirty = true;
    RebuildView(app);
    InvalidateRect(app.window, nullptr, FALSE);

    ReportStatus(app, TrW(L"Trovati ", L"Found ") + std::to_wstring(app.entries.size()) +
                          TrW(L" in ", L" in ") + std::to_wstring(app.search.visited) +
                          TrW(L" cartelle", L" folders"));
    if (app.search.truncated)
        ReportStatus(app, TrW(L"Ricerca interrotta a un limite: restringi o usa un nome",
                              L"Search stopped at a limit: narrow it or use a name"));
}


void DrawToolbar(AppState& app, const Layout& layout, const std::wstring& displayPath)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.FillRect(layout.toolbar, t.headerBg);

    // Breadcrumb: split the path into components, each clickable. Everything is
    // measured from the toolbar rectangle, not from the top of the window: the
    // title bar above it is drawn by the application and its height varies with
    // the UI scale.
    // I pulsanti di navigazione, a sinistra del percorso: indietro, avanti, su,
    // aggiorna, e la stella dei segnalibri. Sono i più usati dopo il percorso, e in
    // File Pilot stanno li'.
    //
    // Erano i caratteri `←` `→` `↑` `↻` `★`. Quattro famiglie di segni diverse in
    // una fila di cinque non si somigliano: spessori diversi, altezze ottiche
    // diverse, e il disegno che cambia col carattere scelto. Adesso sono forme
    // disegnate dallo shader, con **una misura e uno spessore soli** per tutte e
    // cinque — la stessa ragione per cui la linguetta dei gruppi e i pulsanti della
    // finestra non sono piu' caratteri (`docs/02` §7.49, §7.50, §7.58).
    const bool navEnabled[5] = {
        app.historyIndex > 0,
        app.historyIndex + 1 < static_cast<int>(app.history.size()),
        !app.directory.empty(),
        true,
        !app.directory.empty(),
    };
    constexpr float kNavSize = 26.0f;
    constexpr float kNavIcon = 12.0f;   // misura ottica: la stessa dei tre pulsanti
                                        // della finestra (12 su 46), non il 63% del
                                        // pulsante: accanto a testo di 14 px un'icona
                                        // da 15 era la cosa piu' forte della barra
    const float navStroke = 1.2f * scale;

    // I pulsanti e l'indirizzo stanno sulla **stessa riga**, e la riga la decide il
    // testo del percorso: la base e' l'unico numero, e le icone si centrano su
    // quella. Prima avevano un `+5` scritto a mano e finivano 5 pixel piu' in alto
    // dell'indirizzo, che a occhio si vede benissimo anche in una barra sola.
    const float baseline = layout.toolbar.y + 11.0f * scale;
    const float pathRow  = baseline + 2.0f * scale + app.font.LineHeight() * scale * 0.5f;

    float navX = layout.toolbar.x + 8.0f * scale;
    for (int i = 0; i < 5; ++i)
    {
        const Rect button{navX, pathRow - kNavSize * scale * 0.5f, kNavSize * scale,
                          kNavSize * scale};
        const bool bookmarked =
            (i == 4) && std::find(app.bookmarks.begin(), app.bookmarks.end(), app.directory) !=
                            app.bookmarks.end();
        if (i == app.hoveredNav && navEnabled[i])
            r.FillRect(button, t.rowHover, 5.0f * scale);
        else if (bookmarked)
            r.FillRect(button, t.accent.WithAlpha(0.18f), 5.0f * scale);

        const Color color = !navEnabled[i] ? t.textDim.WithAlpha(0.4f)
                          : bookmarked      ? t.accent
                                            : t.textDim;

        // Tutte e cinque nella stessa scatola, centrata nel pulsante: e' quella
        // scatola che le tiene sulla stessa linea, non il disegno di ognuna.
        const float icon = kNavIcon * scale;
        const Rect shape{button.x + (button.w - icon) * 0.5f,
                         button.y + (button.h - icon) * 0.5f, icon, icon};
        switch (i)
        {
            case 0: r.Draw(UIDraw::Arrow(shape, color, navStroke, ArrowDirection::Left)); break;
            case 1: r.Draw(UIDraw::Arrow(shape, color, navStroke, ArrowDirection::Right)); break;
            case 2: r.Draw(UIDraw::Arrow(shape, color, navStroke, ArrowDirection::Up)); break;
            case 3: r.Draw(UIDraw::Refresh(shape, color, navStroke)); break;
            default:
                // La stella e' piena quando la cartella e' nei segnalibri: e' l'unico
                // segno che dice se il comando aggiunge o toglie. Vuota e' la
                // **stessa** forma disegnata come bordo, non una seconda formula.
                r.Draw(UIDraw::Star(shape, color, 1.0f * scale, bookmarked));
                break;
        }

        ClickTarget target;
        target.rect  = button;
        target.kind  = ClickTarget::Kind::NavButton;
        target.index = i;
        app.clickTargets.push_back(target);

        navX += (kNavSize + 2.0f) * scale;
    }

    float penX = navX + 8.0f * scale;

    // Le briciole si misurano **prima** di disegnarle, perche' `DisplayCompactPaths`
    // deve sapere se entrano: un percorso che esce dal riquadro si legge a meta', e
    // la meta' che si perde e' proprio quella che dice dove si e'.
    struct Crumb
    {
        std::string  label;
        std::wstring path;      // il percorso fino a questa briciola, per il clic
        float        width;
    };
    std::vector<Crumb> crumbs;
    {
        std::wstring accumulated;
        size_t start = 0;
        while (start <= displayPath.size())
        {
            size_t slash = displayPath.find(L'\\', start);
            if (slash == std::wstring::npos)
                slash = displayPath.size();

            const std::wstring segment = displayPath.substr(start, slash - start);
            if (accumulated.empty())
                accumulated = segment;
            else if (!segment.empty())
                accumulated += L"\\" + segment;

            const std::string label = ToUtf8(segment);
            crumbs.push_back(Crumb{label,
                                   ShellLocation::IsNamespace(app.directory) ||
                                           IsHome(app.directory)
                                       ? app.directory
                                       : accumulated,
                                   app.font.Measure(label, scale) + 14.0f * scale});
            if (slash == displayPath.size())
                break;
            start = slash + 1;
        }
    }

    // Quanto spazio c'e': fino al conto delle voci, che sta a destra sulla stessa
    // riga. `...` costa la sua larghezza, e quando si stringe va contata.
    const char* kEllipsis = "...";
    const float ellipsisWidth = app.font.Measure(kEllipsis, scale) + 10.0f * scale;
    const float roomRight = static_cast<float>(app.viewportWidth) - 120.0f * scale;
    float total = 0.0f;
    for (const Crumb& crumb : crumbs)
        total += crumb.width;

    size_t firstVisible = 0;
    if (app.compactPaths && penX + total > roomRight && crumbs.size() > 2)
    {
        // Si tagliano le briciole **in testa**, tenendo la radice (`C:`) e la coda:
        // la coda e' quella che si sta guardando, la radice dice di che volume si
        // parla. Le briciole tolte si contano in un `...`, che non e' cliccabile:
        // promettere un clic che non porta da nessuna parte sarebbe peggio.
        float budget = roomRight - penX - ellipsisWidth;
        float used = 0.0f;
        size_t keep = 0;
        for (size_t i = crumbs.size(); i-- > 0;)
        {
            if (used + crumbs[i].width > budget)
                break;
            used += crumbs[i].width;
            ++keep;
        }
        if (keep + 2 <= crumbs.size())
            firstVisible = crumbs.size() - keep;
        else
            firstVisible = 0;
    }

    if (firstVisible > 0)
    {
        // Il `...` al posto delle briciole tolte: nessun bersaglio, perche' non c'e'
        // una destinazione che corrisponda a "quelle in mezzo".
        app.font.Draw(r, penX + 5.0f * scale, baseline + 2.0f * scale, kEllipsis, t.textDim,
                      scale);
        penX += ellipsisWidth;
        if (firstVisible < crumbs.size())
            app.font.Draw(r, penX - 3.0f * scale, baseline + 2.0f * scale, ">", t.textDim, scale);
    }

    for (size_t i = firstVisible; i < crumbs.size(); ++i)
    {
        const Crumb& crumb = crumbs[i];
        const Rect chip{penX, baseline - 4.0f * scale, crumb.width, 22.0f * scale};
        ClickTarget target;
        target.rect  = chip;
        target.kind  = ClickTarget::Kind::Breadcrumb;
        target.path  = crumb.path;
        app.clickTargets.push_back(target);

        if (!crumb.label.empty())
        {
            app.font.Draw(r, penX + 7.0f * scale, baseline + 2.0f * scale, crumb.label,
                          (i + 1 == crumbs.size()) ? t.text : t.textDim, scale);
        }
        penX += crumb.width;

        if (i + 1 < crumbs.size())
        {
            app.font.Draw(r, penX - 4.0f * scale, baseline + 2.0f * scale, ">", t.textDim, scale);
        }
    }

    // La **ricerca globale**: un campo nella riga dei pulsanti, a destra. Cerca per
    // nome nella cartella aperta **e sotto**, e i risultati prendono il posto della
    // lista. Prima di questa riga lo spazio a destra era del solo conto delle voci.
    {
        const float boxWidth  = 200.0f * scale;
        const float boxHeight = 24.0f * scale;
        const Rect box{static_cast<float>(app.viewportWidth) - boxWidth - 96.0f * scale,
                       layout.toolbar.y + (layout.toolbar.h - boxHeight) * 0.5f, boxWidth,
                       boxHeight};
        // Rettangolo con gli angoli arrotondati, e il bordo **sempre**: senza, il campo
        // spento si confondeva con la riga dei pulsanti e non si vedeva dov'era. Il
        // raggio e' otto pixel: abbastanza da leggersi arrotondato, non tanto da
        // sembrare una pastiglia.
        const float radius = 8.0f * scale;
        r.FillRect(box, t.panelBg, radius);
        r.Draw(UIDraw::Border(box, app.searchFieldActive ? t.accent : t.panelBorder, 1.0f,
                              radius));

        std::string text = app.searchText.empty()
                               ? Tr("Cerca in questa cartella e sotto...", "Search here and below...")
                               : ToUtf8(app.searchText);
        const Color ink = app.searchText.empty() ? t.textDim.WithAlpha(0.7f) : t.text;
        app.smallFont.Draw(r, box.x + 8.0f * scale, TextY(layout, box, app.smallFont),
                           FitText(app.smallFont, text, scale, box.w - 16.0f * scale), ink, scale);

        ClickTarget target;
        target.rect = box;
        target.kind = ClickTarget::Kind::SearchField;
        app.clickTargets.push_back(target);
    }

    // Il conto delle voci, a destra: le stesse parole della barra di stato, cosi'
    // la stessa cosa non si dice in due modi.
    char summary[64];
    std::snprintf(summary, sizeof(summary),
                  app.entries.size() == 1 ? Tr("%zu voce", "%zu item")
                                          : Tr("%zu voci", "%zu items"),
                  app.entries.size());
    const float summaryWidth = app.font.Measure(summary, scale);
    app.font.Draw(r, static_cast<float>(app.viewportWidth) - summaryWidth - 14.0f * scale,
                  layout.toolbar.y + 12.0f * scale, summary, t.textDim, scale);
}

// --- barra laterale ---------------------------------------------------------
//
// Quattro sezioni, nell'ordine in cui si vedono in File Pilot: i fissaggi della
// shell, i segnalibri, i volumi, le cartelle standard. Non e' una lista sola
// perche' le voci sono di natura diversa — e perche' i segnalibri li fa l'utente,
// quindi la sezione deve poter stare vuota senza sparire.
//
// Le intestazioni si vedono in due lingue: l'inglese e' anche la **chiave** della
// sezione — e' con quella che si ricorda se e' chiusa — cosi' cambiare lingua non
// richiude le sezioni che l'utente aveva chiuso, e una sezione chiusa non si
// riapre da sola.

// L'etichetta italiana di un comando. La tabella resta com'e' — con le sue
// etichette in inglese, che sono anche i nomi delle chiavi del riferimento — e la
// traduzione sta qui, accanto, in un posto solo.
//
// Se un comando non compare, la sua etichetta resta quella della tabella: mancare
// una traduzione non deve rompere niente, e si vede subito a schermo. La lista e'
// **completa**: tutti i comandi della tabella hanno la loro parola italiana, quindi
// "cosa non e' tradotto" non ha piu' una risposta da leggere qui. Un comando nuovo
// che arrivasse senza traduzione si vedrebbe lo stesso — la sua parola inglese in
// mezzo alle altre — che e' il motivo per cui il ripiego resta.
const char* CommandLabelIt(CommandId id)
{
    switch (id)
    {
        case CmdNewTab:                 return "Nuova scheda";
        case CmdCloseTab:               return "Chiudi scheda";
        case CmdNextTab:                return "Scheda successiva";
        case CmdPrevTab:                return "Scheda precedente";
        case CmdCloseInTree:            return "Chiudi scheda e figli";
        case CmdCloseAllInTree:         return "Chiudi tutto l'albero";
        case CmdDetachTab:              return "Stacca la scheda dal genitore";
        case CmdSplitRight:             return "Dividi a destra";
        case CmdSplitBottom:            return "Dividi in basso";
        case CmdGoUp:                   return "Vai alla cartella superiore";
        case CmdBack:                   return "Indietro";
        case CmdForward:                return "Avanti";
        case CmdRefresh:                return "Aggiorna";
        case CmdRecalcStats:            return "Ricalcola le dimensioni";
        case CmdOpenExplorer:           return "Apri in Esplora risorse";
        case CmdOpenConsole:            return "Apri un terminale qui";
        case CmdCopy:                   return "Copia";
        case CmdCut:                    return "Taglia";
        case CmdPaste:                  return "Incolla";
        case CmdRename:                 return "Rinomina";
        case CmdNewFolder:              return "Nuova cartella";
        case CmdDelete:                 return "Elimina (nel cestino)";
        case CmdDeletePermanent:        return "Elimina definitivamente";
        case CmdGoTo:                   return "Vai a una cartella...";
        case CmdViewDetails:            return "Vista: dettagli";
        case CmdViewList:               return "Vista: elenco";
        case CmdViewTiles:              return "Vista: riquadri";
        case CmdViewIcons:              return "Vista: icone";
        case CmdToggleSidebar:          return "Mostra la barra laterale";
        case CmdToggleHoverToSelect:    return "Il passaggio seleziona";
        case CmdToggleUnderlineOnHover: return "Sottolinea al passaggio";
        case CmdToggleTooltips:         return "Mostra i suggerimenti";
        case CmdToggleShowRecents:      return "Evidenzia i recenti";
        case CmdToggleSortRecents:      return "Recenti in cima";
        case CmdToggleSubfolders:       return "Sottocartelle nella lista";
        case CmdSaveLayout:             return "Salva il layout della finestra";
        case CmdLoadLayout:             return "Carica il layout della finestra";
        case CmdForgetLayout:           return "Dimentica il layout";
        case CmdToggleRememberTabs:     return "Riapri le schede dell'ultima volta";
        case CmdSingleClick:            return "Un clic apre";
        case CmdSingleClickFoldersOnly: return "Un clic: solo le cartelle";
        case CmdActOnPress:             return "Agisci premendo";
        case CmdDoubleClickToParent:    return "Doppio clic sul fondo: su di un livello";
        case CmdShowCheckboxes:         return "Mostra le caselle di scelta";
        case CmdZoomIn:                 return "Ingrandisci";
        case CmdZoomOut:                return "Rimpicciolisci";
        case CmdZoomReset:              return "Ingrandimento normale";
        case CmdTextWhitespace:         return "Testo: segna spazi e tabulazioni";
        case CmdTextLineNumbers:        return "Testo: numeri di riga";
        case CmdTextMinimap:            return "Testo: minimappa";
        case CmdTextWrap:               return "Testo: vai a capo";
        case CmdTextZoomIn:             return "Testo: ingrandisci";
        case CmdTextZoomOut:            return "Testo: rimpicciolisci";
        case CmdImageZoomIn:            return "Immagine: ingrandisci";
        case CmdImageZoomOut:           return "Immagine: rimpicciolisci";
        case CmdImageZoomFit:           return "Immagine: adatta al pannello";
        case CmdToggleImageGrid:        return "Immagine: griglia di sfondo";
        case CmdToggleDetails:          return "Palette: mostra i dettagli";
        case CmdSortName:               return "Ordina per nome";
        case CmdSortType:               return "Ordina per tipo";
        case CmdSortModified:           return "Ordina per data di modifica";
        case CmdSortCreated:            return "Ordina per data di creazione";
        case CmdSortAccessed:           return "Ordina per data di accesso";
        case CmdSortTotal:              return "Ordina per numero di elementi";
        case CmdSortFolders:            return "Ordina per sottocartelle";
        case CmdSortFiles:              return "Ordina per file";
        case CmdSortSize:               return "Ordina per dimensione";
        case CmdSortDirection:          return "Inverti l'ordine";
        case CmdGroupNone:              return "Raggruppa: no";
        case CmdGroupName:              return "Raggruppa per nome";
        case CmdGroupDate:              return "Raggruppa per data";
        case CmdGroupSize:              return "Raggruppa per dimensione";
        case CmdGroupCount:             return "Raggruppa per numero di voci";
        case CmdGroupVolume:            return "Raggruppa per occupazione del disco";
        case CmdFocusFilter:            return "Vai al filtro";
        case CmdClearFilter:            return "Azzera il filtro";
        case CmdScrollTop:              return "Vai in cima";
        case CmdScrollBottom:           return "Vai in fondo";
        case CmdToggleBookmark:         return "Aggiungi questo posto ai segnalibri";
        case CmdClearBookmarks:         return "Togli tutti i segnalibri";
        case CmdClearRecents:           return "Azzera i recenti";
        case CmdLanguageItalian:        return "Lingua: italiano";
        case CmdLanguageEnglish:        return "Lingua: inglese";
        case CmdShowSettingsPath:       return "Dove sta il file delle impostazioni";
        case CmdStartupCurrentFolder:  return "Avvio: la cartella aperta";
        case CmdStartupThisPc:         return "Avvio: Questo PC";
        case CmdColorSchemeLight:       return "Tema: chiaro";
        case CmdColorSchemeDark:        return "Tema: scuro";
        case CmdSystemColorScheme:      return "Tema: segui il sistema";
        case CmdOpenOptions:            return "Apri le opzioni";
        case CmdEditKeyMap:             return "Modifica i tasti";
        case CmdPinTo:                  return "Fissa questa cartella in...";
        case CmdSaveFolderLayout:      return "Salva il layout di questa cartella";
        case CmdLoadFolderLayout:      return "Applica il layout di questa cartella";
        case CmdForgetFolderLayout:    return "Dimentica il layout di questa cartella";
        case CmdUnpin:                  return "Togli questa cartella da...";
        case CmdProperties:             return "Proprietà";
        case CmdPropertiesWindows:      return "Proprietà di Windows";
        case CmdCheckUpdates:           return "Controlla aggiornamenti";
        // L'ultima che mancava. Era l'unica su ottanta, e si vedeva per quello: la
        // palette dei comandi si apriva con una riga in inglese in mezzo alle altre.
        case CmdPalette:                return "Palette dei comandi";
        default:                        return nullptr;
    }
}

// Il pannello dei valori di una colonna. I valori si contano **all'apertura** e
// non si riordinano mai: togliendone uno, le caselle non devono saltare sotto il
// puntatore. Per la stessa ragione l'ordine e' per frequenza decrescente, che non
// cambia mentre si spunta.
constexpr int kFilterAllId  = -1;
constexpr int kFilterNoneId = -2;

// Le voci del menu del tasto destro sull'intestazione — le operazioni sulla
// **colonna**, che il riferimento tiene separate dal filtro per valori.
constexpr int kColumnOpsFitId     = -1;
constexpr int kColumnOpsFitAllId  = -2;
constexpr int kColumnOpsCloseId   = -3;
constexpr int kColumnOpsResetId   = -4;
constexpr int kColumnOpsFactoryId = -5;
constexpr int kColumnOpsLeftId    = -6;
constexpr int kColumnOpsRightId   = -7;
// Le spunte di visibilita', una per colonna: l'id e' la base piu' l'indice, cosi' una
// riga sa dire **quale** colonna senza una tabella accanto.
constexpr int kColumnOpsColumnBase = 100;

// Il contenuto dei quattro segnaposto: l'id e' la base piu' l'indice della colonna da
// mostrare, con lo zero che vuol dire "vuota".
constexpr int kColumnOpsEmptyBase = 200;

// Un segnaposto si riconosce dalla **chiave**, non dall'indice: gli indici si contano e
// si sbagliano, le chiavi no.
bool IsEmptyColumn(int column)
{
    if (column < 0 || column >= kColumnCount)
        return false;
    const SortColumn which = kColumns[column].sort;
    return which >= SortColumn::Empty1 && which <= SortColumn::Empty4;
}

// Rimette le caselle al loro posto dopo una spunta, senza ricontare e senza
// riordinare: cambia solo il segno, e i valori restano quelli di quando il
// pannello si e' aperto.
void RefreshColumnMenuChecks(AppState& app)
{
    if (app.columnPanel < 0)
        return;
    for (GroupMenuItem& item : app.columnMenu.items)
    {
        if (item.id < 0)
            continue;
        const std::pair<std::string, int>& value = app.columnPanelValues[static_cast<size_t>(item.id)];
        const bool kept = app.columns[static_cast<size_t>(app.columnPanel)].excluded.count(value.first) == 0;
        item.label = (kept ? "[x] " : "[ ] ") + value.first;
    }
}

// The filter box sits under the list, the way the reference application puts it.
// It is the only text entry in the window, so it owns the keyboard whenever the
// filter is active.
// `ShowTooltips`: il nome per esteso e due righe di contesto, vicino al puntatore.
//
// Compare **dopo una pausa**, come i suggerimenti del sistema: il conto riparte
// quando il puntatore si muove, quindi chi sta scorrendo l'elenco non se li vede
// passare davanti. Non e' un menu: non prende clic, e non si puo' navigare.
//
// La regola per farlo tacere e' **una sola**: se qualcosa e' aperto sopra la lista,
// il suggerimento non si disegna. Prima era una fila di condizioni sciolte dentro
// `DrawTooltip`, e mancavano i **pannelli**: sul pannello delle proprieta' il nome
// del file finiva disegnato sopra le righe dei metadati, che li' sembra un difetto
// del pannello.
bool TooltipsAllowed(AppState& app)
{
    if (!app.showTooltips)
        return false;
    if (OpenMenu(app) != nullptr || app.contextMenu.open)
        return false;
    if (app.propertiesOpen)
        return false;
    // L'operazione in corso non e' piu' modale, quindi i suggerimenti tornano a
    // comparire anche mentre si copia: chi naviga durante una copia sta usando la
    // finestra, e un suggerimento e' parte del modo in cui la usa.
    return app.columnDrag < 0;
}

void DrawTooltip(AppState& app, const Layout& layout)
{
    if (app.hovered < 0 || !TooltipsAllowed(app))
        return;
    if (NowSeconds() - app.hoverStart < 0.55)
        return;
    if (app.hovered >= static_cast<int>(app.entries.size()))
    {
        // Un indice rimasto dall'elenco di prima, che nessuno ha ancora azzerato
        // (l'azzeramento e' in `LoadDirectory`): non si legge fuori dal vettore, e
        // intanto lo stato torna a "niente sotto il puntatore".
        app.hovered = -1;
        return;
    }

    // Il suggerimento parla della riga che si **vede**. Il filtro, i filtri di
    // colonna e un gruppo chiuso possono togliere la riga da sotto il puntatore
    // senza che il puntatore si muova, e allora resta li' il nome di prima: e' la
    // stessa bugia di `docs/02` §7.55, vista sul suggerimento invece che
    // sull'ispettore.
    bool onVisibleRow = false;
    for (const Row& row : app.rows)
    {
        if (!row.header && row.entry == app.hovered)
        {
            onVisibleRow = true;
            break;
        }
    }
    if (!onVisibleRow)
        return;

    const Entry& entry = app.entries[static_cast<size_t>(app.hovered)];
    const float scale = layout.scale;
    const float pad = 9.0f * scale;

    std::vector<std::string> lines;
    lines.push_back(entry.name);
    std::string second = entry.typeText;
    if (!entry.modifiedText.empty())
        second += "   " + entry.modifiedText;
    if (!entry.isDirectory && !entry.sizeText.empty())
        second += "   " + entry.sizeText;
    if (!second.empty())
        lines.push_back(second);
    if (entry.isDirectory)
    {
        DirStats stats;
        if (entry.location.empty() &&
            app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats) && !stats.partial)
            lines.push_back(HumanCount(stats.folders) +
                            (stats.folders == 1 ? Tr(" cartella   ", " folder   ")
                                                : Tr(" cartelle   ", " folders   ")) +
                            HumanCount(stats.files) +
                            (stats.files == 1 ? Tr(" file   ", " file   ")
                                              : Tr(" file   ", " files   ")) +
                            HumanBytes(stats.logicalBytes));
    }

    float width = 0.0f;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const float measured = i == 0 ? app.font.Measure(lines[i], scale)
                                      : app.smallFont.Measure(lines[i], scale);
        width = (std::max)(width, measured);
    }
    const float height = app.font.LineHeight() * scale + 3.0f * scale +
                         static_cast<float>(lines.size() - 1) * app.smallFont.LineHeight() * scale +
                         pad * 2.0f;

    float x = app.pointerX + 16.0f * scale;
    float y = app.pointerY + 20.0f * scale;
    x = (std::min)(x, static_cast<float>(app.viewportWidth) - width - pad * 2.0f - 8.0f * scale);
    y = (std::min)(y, static_cast<float>(app.viewportHeight) - height - 8.0f * scale);
    const Rect box{(std::max)(8.0f * scale, x), (std::max)(8.0f * scale, y),
                   width + pad * 2.0f, height};

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    r.Draw(UIDraw::Shadow(Rect{box.x + 2.0f * scale, box.y + 4.0f * scale, box.w, box.h},
                          t.shadow, 16.0f * scale, 6.0f * scale));
    r.Draw(UIDraw::Fill(box, t.panelBg, 6.0f * scale));
    r.Draw(UIDraw::Border(box, t.panelBorder, 1.0f, 6.0f * scale));

    float textY = box.y + pad;
    app.font.Draw(r, box.x + pad, textY, lines[0], t.text, scale);
    textY += app.font.LineHeight() * scale + 3.0f * scale;
    for (size_t i = 1; i < lines.size(); ++i)
    {
        app.smallFont.Draw(r, box.x + pad, textY, lines[i], t.textDim, scale);
        textY += app.smallFont.LineHeight() * scale;
    }
}

void DrawPropertiesPanel(AppState& app, const Layout& layout)
{
    if (!app.propertiesOpen)
        return;

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect panel = PropertiesPanelRect(app, layout);

    r.FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                    static_cast<float>(app.viewportHeight)}, Color{0.0f, 0.0f, 0.0f, 0.45f});
    r.Draw(UIDraw::Shadow(Rect{panel.x + 3.0f * scale, panel.y + 6.0f * scale, panel.w, panel.h},
                          t.shadow, 22.0f * scale, 10.0f * scale));
    r.Draw(UIDraw::Fill(panel, t.panelBg, 10.0f * scale));
    r.Draw(UIDraw::Border(panel, t.panelBorder, 1.0f, 10.0f * scale));

    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.y + 12.0f * scale,
                       ToUtf8(app.propertiesTitle), t.textDim, scale);
    app.font.Draw(r, panel.x + 16.0f * scale, panel.y + 34.0f * scale,
                  FitText(app.font, ToUtf8(app.propertiesSubtitle), scale, panel.w - 32.0f * scale),
                  t.text, scale);

    const float listTop    = panel.y + 66.0f * scale;
    const float listBottom = panel.bottom() - 36.0f * scale;
    const float rowHeight  = 22.0f * scale;
    r.FillRect(Rect{panel.x + 1.0f, listTop - 6.0f * scale, panel.w - 2.0f, 1.0f}, t.separator);

    const float content = static_cast<float>(app.propertiesRows.size()) * rowHeight;
    const float room    = listBottom - listTop;
    const float maxScroll = (std::max)(0.0f, content - room);
    app.propertiesScroll = (std::max)(0.0f, (std::min)(app.propertiesScroll, maxScroll));

    const int visible = (std::max)(1, static_cast<int>(room / rowHeight));
    int first = static_cast<int>(app.propertiesScroll / rowHeight);
    if (first > static_cast<int>(app.propertiesRows.size()) - visible)
        first = (std::max)(0, static_cast<int>(app.propertiesRows.size()) - visible);

    const float labelX = panel.x + 16.0f * scale;
    const float valueX = panel.x + 120.0f * scale;
    const float valueMax = panel.right() - valueX - 16.0f * scale;
    for (int i = first; i < static_cast<int>(app.propertiesRows.size()); ++i)
    {
        const float y = listTop + static_cast<float>(i - first) * rowHeight;
        if (y + rowHeight > listBottom)
            break;
        const InspectorRow& row = app.propertiesRows[static_cast<size_t>(i)];
        app.smallFont.Draw(r, labelX, y + 4.0f * scale,
                           FitText(app.smallFont, row.label, scale, 96.0f * scale), t.textDim,
                           scale);
        const std::string shown = row.tail
                                      ? FitTextTail(app.smallFont, row.value, scale, valueMax)
                                      : FitText(app.smallFont, row.value, scale, valueMax);
        app.smallFont.Draw(r, valueX, y + 4.0f * scale, shown, t.text, scale);
    }

    // La barra di scorrimento, quando le righe non ci stanno tutte.
    if (content > room)
    {
        const float barW = 4.0f * scale;
        const Rect track{panel.right() - 10.0f * scale, listTop, barW, room};
        r.Draw(UIDraw::Fill(track, t.separator, barW * 0.5f));
        const float thumbH = (std::max)(24.0f * scale, room * room / content);
        const float offset = (maxScroll > 0.0f) ? (app.propertiesScroll / maxScroll) : 0.0f;
        r.Draw(UIDraw::Fill(Rect{track.x, track.y + (room - thumbH) * offset, barW, thumbH},
                            t.scrollThumb, barW * 0.5f));
    }

    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.bottom() - 22.0f * scale,
                       Tr("Su/Giù scorrono   Esc chiude", "Up/Down scroll   Esc closes"),
                       t.textDim, scale);
    const Rect windowsButton = PropertiesWindowsButton(app, layout, panel);
    r.Draw(UIDraw::Fill(windowsButton, t.rowHover, 4.0f * scale));
    r.Draw(UIDraw::Border(windowsButton, t.panelBorder, 1.0f, 4.0f * scale));
    app.smallFont.Draw(r, windowsButton.x + 10.0f * scale,
                       TextY(layout, windowsButton, app.smallFont),
                       Tr("Proprietà di Windows...", "Windows properties..."), t.text, scale);
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
    {"View",    "Command palette",             CmdPalette},
    // La lingua: due voci, non un interruttore. Con due si vede sempre quale e'
    // accesa, e non serve indovinare lo stato di partenza.
    {"Language", "Language: Italian",           CmdLanguageItalian},
    {"Language", "Language: English",           CmdLanguageEnglish},
    {"Options",  "Where the settings file is",  CmdShowSettingsPath},
    // Gli aggiornamenti: **esplicito**, e senza tasto di partenza. Una richiesta a
    // GitHub e' telemetria di fatto, e non deve partire ne' da sola ne' per sbaglio.
    {"Updates", "Check for updates",            CmdCheckUpdates},
    // La pagina di avvio: due voci, perche' "quale cartella all'avvio" e' una
    // domanda e le risposte sensate sono due.
    {"Startup",  "Startup: the open folder",    CmdStartupCurrentFolder},
    {"Startup",  "Startup: This PC",            CmdStartupThisPc},
    // L'aspetto: due tavolozze e "segui il sistema", le tre chiavi dello spec.
    {"Appearance", "Color scheme: dark",          CmdColorSchemeDark},
    {"Appearance", "Color scheme: light",         CmdColorSchemeLight},
    {"Appearance", "Color scheme: follow the system", CmdSystemColorScheme},
    {"Tabs",    "New tab",                     CmdNewTab},
    {"Tabs",    "Close tab and its children",   CmdCloseInTree},
    {"Tabs",    "Close the whole tab tree",     CmdCloseAllInTree},
    {"Tabs",    "Detach tab from its parent",   CmdDetachTab},
    {"Tabs",    "Close current tab",           CmdCloseTab},
    {"Tabs",    "Next tab",                    CmdNextTab},
    {"Tabs",    "Previous tab",                CmdPrevTab},
    // I due pannelli. Stanno con le schede perche' sono la stessa domanda — quante
    // viste della stessa finestra — e non hanno un comando inverso: la chiusura di un
    // pannello e' la tappa dopo.
    {"Tabs",    "Split right",                  CmdSplitRight},
    {"Tabs",    "Split bottom",                 CmdSplitBottom},
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
    {"View",    "Hover to select",              CmdToggleHoverToSelect},
    {"View",    "Underline on hover",           CmdToggleUnderlineOnHover},
    {"View",    "Show tooltips",                CmdToggleTooltips},
    {"View",    "Highlight recents",            CmdToggleShowRecents},
    {"View",    "Recents on top",                CmdToggleSortRecents},
    {"View",    "Subfolders in the list",         CmdToggleSubfolders},
    {"View",    "Save layout",                     CmdSaveLayout},
    {"View",    "Load layout",                     CmdLoadLayout},
    {"View",    "Forget layout",                   CmdForgetLayout},
    {"Startup", "Remember open tabs",              CmdToggleRememberTabs},
    // "Interaction" e' il nome del gruppo nella tabella delle chiavi. Sono cinque
    // chiavi, non cinque funzioni: nessuna fa niente da sola, cambiano *come* si
    // apre e si sceglie.
    {"Interaction", "Single click opens",         CmdSingleClick},
    {"Interaction", "Single click: folders only", CmdSingleClickFoldersOnly},
    {"Interaction", "Act on press",               CmdActOnPress},
    {"Interaction", "Double click on empty space goes up", CmdDoubleClickToParent},
    {"Interaction", "Show checkboxes",            CmdShowCheckboxes},
    {"View",    "Zoom in",                     CmdZoomIn},
    // Le opzioni del visualizzatore di testo. Nella tabella delle chiavi
    // dell'originale sono `InspectorShowWhitespace`, `InspectorHideLineNumbers`,
    // `InspectorHideMinimap`, `InspectorWordWrap`, `InspectorZoomLevel`: le stesse
    // cose, con i nomi che ha scelto chi le ha scritte la' dentro.
    {"Inspector", "Text: show whitespace",       CmdTextWhitespace},
    {"Inspector", "Text: toggle line numbers",   CmdTextLineNumbers},
    {"Inspector", "Text: toggle minimap",        CmdTextMinimap},
    {"Inspector", "Text: toggle word wrap",      CmdTextWrap},
    {"Inspector", "Text: zoom in",               CmdTextZoomIn},
    {"Inspector", "Text: zoom out",              CmdTextZoomOut},
    {"Inspector", "Image: zoom in",              CmdImageZoomIn},
    {"Inspector", "Image: zoom out",             CmdImageZoomOut},
    {"Inspector", "Image: fit to panel",         CmdImageZoomFit},
    {"Inspector", "Image: toggle background grid", CmdToggleImageGrid},
    {"View",    "Zoom out",                    CmdZoomOut},
    {"View",    "Reset zoom",                  CmdZoomReset},
    {"View",    "Toggle command details",      CmdToggleDetails},
    {"Sort",    "Sort by name",                CmdSortName},
    {"Sort",    "Sort by type",                CmdSortType},
    {"Sort",    "Sort by date modified",       CmdSortModified},
    {"Sort",    "Sort by date created",        CmdSortCreated},
    {"Sort",    "Sort by date accessed",       CmdSortAccessed},
    {"Sort",    "Sort by total items",         CmdSortTotal},
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
    {"Options", "Open options",                 CmdOpenOptions},
    {"Options", "Key map: edit the keys",        CmdEditKeyMap},
    // Le due domande del fissaggio stanno con i **posti**, che e' cio' di cui
    // parlano: nella palette cadono nella stessa banda delle cartelle note.
    {"Places",  "Pin this folder to...",          CmdPinTo},
    {"View",    "Save this folder's layout",     CmdSaveFolderLayout},
    {"View",    "Load this folder's layout",     CmdLoadFolderLayout},
    {"View",    "Forget this folder's layout",   CmdForgetFolderLayout},
    {"Places",  "Unpin this folder from...",      CmdUnpin},
    {"File",    "Properties",                   CmdProperties},
    {"File",    "Windows properties",           CmdPropertiesWindows},
};

const char* CommandLabelIt(CommandId id);
// `RunCommand` arriva da `app_actions.h` e `ContextMenuClose` da
// `view_context_menu.h`: il confine e' scritto li', e due dichiarazioni sono il modo
// di farle divergere.
// `ReportStatus`, `RememberSettings`, `ClosePinMenu` e l'editor della mappa non si
// dichiarano qui: arrivano dagli header, perche' li' e' dove il confine e' scritto
// (`view_statusbar.h`, `app_actions.h`). Una dichiarazione in due posti e' il modo
// di farli divergere.

// --- l'editor della mappa dei tasti ------------------------------------------
//
// La mappa e' un **dato** (`src/keymap.*`): una tabella `comando -> tasto`. Questo e'
// il posto da cui si cambia, ed e' l'ultima cosa che mancava perche' la mappa
// servisse a qualcosa: `Rebind`, `Clear` e `Reset` erano scritti e non li chiamava
// nessuno.
//
// Una riga per **comando**, non una per tasto: anche i comandi che un tasto non ce
// l'hanno devono poterlo ricevere, ed e' il motivo per cui l'elenco si costruisce
// dalla tabella dei comandi invece che dalle righe della mappa. Il tasto sta a
// destra, scritto **dalla mappa** (`LabelFor`): quella riga non puo' dire una cosa
// diversa da quella che il tasto fa.
//
// La mappa resta **in memoria**, per la sessione: `docs/04` dice cosi' per M9, e
// `docs/06` vieta di scrivere un file di impostazioni di nascosto. Si vede qui, e si
// vede cosa non c'e'.

// L'id della riga dei tasti di partenza. Negativo, come i due id del filtro di una
// colonna: gli id veri di questo elenco sono `CommandId`, che sono tutti >= 0.
constexpr int kKeyMapResetId = -2;

// L'etichetta di un comando come la legge chi la usa. La tabella la scrive in inglese
// — che e' anche il nome della chiave — e in italiano la da' `CommandLabelIt`. La
// regola stava dentro la palette; alla seconda volta che serviva e' diventata una
// funzione, invece di due righe uguali che possono divergere.
std::string CommandLabel(const CommandDef& command)
{
    const char* italian = CommandLabelIt(command.id);
    if (gLanguage != Language::English && italian != nullptr)
        return italian;
    return command.label;
}


// Il cursore sul comando: l'elenco si rifa' dopo ogni rimappatura e dopo la cattura,
// e senza questo si tornerebbe in cima. Mentre si aspetta un tasto conta due volte,
// perche' il messaggio **nomina** un comando: se il cursore stesse su un altro, la
// riga illuminata direbbe una cosa diversa da quella che si sta scrivendo.
void SelectKeyMapCommand(AppState& app, int command)
{
    for (int i = 0; i < static_cast<int>(app.keyMap.rows.size()); ++i)
    {
        const GroupMenuRow& row = app.keyMap.rows[static_cast<size_t>(i)];
        if (!row.header && app.keyMap.items[static_cast<size_t>(row.item)].id == command)
        {
            app.keyMap.selected = i;
            return;
        }
    }
}


// I tre comandi che **aprono questa lista**: togliere il loro ultimo tasto vuol dire
// non poterci piu' rientrare da questa sessione. La mappa e' di sessione, quindi un
// riavvio rimette i tasti di partenza — ma per rientrare bisognerebbe chiudere
// l'applicazione, e questa e' una trappola che si tende da soli senza accorgersene.
// Non si vieta niente: la mappa e' la fonte unica e vale per tutti i comandi. Si
// **dice**, nella riga in fondo dove sta gia' l'altro avviso.
bool OpensThisList(int command)
{
    return command == CmdOpenOptions || command == CmdPalette || command == CmdEditKeyMap;
}


void EndKeyCapture(AppState& app, int command)
{
    app.keyCaptureCommand = -1;
    BuildKeyMapMenu(app, command);
}

bool IsBookmarked(const AppState& app)
{
    return std::find(app.bookmarks.begin(), app.bookmarks.end(), app.directory) != app.bookmarks.end();
}


// --- il fissaggio: dove si puo' mettere una cartella -------------------------
//
// `docs/04` M6, deciso dall'autore il 2026-10-16: **qualunque sezione si puo'
// pinnare**, e non e' "aggiungi ai segnalibri" — e' "fissa questa cartella dove dico
// io". Le destinazioni sono le due sezioni della barra laterale che **accettano una
// riga**: `STORAGE` sono i volumi e `PLACES` le cartelle standard, e tutte e due le
// legge la shell, che non ci lascia scrivere. Le cartelle standard stanno gia' la' —
// `PLACES` di sola lettura e' anche il posto giusto per loro — e percio' "fissa
// Desktop" vuol dire fissarlo qui, fra i nostri.
//
// I fissaggi sono **nostri** e stanno in memoria, per la sessione. L'accesso rapido di
// Windows non ha un'API per aggiungerci una voce: i suoi fissaggi stanno in un *jump
// list*, e l'unico modo di toccarlo scrive un file **della shell**. Quindi le nostre
// righe si mostrano accanto alle sue invece di finire dentro le sue — decisione (a) di
// `docs/04` — e non si prova la (b), che lascerebbe una traccia nella barra laterale di
// chi lavora. Quello che vive in memoria lo dice anche `docs/06`: `settings.ini` non
// contiene percorsi, e neanche questo elenco ci finisce.
enum class PinTarget { QuickAccess, Bookmarks };

// Le destinazioni, in ordine: e' anche l'ordine delle righe del pannello, e l'id di
// una riga e' il suo **indice** — cosi' una destinazione in piu' si aggiunge in un
// posto solo.
constexpr int kPinTargetCount = 2;

const char* PinTargetLabel(PinTarget target)
{
    return target == PinTarget::QuickAccess ? Tr("Accesso rapido", "Quick access")
                                            : Tr("Segnalibri", "Bookmarks");
}

PinTarget PinTargetAt(int index)
{
    return index == 0 ? PinTarget::QuickAccess : PinTarget::Bookmarks;
}

// L'elenco di una destinazione. E' una funzione sola perche' le due destinazioni
// hanno due elenchi di tipo diverso ma la stessa forma, e chi chiede — fissare,
// togliere, sapere se c'e' gia' — non deve sapere quale sta guardando.
std::vector<std::wstring>& PinList(AppState& app, PinTarget target)
{
    return target == PinTarget::QuickAccess ? app.pins : app.bookmarks;
}

bool PinnedIn(const AppState& app, PinTarget target, const std::wstring& path)
{
    if (path.empty())
        return false;
    const std::vector<std::wstring>& list =
        (target == PinTarget::QuickAccess) ? app.pins : app.bookmarks;
    return std::find(list.begin(), list.end(), path) != list.end();
}

// --- il pannello dell'operazione in corso ------------------------------------
//
// Una copia da 5 GB non e' un'attesa: e' qualcosa che si guarda e che si puo'
// fermare. Prima questo non c'era — l'operazione girava sincrona sul thread della
// finestra, che restava ferma — e le tre cose che mancavano erano un conto, un
// numero che si muove e un pulsante. Il conto e il thread stanno in `FileOps::Job`;
// qui c'e' il pannello, e l'esito che si raccoglie quando l'operazione finisce.

// Il pannello compare solo se l'operazione sta ancora girando dopo un momento: una
// copia di due file piccoli finisce prima, e un pannello che compare per tre
// fotogrammi e' peggio di nessun pannello. La soglia e' quella che usa anche Esplora
// risorse per la sua finestra di stato.
constexpr double kOperationPanelDelay = 0.35;

// Come si chiama l'operazione in corso.
std::wstring OperationTitle(FileOps::Action action)
{
    switch (action)
    {
        case FileOps::Action::Copy:    return TrW(L"Copia in corso", L"Copying");
        case FileOps::Action::Move:    return TrW(L"Spostamento in corso", L"Moving");
        case FileOps::Action::Recycle: return TrW(L"Eliminazione in corso", L"Deleting");
        default:                       return TrW(L"Eliminazione definitiva",
                                                  L"Deleting permanently");
    }
}

// La riga sotto il titolo: dove stanno andando le cose, o quante se ne buttano via —
// e quante aspettano il loro turno. La coda si dice **qui** e non solo una volta nella
// barra di stato: il pannello mostra una operazione sola, e le altre non si vedono da
// nessun'altra parte.
std::wstring OperationSubtitle(const AppState& app)
{
    const FileOps::Job& job = *app.operation;

    std::wstring text;
    switch (job.action())
    {
        case FileOps::Action::Copy:
        case FileOps::Action::Move:
            text = TrW(L"in ", L"into ") + LeafName(job.destination());
            break;
        default:
            text = CountItems(job.sources().size());
            break;
    }

    if (!app.operationQueue.empty())
        text += TrW(L" — ", L" — ") + CountItems(app.operationQueue.size()) +
                TrW(L" in coda", L" queued");
    return text;
}

void DrawOperationPanel(AppState& app, const Layout& layout)
{
    if (!OperationPanelVisible(app))
        return;

    const FileOps::Progress progress = app.operation->Snapshot();

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect panel = OperationPanelRect(app, layout);

    r.FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                    static_cast<float>(app.viewportHeight)}, Color{0.0f, 0.0f, 0.0f, 0.45f});
    r.Draw(UIDraw::Shadow(Rect{panel.x + 3.0f * scale, panel.y + 6.0f * scale, panel.w, panel.h},
                          t.shadow, 22.0f * scale, 10.0f * scale));
    r.Draw(UIDraw::Fill(panel, t.panelBg, 10.0f * scale));
    r.Draw(UIDraw::Border(panel, t.panelBorder, 1.0f, 10.0f * scale));

    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.y + 12.0f * scale,
                       ToUtf8(OperationTitle(app.operation->action())), t.textDim, scale);
    app.font.Draw(r, panel.x + 16.0f * scale, panel.y + 34.0f * scale,
                  FitText(app.font, ToUtf8(OperationSubtitle(app)), scale,
                          panel.w - 32.0f * scale),
                  t.text, scale);

    // La voce in corso, e sotto la barra.
    const float nameY = panel.y + 66.0f * scale;
    app.smallFont.Draw(r, panel.x + 16.0f * scale, nameY,
                       FitText(app.smallFont, ToUtf8(progress.currentItem), scale,
                               panel.w - 32.0f * scale),
                       t.text, scale);

    // La barra: la frazione del conto. `volumeTrack` e' il token `Progress` dello
    // spec, e il suo primo lettore e' stata la barra di un disco — questo e' il
    // secondo, e per questo non e' piu' un token senza lettore.
    const Rect track{panel.x + 16.0f * scale, nameY + 24.0f * scale, panel.w - 32.0f * scale,
                     8.0f * scale};
    r.Draw(UIDraw::Fill(track, t.volumeTrack, 4.0f * scale));

    if (progress.preparing)
    {
        app.smallFont.Draw(r, panel.x + 16.0f * scale, track.bottom() + 8.0f * scale,
                           Tr("Preparazione...", "Preparing..."), t.textDim, scale);
    }
    else
    {
        const uint64_t total = progress.totalBytes;
        const double fraction = total > 0
                                    ? static_cast<double>(progress.doneBytes) / static_cast<double>(total)
                                    : 0.0;
        const float filled = static_cast<float>((std::max)(0.0, (std::min)(1.0, fraction)));
        if (filled > 0.0f)
            r.Draw(UIDraw::Fill(Rect{track.x, track.y, track.w * filled, track.h}, t.accent,
                                4.0f * scale));

        // I byte, e quante voci. "piu' di" quando la misura non e' arrivata in fondo:
        // un numero esatto che non lo e' sarebbe peggio di un numero dichiarato
        // approssimativo.
        std::string bytes;
        if (progress.totalBytes > 0)
        {
            bytes = (progress.measured ? std::string() : std::string("> ")) +
                    HumanBytes(progress.doneBytes) + " / " + HumanBytes(progress.totalBytes) +
                    " (" + std::to_string(static_cast<int>(fraction * 100.0)) + "%)";
        }

        std::string counts;
        if (progress.totalItems > 0)
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), Tr("%d di %d voci", "%d of %d items"),
                          progress.doneItems, progress.totalItems);
            counts = buffer;
        }
        else if (progress.doneItems > 0)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), Tr("%d voci", "%d items"), progress.doneItems);
            counts = buffer;
        }

        app.smallFont.Draw(r, panel.x + 16.0f * scale, track.bottom() + 8.0f * scale, bytes,
                           t.text, scale);
        app.smallFont.Draw(r, panel.right() - 16.0f * scale - app.smallFont.Measure(counts, scale),
                           track.bottom() + 8.0f * scale, counts, t.textDim, scale);
    }

    // Il pulsante, e cosa fa la tastiera. Un annullamento chiesto si dice: finche' la
    // shell non ha davvero smesso, chi ha premuto deve sapere che e' stato ascoltato
    // e che ci vuole un momento.
    const Rect cancel = OperationCancelButton(app, layout, panel);
    r.Draw(UIDraw::Fill(cancel, t.rowHover, 4.0f * scale));
    r.Draw(UIDraw::Border(cancel, t.panelBorder, 1.0f, 4.0f * scale));
    app.smallFont.Draw(r, cancel.x + 12.0f * scale, TextY(layout, cancel, app.smallFont),
                       Tr("Annulla", "Cancel"), t.text, scale);

    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.bottom() - 26.0f * scale,
                       progress.cancelRequested ? Tr("Annullamento...", "Cancelling...")
                                                : Tr("Esc annulla", "Esc cancels"),
                       progress.cancelRequested ? t.accent : t.textDim, scale);
}


// L'operazione e' finita: si raccoglie l'esito, si aggiorna la vista e si dice com'e'
// andata. E' l'unico posto in cui si legge il risultato, ed e' il motivo per cui le
// tre operazioni lunghe non hanno piu' un finale ciascuna.
//
// `FinishOperationNow` fa il lavoro ed esce dalle sue strade come vuole;
// `FinishOperation` e' quella che chiama il ciclo principale, e dopo aggiunge la sola
// cosa che manca: se c'e' una richiesta in coda, adesso tocca a lei. L'avanzamento sta
// qui e non nei singoli esiti perche' cosi' non si puo' dimenticare in uno dei rami.
void FinishOperationNow(AppState& app)
{
    std::unique_ptr<FileOps::Job> job = std::move(app.operation);
    if (job == nullptr)
        return;

    const FileOps::Action action = job->action();
    const std::vector<std::wstring> sources = job->sources();
    const std::wstring destination = job->destination();
    const OperationSource source = app.operationSource;
    const float scroll = app.scroll;

    // Adesso si puo' navigare mentre un'operazione gira: la vista si rifa' solo se la
    // cartella sullo schermo e' ancora quella da cui si e' partiti. Altrimenti si
    // riscriverebbe un elenco che non c'entra, con una selezione che non gli
    // appartiene.
    const bool StillOnScreen = (app.directory == app.operationOrigin);

    std::vector<std::wstring> created;
    std::wstring error;
    const FileOps::Result result = job->Finish(&created, &error);
    job.reset();

    app.operationSource = OperationSource::None;

    const bool copied  = action == FileOps::Action::Copy;
    const bool discarded = action == FileOps::Action::Recycle ||
                           action == FileOps::Action::DeleteForever;

    if (result == FileOps::Result::Failed)
    {
        if (source == OperationSource::Drop)
        {
            app.dropReported = true;
            DiagLog("  caduta non riuscita: " + ToUtf8(error));
        }
        ReportStatus(app,
                     (discarded ? TrW(L"Eliminazione non riuscita: ", L"Delete failed: ")
                                : (copied ? TrW(L"Copia non riuscita: ", L"Copy failed: ")
                                          : TrW(L"Spostamento non riuscito: ", L"Move failed: "))) +
                         error);
        return;
    }

    if (result == FileOps::Result::Cancelled)
    {
        // Un annullamento non e' un guasto, ma non e' nemmeno "fatto": un gesto che
        // finisce senza dire niente e' indistinguibile da un difetto.
        if (source == OperationSource::Drop)
            app.dropReported = true;
        std::wstring cancelled = discarded ? TrW(L"Eliminazione annullata", L"Delete cancelled")
                                           : TrW(L"Operazione annullata", L"Operation cancelled");
        if (app.operationQueueDropped > 0)
        {
            const bool one = app.operationQueueDropped == 1;
            cancelled += TrW(L" — ", L" — ") + CountItems(app.operationQueueDropped) +
                         (one ? TrW(L" in coda annullato", L" queued cancelled")
                              : TrW(L" in coda annullati", L" queued cancelled"));
            app.operationQueueDropped = 0;
        }
        ReportStatus(app, cancelled);
        // Quello che era partito prima dell'annullamento c'e' ancora: la vista si
        // rifa', tenendo la selezione dove ancora esiste.
        if (StillOnScreen)
            ReloadKeeping(app, app.selection, app.cursor, scroll);
        return;
    }

    if (source == OperationSource::Drop)
    {
        app.dropReported = true;

        // La vista si rifa' sempre: uno spostamento toglie gli originali da qui, e una
        // copia puo' aver aggiunto qualcosa se la destinazione e' la cartella aperta.
        // Si seleziona solo cio' che e' arrivato *qui*, perche' quello che e' finito
        // altrove non si vede e non si potrebbe evidenziare.
        std::unordered_set<std::wstring> keep;
        std::wstring landed;
        if (!discarded && _wcsicmp(destination.c_str(), app.directory.c_str()) == 0)
        {
            for (const std::wstring& path : created)
            {
                const std::wstring name = LeafName(path);
                keep.insert(name);
                if (landed.empty())
                    landed = name;
            }
        }
        if (StillOnScreen)
            ReloadKeeping(app, keep, landed, scroll);

        // La caduta puo' aver scritto nell'**altro** pannello, se e' li' che e' stata
        // mollata. Quel pannello tiene le sue voci nel tab e non se ne accorgerebbe:
        // resterebbe a mostrare un elenco dove i file appena arrivati non ci sono.
        if (!discarded)
            ReloadPaneShowing(app, destination);

        DiagLog("  esito della caduta: " +
                std::string(discarded ? "eliminazione" : (copied ? "copia" : "spostamento")) +
                " di " + std::to_string(sources.size()) + " elementi" +
                (discarded ? std::string()
                           : ", prodotti " + std::to_string(created.size())));

        const bool one = sources.size() == 1;
        const std::wstring verb =
            discarded ? TrW(one ? L"Buttato via " : L"Buttati via ", L"Thrown away ")
            : copied  ? TrW(one ? L"Copiato " : L"Copiati ", L"Copied ")
                      : TrW(one ? L"Spostato " : L"Spostati ", L"Moved ");
        std::wstring outcome = verb + CountItems(sources.size());
        outcome += discarded ? TrW(L" nel cestino", L" to the recycle bin")
                             : TrW(L" in ", L" into ") + LeafName(destination);
        ReportStatus(app, outcome);
        return;
    }

    if (discarded)
    {
        if (StillOnScreen)
            ReloadKeeping(app, /*keep=*/{}, /*preferCursor=*/{}, scroll);
        ReportStatus(app,
                     action == FileOps::Action::DeleteForever
                         ? VerbItems(sources.size(), L"Eliminato definitivamente ",
                                     L"Eliminati definitivamente ", L"Deleted permanently ")
                         : TrW(L"Nel cestino: ", L"Moved to the recycle bin: ") +
                               CountItems(sources.size()));
        return;
    }

    // Un incolla. Un taglio si consuma con l'incolla: lasciarlo nella clipboard
    // farebbe spostare gli stessi file una seconda volta al prossimo Ctrl+V.
    if (action == FileOps::Action::Move)
        FileOps::ClearClipboard(app.window);

    // Si seleziona cio' che la shell ha DAVVERO creato. Con un conflitto di nome il
    // nome finale non e' quello di partenza, e selezionare quello di partenza metteva
    // il cursore sull'originale: un Canc subito dopo cancellava la cosa sbagliata.
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
        for (const std::wstring& path : sources)
            keep.insert(LeafName(path));
    }

    if (StillOnScreen)
        ReloadKeeping(app, keep, landed, scroll);
    ReportStatus(app, VerbItems(sources.size(),
                                action == FileOps::Action::Move ? L"Spostato " : L"Incollato ",
                                action == FileOps::Action::Move ? L"Spostati " : L"Incollati ",
                                action == FileOps::Action::Move ? L"Moved " : L"Pasted "));
}

void FinishOperation(AppState& app)
{
    FinishOperationNow(app);
    StartNextOperation(app);
}

// --- gli aggiornamenti -------------------------------------------------------
//
// L'unica cosa che questa applicazione manda in rete, e parte **solo** dal comando
// esplicito `Controlla aggiornamenti` (`docs/06`): nessun timer, nessun controllo
// all'avvio. Il lavoro si guarda nella sua finestra (`view_update.*`), che legge
// `Progress()` mentre disegna; qui resta l'esito, che si legge **una volta sola**.
void FinishUpdate(AppState& app)
{
    std::unique_ptr<Update::Job> job = std::move(app.updateJob);
    if (job == nullptr)
        return;

    Update::ReleaseInfo release;
    std::wstring error;
    app.updateResult = job->Finish(&release, &error);
    job.reset();

    app.updateDone       = true;
    app.updateVersion    = release.version;
    app.updateSetupPath  = release.setupPath;
    app.updateError      = error;
    app.updateInstalling = false;

    // La finestra resta aperta: e' il posto dove l'esito si legge e dove si sceglie se
    // installarlo adesso. Prima si chiudeva il programma da solo, e chi lo guardava
    // leggeva un guasto (`docs/04` M13).
    app.updateDialogOpen = true;

    switch (app.updateResult)
    {
        case Update::Result::Ready:
            DiagLog("--- aggiornamenti: setup verificato, in attesa del riavvio");
            break;
        case Update::Result::Failed:
            DiagLog("--- aggiornamenti: " + ToUtf8(error));
            break;
        default:
            break;
    }
}



// Un cambiamento visto da fuori: la cartella aperta toccata da un altro programma.
//
// Il numero che si vede di una cartella e' **ricorsivo**, quindi un file cambiato
// in fondo rende vecchi i numeri di tutte le cartelle che lo contengono: si
// invalida il percorso cambiato e i suoi genitori. Le **righe** invece cambiano
// solo se il cambiamento e' di nome — una voce creata, cancellata, rinominata —
// perche' un file modificato resta la stessa riga con lo stesso nome: ricaricare
// l'elenco per una data di modifica vorrebbe dire far perdere il posto a chi sta
// lavorando, e per niente.
void ApplyExternalChange(AppState& app)
{
    std::vector<DirectoryWatcher::Change> changes;
    if (!app.watcher.Take(&changes))
        return;

    bool listingChanged = false;
    for (const DirectoryWatcher::Change& change : changes)
    {
        app.stats.InvalidateIncludingParents(change.path);
        if (DirectoryWatcher::ChangesListing(change.action))
            listingChanged = true;
    }

    DiagLog("--- " + DiagClock() + " cambiamento esterno: " + std::to_string(changes.size()) +
            " voci, righe da ricaricare: " + (listingChanged ? "si" : "no"));

    if (listingChanged)
        ReloadKeeping(app, app.selection, app.cursor, app.scroll);
}

// --- i due pannelli ---------------------------------------------------------
//
// Il divisore, e basta. La testata del pannello inattivo con il percorso non c'e'
// piu': al suo posto c'e' la fila delle schede di **ogni** pannello
// (`DrawPaneTabs`, in `view_titlebar.cpp`), che dice le stesse cose e in piu' si
// clicca.

// Il divisore: la striscia che si prende col mouse. Si accende sotto il puntatore e
// mentre la si trascina — senza, e' sei pixel di sfondo e nessuno sa che ci sono.
void DrawSplitter(AppState& app)
{
    if (app.panes.size() < 2)
        return;

    const Rect strip = SplitterRect(app);
    const bool hot =
        app.splitDrag >= 0 ||
        strip.Contains(app.pointerX, app.pointerY);
    app.renderer->FillRect(strip, hot ? app.theme.textDim.WithAlpha(0.55f) : app.theme.separator);
}

void DrawFrame(AppState& app, const Layout& layout)
{
    app.clickTargets.clear();


    app.renderer->FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                               static_cast<float>(app.viewportHeight)}, app.theme.windowBg);

    // `RoundCorners`: uno stato di disegno per tutto il fotogramma. Le forme che
    // usano il raggio come spessore non vengono toccate (vedi `AppendQuad`).
    app.renderer->SetCornerScale(app.roundCorners ? 1.0f : 0.0f);

    // `InactivePanelContentOpacity`: il pannello senza fuoco si attenua. A 1 non
    // cambia niente, ed e' il valore di partenza.
    const auto panelOpacity = [&app](AppState::FocusArea area)
    {
        return (app.inactiveOpacity >= 0.999f || app.focusArea == area) ? 1.0f
                                                                      : app.inactiveOpacity;
    };

    DrawTitleBar(app, layout);
    DrawToolbar(app, layout, LocationLabel(app, app.directory));
    app.renderer->SetOpacity(panelOpacity(AppState::FocusArea::Sidebar));
    DrawSidebar(app, layout);
    app.renderer->SetOpacity(1.0f);

    // --- i pannelli ----------------------------------------------------------
    //
    // Con un pannello solo questo ciclo fa un giro e disegna lo stato vivo: la finestra
    // di prima, che non sa di essere un caso particolare.
    //
    // Con due, il pannello **attivo** e' lo stato vivo; l'altro scambia lo stato col suo
    // tab attivo, si disegna, e riscambia. Lo scambio e' di campi e non di vettori
    // (`SwapLiveWithPane`), quindi un fotogramma a due pannelli costa quanto uno a uno —
    // ed e' la ragione per cui non esiste una seconda copia della lista.
    const int paneCount = static_cast<int>(app.panes.size());
    for (int pane = 0; pane < paneCount; ++pane)
    {
        const bool active = (pane == app.activePane);
        if (!active)
        {
            SwapLiveWithPane(app, pane);
            // Le righe del pannello che non si sta guardando si costruiscono **mentre**
            // e' lui in vita, e la prima volta che servono: dopo restano sue
            // (`Pane::view`), e tornare a guardarlo non costa una rilettura.
            if (app.viewDirty)
                RebuildView(app);
        }

        // Il pannello attivo usa il layout che il fotogramma ha gia' calcolato — e'
        // `ComputeLayout`, cioe' il suo. Gli altri se lo fanno dare.
        const Layout paneLayout = active ? layout : PaneLayout(app, pane);
        float columnX[kColumnCount] = {};

        // La fila delle schede **di questo** pannello, sopra il suo riquadro. Con un
        // pannello solo la striscia e' vuota e la fila resta quella della barra del
        // titolo: questa chiamata non disegna niente e l'interfaccia non si sposta.
        DrawPaneTabs(app, paneLayout, pane);

        DrawHeader(app, paneLayout, columnX);

        // La lista disegna **solo dentro il suo rettangolo**: il ritaglio lo porta il
        // vertice di ogni cella, e a tagliare e' il pixel shader. Il controllo che salta
        // le righe fuori dal riquadro non basta: salta quelle che stanno fuori tutte
        // intere, non taglia quelle a meta' — e una cella di griglia e' alta quanto una
        // miniatura, quindi il pezzo che esce dal bordo finisce **sopra la barra del
        // titolo**. Si vedeva: quattro icone disegnate sopra le schede.
        //
        // Non serve piu' chiudere e riaprire il lotto attorno al ritaglio: era il prezzo
        // dichiarato in `AGENTS.md` §8 (tre disegni per fotogramma invece di uno, cinque
        // con il visualizzatore di testo), ed era il debito che questo lavoro salda.
        app.renderer->SetClip(paneLayout.list);
        app.renderer->SetOpacity(panelOpacity(AppState::FocusArea::List));
        DrawList(app, paneLayout, columnX);
        app.renderer->SetOpacity(1.0f);
        app.renderer->ClearClip();

        // The rubber band is drawn over the rows it is about to select, and only over
        // the list: it is clamped to that region so dragging past the edge does not
        // look like it is selecting the sidebar. Del pannello attivo: si trascina dove
        // si sta guardando, e cambiare pannello durante il gesto lo interromperebbe.
        if (active && app.rectSelecting)
        {
            const float x0 = (std::min)(app.rectX0, app.rectX1);
            const float y0 = (std::min)(app.rectY0, app.rectY1);
            const Rect band{x0, y0, std::fabs(app.rectX1 - app.rectX0),
                            std::fabs(app.rectY1 - app.rectY0)};
            app.renderer->FillRect(band, app.theme.accent.WithAlpha(0.18f), 2.0f * layout.scale);
            app.renderer->BorderRect(band, app.theme.accent.WithAlpha(0.7f), 1.0f,
                                     2.0f * layout.scale);
        }

        DrawFilterBar(app, paneLayout);

        if (!active)
        {
            // Lo stato si rimette com'era **dopo** averlo disegnato tutto: le schede
            // leggono la cartella del pannello, e da qui in poi lo stato vivo e' di
            // nuovo quello del pannello attivo.
            SwapLiveWithPane(app, pane);
        }
    }

    // Il divisore sta sopra i due pannelli: e' la striscia di sei pixel che li separa, e
    // sotto il puntatore si accende per dire che si puo' prendere.
    DrawSplitter(app);

    app.renderer->SetOpacity(panelOpacity(AppState::FocusArea::Inspector));
    DrawInspector(app, layout);
    app.renderer->SetOpacity(1.0f);
    DrawStatusBar(app, layout);

    // Last, so they cover everything and their click targets are the first ones
    // HitTest finds (it walks the list backwards). At most one is open.
    GroupMenuDraw(app, layout, app.palette);
    GroupMenuDraw(app, layout, app.goTo);
    // Il pannello dei valori sta attaccato all'intestazione da cui e' nato, e si
    // disegna per ultimo: e' l'unico che deve coprire la lista.
    GroupMenuDraw(app, layout, app.columnMenu);
    // Il menu del tasto destro sull'intestazione: le operazioni sulla colonna. Sta
    // dove sta quello dei valori, perche' e' lo stesso genere di pannello — ancorato
    // all'intestazione da cui e' nato — e i due non sono mai aperti insieme.
    GroupMenuDraw(app, layout, app.columnOps);
    // L'editor dei tasti prende il posto delle opzioni: si apre da una loro riga, e
    // aprendosi le chiude, quindi non si vedono mai insieme.
    GroupMenuDraw(app, layout, app.keyMap);
    // Le destinazioni del fissaggio stanno sopra tutto: si aprono dal menu contestuale
    // o dalla palette, che si chiudono nell'aprirsi.
    GroupMenuDraw(app, layout, app.pinMenu);
    // Le opzioni stanno sopra tutto il resto tranne il proprio sottoelenco: un
    // controllo `Choice` aperto e' l'ultima cosa su cui si sta agendo.
    GroupMenuDraw(app, layout, app.options);
    GroupMenuDraw(app, layout, app.optionsChoice);
    ContextMenuDraw(app, layout);
    // Il pannello delle proprieta' e' modale come un menu, e si disegna per ultimo:
    // copre tutto quello che c'e' sotto.
    DrawPropertiesPanel(app, layout);
    // L'operazione in corso sta sopra anche alle proprieta': e' l'unica cosa che si
    // sta muovendo, e l'unica su cui si puo' agire.
    DrawOperationPanel(app, layout);

    // La finestra dell'aggiornamento e' modale e sta sopra a tutto: se e' aperta, e' la
    // sola cosa su cui si puo' agire.
    DrawUpdateDialog(app, layout);

    // Il suggerimento viene dopo tutto, ma non prende clic: non e' un menu.
    DrawTooltip(app, layout);
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
    if (app.renderer == nullptr || app.viewportWidth <= 0 || app.viewportHeight <= 0)
        return;

    // Le righe si ricostruiscono **qui**, un attimo prima di disegnarle, e non nel
    // ciclo dei messaggi: fra il punto in cui si faceva e questo c'e'
    // `FinishOperation`, che quando un'operazione finisce ricarica la cartella e
    // sostituisce `entries` (`ReloadKeeping` → `ReadDirectory`). Disegnare righe
    // costruite sull'elenco di prima vuol dire leggere il nuovo con gli indici del
    // vecchio — indici oltre la fine del vettore, quindi memoria che sembra valida e
    // non lo e' — ed e' esattamente la caduta sul `Canc` di `docs/02` §7.59,
    // trovata con AddressSanitizer (`container-overflow` su `entry.wideName`).
    //
    // Sta qui e non nel ciclo anche perche' `RenderFrame` ha un secondo chiamante:
    // l'evidenziazione della destinazione durante un trascinamento OLE.
    if (app.viewDirty)
        RebuildView(app);

    app.renderer->BeginFrame(app.viewportWidth, app.viewportHeight);

    // Le anteprime pronte si caricano prima del fotogramma che le disegna: e' il
    // solo punto in cui l'atlante viene toccato, e gira sul thread che possiede il
    // contesto grafico.
    app.previews.Pump();

    app.renderer->Begin(app.viewportWidth, app.viewportHeight);
    app.renderer->ReserveQuads(app.entries.size() * 8 + 512);
    DrawFrame(app, ComputeLayout(app));
    app.renderer->End();

    app.renderer->Present();

    // `Animations`: finche' un pannello sta comparendo, il fotogramma dopo va
    // chiesto. Senza, l'animazione si fermerebbe al primo disegno — il pannello
    // resterebbe fermo a meta' dissolvenza, che e' peggio di nessuna animazione.
    if (app.animations)
    {
        const GroupMenu* menus[] = {&app.palette, &app.goTo,   &app.options,
                                    &app.optionsChoice, &app.columnMenu, &app.pinMenu,
                                    &app.keyMap};
        bool appearing = app.contextMenu.open &&
                         PanelAppear(app.contextMenu.openedAt, true) < 1.0f;
        for (const GroupMenu* menu : menus)
            appearing = appearing || (menu->open && PanelAppear(menu->openedAt, true) < 1.0f);
        if (appearing)
            InvalidateRect(app.window, nullptr, FALSE);
    }
}

// --- window plumbing -------------------------------------------------------
AppState* StateFromWindow(HWND window)
{
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    AppState* app = StateFromWindow(window);

    // I messaggi dell'input stanno nei loro moduli — la tastiera in `input_keys.cpp`,
    // il mouse in `input_mouse.cpp` — e qui si smista soltanto. Un messaggio che
    // nessuno dei due riconosce torna `kMessageNotHandled` e prosegue di sotto, dove
    // sono rimasti i messaggi della **finestra** (`docs/05` §2): `WM_NCCALCSIZE`,
    // `WM_NCHITTEST`, `WM_SIZE`, `WM_SETTINGCHANGE`, `WM_DESTROY`.
    const LRESULT keyResult = HandleKeyMessage(window, app, message, wparam, lparam);
    if (keyResult != kMessageNotHandled)
        return keyResult;
    const LRESULT mouseResult = HandleMouseMessage(window, app, message, wparam, lparam);
    if (mouseResult != kMessageNotHandled)
        return mouseResult;

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
                        case ClickTarget::Kind::SidebarToggle:
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

        // Il tema del sistema puo' cambiare mentre l'applicazione e' aperta, ed e'
        // esattamente quello che `SystemColorScheme` promette di seguire. Il messaggio
        // arriva anche per altre preferenze, quindi la tavolozza si rifa' solo se e' il
        // sistema a decidere; il costo, comunque, e' un fotogramma.
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            if (app != nullptr && app->systemColorScheme)
                ApplyTheme(*app);
            return 0;

        case WM_SIZE:
            if (app != nullptr)
            {
                app->viewportWidth  = LOWORD(lparam);
                app->viewportHeight = HIWORD(lparam);
            }
            return 0;

        // Il sorvegliante dice che la cartella aperta e' cambiata da fuori: e' il
        // pezzo che fa aggiornare i numeri **da soli** invece di aspettare `F5`.
        case kDirectoryChanged:
            if (app != nullptr)
                ApplyExternalChange(*app);
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
            // Le schede aperte, se si e' chiesto di ricordarle. Prima di tutto il
            // resto: le schede sono ancora quelle, e dopo `PostQuitMessage` non c'e'
            // piu' tempo per scrivere niente.
            if (app != nullptr)
                SaveSession(*app);
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



} // namespace

// Le azioni che restano in questo file e che i moduli chiamano: **la cucitura dello
// spezzamento**. Ognuna di queste dovrebbe avere un modulo suo, e il nome lo dice:
// `RememberSettings` vorrebbe un `settings.*` (il file delle preferenze vive ancora
// qui), `BuildKeyMapMenu`/`CloseKeyMap` un `view_keymap.*`, `ClosePinMenu` un
// `view_pins.*`. Finche' non ce l'hanno, il confine e' **dichiarato qui** invece di
// essere implicito in una funzione interna che nessun altro file puo' chiamare:
// `src/app_actions.h` e' la lista, e questa e' l'unica implementazione.
namespace Bolt
{

// Chiudere l'editor chiude anche la cattura: un tasto in attesa di essere scritto non
// deve sopravvivere al pannello che lo stava chiedendo, o al primo tasto premuto
// dopo cambierebbe la mappa senza che nessuno l'abbia chiesto.
void CloseKeyMap(AppState& app)
{
    app.keyMap.open       = false;
    app.keyCaptureCommand = -1;
}

// L'aggiornamento e' un comando esplicito: qui si fa partire e non si aspetta. Il
// lavoro sta su un thread (`update.*`) e lo racconta la sua finestra (`view_update.*`);
// `FinishUpdate` raccoglie l'esito quando il ciclo principale vede che ha finito.
//
// Se un aggiornamento e' gia' stato scaricato e verificato — `Piu' tardi` chiude la
// finestra ma non butta il setup — il comando **riapre quello stato** invece di
// riscaricare trenta megabyte per niente.
void CheckForUpdates(AppState& app)
{
    // Tutto quello che riguarda l'aggiornamento si legge **nella sua finestra**: la
    // barra di stato in basso non dice piu' niente, nemmeno «c'e' gia' un controllo in
    // corso». Due posti che dicono la stessa cosa sono due posti da tenere allineati, e
    // uno dei due era in basso a sinistra dove non si guarda.
    if (app.updateJob != nullptr)
    {
        app.updateDialogOpen = true;
        return;
    }

    if (app.updateDone && app.updateResult == Update::Result::Ready &&
        !app.updateSetupPath.empty())
    {
        app.updateDialogOpen = true;
        DiagLog("--- aggiornamenti: riaperta la finestra sul setup gia' verificato");
        return;
    }

    app.updateDone       = false;
    app.updateInstalling = false;
    app.updateResult     = Update::Result::UpToDate;
    app.updateVersion.clear();
    app.updateSetupPath.clear();
    app.updateError.clear();
    app.updateDialogOpen = true;
    app.updateJob = Update::Job::Start();
    DiagLog("--- aggiornamenti: controllo chiesto dall'utente");
}

// Dichiarato qui perche' la testata della mappa dei tasti lo usa, e la definizione sta
// piu' sotto con le altre funzioni della palette.
std::string CommandLabelById(int id);

void BuildKeyMapMenu(AppState& app, int selectCommand)
{
    std::vector<GroupMenuItem> items;
    items.reserve(sizeof(kCommandTable) / sizeof(kCommandTable[0]) + 1);

    // I tasti di partenza, in cima e **senza banda di gruppo**: non e' un comando, e
    // metterlo sotto l'intestazione di una famiglia sarebbe dire che lo e'. Sta dove
    // lo si trova senza cercarlo, perche' e' l'unica via d'uscita da una mappa che ha
    // tolto i tasti per riaprire questa lista.
    {
        GroupMenuItem item;
        item.label = Tr("Tasti di partenza", "Default keys");
        item.id    = kKeyMapResetId;
        items.push_back(std::move(item));
    }

    for (const CommandDef& command : kCommandTable)
    {
        GroupMenuItem item;
        item.group = command.group;
        item.label = CommandLabel(command);
        // Vuoto quando il comando non ha tasti: la riga si legge lo stesso, e il
        // posto vuoto a destra **e'** l'informazione che manca.
        item.detail = app.keys.LabelFor(command.id);
        item.id     = command.id;
        items.push_back(std::move(item));
    }

    CloseKeyMap(app);
    ClosePinMenu(app);
    app.options.open       = false;
    app.optionsChoice.open = false;
    app.palette.open       = false;
    app.goTo.open          = false;

    GroupMenuOpen(app.keyMap, Tr("MAPPA DEI TASTI", "KEY MAP"),
                  Tr("Scrivi per filtrare i comandi...", "Type to filter commands..."),
                  std::move(items));
    // Il tasto si scrive a destra della riga: `detailed` e' quello che fa disegnare il
    // dettaglio, ed e' l'unica cosa che questo elenco ha in piu' della palette.
    app.keyMap.detailed = true;
    // La testata: che cosa si sta cambiando e da dove si apre. Dopo una rimappatura il
    // tasto della mappa puo' essere un altro, e `LabelsFor` lo dice gia' aggiornato.
    app.keyMap.caption     = Tr("Quale tasto fa quale comando.", "Which key runs which command.");
    app.keyMap.command     = CommandLabelById(CmdEditKeyMap);
    app.keyMap.commandKeys = app.keys.LabelsFor(CmdEditKeyMap);
    app.keyMap.hint     = Tr("Invio rimappa   Canc toglie   Esc chiude",
                             "Enter rebinds   Del removes   Esc closes");

    // Il cursore resta sul comando appena toccato: dopo una rimappatura l'elenco si
    // rifa' (la riga mostra il tasto nuovo).
    if (selectCommand >= 0)
        SelectKeyMapCommand(app, selectCommand);
}

void ClosePinMenu(AppState& app)
{
    app.pinMenu.open = false;
}

// --- layout ----------------------------------------------------------------


// La geometria **intera**: barre, riquadro di navigazione, ispettore, e la fascia
// dove stanno i pannelli. E' quello che era `ComputeLayout` prima che ci fossero due
// pannelli, e resta l'unico posto in cui la finestra si divide.
Layout WindowLayout(const AppState& app)
{
    const float scale = app.zoom;
    const float width = static_cast<float>(app.viewportWidth);
    const float height = static_cast<float>(app.viewportHeight);

    const float sidebarWidth   = app.sidebarVisible ? 186.0f * scale : 0.0f;
    const float inspectorWidth = app.inspectorWidth * scale;
    const float titleHeight    = 34.0f * scale;
    const float toolbarHeight  = 38.0f * scale;
    // A grid view has no columns to head, so it gives the space back to the list. Con
    // **due** pannelli l'intestazione c'e' sempre, anche a griglia: senza, i due
    // pannelli avrebbero geometrie verticali diverse a seconda di quale dei due ha il
    // fuoco — cioe' si sposterebbero da soli passandoci sopra col puntatore.
    const bool gridView = app.panes.size() < 2 &&
                          (app.viewMode == ViewMode::Tiles || app.viewMode == ViewMode::Icons);
    const float headerHeight   = gridView ? 0.0f : 26.0f * scale;
    const float statusHeight   = 22.0f * scale;
    const float filterHeight   = 24.0f * scale;
    const float gap            = 6.0f * scale;
    // La fila delle schede di **ogni** pannello: con due se ne riserva una per pannello,
    // fra la barra degli strumenti e l'intestazione delle colonne. Con un pannello solo
    // la riserva e' zero, perche' la fila e' quella della barra del titolo: spostarla
    // per fare posto a qualcosa che non c'e' cambierebbe l'interfaccia a chi non ha
    // diviso niente.
    const float tabStripHeight = (app.panes.size() >= 2) ? TabStripHeight(scale) : 0.0f;

    Layout layout;
    layout.scale      = scale;
    // `Spacing` e' una preferenza: l'altezza della riga di lista la segue, e per
    // questo e' un valore derivato e non un 24 scritto a mano.
    layout.rowHeight  = (app.rowSpacing > 0.0f ? app.rowSpacing : 24.0f) * scale;
    layout.titleBar   = Rect{0.0f, 0.0f, width, titleHeight};
    layout.toolbar    = Rect{0.0f, titleHeight, width, toolbarHeight};
    layout.sidebar    = Rect{gap, titleHeight + toolbarHeight + gap, sidebarWidth,
                             height - titleHeight - toolbarHeight - statusHeight - gap * 3};
    // La fascia delle schede, larga quanto la colonna dei pannelli: `PaneLayout` la
    // stringe alla meta' di ognuno. Vuota (altezza zero) con un pannello solo.
    layout.tabs       = Rect{sidebarWidth + gap * 2, titleHeight + toolbarHeight + gap,
                             width - sidebarWidth - inspectorWidth - gap * 4, tabStripHeight};
    layout.header     = Rect{layout.tabs.x, layout.tabs.bottom(), layout.tabs.w, headerHeight};
    layout.filterBar  = Rect{layout.header.x, height - statusHeight - gap - filterHeight,
                             layout.header.w, filterHeight};
    layout.list       = Rect{layout.header.x, layout.header.bottom(),
                             layout.header.w,
                             layout.filterBar.y - gap - layout.header.bottom()};
    layout.inspector  = Rect{width - inspectorWidth - gap, titleHeight + toolbarHeight + gap,
                             inspectorWidth,
                             layout.list.h + headerHeight + filterHeight + gap + tabStripHeight};
    layout.statusBar  = Rect{0.0f, height - statusHeight, width, statusHeight};
    return layout;
}

// Il layout **di un pannello**: la finestra, con intestazione, filtro e lista
// ristretti alla sua meta'. Con un pannello solo e' la finestra stessa, ed e' il
// motivo per cui tutto il resto del programma non deve sapere che i pannelli
// esistono.
//
// Cambia solo la x e la larghezza (o la y e l'altezza, se sono uno sopra l'altro):
// intestazione, filtro e lista di un pannello hanno le stesse misure verticali di
// prima, perche' le decide una volta sola `WindowLayout`.
Layout PaneLayout(const AppState& app, int pane)
{
    Layout layout = WindowLayout(app);
    if (app.panes.size() < 2 || pane < 0 || pane >= static_cast<int>(app.panes.size()))
        return layout;

    const float gap      = kSplitterWidth * layout.scale;
    const float fraction = std::clamp(app.splitFraction, 0.25f, 0.75f);

    if (app.splitColumn)
    {
        // Affiancati: la fascia dei pannelli si divide in due, e il divisore si prende
        // sei pixel nel mezzo.
        const float usable = (std::max)(2.0f, layout.header.w - gap);
        const float first  = usable * fraction;
        const float x      = (pane == 0) ? layout.header.x : layout.header.x + first + gap;
        const float w      = (pane == 0) ? first : usable - first;
        layout.tabs      = Rect{x, layout.tabs.y, w, layout.tabs.h};
        layout.header    = Rect{x, layout.header.y, w, layout.header.h};
        layout.filterBar = Rect{x, layout.filterBar.y, w, layout.filterBar.h};
        layout.list      = Rect{x, layout.list.y, w, layout.list.h};
        return layout;
    }

    // Uno sopra l'altro: si divide l'altezza della colonna, e ogni pannello tiene la
    // **sua** intestazione e il **suo** filtro — sono quello che dice cosa si sta
    // guardando, e un pannello senza sarebbe una lista di nomi senza contesto.
    const float headerHeight = layout.header.h;
    const float filterHeight = layout.filterBar.h;
    const float stripHeight  = layout.tabs.h;
    // Il riquadro di un pannello comincia dalla **sua** fila di schede: la y di
    // partenza e' quella della fascia, non quella dell'intestazione, o il secondo
    // pannello si prenderebbe la fila del primo.
    const float top          = (stripHeight > 0.0f) ? layout.tabs.y : layout.header.y;
    const float usable       = (std::max)(2.0f, layout.filterBar.bottom() - top - gap);
    const float first        = usable * fraction;
    const float y            = (pane == 0) ? top : top + first + gap;
    const float h            = (pane == 0) ? first : usable - first;
    const float listHeight   = (std::max)(0.0f, h - stripHeight - headerHeight - filterHeight);
    layout.tabs      = Rect{layout.tabs.x, y, layout.tabs.w, stripHeight};
    layout.header    = Rect{layout.tabs.x, y + stripHeight, layout.tabs.w, headerHeight};
    layout.list      = Rect{layout.header.x, layout.header.bottom(), layout.header.w, listHeight};
    layout.filterBar = Rect{layout.header.x, layout.header.bottom() + listHeight, layout.header.w,
                            filterHeight};
    return layout;
}

// Il rettangolo del divisore: la striscia che si prende col mouse. Vuoto con un
// pannello solo, cosi' chi lo interroga non deve chiedere anche quanti sono.
Rect SplitterRect(const AppState& app)
{
    const Layout window = WindowLayout(app);
    if (app.panes.size() < 2)
        return Rect{};

    const float gap      = kSplitterWidth * window.scale;
    const float fraction = std::clamp(app.splitFraction, 0.25f, 0.75f);

    // Il divisore arriva fino in cima alla fascia delle schede: con due pannelli la
    // colonna comincia li', e una presa che si fermasse all'intestazione lascerebbe
    // una striscia di sei pixel che non si prende.
    const float top = (window.tabs.h > 0.0f) ? window.tabs.y : window.header.y;
    if (app.splitColumn)
    {
        const float usable = (std::max)(2.0f, window.header.w - gap);
        return Rect{window.header.x + usable * fraction, top, gap,
                    window.filterBar.bottom() - top};
    }
    const float usable = (std::max)(2.0f, window.filterBar.bottom() - top - gap);
    return Rect{window.header.x, top + usable * fraction, window.header.w, gap};
}

// Il pannello sotto un punto, o -1 se il punto non e' di nessuno. E' la stessa
// geometria che decide chi prende il fuoco al clic (`PaneLayout`): due elenchi di
// rettangoli diversi prima o poi direbbero due cose diverse, e qui il secondo sarebbe
// una caduta che finisce nel pannello sbagliato.
int PaneAtPoint(const AppState& app, float x, float y)
{
    for (int pane = 0; pane < static_cast<int>(app.panes.size()); ++pane)
    {
        const Layout box = PaneLayout(app, pane);
        if (box.tabs.Contains(x, y) || box.header.Contains(x, y) || box.list.Contains(x, y) ||
            box.filterBar.Contains(x, y))
            return pane;
    }
    return -1;
}

// Il layout della **vista su cui si sta lavorando**: e' quello che serve a ogni hit
// test e a ogni comando, che cosi' continuano a leggere `layout.header` e
// `layout.list` come prima dei pannelli.
Layout ComputeLayout(const AppState& app)
{
    return PaneLayout(app, app.activePane);
}


// --- l'anteprima di quello che c'e' dentro una cartella ----------------------


// --- il pannello delle proprieta' in-app ------------------------------------
//
// La voce `Proprietà` non apre piu' la finestra di Windows: apre questa, che mostra
// le stesse righe dell'inspector in una finestra nostra. `Proprietà di Windows`
// resta, accanto, per cio' che solo la shell sa — permessi, versioni precedenti, le
// pagine registrate dai gestori dei tipi di file, e la scrittura.
//
// Le righe le costruisce `BuildInspectorRows`, la stessa che usa l'inspector: due
// viste della stessa cosa con un solo calcolo, o prima o poi mostrerebbero numeri
// diversi.
void OpenPropertiesPanel(AppState& app)
{
    app.propertiesRows.clear();
    app.propertiesScroll = 0.0f;

    const int selectedCount = static_cast<int>(app.selection.size());
    const int cursorIndex   = CursorIndex(app);

    if (selectedCount > 1)
    {
        // Piu' voci: l'aggregato, come nell'inspector.
        uint64_t totalBytes = 0;
        int files = 0;
        int folders = 0;
        std::unordered_map<std::string, int> byType;
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
                if (item.location.empty() &&
                    app.stats.TryGet(JoinPath(app.directory, item.wideName), &stats))
                    totalBytes += stats.logicalBytes;
            }
            else
            {
                ++files;
                totalBytes += item.bytes;
            }
            ++byType[item.typeText];
        }
        std::vector<std::pair<std::string, int>> types(byType.begin(), byType.end());
        std::sort(types.begin(), types.end(),
                  [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                      return a.second > b.second;
                  });

        app.propertiesTitle    = TrW(L"PROPRIETÀ", L"PROPERTIES");
        app.propertiesSubtitle = std::to_wstring(selectedCount) +
                                 TrW(L" elementi selezionati", L" items selected");
        app.propertiesRows.push_back(
            {Tr("Elementi", "Items"), std::to_string(files) + Tr(" file, ", " files, ") +
                                       std::to_string(folders) +
                                       (folders == 1 ? Tr(" cartella", " folder")
                                                     : Tr(" cartelle", " folders")),
             false, false});
        app.propertiesRows.push_back(
            {Tr("Dimensione", "Size"), HumanBytes(totalBytes), false, false});
        for (size_t i = 0; i < types.size(); ++i)
            app.propertiesRows.push_back({(i == 0) ? Tr("Tipi", "Types") : "",
                                          std::to_string(types[i].second) + " " + types[i].first,
                                          false, false});
    }
    else if (cursorIndex >= 0)
    {
        const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
        app.propertiesTitle    = TrW(L"PROPRIETÀ", L"PROPERTIES");
        app.propertiesSubtitle = Widen(entry.name);
        BuildInspectorRows(app, entry, &app.propertiesRows);
    }
    else
    {
        return;   // niente da mostrare, niente pannello
    }

    app.propertiesOpen = true;
}


// --- le superfici che un comando apre ---------------------------------------
//
// Sono **superfici**, non comandi: costruiscono un menu (o il pannello
// dell'operazione) e lo consegnano all'input. Vivono qui perche' il modulo
// che dovrebbe averle (`view_palette.*`, `view_goto.*`, `view_pins.*`,
// `view_operation.*`) non c'e' ancora, e un comando deve poterle chiamare:
// interno e irraggiungibile sarebbe stato peggio.

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
// Il **menu dell'applicazione**, dietro il pulsante ☰ della barra del titolo: le stesse
// voci del riferimento (`GoTo`, `Commands`, i layout, `Options`) piu' quella che nel
// riferimento e' il suo setup e qui e' l'aggiornamento. In fondo, come li', la versione.
//
// E' un menu contestuale a tutti gli effetti — un livello solo, separatori, voci spente —
// quindi usa quel componente invece di un secondo modo di disegnare un menu.
void BuildAppMenu(AppState& app, float x, float y)
{
    ContextMenu& menu = app.contextMenu;
    menu.items.clear();
    menu.x = x;
    menu.y = y;
    menu.openPath.clear();
    menu.levels.assign(1, ContextMenuLevel{});
    menu.shellIsBackground = false;
    menu.shell     = nullptr;   // non c'e' nessuna shell da interrogare
    menu.shellMenu = nullptr;
    menu.openedAt  = NowSeconds();

    // Le scorciatoie si chiedono alla mappa dei tasti: se domani si rimappa il GoTo, il
    // menu lo dice senza che nessuno lo aggiorni.
    const auto shortcut = [&app](int command)
    {
        std::string text;
        for (const std::string& label : app.keys.LabelsFor(command))
            text += (text.empty() ? "" : "  ") + label;
        return text;
    };

    const bool home = IsHome(app.directory);

    menu.items.push_back({Tr("Vai a...", "GoTo"), CmdGoTo, false, false, shortcut(CmdGoTo)});
    menu.items.push_back({Tr("Comandi", "Commands"), CmdPalette, false, false,
                          shortcut(CmdPalette)});
    menu.items.push_back({"", 0, /*separator=*/true});

    // `Load Layout` nel riferimento si vede **spento** finche' non c'e' un layout da
    // caricare: e' la stessa informazione, detta invece che nascosta.
    menu.items.push_back({Tr("Applica il layout", "Load Layout"), CmdLoadFolderLayout, false,
                          home || !HasFolderLayout(app)});
    menu.items.push_back({Tr("Salva il layout", "Save Layout"), CmdSaveFolderLayout, false, home});
    menu.items.push_back({"", 0, /*separator=*/true});
    menu.items.push_back({Tr("Opzioni", "Options"), CmdOpenOptions, false, false,
                          shortcut(CmdOpenOptions)});
    menu.items.push_back({"", 0, /*separator=*/true});
    menu.items.push_back({Tr("Controlla aggiornamenti", "Check for updates"), CmdCheckUpdates});

    // La versione, come nel riferimento: "FILE PILOT v0.8.5 - Aug 31 2026 [Beta]". La
    // data di compilazione non l'abbiamo, e inventarla sarebbe peggio che non scriverla.
    menu.subtitle = std::string("BOLT v") + ToUtf8(Update::CurrentVersion());

    ClosePinMenu(app);
    app.options.open = false;
    app.palette.open = false;
    app.goTo.open    = false;
    menu.open = true;
}

// Il pannello delle destinazioni: due righe, e quella che **non cambierebbe niente**
// si mostra spenta — il componente dei menu sa farlo adesso, e la ragione la dice il
// titolo del pannello (`FISSA IN` contro `TOGLI DA`), non un'aggiunta al nome.
void BuildPinMenu(AppState& app, bool removing)
{
    // Su quale percorso si sta lavorando: quello del menu contestuale se c'e' (una
    // riga della barra laterale), altrimenti la cartella aperta. Il pannello lo
    // ricorda perche' la scelta arriva dopo, quando il menu e' gia' chiuso.
    const std::wstring& path =
        app.contextTargetPath.empty() ? app.directory : app.contextTargetPath;
    app.pinMenuTarget = path;
    // Applicabile vuol dire: "questa destinazione **cambierebbe**". Fissare vuol dire
    // "se non c'e' gia'", togliere vuol dire "se c'e'": la stessa domanda, e il verso
    // lo dice `removing`.
    const auto applicable = [&](PinTarget target) { return PinnedIn(app, target, path) == removing; };

    // Un pannello su cui non si puo' agire non si apre: quando non c'e' niente da
    // fare — la cartella e' gia' dappertutto, o non e' fissata da nessuna parte —
    // l'esito si legge nella barra di stato, che e' dove finisce ogni azione.
    if (path.empty())
        return;
    if (!applicable(PinTarget::QuickAccess) && !applicable(PinTarget::Bookmarks))
    {
        ReportStatus(app, removing
                              ? TrW(L"Questa cartella non e' fissata da nessuna parte",
                                    L"This folder is not pinned anywhere")
                              : TrW(L"Questa cartella e' gia' fissata in tutte le sezioni",
                                    L"This folder is already pinned in every section"));
        return;
    }

    std::vector<GroupMenuItem> items;
    items.reserve(kPinTargetCount);
    for (int i = 0; i < kPinTargetCount; ++i)
    {
        const PinTarget target = PinTargetAt(i);
        GroupMenuItem item;
        item.label   = PinTargetLabel(target);
        item.id      = i;
        item.enabled = applicable(target);
        items.push_back(std::move(item));
    }

    app.pinMenuRemoving = removing;
    CloseKeyMap(app);
    ClosePinMenu(app);
    app.palette.open       = false;
    app.goTo.open          = false;
    app.options.open       = false;
    app.optionsChoice.open = false;
    app.columnMenu.open    = false;
    // Il menu contestuale e' modale lui pure, e nell'ordine dell'input viene **prima**
    // dei menu: se restasse aperto sotto, il pannello non riceverebbe ne' clic ne'
    // tasti. Si chiude, e le due voci si scelgono qui.
    ContextMenuClose(app);

    GroupMenuOpen(app.pinMenu, removing ? Tr("TOGLI DA", "UNPIN FROM") : Tr("FISSA IN", "PIN TO"),
                  Tr("Dove?", "Where?"), std::move(items));
    app.pinMenu.hint = removing ? Tr("Invio toglie   Esc chiude", "Enter unpins   Esc closes")
                                : Tr("Invio fissa   Esc chiude", "Enter pins   Esc closes");
}
// --- building the two menus -------------------------------------------------
// La seconda linea di una riga della palette: che cosa fa questo **gruppo** di
// comandi. Una frase per comando sarebbero 107 frasi da tenere vere una per una, e
// meta' sarebbero la ripetizione del nome; il gruppo dice il contesto — il nome dice
// gia' l'azione — e le due righe insieme si leggono come una frase sola.
// L'etichetta di un comando **per id**: `CommandLabel` vuole la riga della tabella, e
// qui serve il nome di un comando che si conosce per id (la testata di un menu).
std::string CommandLabelById(int id)
{
    for (const CommandDef& command : kCommandTable)
        if (command.id == id)
            return CommandLabel(command);
    return std::string();
}

std::string GroupDescription(const std::string& group)
{
    if (group == "Appearance")  return Tr("Come si vedono i colori e i caratteri", "How colours and fonts look");
    if (group == "Bookmarks")   return Tr("I posti fissati, e la loro memoria", "Saved places, and their memory");
    if (group == "File")        return Tr("Le operazioni sui file scelti", "What to do with the chosen files");
    if (group == "Folder")      return Tr("La cartella aperta: leggerla, ricaricarla, aprirla fuori", "The open folder: read, reload, open elsewhere");
    if (group == "GoTo")        return Tr("Andare in un altro posto", "Go somewhere else");
    if (group == "GroupBy")     return Tr("Come si raggruppano le righe", "How the rows are grouped");
    if (group == "Inspector")   return Tr("Il pannello di destra e cio' che mostra", "The right-hand panel and what it shows");
    if (group == "Interaction") return Tr("Come si apre e si sceglie una voce", "How an item is opened and chosen");
    if (group == "Language")    return Tr("In che lingua si legge il programma", "The language the program speaks");
    if (group == "Options")     return Tr("Il pannello delle preferenze, e chi lo apre", "The preferences panel, and who opens it");
    if (group == "Places")      return Tr("Le cartelle note, pronte da aprire", "The known folders, ready to open");
    if (group == "Recents")     return Tr("Le cartelle gia' visitate in questa sessione", "Folders already visited in this session");
    if (group == "Scroll")      return Tr("Andare in cima o in fondo senza il puntatore", "Top and bottom without the pointer");
    if (group == "Sort")        return Tr("Su quale colonna si ordina, e in che verso", "Which column sorts, and which way");
    if (group == "Startup")     return Tr("Dove si apre il programma", "Where the program opens");
    if (group == "Tabs")        return Tr("Le schede: aprirle, chiuderle, spostarle", "Tabs: open, close, move");
    if (group == "Text")        return Tr("Il visualizzatore di testo dell'ispettore", "The inspector's text viewer");
    if (group == "Updates")     return Tr("Cercare una versione nuova, quando lo chiedi", "Looking for a new version, when asked");
    if (group == "View")        return Tr("Come si guarda l'elenco", "How the list is shown");
    return std::string();
}

void BuildPaletteMenu(AppState& app)
{
    // Un comando scelto dalla palette non ha una riga sotto il puntatore: agisce
    // sulla cartella aperta, e il bersaglio del menu contestuale di prima non vale
    // piu'.
    app.contextTargetPath.clear();
    app.contextTargetLabel.clear();
    std::vector<GroupMenuItem> items;

    // I preferiti in cima, in un gruppo loro: la stella serve a non ricercare ogni
    // volta nell'elenco lungo. Un comando preferito **non** compare anche nel suo
    // gruppo — due righe per lo stesso comando sono due cose da tenere allineate.
    for (const CommandDef& command : kCommandTable)
    {
        if (app.favoriteCommands.count(command.id) == 0)
            continue;
        std::string label = CommandLabel(command);
        if (command.id == CmdToggleBookmark && IsBookmarked(app))
            label = Tr("Togli questo posto dai segnalibri", "Remove bookmark from this folder");
        items.push_back(GroupMenuItem{Tr("Preferiti", "Favorites"), label, "", command.id});
        items.back().chips = app.keys.LabelsFor(command.id);
        items.back().description = GroupDescription(command.group);
    }

    for (const CommandDef& command : kCommandTable)
    {
        if (app.favoriteCommands.count(command.id) != 0)
            continue;
        // L'etichetta della tabella e' quella inglese, che e' anche il nome della
        // chiave; in italiano la da' `CommandLabelIt`, e se non c'e' resta quella.
        // In inglese vale l'etichetta della tabella; in italiano la traduzione, se
        // c'e'. Il ripiego e' sempre la tabella: una traduzione mancante lascia la
        // parola in inglese invece di lasciare il vuoto.
        std::string label = CommandLabel(command);
        // The one command whose wording depends on state. The table stays data.
        if (command.id == CmdToggleBookmark && IsBookmarked(app))
            label = Tr("Togli questo posto dai segnalibri", "Remove bookmark from this folder");
        items.push_back(GroupMenuItem{command.group, label, "", command.id});
        // I tasti del comando, uno per pastiglia, sulla destra: si impara il comando
        // leggendolo, che e' quello che fa anche il riferimento nella sua lista.
        items.back().chips = app.keys.LabelsFor(command.id);
        // La seconda linea: il contesto del gruppo (vedi `GroupDescription`).
        items.back().description = GroupDescription(command.group);
    }

    // Locations are data, not code: one entry per known folder, so the palette
    // covers the sidebar without a command being declared for each.
    for (int i = 0; i < static_cast<int>(app.places.size()); ++i)
    {
        items.push_back(GroupMenuItem{"Places", app.places[static_cast<size_t>(i)].label, "",
                                      kPalettePlaceBase + i});
        items.back().description = GroupDescription("Places");
    }

    CloseKeyMap(app);
    ClosePinMenu(app);
    app.goTo.open = false;   // one menu at a time
    app.options.open = false;
    GroupMenuOpen(app.palette, Tr("PALETTE DEI COMANDI", "COMMAND PALETTE"),
                  Tr("Scrivi per filtrare i comandi...", "Type to filter commands..."),
                  std::move(items));
    // La riga in fondo dice i tasti **della mappa**, non due stringhe scritte a
    // mano: se il tasto cambia, cambia quello che si legge. Prima erano lettere
    // dentro una frase, ed erano l'ultimo posto in cui una scorciatoia viveva per
    // conto suo.
    // Col dettaglio acceso la colonna di destra porta i **tasti**, non il gruppo: il
    // gruppo lo dicono gia' le intestazioni, e ripeterlo toglieva spazio all'unica
    // cosa che li' non si puo' leggere altrove.
    // Il dettaglio **c'e'**: senza `detailed` la colonna di destra non si disegna
    // affatto, e le pastiglie dei tasti sarebbero state scritte e mai viste — la
    // palette mostrava un elenco di nomi e basta.
    app.palette.detailed             = true;
    app.palette.detailFallsBackToGroup = false;
    app.palette.detailAsChips          = true;
    // Righe a due linee: nome e contesto. E' la forma della lista dei comandi del
    // riferimento, e il motivo per cui le pastiglie dei tasti ci stanno accanto.
    app.palette.richRows               = true;
    // La testata: che cosa si sta guardando, e da dove si apre anche da tastiera.
    app.palette.caption     = Tr("Scegli un comando: Invio lo esegue.", "Pick a command: Enter runs it.");
    app.palette.command     = CommandLabelById(CmdPalette);
    app.palette.commandKeys = app.keys.LabelsFor(CmdPalette);
    app.palette.hint = std::string(Tr("Invio esegue   Esc chiude   ", "Enter runs   Esc closes   ")) +
                       app.keys.LabelFor(CmdPalette) + std::string(Tr(" apre e chiude   ", " toggles   ")) +
                       app.keys.LabelFor(CmdOpenOptions) + std::string(Tr(" opzioni", " options"));
}
void BuildGoToMenu(AppState& app)
{
    std::vector<GroupMenuItem> items;
    app.goToTargets.clear();
    app.goToCommands.clear();

    const auto add = [&items, &app](const char* group, const std::wstring& path, const std::string& label)
    {
        const int id = static_cast<int>(app.goToTargets.size());
        items.push_back(GroupMenuItem{group, label, ToUtf8(path), id});
        app.goToTargets.push_back(path);
        app.goToCommands.push_back(-1);   // -1: la voce porta a un percorso
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

    // I **fissaggi nostri** (`Fissa in...`), che nella barra laterale stanno in cima
    // all'accesso rapido: senza questa banda una cartella fissata non si raggiunge
    // dal GoTo, che e' il posto dove si va a cercare una destinazione per nome.
    // Le voci della **shell** (Desktop, Download...) non si ripetono qui: sono un
    // elenco fisso che si legge gia' nella barra laterale, e ripeterlo raddoppia il
    // gruppo senza aggiungere niente da cercare.
    for (const std::wstring& path : app.pins)
        add("QuickAccess", path, ToUtf8(LeafName(path)));

    for (const Place& place : app.places)
        add("Places", place.path, place.label);

    for (const std::wstring& path : app.bookmarks)
        add("Bookmarks", path, ToUtf8(LeafName(path)));

    for (const RecentEntry& entry : app.recents.Ranked())
        add("Recents", entry.path, ToUtf8(LeafName(entry.path)));

    // Le due voci che il riferimento ha in fondo al gruppo dei recenti
    // (`GotoGroupOption_ShowAllRecents`, `GotoGroupOption_RecentsClear`).
    //
    // «Mostra tutti i recenti» **non c'e'**, e non per dimenticanza: nel riferimento
    // serve perche' il gruppo ne mostra solo una parte. Qui ci sono gia' tutti, e un
    // comando che non cambia niente e' peggio di un comando assente (`AGENTS.md` §6).
    // «Cancella i recenti» invece fa esattamente quel che dice.
    if (!app.recents.Ranked().empty())
    {
        const int id = static_cast<int>(app.goToTargets.size());
        items.push_back(GroupMenuItem{"Recents", std::string(Tr("Cancella i recenti", "Clear recents")),
                                      std::string(), id});
        app.goToTargets.push_back(std::wstring());
        app.goToCommands.push_back(CmdClearRecents);
    }

    CloseKeyMap(app);
    ClosePinMenu(app);
    app.palette.open = false;
    app.options.open = false;
    GroupMenuOpen(app.goTo, Tr("VAI A", "GO TO"),
                  Tr("Scrivi per filtrare le cartelle...", "Type to filter locations..."),
                  std::move(items));
    app.goTo.detailed = true;   // a location without its path is not much use
    app.goTo.hint = std::string(Tr("Invio va   Esc chiude   ", "Enter goes   Esc closes   ")) +
                    app.keys.LabelFor(CmdGoTo) + std::string(Tr(" apre e chiude", " toggles"));
}
// Un'operazione lunga si avvia sempre da qui: cosi' la sorgente e l'ora non si
// possono dimenticare in uno dei posti che la chiamano. Accettata vuol dire **sara'
// fatta**: se ce n'e' gia' una in corso la richiesta va in coda, e parte quando
// quella finisce. Restituisce falso solo quando non c'e' niente da fare, perche' un
// chiamante che riceve `true` deve poter raccontare al sistema che l'ha presa in
// carico.
bool BeginOperation(AppState& app, FileOps::Request request, OperationSource source)
{
    if (request.empty())
        return false;

    if (app.operation != nullptr)
    {
        app.operationQueue.push_back(QueuedOperation{std::move(request), source});

        const size_t queued = app.operationQueue.size();
        ReportStatus(app, CountItems(queued) +
                              TrW(L" in coda — parte quando finisce quella in corso.",
                                  L" queued — it starts when the current one ends."));
        return true;
    }

    app.operation       = FileOps::Start(app.window, request);
    app.operationSource = source;
    app.operationOrigin = app.directory;
    app.operationStart  = NowSeconds();
    app.operationQueueDropped = 0;
    return true;
}

// La coda avanza di una posizione: si chiama quando la precedente ha finito, ed e'
// l'unico posto da cui parte un'operazione che non viene da un gesto.
void StartNextOperation(AppState& app)
{
    if (app.operation != nullptr || app.operationQueue.empty())
        return;

    QueuedOperation next = std::move(app.operationQueue.front());
    app.operationQueue.erase(app.operationQueue.begin());

    app.operation       = FileOps::Start(app.window, next.request);
    app.operationSource = next.source;
    app.operationOrigin = app.directory;
    app.operationStart  = NowSeconds();
}

// ---------------------------------------------------------------------------
// Le superfici che l'input chiama.
//
// Sono le stesse funzioni di prima, non riscritte: escono dal namespace anonimo ed
// entrano qui perche' `input_keys.cpp` e `input_mouse.cpp` sono altri file, e un
// simbolo interno a `main.cpp` non attraversa un confine di compilazione. La loro
// casa vera e' quella scritta accanto alla dichiarazione in `app_actions.h`
// (`view_keymap.*`, `view_columns.*`, `view_properties.*`, `view_operation.*`,
// `view_pins.*`, `search.*`): quando arrivera', il corpo si spostera' di peso e la
// riga sparira' da quell'elenco — che e' la regola di quella cucitura.
// ---------------------------------------------------------------------------

// |text| **per valore**: la funzione comincia azzerando `app.searchText`, e quel testo
// e' proprio `app.searchText` quando arriva da `Invio`. Passato per riferimento, il
// primo `clear()` lo svuotava, e la ricerca partiva con la query vuota — che il
// controllo qui sotto respingeva, senza dire niente. Il registro l'ha detto in una riga.
void StartSearch(AppState& app, std::wstring text)
{
    // La ricerca si azzera **prima** di riempire: il testo che si cerca arriva come
    // argomento e non dallo stato, perche' azzerare lo stato dopo averlo letto vuol dire
    // cercare la stringa vuota — che trova tutto. Visto: 31 voci invece delle 15 `voce`.
    app.search = AppState::SearchJob{};
    app.searchText.clear();
    app.search.query = FoldCase(text);
    DiagLog("--- ricerca: avvio, query '" + ToUtf8(app.search.query) + "' da " +
            ToUtf8(app.directory));

    if (app.search.query.empty())
        return;

    // Dal campo si cerca: la cartella aperta **e sotto**. Da una cartella di spazio dei
    // nomi non c'e' un albero da camminare, e lo si dice invece di cercare a vuoto.
    if (ShellLocation::IsNamespace(app.directory) || IsHome(app.directory))
    {
        ReportStatus(app, TrW(L"Da qui non c'e' un albero di cartelle da cercare",
                              L"There is no folder tree to search from here"));
        return;
    }

    app.search.active = true;
    app.search.root   = app.directory;
    app.search.pending.push_back(app.directory);
    ReportStatus(app, TrW(L"Cerco...", L"Searching..."));
}

// Fermare la ricerca e tornare alla cartella: i risultati non sono una cartella, e
// lasciarli li' dopo aver premuto Esc confonderebbe chi guarda.
void StopSearch(AppState& app)
{
    app.search = AppState::SearchJob{};
    app.searchText.clear();
    app.searchFieldActive = false;
    LoadDirectory(app, app.directory, /*recordHistory=*/false);
}

// La colonna sotto un punto dell'intestazione, o -1. Una colonna nascosta non ha un
// rettangolo: la sua frazione e' quella della prossima visibile (`ColumnEdges`),
// quindi si cerca **fra quelle visibili** invece di fidarsi dell'indice.
int ColumnAtPoint(const AppState& app, const Layout& layout, float x)
{
    if (layout.header.w <= 0.0f)
        return -1;
    const float fraction = (x - layout.header.x) / layout.header.w;
    float edges[kColumnCount + 1];
    ColumnEdges(app, edges);
    for (int i = 0; i < kColumnCount; ++i)
    {
        if (!ColumnVisible(app, i))
            continue;
        // Il bordo destro e' quello della prossima **visibile a destra**, non di
        // `i + 1`: dopo uno spostamento i due non coincidono.
        const int   next  = NextVisibleColumn(app, i);
        const float right = next < kColumnCount ? edges[next] : 1.0f;
        if (fraction >= edges[i] && fraction < right)
            return i;
    }
    return -1;
}

// Il menu del **tasto destro sull'intestazione**: le operazioni sulla colonna. Non ha
// le spunte di visibilita' che il riferimento mette in fondo allo stesso pannello: da
// noi le colonne si accendono e si spengono dalle opzioni e dalla palette, e due posti
// che fanno la stessa cosa sono due posti da tenere allineati.
void BuildColumnOpsMenu(AppState& app, int column)
{
    const Layout layout = ComputeLayout(app);
    float edges[kColumnCount + 1];
    ColumnEdges(app, edges);

    app.columnOpsIndex = column;
    app.columnOps.anchor = Rect{layout.header.x + layout.header.w * edges[column],
                                layout.header.y, 0.0f, layout.header.h};

    const bool closable = column > 0;
    std::vector<GroupMenuItem> items;
    // Sposta a sinistra o a destra: si accende solo dove c'e' dove andare — il nome non
    // si sposta (e' la colonna che prende lo spazio che resta) e la prima e l'ultima
    // non hanno una vicina da quel lato.
    const bool canLeft  = column > 0 && PrevVisibleColumn(app, column) > 0;
    const bool canRight = column > 0 && NextVisibleColumn(app, column) < kColumnCount;
    items.push_back({"", Tr("Sposta a sinistra", "Shift Column Left"), "",
                     kColumnOpsLeftId, OptionControl::None, "", "", false, 0.0f, canLeft});
    items.push_back({"", Tr("Sposta a destra", "Shift Column Right"), "",
                     kColumnOpsRightId, OptionControl::None, "", "", false, 0.0f, canRight});
    items.push_back({"", Tr("Adatta questa colonna", "Size Column To Fit"), "",
                     kColumnOpsFitId});
    items.push_back({"", Tr("Adatta tutte le colonne", "Size All Columns To Fit"), "",
                     kColumnOpsFitAllId});
    items.push_back({"", Tr("Chiudi colonna", "Close Column"),
                     closable ? std::string() : Tr("il nome resta", "Name stays"),
                     kColumnOpsCloseId});
    items.push_back({"", Tr("Ripristina le colonne", "Restore Default Columns"), "",
                     kColumnOpsResetId});
    // Le colonne "di fabbrica" ci sono e si vedono **spente**, con la ragione accanto:
    // e' la stessa regola del riferimento, dove `System Light / Dark Scheme` sta li' e
    // non si tocca invece di sparire. Le "di fabbrica" sarebbero quelle del prodotto
    // originale, che non conosciamo: dichiararlo e' meglio che inventarle.
    items.push_back({"", Tr("Colonne di fabbrica", "Restore Factory Columns"),
                     Tr("non le conosciamo", "not known here"),
                     kColumnOpsFactoryId, OptionControl::None, "", "", false, 0.0f, false});

    // Il contenuto di un segnaposto `Empty`. Compare solo sul menu di una di quelle
    // colonne, e da qualche parte bisogna pur dire cosa mostra: il posto naturale e' il
    // menu della colonna stessa, che e' dove si va quando una colonna non va bene. Le
    // scelte sono le colonne che una riga sa dire — il nome no, c'e' gia' ed e' quello che
    // prende lo spazio che resta; i segnaposto no, perche' un segnaposto che punta a un
    // altro segnaposto e' una catena che non finisce.
    if (IsEmptyColumn(column))
    {
        const int chosen = app.emptyColumnSource[column - kEmptyColumnFirst];
        items.push_back({Tr("Contenuto", "Contents"), Tr("Vuota", "Empty"), "",
                         kColumnOpsEmptyBase});
        for (int i = 1; i < kEmptyColumnFirst; ++i)
        {
            items.push_back({Tr("Contenuto", "Contents"),
                             (i == chosen ? "[x] " : "[ ] ") + ColumnLabel(i), "",
                             kColumnOpsEmptyBase + i});
        }
    }

    // Le spunte di visibilita'. Il nome non c'e': e' la colonna che prende lo spazio
    // che resta e chiuderla vorrebbe dire un elenco senza nomi — il perche' sta scritto
    // nella riga `Chiudi colonna`, un posto sopra.
    for (int i = 1; i < kColumnCount; ++i)
    {
        const bool shown = ColumnVisible(app, i);
        items.push_back({Tr("Colonne", "Columns"),
                         (shown ? "[x] " : "[ ] ") + ColumnTitle(app, i), "",
                         kColumnOpsColumnBase + i});
    }

    GroupMenuOpen(app.columnOps, ColumnTitle(app, column),
                  Tr("Cerca un'operazione...", "Find an operation..."), std::move(items));
    app.columnOps.anchored    = true;
    app.columnOps.caption     = Tr("Adatta, chiudi o riporta le colonne di partenza.",
                                   "Size, close or restore the columns.");
    app.columnOps.command     = Tr("Opzioni della colonna", "Show Column Options");
    app.columnOps.commandKeys = {Tr("Tasto destro", "MouseRight")};
}

// Cosa fa l'Invio, o un clic, in quel menu.
void RunColumnOpsSelection(AppState& app)
{
    if (app.columnOpsIndex < 0)
        return;
    const int id     = GroupMenuSelectedId(app.columnOps);
    const int column = app.columnOpsIndex;

    // Una spunta di visibilita' **non chiude** il pannello: e' una lista di caselle, e
    // se ne spuntano piu' d'una di seguito. Il segno cambia sul posto, senza ricostruire
    // l'elenco — ricostruirlo rimetterebbe il cursore in cima a ogni spunta.
    // Il contenuto di un segnaposto. Come le spunte di visibilita' **non chiude** il
    // pannello: dopo aver scelto si vuole vedere il risultato nella lista che resta
    // sotto, non rimettere tutto com'era.
    if (id >= kColumnOpsEmptyBase)
    {
        const int target = id - kColumnOpsEmptyBase;
        if (target < 0 || target >= kEmptyColumnFirst || !IsEmptyColumn(column))
            return;

        app.emptyColumnSource[column - kEmptyColumnFirst] = target;

        // Le spunte si aggiornano **sul posto**, come quelle di visibilita': ricostruire
        // l'elenco rimetterebbe il cursore in cima a ogni scelta. E le larghezze si
        // riadattano, perche' il contenuto nuovo puo' essere piu' largo di «Vuota 3».
        for (GroupMenuItem& item : app.columnOps.items)
        {
            if (item.id < kColumnOpsEmptyBase)
                continue;
            const int which = item.id - kColumnOpsEmptyBase;
            item.label = (which == target ? "[x] " : "[ ] ") +
                         (which == 0 ? std::string(Tr("Vuota", "Empty")) : ColumnLabel(which));
        }
        FitColumns(app);
        app.viewDirty = true;
        return;
    }
    if (id >= kColumnOpsColumnBase)
    {
        const int target = id - kColumnOpsColumnBase;
        if (target <= 0 || target >= kColumnCount)
            return;
        SetColumnVisible(app, target, !ColumnVisible(app, target));
        // Accendere una colonna che non c'era vuol dire che le altre si erano prese il
        // suo spazio: senza riadattare, la nuova nasce larga venti pixel e si legge
        // `36 E` invece di `36 B`. Le larghezze tornano "adattate al contenuto", che
        // e' il valore di partenza di una colonna appena comparsa.
        if (ColumnVisible(app, target))
            FitColumns(app);
        for (GroupMenuItem& item : app.columnOps.items)
        {
            if (item.id < kColumnOpsColumnBase)
                continue;
            const int which = item.id - kColumnOpsColumnBase;
            item.label = (ColumnVisible(app, which) ? "[x] " : "[ ] ") + ColumnTitle(app, which);
        }
        app.viewDirty = true;
        return;
    }

    app.columnOps.open = false;
    app.columnOpsIndex = -1;

    switch (id)
    {
        case kColumnOpsLeftId:   ShiftColumn(app, column, -1); break;
        case kColumnOpsRightId:  ShiftColumn(app, column, 1); break;
        case kColumnOpsFitId:    FitColumn(app, column); break;
        case kColumnOpsFitAllId: app.autoFitColumns = false; FitColumns(app); break;
        case kColumnOpsCloseId:  SetColumnVisible(app, column, false); break;
        case kColumnOpsResetId:  ResetColumns(app); break;
        default:                 return;   // spenta: la voce non si sceglie
    }
    app.viewDirty = true;
}

void BuildColumnMenu(AppState& app, int column)
{
    // Il pannello si appoggia al bordo sinistro della sua colonna: e' cosi' che si
    // vede a quale intestazione appartiene, senza doverlo scrivere.
    const Layout layout = ComputeLayout(app);
    app.columnMenu.anchor = Rect{layout.header.x + layout.header.w *
                                     app.columns[static_cast<size_t>(column)].fraction,
                                 layout.header.y, 0.0f, layout.header.h};

    std::vector<std::pair<std::string, int>> counts;
    std::unordered_map<std::string, int> index;
    for (const Entry& entry : app.entries)
    {
        const std::string value = ColumnValueOf(app, entry, column);
        if (value.empty())
            continue;
        auto found = index.find(value);
        if (found == index.end())
        {
            index[value] = static_cast<int>(counts.size());
            counts.push_back({value, 0});
        }
        counts[static_cast<size_t>(index[value])].second += 1;
    }

    std::sort(counts.begin(), counts.end(),
              [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                  if (a.second != b.second)
                      return a.second > b.second;
                  return a.first < b.first;
              });
    if (counts.size() > 40)
        counts.resize(40);   // dichiarato: oltre, l'elenco non si legge piu'

    app.columnPanelValues = std::move(counts);
    app.columnPanel       = column;

    std::vector<GroupMenuItem> items;
    items.push_back({"", Tr("Tutti", "All"), "", kFilterAllId});
    items.push_back({"", Tr("Nessuno", "None"), "", kFilterNoneId});
    for (int i = 0; i < static_cast<int>(app.columnPanelValues.size()); ++i)
    {
        const std::pair<std::string, int>& value = app.columnPanelValues[static_cast<size_t>(i)];
        const bool kept = app.columns[static_cast<size_t>(column)].excluded.count(value.first) == 0;
        items.push_back({"", (kept ? "[x] " : "[ ] ") + value.first,
                         std::to_string(value.second), i});
    }

    app.columnMenu.detailed = true;
    GroupMenuOpen(app.columnMenu, ColumnTitle(app, column),
                  Tr("Scrivi per filtrare i valori...", "Type to filter values..."),
                  std::move(items));
    app.columnMenu.anchored = true;
    // La testata del comando: che cosa fa questo pannello e da dove si apre anche da
    // tastiera. E' quello che il riferimento scrive in cima al menu dei valori.
    app.columnMenu.caption = Tr("Scegli quali valori tenere in questa colonna.",
                                "Choose which values to keep in this column.");
    app.columnMenu.command = Tr("Apri l'elenco dei valori", "Open Values Dropdown");
}

// Cosa fa l'Invio, o un clic, dentro il pannello dei valori. Non lo chiude: una
// lista di caselle si spunta **a piu' riprese**, ed e' tutto il motivo per cui e'
// una lista e non un singolo interruttore.
void RunColumnMenuSelection(AppState& app)
{
    if (app.columnPanel < 0)
        return;
    const int id = GroupMenuSelectedId(app.columnMenu);
    auto& column = app.columns[static_cast<size_t>(app.columnPanel)];

    if (id == kFilterAllId)
    {
        column.excluded.clear();
    }
    else if (id == kFilterNoneId)
    {
        for (const std::pair<std::string, int>& value : app.columnPanelValues)
            column.excluded.insert(value.first);
    }
    else if (id >= 0 && id < static_cast<int>(app.columnPanelValues.size()))
    {
        const std::string& value = app.columnPanelValues[static_cast<size_t>(id)].first;
        if (column.excluded.count(value) != 0)
            column.excluded.erase(value);
        else
            column.excluded.insert(value);
    }

    RefreshColumnMenuChecks(app);
    app.viewDirty = true;
    app.scroll    = 0.0f;
}

Rect PropertiesPanelRect(const AppState& app, const Layout& layout)
{
    const float scale = layout.scale;
    const float width = (std::min)(620.0f * scale,
                                   static_cast<float>(app.viewportWidth) - 80.0f * scale);
    const float height = (std::min)(520.0f * scale,
                                    static_cast<float>(app.viewportHeight) - 100.0f * scale);
    return Rect{(static_cast<float>(app.viewportWidth) - width) * 0.5f, 70.0f * scale,
                width, height};
}

// Il pulsante in fondo al pannello: riapre la finestra di Windows. E' l'unica cosa
// del pannello su cui si clicca, e dice a parole cosa apre, perche' non e' un
// comando che si indovina.
Rect PropertiesWindowsButton(const AppState& app, const Layout& layout, const Rect& panel)
{
    const float scale = layout.scale;
    const std::string label = Tr("Proprietà di Windows...", "Windows properties...");
    const float width = app.smallFont.Measure(label, scale) + 20.0f * scale;
    return Rect{panel.right() - width - 14.0f * scale, panel.bottom() - 28.0f * scale,
                width, 20.0f * scale};
}

bool PropertiesPanelKey(AppState& app, WPARAM key)
{
    switch (key)
    {
        case VK_ESCAPE: app.propertiesOpen = false; return true;
        case VK_UP:     app.propertiesScroll -= 22.0f; return true;
        case VK_DOWN:   app.propertiesScroll += 22.0f; return true;
        case VK_PRIOR:  app.propertiesScroll -= 200.0f; return true;
        case VK_NEXT:   app.propertiesScroll += 200.0f; return true;
        case VK_HOME:   app.propertiesScroll = 0.0f; return true;
        case VK_END:    app.propertiesScroll = 1e9f; return true;
        default:        return true;   // modale: non passa niente alla lista
    }
}

// La cattura del tasto. Il campo di ricerca diventa il **messaggio** — «premi un tasto
// per Rinomina» — invece di un secondo riquadro: e' il posto in cui si sta gia'
// guardando, avendo appena premuto Invio su quella riga.
void BeginKeyCapture(AppState& app, int command)
{
    app.keyCaptureCommand = command;

    // Ogni id che arriva qui viene dalla tabella, quindi il comando si trova sempre:
    // l'elenco non ha altre righe.
    std::string label;
    for (const CommandDef& candidate : kCommandTable)
    {
        if (candidate.id == command)
        {
            label = CommandLabel(candidate);
            break;
        }
    }

    // Il filtro si **svuota**: con un termine dentro, al posto del messaggio si
    // leggerebbe il termine, e chi aspetta leggerebbe la cosa sbagliata. L'elenco si
    // rifa' subito e il cursore va sul comando che si sta per rimappare.
    app.keyMap.filter.clear();
    GroupMenuRebuild(app.keyMap);
    SelectKeyMapCommand(app, command);
    app.keyMap.placeholder = std::string(Tr("Premi un tasto per ", "Press a key for ")) + label;
    // La riga in fondo dice anche **perche'** un tasto puo' non bastare: un rifiuto
    // muto sembrerebbe un tasto che non arriva. E lo dice **prima**, quando il tasto
    // che si sta spostando e' quello che riapre questo elenco.
    app.keyMap.hint =
        OpensThisList(command)
            ? Tr("Attenzione: e' il tasto che apre questo elenco   Esc annulla   Canc toglie",
                 "Careful: this key opens this list   Esc cancels   Del removes")
            : Tr("Ctrl, Alt o un tasto funzione   Esc annulla   Canc toglie",
                 "Ctrl, Alt or a function key   Esc cancels   Del removes");
}

// Un tasto mentre si aspetta il tasto nuovo. Non e' un comando da eseguire: e' il
// **dato** che si sta scrivendo, ed e' per questo che viene chiamata prima di ogni
// altra lettura della tastiera.
void KeyMapKeyDown(AppState& app, WPARAM key)
{
    const int command = app.keyCaptureCommand;
    if (key == VK_ESCAPE)
    {
        EndKeyCapture(app, command);
        return;
    }
    // `Canc` toglie il tasto invece di diventarlo: e' il gesto di tutti gli editor di
    // scorciatoie, ed e' per questo che non si puo' assegnare a niente.
    if (key == VK_DELETE)
    {
        app.keys.Clear(command);
        EndKeyCapture(app, command);
        return;
    }

    // I modificatori si leggono dallo **stato**, non dal messaggio: `GetKeyState` e'
    // l'unico che li vede, e li vede anche quando il tasto arriva iniettato — un
    // modificatore dentro `SendMessage` per la finestra non esiste.
    const Shortcut pressed{static_cast<unsigned>(key),
                           (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                           (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                           (GetKeyState(VK_MENU) & 0x8000) != 0};
    // Un modificatore da solo, o un tasto che serve a scrivere, non e' una risposta:
    // si **resta in attesa** invece di assegnare qualcosa di muto o di rompere il
    // filtro. L'altra via d'uscita e' `Esc`, e la riga in fondo lo dice.
    if (!KeyMap::Assignable(pressed))
        return;

    app.keys.Rebind(command, pressed);
    EndKeyCapture(app, command);
}

// Cosa fa l'Invio (o un clic) su una riga: comincia la cattura. **Non** esegue il
// comando — eseguirlo sarebbe il contrario di quello che si sta chiedendo — e non
// chiude l'elenco: si rimappano piu' comandi di seguito, ed e' l'unica cosa che si
// viene a fare qui.
void RunKeyMapSelection(AppState& app)
{
    const int id = GroupMenuSelectedId(app.keyMap);
    if (id == kKeyMapResetId)
    {
        app.keys.Reset();
        BuildKeyMapMenu(app, kKeyMapResetId);
        ReportStatus(app, TrW(L"Tasti riportati a quelli di partenza",
                              L"Key map restored to its defaults"));
        return;
    }
    if (id < 0)
        return;
    BeginKeyCapture(app, id);
}

// Cosa fa la riga scelta: la stessa funzione per le due domande, perche' sono la
// stessa lista vista al contrario.
void RunPinMenuSelection(AppState& app)
{
    const int id = GroupMenuSelectedId(app.pinMenu);
    app.pinMenu.open = false;
    if (id < 0 || id >= kPinTargetCount)
        return;

    const PinTarget target = PinTargetAt(id);
    const std::wstring path = app.pinMenuTarget;
    std::vector<std::wstring>& list = PinList(app, target);
    const bool already = PinnedIn(app, target, path);

    std::wstring message;
    if (app.pinMenuRemoving)
    {
        if (!already)
        {
            message = std::wstring(TrW(L"Non era fissata in ", L"Was not pinned to ")) +
                      Widen(PinTargetLabel(target));
        }
        else
        {
            list.erase(std::find(list.begin(), list.end(), path));
            message = std::wstring(TrW(L"Tolta da ", L"Unpinned from ")) +
                      Widen(PinTargetLabel(target));
        }
    }
    else if (already)
    {
        message = std::wstring(TrW(L"Era gia' fissata in ", L"Was already pinned to ")) +
                  Widen(PinTargetLabel(target));
    }
    else
    {
        list.push_back(path);
        message = std::wstring(TrW(L"Fissata in ", L"Pinned to ")) +
                  Widen(PinTargetLabel(target));
    }

    ReportStatus(app, message);

    // La Home elenca i fissaggi in una banda: se e' aperta, la banda adesso e'
    // cambiata. Rifarla subito, invece di aspettare che l'utente prema F5.
    if (IsHome(app.directory))
        LoadDirectory(app, app.directory, /*recordHistory=*/false);
}

bool OperationPanelVisible(const AppState& app)
{
    return app.operation != nullptr && NowSeconds() - app.operationStart >= kOperationPanelDelay;
}

Rect OperationPanelRect(const AppState& app, const Layout& layout)
{
    const float scale = layout.scale;
    const float width  = (std::min)(520.0f * scale,
                                    static_cast<float>(app.viewportWidth) - 80.0f * scale);
    const float height = 172.0f * scale;
    return Rect{(static_cast<float>(app.viewportWidth) - width) * 0.5f,
                (static_cast<float>(app.viewportHeight) - height) * 0.5f, width, height};
}

Rect OperationCancelButton(const AppState& app, const Layout& layout, const Rect& panel)
{
    const float scale = layout.scale;
    const std::string label = Tr("Annulla", "Cancel");
    const float width = app.smallFont.Measure(label, scale) + 24.0f * scale;
    return Rect{panel.right() - width - 16.0f * scale, panel.bottom() - 32.0f * scale, width,
                22.0f * scale};
}

// Annulla l'operazione in corso, e con lei quello che aspetta: chi ferma una copia non
// si aspetta che ne parta un'altra appena quella e' morta. Il numero delle richieste
// buttate via si ricorda (`operationQueueDropped`) perche' l'esito lo racconta dopo
// `FinishOperation`, che non le ha viste.
//
// Lo chiamano le due cose che possono fermare un'operazione: `Esc` e il pulsante.
void CancelOperation(AppState& app)
{
    if (app.operation == nullptr)
        return;

    app.operation->Cancel();
    app.operationQueueDropped = static_cast<int>(app.operationQueue.size());
    app.operationQueue.clear();
    ReportStatus(app, TrW(L"Annullamento...", L"Cancelling..."));
}

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

    // Una voce del gruppo dei recenti puo' essere un **comando** invece di un
    // percorso: e' cosi' che «Cancella i recenti» sta li' senza essere un percorso
    // che non esiste.
    const int command = (id < static_cast<int>(app.goToCommands.size()))
                            ? app.goToCommands[static_cast<size_t>(id)]
                            : -1;
    app.goTo.open = false;
    if (command >= 0)
    {
        RunCommand(app, command);
        return;
    }

    const std::wstring& target = app.goToTargets[static_cast<size_t>(id)];
    // Un **file** aperto di recente si apre, non si naviga: `LoadDirectory` su un
    // file non avrebbe niente da leggere.
    if (!PathIsDirectoryW(target.c_str()))
        ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else
        LoadDirectory(app, target);
}

// `Riavvia ora`: il setup e' gia' sul disco e verificato. Si avvia **adesso** con due
// secondi di attesa — un programma che sta uscendo tiene il proprio eseguibile, e
// l'installer in silenzioso, invece di aspettare, rinuncia senza dirlo (`docs/02`
// §7.75) — poi questa finestra si chiude, Inno aspetta che sia chiusa, installa, e
// **riapre Bolt** dal percorso installato.
//
// Non si riapre l'eseguibile da cui si e' partiti: con il pacchetto portatile quello
// resta la versione vecchia, e riaprirlo vorrebbe dire rimettersi davanti lo stesso
// avviso.
void BeginInstallUpdate(AppState& app)
{
    if (app.updateSetupPath.empty() || app.window == nullptr)
        return;

    app.updateInstalling = true;

    // Dove finisce un'installazione per utente: `DefaultDirName={autopf}\Bolt` in
    // `installer/bolt.iss`, cioe' `%LOCALAPPDATA%\Programs\Bolt`.
    wchar_t localAppData[MAX_PATH] = {};
    std::wstring installed;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0)
        installed = std::wstring(localAppData) + L"\\Programs\\Bolt\\Bolt.exe";

    std::wstring command = L"/c ping -n 3 127.0.0.1 >nul & \"" + app.updateSetupPath +
                           L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
    if (!installed.empty())
        command += L" & start \"\" \"" + installed + L"\"";

    const HINSTANCE started = ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(),
                                            nullptr, SW_HIDE);
    if (reinterpret_cast<INT_PTR>(started) > 32)
    {
        DiagLog("--- aggiornamenti: setup avviato, Bolt si chiude per l'installazione");
        PostMessageW(app.window, WM_CLOSE, 0, 0);
        return;
    }

    app.updateInstalling = false;
    app.updateError = TrW(L"il setup non si avvia (errore ", L"the setup cannot be started (error ") +
                      std::to_wstring(GetLastError()) + L").";
    app.updateResult = Update::Result::Failed;
    DiagLog("--- aggiornamenti: setup non avviato, errore " +
            std::to_string(GetLastError()));
}

} // namespace Bolt

// --- trascinamento: la parte che sa la vista -------------------------------
//
// Il modulo OLE (`drag_drop.cpp`) non sa cosa sia una riga o una cartella aperta:
// sa leggere un oggetto dati e consegnare una caduta. La destinazione la decide
// chi conosce l'elenco, cioe' queste tre funzioni.

namespace DragDrop
{

DragDrop::Target TargetAt(HWND window, POINT client)
{
    DragDrop::Target target;
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr)
        return target;

    const float x = static_cast<float>(client.x);
    const float y = static_cast<float>(client.y);

    // Un trascinamento nato da una cartella di spazio dei nomi porta oggetti della
    // shell, che non hanno un percorso: metterli "dentro" una cartella nostra non e'
    // una cosa che sappiamo fare. Si trascinano fuori, verso un'altra applicazione,
    // che di quegli oggetti sa cosa fare.
    if (app->draggingNamespaceItems)
        return target;

    // Il riquadro di navigazione. Le sue righe sono gia' bersagli di clic, quindi la
    // geometria la sa `HitTest`: rifarla qui vorrebbe dire sbagliarla appena una
    // sezione si chiude.
    if (const ClickTarget* place = HitTest(*app, x, y, ClickTarget::Kind::Place))
    {
        target.path       = place->path;
        target.sidebarRow = place->index;
        if (!app->recycleBinPath.empty() && place->path == app->recycleBinPath)
            target.verb = DragDrop::DropVerb::Discard;
        else if (!ShellLocation::IsNamespace(place->path) && !IsHome(place->path))
            target.verb = DragDrop::DropVerb::DropInto;
        // "Questo PC", la Home, o un'altra cartella di spazio dei nomi: non si sa
        // dove mettere i file. Rifiutare e' meglio che promettere e non fare.
        return target;
    }

    // Con due pannelli la caduta arriva sempre a **questa** finestra, che e' una sola:
    // il pannello lo decide il punto del rilascio, non il fuoco. Cambiare pannello
    // mentre si trascina vorrebbe dire spostare la destinazione sotto le mani di chi
    // sta mollando, e chi trascina da qui non vuole perdere il segno.
    const int pane = PaneAtPoint(*app, x, y);
    if (pane < 0)
        return target;   // fuori dai pannelli non si lascia cadere niente

    if (pane != app->activePane)
    {
        // L'**altro** pannello: la sua cartella e le sue righe stanno nel tab acceso,
        // perche' un pannello che non si sta guardando tiene le voci li'
        // (`SwapLiveWithPane` le scambia al disegno). Lo stato vivo e' dell'altro, e
        // leggerlo qui vorrebbe dire prendere le righe sbagliate.
        const Pane& other = app->panes[static_cast<size_t>(pane)];
        if (other.activeTab < 0 || other.activeTab >= static_cast<int>(other.tabs.size()))
            return target;
        const Tab& tab = other.tabs[static_cast<size_t>(other.activeTab)];
        const std::wstring& directory = tab.directory;

        // Una posizione che non e' un percorso (la Home, "Questo PC") non ha dove
        // ricevere i file: si rifiuta, invece di promettere e non fare.
        if (directory.empty() || IsHome(directory) || ShellLocation::IsNamespace(directory))
            return target;

        if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
        {
            if (row->index >= 0 && row->index < static_cast<int>(tab.entries.size()))
            {
                const Entry& entry = tab.entries[static_cast<size_t>(row->index)];
                // Una cartella si apre per lasciarci qualcosa dentro. Il percorso si
                // compone qui e non con `EntryLocation`: quella attacca la coda alla
                // cartella **viva**, che e' dell'altro pannello.
                if (entry.isDirectory && entry.location.empty())
                {
                    target.path    = JoinPath(directory, entry.wideName);
                    target.verb    = DragDrop::DropVerb::DropInto;
                    target.ontoRow = true;
                    return target;
                }
            }
        }

        // Il resto del pannello: la sua cartella. Un file sotto il puntatore vale come
        // il fondo della lista, come nell'altro pannello.
        target.path = directory;
        target.verb = DragDrop::DropVerb::DropInto;
        return target;
    }

    if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
    {
        const Entry& entry = app->entries[static_cast<size_t>(row->index)];
        // Il percorso di una riga lo sa `EntryLocation`: sulla Home il nome della
        // riga e' un'identita', non una coda da attaccare alla cartella aperta.
        const std::wstring full = EntryLocation(*app, entry);
        if (entry.isDirectory)
        {
            // Una cartella va aperta per lasciarci qualcosa dentro: la riga mostra
            // il nome, la destinazione e' il percorso.
            target.path    = full;
            target.verb    = DragDrop::DropVerb::DropInto;
            target.ontoRow = true;
            return target;
        }
        // Sopra un file la destinazione e' la cartella che lo contiene, come in
        // Esplora risorse: li' un file non riceve niente, ma la lista si'. Sulla
        // Home non ci sono file, e se ci fossero la cartella aperta non e' un
        // percorso: si rifiuta.
        if (!IsHome(app->directory))
        {
            target.path = app->directory;
            target.verb = DragDrop::DropVerb::DropInto;
        }
        return target;
    }

    const Layout layout = ComputeLayout(*app);
    if (layout.list.Contains(x, y) && !IsHome(app->directory))
    {
        target.path = app->directory;
        target.verb = DragDrop::DropVerb::DropInto;
    }
    return target;   // fuori dalla lista non si lascia cadere niente
}

void Highlight(HWND window, const DragDrop::Target& target)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr)
        return;

    const std::wstring previous = app->dropTarget;
    const bool previousRow      = app->dropTargetIsRow;
    const int  previousSidebar  = app->dropTargetSidebarRow;

    app->dropTarget           = target.path;
    app->dropTargetIsRow      = target.ontoRow && !target.Empty();
    app->dropTargetSidebarRow = target.Empty() ? -1 : target.sidebarRow;

    // L'evidenziazione va vista ADESSO: se il trascinamento e' nato da qui, il
    // ciclo principale e' fermo dentro `DoDragDrop`, e senza questo disegno la
    // riga non si accenderebbe mai. Solo quando cambia: `DragOver` arriva a ogni
    // movimento del mouse, e un fotogramma per movimento sarebbe lavoro sprecato.
    if (app->dropTarget != previous || app->dropTargetIsRow != previousRow ||
        app->dropTargetSidebarRow != previousSidebar)
        RenderFrame(*app);
}

// Un rilascio che non produce niente **si spiega**: i file erano gia' nella cartella
// dove li si e' mollati. Non e' un errore — la barra di stato non e' un registro di
// guasti — ma un gesto che finisce in silenzio sembra un programma che non risponde.
void SameFolderAlready(HWND window)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr)
        return;
    ReportStatus(*app, TrW(L"Sono gia' in questa cartella: non c'e' niente da spostare.",
                           L"Already in this folder: nothing to move."));
}

// L'altro "niente" che va detto: il pannello sotto il puntatore non mostra una
// cartella — la Home, "Questo PC", un posto dello spazio dei nomi. Il puntatore lo ha
// gia' detto restando "vietato", e questo non cambia cosa si puo' fare; dice solo
// perche', perche' un rilascio che non fa niente sembra un programma che non risponde.
//
// La domanda e' della **vista**: che cartella mostra un pannello lo sa solo lei, e la
// risposta cambia col fuoco e con la scheda accesa.
void NoFolderHere(HWND window, POINT client)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr)
        return;

    const float x = static_cast<float>(client.x);
    const float y = static_cast<float>(client.y);
    const int pane = PaneAtPoint(*app, x, y);
    if (pane < 0)
        return;   // fuori dai pannelli non c'e' nessuna vista da spiegare

    // La cartella del pannello sotto il puntatore: quello attivo la tiene in vita, gli
    // altri nella scheda accesa — un pannello che non si sta guardando non ha campi vivi.
    std::wstring shown;
    if (pane == app->activePane)
    {
        shown = app->directory;
    }
    else
    {
        const Pane& other = app->panes[static_cast<size_t>(pane)];
        if (other.activeTab >= 0 && other.activeTab < static_cast<int>(other.tabs.size()))
            shown = other.tabs[static_cast<size_t>(other.activeTab)].directory;
    }
    // Una cartella c'e': il "niente" aveva un'altra ragione, e non e' questa da dire.
    if (!shown.empty() && !IsHome(shown) && !ShellLocation::IsNamespace(shown))
        return;

    ReportStatus(*app, TrW(L"Qui non c'e' una cartella dove mettere i file.",
                           L"There is no folder here to put files in."));
}

bool Drop(HWND window, const std::vector<std::wstring>& sources, const DragDrop::Target& target,
          bool move)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr || sources.empty())
        return false;

    const bool discard = target.verb == DragDrop::DropVerb::Discard;

    FileOps::Request request;
    request.action      = discard ? FileOps::Action::Recycle
                                  : (move ? FileOps::Action::Move : FileOps::Action::Copy);
    request.sources     = sources;
    request.destination = discard ? std::wstring() : target.path;

    // Se un'operazione e' gia' in corso questa va **in coda** e restituisce `true`:
    // la caduta e' accettata, e chi ha lasciato andare il tasto deve riceverlo — un
    // `DROPEFFECT_NONE` gli farebbe credere di non aver prodotto niente.
    const bool started = BeginOperation(*app, std::move(request), OperationSource::Drop);

    // La caduta dichiara che l'esito lo dira' questa finestra **solo se e' partita**:
    // chi ha lasciato andare il tasto riceve l'effetto prima che l'operazione sia
    // finita, e senza questo segno crederebbe che l'abbia fatta un altro programma e
    // racconterebbe lui come e' andata. Se non e' partita, l'effetto e' "niente" e
    // non c'e' niente da raccontare.
    if (started)
        app->dropReported = true;
    return started;
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
    // L'argomento si **ricorda** e basta: se e' una cartella lo si chiede dopo
    // `LoadSettings`, perche' la risposta dipende da `LongPathsSupport` — e
    // `GetFileAttributesW` su un percorso oltre i 260 caratteri fallisce finche' il
    // prefisso non c'e', quindi chiederlo qui vorrebbe dire rifiutare l'argomento
    // prima di sapere se l'opzione e' accesa.
    std::wstring startArgument;
    {
        int argumentCount = 0;
        if (LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount))
        {
            if (argumentCount > 1)
                startArgument = NormalizePath(arguments[1]);
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
    // l'applicazione e' scura: una linea bianca su un bordo scuro si vede. Chi lo
    // allinea e' `ApplyTheme`, che conosce la tavolozza in uso — e la tavolozza si
    // conosce solo dopo aver letto le impostazioni, piu' sotto, quando la finestra e'
    // gia' registrata nello stato. Sta li' anche il cambio di tema dalla palette,
    // cosi' le due facce della stessa scelta non possono divergere.

    // Gli angoli arrotondati di Windows 11 valgono per le finestre con
    // didascalia; questa non ne ha, quindi vanno chiesti.
    const DWORD cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference,
                          sizeof(cornerPreference));

    // --- il backend: Direct3D 11 se c'e', altrimenti OpenGL ---------------------
    //
    // L'originale espone `GraphicsBackend` con `Direct3D` e `OpenGL` e lo sceglie a
    // runtime; qui la scelta e' la stessa, con una variabile d'ambiente per
    // forzarla (`BOLT_BACKEND=opengl` o `d3d11`) e il **fallback** sempre pronto: un
    // backend che non parte non deve impedire di usare il programma.
    AppState app;
    std::string error;

    HDC   dc      = nullptr;
    HGLRC context = nullptr;
    UIRenderer    glRenderer;
    D3D11Renderer d3dRenderer(window);
    bool usingOpenGL = false;

    // `getenv_s` e non `getenv`: la versione "unsafe" e' segnalata a /W4, e la
    // scelta del backend non vale un warning.
    char   requested[32] = {};
    size_t requestedLength = 0;
    getenv_s(&requestedLength, requested, sizeof(requested), "BOLT_BACKEND");
    const bool wantOpenGl = _stricmp(requested, "opengl") == 0;
    const bool wantD3D    = _stricmp(requested, "d3d11") == 0;

    if (!wantOpenGl)
    {
        if (d3dRenderer.Initialize(&error))
        {
            app.renderer = &d3dRenderer;
        }
        else if (wantD3D)
        {
            ShowError(error);
            return 1;
        }
        else
        {
            // Non e' un errore: il Direct3D non c'e', si ripiega sull'OpenGL.
            DiagLog(std::string("--- backend Direct3D 11 non disponibile: ") + error);
            error.clear();
        }
    }

    if (app.renderer == nullptr)
    {
        if (!CreateOpenGLContext(window, &dc, &context))
        {
            ShowError("Could not create an OpenGL context.");
            return 1;
        }

        const char* missingFunction = nullptr;
        if (!LoadOpenGLFunctions(&missingFunction))
        {
            ShowError(std::string("Missing OpenGL entry point: ") +
                      (missingFunction ? missingFunction : "?"));
            return 1;
        }

        glRenderer.Attach(window, dc, context);
        // Gli shader si cercano **accanto all'eseguibile** (`ShaderDirectory` in
        // `ui_renderer.cpp`), e solo se li' non ci sono nel percorso di compilazione:
        // un programma installato non puo' leggere dal disco di chi l'ha costruito.
        // Qui si passa dalla versione dell'interfaccia, che quella scelta la fa; il
        // percorso di compilazione non si nomina piu' da fuori.
        if (!glRenderer.Initialize(&error))
        {
            ShowError(error);
            return 1;
        }
        app.renderer = &glRenderer;
        usingOpenGL  = true;
    }

    DiagLog(std::string("--- backend: ") + app.renderer->Name());

    // One atlas size for every layer, so the glyph, icon and thumbnail UVs
    // cannot diverge. Layer 0 holds the body glyphs, layer 1 the shell icons,
    // layer 2 the file thumbnails, layer 3 the secondary glyphs.
    // Il livello delle texture serve anche a `ApplyFonts`, quindi la costante sta
    // in cima al file; qui restano le due misure dell'anteprima.
    constexpr int kPreviewCellSize  = 128;
    constexpr int kPreviewCapacity  = 64;

    // Sei strati: i quattro di sempre, il carattere a spaziatura fissa e l'immagine
    // dell'anteprima grande.
    if (!app.renderer->CreateTextureArray(/*layers=*/6, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    if (!app.font.Build(*app.renderer, L"Segoe UI", 15, kGlyphLayer, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    // The secondary size is rasterised rather than scaled down. Drawing the body
    // atlas at 0.8-0.95 resamples glyphs that were hinted for 15 px, which is
    // exactly where small text loses its crispness; a second body costs one more
    // atlas layer and makes the whole secondary tier sharp.
    if (!app.smallFont.Build(*app.renderer, L"Segoe UI",
                             15 - AppState::kSmallFontDelta, kSmallGlyphLayer,
                             kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    // Il carattere del testo. Se non c'e' — non tutte le installazioni hanno
    // Consolas — si ripiega su quello secondario: si legge lo stesso, le colonne non
    // sono allineate, e va detto invece di far finta.
    app.monoFontOk = app.monoFont.Build(*app.renderer, L"Consolas", 14, kMonoLayer,
                                        kTextureLayerSize, &error);
    if (!app.monoFontOk)
        error.clear();

    if (!app.icons.Initialize(*app.renderer, kIconLayer, kTextureLayerSize, &error))
    {
        ShowError(error);
        return 1;
    }

    if (!app.previews.Initialize(*app.renderer, kPreviewLayer, kTextureLayerSize,
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

    // Le preferenze si rileggono **prima** della prima costruzione della vista: la
    // lingua e l'aspetto devono essere quelli giusti al primo fotogramma.
    LoadSettings(app);
    // L'atlante dei glifi e' stato costruito con i valori di partenza; se il file
    // delle impostazioni ne chiede altri, si rifa' adesso. Sono le stesse tre
    // misure che comanda il pannello delle opzioni, e si validano la' dentro.
    if (app.fontFace != L"Segoe UI" || app.uiFontSize != 15 ||
        app.monoFace != L"Consolas" || app.monoFontSize != 14)
        ApplyFonts(app);
    InitColumns(app);
    BuildPlaces(app);
    app.window = window;
    // La finestra esiste: la tavolozza si applica adesso, e con essa il riquadro di
    // DWM. Prima non si poteva, perche' `ApplyTheme` passa dalla finestra per il bordo.
    ApplyTheme(app);

    std::wstring startDirectory;
    if (!startArgument.empty())
    {
        // `IOPath` mette il prefisso solo se l'opzione e' accesa, ed e' l'unico modo
        // perche' l'argomento di una cartella molto profonda venga accettato.
        const std::wstring probe = IOPath(startArgument, app.longPaths);
        const DWORD attributes  = GetFileAttributesW(probe.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            startDirectory = startArgument;   // si tiene il nome **com'e' stato scritto**
        // Un nome di spazio dei nomi non ha attributi da leggere: si riconosce da
        // come e' scritto, ed e' l'unico modo di aprire Bolt sul cestino.
        else if (ShellLocation::IsNamespace(startArgument))
            startDirectory = startArgument;
    }

    // La **pagina di avvio** (`settings.ini`, `startupFolder`): vale solo quando dalla
    // riga di comando non e' arrivato niente — un percorso chiesto esplicitamente
    // vince sempre sulla preferenza.
    if (startDirectory.empty() && !app.startupFolder.empty())
    {
        // "Questo PC" si salva come nome di spazio dei nomi: non ha attributi da
        // leggere, e senza questo caso la preferenza verrebbe scartata come una
        // cartella sparita.
        if (ShellLocation::IsNamespace(app.startupFolder))
        {
            startDirectory = app.startupFolder;
        }
        else
        {
            const std::wstring probe = IOPath(app.startupFolder, app.longPaths);
            const DWORD attributes  = GetFileAttributesW(probe.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                startDirectory = app.startupFolder;
            }
            else
            {
                // Una cartella che non c'e' piu' non si apre, e non si apre in
                // silenzio: chi l'aveva scelta deve sapere perche' si ritrova
                // altrove.
                ReportStatus(app, TrW(L"La cartella di avvio non esiste piu': apro la Home",
                                      L"The startup folder no longer exists: opening Home"));
            }
        }
    }

    const std::wstring initialDirectory = startDirectory.empty()
                                              ? std::wstring(kHomeLocation)
                                              : startDirectory;

    // Un pannello solo, con dentro un tab: la finestra nasce come prima dei pannelli.
    // `panes` non e' mai vuoto — e' quello che permette a tutto il resto di scrivere
    // `panes[activePane]` senza chiedersi se c'e'.
    app.panes.push_back(Pane{});
    app.panes[0].tabs.push_back(Tab{});
    app.activePane = 0;
    // La sessione, se e' stato chiesto di ricordarla: le schede di ieri si riaprono.
    // Un argomento sulla riga di comando vince — chi ha chiesto una cartella vuole
    // quella, non le sue schede di ieri.
    const bool restored = startDirectory.empty() && RestoreSession(app);
    if (!restored)
        LoadDirectory(app, initialDirectory);

    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    // La sorveglianza delle modifiche esterne. Se non parte si va avanti lo
    // stesso — il programma funziona, e' l'aggiornamento automatico che manca —
    // ma si dice: un aggiornamento che non arriva sembra un difetto dell'app e
    // non lo e'.
    if (!app.watcher.Start(window, kDirectoryChanged))
        DiagLog("--- sorveglianza delle cartelle non disponibile");
    if (!IsHome(app.directory))
        app.watcher.Watch(app.directory);

    // Da qui in poi la finestra accetta file trascinati da fuori. Se il sistema
    // non vuole registrarla lo si dice: un trascinamento che non arriva sembra un
    // difetto dell'applicazione, ed e' un limite del sistema.
    if (!DragDrop::Register(window))
        ReportStatus(app, TrW(L"Trascinamento in entrata non disponibile su questo sistema.",
                              L"Incoming drag and drop is not available on this system."));

    ShowWindow(window, SW_SHOW);

    // Lo stato di disegno che in OpenGL e' globale e in Direct3D 11 lo mette il
    // backend (blend, niente profondita', niente culling) sta qui solo per
    // l'OpenGL.
    if (usingOpenGL)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
    }

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
        // Solo il caso delle **statistiche**, che e' l'unico che ha bisogno di un
        // tetto: un gruppo che dipende dai numeri va rifatto mentre i numeri
        // arrivano, ma non una volta per fotogramma. Tutto il resto — filtro,
        // ordinamento, raggruppamento, cartella — si rifa' in `RenderFrame`, subito
        // prima di disegnare, perche' fra questo punto e il disegno c'e'
        // `FinishOperation` che **cambia l'elenco** (`docs/02` §7.59).
        if (app.groupMode != GroupMode::None && statsRevision != app.viewStatsRevision &&
            now - app.lastViewRebuild > 0.25)
        {
            RebuildView(app);
            app.viewStatsRevision = statsRevision;
            app.lastViewRebuild   = now;
        }

        // La ricerca globale avanza **qui**: un passo a ogni giro del ciclo, che
        // succede comunque. Con un `SetTimer` la ricerca non partiva affatto — la
        // barra si svuotava e la lista restava quella di prima, cioe' il passo non
        // arrivava mai. Un giro che c'e' gia' non puo' non arrivare.
        if (app.search.active)
            AdvanceSearch(app);
        // Il controllo degli aggiornamenti lo racconta la sua finestra, che legge
        // `Progress()` mentre disegna; qui si raccoglie l'esito **una volta sola**,
        // quando il thread ha finito. La barra di stato non dice piu' niente: era il
        // posto sbagliato per un lavoro con una durata (`docs/04` M13).
        // punto e', e quando ha finito l'esito si legge qui, una volta sola.
        // Il lavoro lo racconta la sua finestra, che legge `Progress()` mentre disegna;
        // qui si raccoglie l'esito **una volta sola**, quando il thread ha finito. La
        // barra di stato non dice piu' niente: era il posto sbagliato per un lavoro con
        // una durata (`docs/04` M13).
        if (app.updateJob != nullptr && !app.updateJob->Running())
            FinishUpdate(app);

        // `HoverToSelect`: fermandosi su una riga la si seleziona, dopo una pausa
        // breve. Non mentre si trascina e non con un menu aperto: sono gesti che
        // stanno usando il puntatore per un'altra cosa.
        if (app.hoverToSelect && app.hovered >= 0 &&
            app.hovered < static_cast<int>(app.entries.size()) && !app.dragArmed &&
            OpenMenu(app) == nullptr && !app.contextMenu.open &&
            now - app.hoverStart > 0.35 && !app.renameActive)
        {
            const Entry& entry = app.entries[static_cast<size_t>(app.hovered)];
            if (app.selection.count(entry.wideName) == 0 || app.cursor != entry.wideName)
            {
                app.selection.clear();
                app.selection.insert(entry.wideName);
                app.cursor = entry.wideName;
            }
        }

        // Un'operazione lunga finita: l'esito si raccoglie qui, una volta sola, e il
        // pannello si chiude. Prima di disegnare, o l'ultimo fotogramma mostrerebbe un
        // pannello che ha gia' finito.
        if (app.operation != nullptr && !app.operation->Running())
            FinishOperation(app);

        RenderFrame(app);
    }

    SetWindowLongPtrW(window, GWLP_USERDATA, 0);

    // Anche un controllo degli aggiornamenti a meta' si ferma: e' rete, e il thread
    // che la sta usando non deve sopravvivere alla finestra che lo ha chiesto.
    if (app.updateJob != nullptr)
        app.updateJob->Cancel();

    // Un'operazione a meta' non si abbandona: il thread tiene lo stato che il pannello
    // sta leggendo, e quello stato non puo' sopravvivere all'applicazione. Si chiede di
    // fermarsi e si aspetta, **prima** di `CoUninitialize` — dopo, il thread di lavoro
    // sarebbe l'unico rimasto con COM addosso.
    if (app.operation != nullptr)
    {
        app.operation->Cancel();
        app.operation->Finish(nullptr, nullptr);
        app.operation.reset();
    }

    // Le richieste in coda non sono mai partite, quindi non c'e' niente da fermare:
    // si buttano, e la finestra se ne va senza lasciare un thread dietro.
    app.operationQueue.clear();

    app.stats.Stop();
    OleUninitialize();
    app.watcher.Stop();
    app.previews.Shutdown();
    app.renderer->Shutdown();
    if (usingOpenGL)
    {
        wglMakeCurrent(nullptr, nullptr);
        if (context != nullptr)
            wglDeleteContext(context);
        if (dc != nullptr)
            ReleaseDC(window, dc);
    }
    DestroyWindow(window);
    CoUninitialize();
    return 0;
}
