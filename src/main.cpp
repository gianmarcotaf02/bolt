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
#include "input_targets.h"
#include "language.h"
#include "navigation.h"
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
#include "app_actions.h"

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



// Scegliere una tavolozza dalla palette o dalle opzioni: si applica subito e si
// ricorda. Sceglierne una vuol dire **smettere** di seguire il sistema: e' quello che
// uno si aspetta premendo "Tema: chiaro", e l'alternativa sarebbe un comando che
// sembra non fare niente. L'esito lo dice chi chiama, perche' dalla palette il
// pannello si chiude e senza una riga di stato non si saprebbe cosa e' cambiato.
void SetColorScheme(AppState& app, ColorScheme scheme)
{
    app.colorScheme       = scheme;
    app.systemColorScheme = false;
    ApplyTheme(app);
    RememberSettings(app);
}

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
    const char* const kNavGlyphs[5] = {"←", "→", "↑", "↻", "★"};
    const bool navEnabled[5] = {
        app.historyIndex > 0,
        app.historyIndex + 1 < static_cast<int>(app.history.size()),
        !app.directory.empty(),
        true,
        !app.directory.empty(),
    };
    constexpr float kNavSize = 26.0f;
    float navX = layout.toolbar.x + 8.0f * scale;
    for (int i = 0; i < 5; ++i)
    {
        const Rect button{navX, layout.toolbar.y + 5.0f * scale, kNavSize * scale,
                          kNavSize * scale};
        const bool bookmarked =
            (i == 4) && std::find(app.bookmarks.begin(), app.bookmarks.end(), app.directory) !=
                            app.bookmarks.end();
        if (i == app.hoveredNav && navEnabled[i])
            r.FillRect(button, t.rowHover, 5.0f * scale);
        else if (bookmarked)
            r.FillRect(button, t.accent.WithAlpha(0.18f), 5.0f * scale);

        // La stella e' piena quando la cartella e' nei segnalibri: e' l'unico segno
        // che dice se il comando aggiunge o toglie.
        const char* glyph = (i == 4 && !bookmarked) ? "☆" : kNavGlyphs[i];
        const Color color = !navEnabled[i] ? t.textDim.WithAlpha(0.4f)
                          : bookmarked      ? t.accent
                                            : t.textDim;
        const float glyphWidth = app.font.Measure(glyph, scale);
        app.font.Draw(r, button.x + (button.w - glyphWidth) * 0.5f,
                      TextY(layout, button, app.font), glyph, color, scale);

        ClickTarget target;
        target.rect  = button;
        target.kind  = ClickTarget::Kind::NavButton;
        target.index = i;
        app.clickTargets.push_back(target);

        navX += (kNavSize + 2.0f) * scale;
    }

    float penX = navX + 8.0f * scale;
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
        // In una cartella dello spazio dei nomi la briciola e' il nome che la
        // shell le da', che non e' una posizione: cliccarla deve riportare li', non
        // cercare una cartella chiamata "Cestino".
        target.path  = ShellLocation::IsNamespace(app.directory) ? app.directory : accumulated;
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
        case CmdSortModified:           return "Ordina per data";
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
        case CmdColorSchemeLight:       return "Tema: chiaro";
        case CmdColorSchemeDark:        return "Tema: scuro";
        case CmdSystemColorScheme:      return "Tema: segui il sistema";
        case CmdOpenOptions:            return "Apri le opzioni";
        case CmdEditKeyMap:             return "Modifica i tasti";
        case CmdPinTo:                  return "Fissa questa cartella in...";
        case CmdUnpin:                  return "Togli questa cartella da...";
        case CmdProperties:             return "Proprietà";
        case CmdPropertiesWindows:      return "Proprietà di Windows";
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
    GroupMenuOpen(app.columnMenu, ColumnLabel(column),
                  Tr("Scrivi per filtrare i valori...", "Type to filter values..."),
                  std::move(items));
    app.columnMenu.anchored = true;
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
void DrawTooltip(AppState& app, const Layout& layout)
{
    if (!app.showTooltips || app.hovered < 0 || app.columnDrag >= 0 || OpenMenu(app) != nullptr ||
        app.contextMenu.open)
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
        if (app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats) && !stats.partial)
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
    {"Language", "Where the settings file is",  CmdShowSettingsPath},
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

void EndKeyCapture(AppState& app, int command)
{
    app.keyCaptureCommand = -1;
    BuildKeyMapMenu(app, command);
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

// Il pannello delle destinazioni: due righe, e la riga che **vale** porta la ragione
// accanto al nome invece di essere disegnata spenta. Il componente dei menu non sa
// disegnare una riga spenta — lo sa solo il pannello delle opzioni — e per due righe
// non vale la pena darglielo: la riga si puo' scegliere lo stesso, e sceglierla **lo
// dice**. Un elenco di due righe non ha bisogno di un meccanismo in piu' per dire
// "questa non si tocca".
void BuildPinMenu(AppState& app, bool removing)
{
    const std::wstring& path = app.directory;
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

    const char* note = removing ? Tr(" (non e' fissata qui)", " (not pinned here)")
                                : Tr(" (e' gia' fissata qui)", " (already pinned here)");

    std::vector<GroupMenuItem> items;
    items.reserve(kPinTargetCount);
    for (int i = 0; i < kPinTargetCount; ++i)
    {
        const PinTarget target = PinTargetAt(i);
        GroupMenuItem item;
        item.label = std::string(PinTargetLabel(target)) + (applicable(target) ? "" : note);
        item.id    = i;
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


// Cosa fa la riga scelta: la stessa funzione per le due domande, perche' sono la
// stessa lista vista al contrario.
void RunPinMenuSelection(AppState& app)
{
    const int id = GroupMenuSelectedId(app.pinMenu);
    app.pinMenu.open = false;
    if (id < 0 || id >= kPinTargetCount)
        return;

    const PinTarget target = PinTargetAt(id);
    const std::wstring path = app.directory;
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
}

// --- building the two menus -------------------------------------------------
void BuildPaletteMenu(AppState& app)
{
    std::vector<GroupMenuItem> items;
    for (const CommandDef& command : kCommandTable)
    {
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
    }

    // Locations are data, not code: one entry per known folder, so the palette
    // covers the sidebar without a command being declared for each.
    for (int i = 0; i < static_cast<int>(app.places.size()); ++i)
    {
        items.push_back(GroupMenuItem{"Places", app.places[static_cast<size_t>(i)].label, "",
                                      kPalettePlaceBase + i});
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
    // Col dettaglio acceso il gruppo fa da dettaglio, che e' quello che la palette
    // mostra da sempre: il flag dice a chi disegna che li' il ripiego e' voluto.
    app.palette.detailFallsBackToGroup = true;
    app.palette.hint = std::string(Tr("Invio esegue   Esc chiude   ", "Enter runs   Esc closes   ")) +
                       app.keys.LabelFor(CmdPalette) + std::string(Tr(" apre e chiude   ", " toggles   ")) +
                       app.keys.LabelFor(CmdOpenOptions) + std::string(Tr(" opzioni", " options"));
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

// I comandi che agiscono su un **percorso** non valgono dove un percorso non c'e':
// nel cestino le voci si spostano, si cancellano e si ripristinano coi verbi della
// shell, che quegli oggetti li conoscono. Qui si dice, invece di far finta che sia
// un guasto e lasciare l'utente a chiedersi perche' non succede niente.
bool RefuseInNamespace(AppState& app, const wchar_t* action)
{
    if (!ShellLocation::IsNamespace(app.directory))
        return false;
    ReportStatus(app, std::wstring(action) +
                          TrW(L": qui non si puo', questa cartella non e' un percorso. "
                              L"Le voci si gestiscono coi comandi del menu contestuale.",
                              L": not here, this folder is not a path. "
                              L"Use the context menu commands for these items."));
    return true;
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

bool OperationPanelVisible(const AppState& app)
{
    return app.operation != nullptr && NowSeconds() - app.operationStart >= kOperationPanelDelay;
}

// Un'operazione lunga si avvia sempre da qui: cosi' la sorgente e l'ora non si
// possono dimenticare in uno dei tre posti che la chiamano. Restituisce falso se
// un'operazione e' gia' in corso: la seconda sostituirebbe la prima, e sostituirla
// vuol dire annullarla **senza dirlo**.
bool BeginOperation(AppState& app, std::unique_ptr<FileOps::Job> job, OperationSource source)
{
    if (app.operation != nullptr)
    {
        ReportStatus(app, TrW(L"C'e' gia' un'operazione in corso.",
                              L"An operation is already running."));
        return false;
    }

    app.operation       = std::move(job);
    app.operationSource = source;
    app.operationStart  = NowSeconds();
    return true;
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

// La riga sotto il titolo: dove stanno andando le cose, o quante se ne buttano via.
std::wstring OperationSubtitle(const FileOps::Job& job)
{
    switch (job.action())
    {
        case FileOps::Action::Copy:
        case FileOps::Action::Move:
            return TrW(L"in ", L"into ") + LeafName(job.destination());
        default:
            return CountItems(job.sources().size());
    }
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
                  FitText(app.font, ToUtf8(OperationSubtitle(*app.operation)), scale,
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

// Il pannello e' modale come quello delle proprieta': prende la tastiera e non
// lascia passare niente. `Esc` chiede di annullare, che e' l'unica cosa sensata che
// possa fare un tasto mentre qualcosa e' in corso.
bool OperationPanelKey(AppState& app, WPARAM key)
{
    if (key == VK_ESCAPE && app.operation != nullptr)
    {
        app.operation->Cancel();
        ReportStatus(app, TrW(L"Annullamento...", L"Cancelling..."));
    }
    return true;
}

// L'operazione e' finita: si raccoglie l'esito, si aggiorna la vista e si dice com'e'
// andata. E' l'unico posto in cui si legge il risultato, ed e' il motivo per cui le
// tre operazioni lunghe non hanno piu' un finale ciascuna.
void FinishOperation(AppState& app)
{
    std::unique_ptr<FileOps::Job> job = std::move(app.operation);
    if (job == nullptr)
        return;

    const FileOps::Action action = job->action();
    const std::vector<std::wstring> sources = job->sources();
    const std::wstring destination = job->destination();
    const OperationSource source = app.operationSource;
    const float scroll = app.scroll;

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
        ReportStatus(app, discarded ? TrW(L"Eliminazione annullata", L"Delete cancelled")
                                    : TrW(L"Operazione annullata", L"Operation cancelled"));
        // Quello che era partito prima dell'annullamento c'e' ancora: la vista si
        // rifa', tenendo la selezione dove ancora esiste.
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
        ReloadKeeping(app, keep, landed, scroll);

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

    ReloadKeeping(app, keep, landed, scroll);
    ReportStatus(app, VerbItems(sources.size(),
                                action == FileOps::Action::Move ? L"Spostato " : L"Incollato ",
                                action == FileOps::Action::Move ? L"Spostati " : L"Incollati ",
                                action == FileOps::Action::Move ? L"Moved " : L"Pasted "));
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

// Nome predefinito di una cartella nuova, come quello di Esplora risorse.
// Il nome di partenza di una cartella nuova: e' l'unico nome di file che scriviamo
// noi, quindi segue la lingua dell'interfaccia, come fa Esplora risorse.
std::wstring NewFolderName() { return TrW(L"Nuova cartella", L"New folder"); }

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
        ReportStatus(app, TrW(L"Rinominato in ", L"Renamed to ") + landed);
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, TrW(L"Rinomina non riuscita: ", L"Rename failed: ") + error);
    }
}

void DoCopyToClipboard(AppState& app, bool cut)
{
    const std::vector<std::wstring> paths = SelectedPaths(app);
    if (paths.empty())
        return;

    if (!FileOps::SetClipboardFiles(app.window, paths, cut))
    {
        ReportStatus(app, TrW(L"Clipboard non disponibile.", L"Clipboard unavailable."));
        return;
    }

    app.clipboardCut = cut;
    ReportStatus(app, VerbItems(paths.size(),
                                cut ? L"Tagliato " : L"Copiato ",
                                cut ? L"Tagliati " : L"Copiati ",
                                cut ? L"Cut " : L"Copied "));
}

void DoPaste(AppState& app)
{
    std::vector<std::wstring> paths;
    bool cut = false;
    if (!FileOps::ReadClipboardFiles(&paths, &cut))
    {
        ReportStatus(app, TrW(L"Niente da incollare.", L"Nothing to paste."));
        return;
    }

    // Nel registro, perche' una volta un incolla e' partito senza che si riuscisse a
    // capire da quale gesto: senza una riga, l'unica traccia era un messaggio di
    // stato che dura quattro secondi.
    DiagLog("  incolla: " + std::to_string(paths.size()) + " percorsi, " +
            (cut ? "tagliati" : "copiati") + ", in " + ToUtf8(LeafName(app.directory)));

    // L'operazione adesso e' lunga: si avvia sul suo thread e l'esito si raccoglie
    // quando finisce, in `FinishOperation`. Qui si dice solo che e' partita.
    BeginOperation(app,
                   FileOps::Job::StartTransfer(app.window, paths, app.directory, cut),
                   OperationSource::Command);
}

void DoDelete(AppState& app, bool permanently)
{
    const std::vector<std::wstring> paths = SelectedPaths(app);
    if (paths.empty())
        return;

    // Anche l'eliminazione e' lunga — un albero di cartelle intero — e passa dallo
    // stesso pannello della copia.
    BeginOperation(app, FileOps::Job::StartDelete(app.window, paths, permanently),
                   OperationSource::Command);
}

void DoNewFolder(AppState& app)
{
    const float scroll = app.scroll;
    std::vector<std::wstring> created;
    std::wstring error;
    const std::wstring wanted = NewFolderName();
    const FileOps::Result result = FileOps::CreateFolder(
        app.window, app.directory, wanted.c_str(), &created, &error);

    if (result == FileOps::Result::Ok)
    {
        const std::wstring name = created.empty() ? wanted : LeafName(created.front());
        std::unordered_set<std::wstring> keep;
        keep.insert(name);
        ReloadKeeping(app, keep, name, scroll);

        // Creare e rinominare sono un'azione sola: chi preme Ctrl+Shift+N vuole
        // dare un nome, non guardare un'altra "Nuova cartella".
        BeginRename(app);
    }
    else if (result == FileOps::Result::Failed)
    {
        ReportStatus(app, TrW(L"Cartella non creata: ", L"Folder not created: ") + error);
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
            ReportStatus(app, TrW(L"Il modello di questo tipo non si e' potuto copiare: "
                                  L"il file e' stato creato vuoto.",
                                  L"The template for this type could not be copied: "
                                  L"the file was created empty."));
            return;
        }
    }
    else
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            ReportStatus(app, TrW(L"Il file e' stato creato, ma il contenuto del modello no.",
                                  L"The file was created, but not the template content."));
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
            ReportStatus(app, TrW(L"Il file e' stato creato, ma il contenuto del modello no.",
                                  L"The file was created, but not the template content."));
            return;
        }
    }

    // Il modello c'era ma era vuoto: il file e' nato vuoto, e chi lo apre lo deve
    // sapere adesso e non quando l'applicazione gli dira' che e' danneggiato.
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        info.nFileSizeHigh == 0 && info.nFileSizeLow == 0)
    {
        ReportStatus(app, TrW(L"Il modello di questo tipo e' vuoto: ",
                              L"The template for this type is empty: ") +
                              LeafName(path) + TrW(L" e' stato creato vuoto.",
                                                   L" was created empty."));
    }
}

// --- menu execution ---------------------------------------------------------
// `RunCommand` e `ShowProperties` arrivano dagli header (`app_actions.h`,
// `view_context_menu.h`).

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


// --- menu contestuale: geometria, disegno, interazione ----------------------
// Il menu contestuale e' un modulo (`view_context_menu.*`): qui resta il clic che lo
// apre e la regola di quando un clic solo attiva.

// Un clic solo attiva? Dipende da due chiavi, e la regola sta in un posto solo
// perche' i tre gesti che la usano (pressione, rilascio, doppio clic) non possano
// rispondere in modo diverso.
bool SingleClickActivates(const AppState& app, const Entry& entry)
{
    if (!app.singleClick)
        return false;
    if (app.singleClickFoldersOnly && !entry.isDirectory)
        return false;   // sui file serve ancora il doppio clic
    return true;
}

// --- i livelli del menu contestuale ----------------------------------------
//
// Un menu non e' una lista: e' un albero di liste. Lo chiamiamo *livello*: 0 e'
// il primo, 1 il sottomenu aperto, e cosi' via. `openPath` tiene gli indici delle
// voci aperte, uno per livello, e `levels` la scelta e la finestra di ogni
// livello. Il livello piu' profondo aperto e' quello su cui si agisce.



void DrawFrame(AppState& app, const Layout& layout)
{
    app.clickTargets.clear();

    float columnX[kColumnCount] = {};

    app.renderer->FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                               static_cast<float>(app.viewportHeight)}, app.theme.windowBg);

    DrawTitleBar(app, layout);
    DrawToolbar(app, layout, LocationLabel(app, app.directory));
    DrawSidebar(app, layout);
    DrawHeader(app, layout, columnX);

    // La lista disegna **solo dentro il suo rettangolo**, e il ritaglio lo fa il
    // scissor. Il controllo che salta le righe fuori dal riquadro non basta: salta
    // quelle che stanno fuori tutte intere, non taglia quelle a meta' — e una cella
    // di griglia e' alta quanto una miniatura, quindi il pezzo che esce dal bordo
    // finisce **sopra la barra del titolo**. Si vedeva: quattro icone disegnate
    // sopra le schede, con la riga di nomi piu' in basso.
    //
    // I quadrilateri si accumulano in un batch solo, quindi il batch va chiuso e
    // riaperto attorno al ritaglio: tre disegni per fotogramma invece di uno. E' il
    // prezzo del ritaglio, e si paga.
    app.renderer->End();
    app.renderer->SetScissor(layout.list);
    app.renderer->Begin(app.viewportWidth, app.viewportHeight);

    DrawList(app, layout, columnX);

    app.renderer->End();
    app.renderer->ClearScissor();
    app.renderer->Begin(app.viewportWidth, app.viewportHeight);

    // The rubber band is drawn over the rows it is about to select, and only over
    // the list: it is clamped to that region so dragging past the edge does not
    // look like it is selecting the sidebar.
    if (app.rectSelecting)
    {
        const float x0 = (std::min)(app.rectX0, app.rectX1);
        const float y0 = (std::min)(app.rectY0, app.rectY1);
        const Rect band{x0, y0, std::fabs(app.rectX1 - app.rectX0), std::fabs(app.rectY1 - app.rectY0)};
        app.renderer->FillRect(band, app.theme.accent.WithAlpha(0.18f), 2.0f * layout.scale);
        app.renderer->BorderRect(band, app.theme.accent.WithAlpha(0.7f), 1.0f, 2.0f * layout.scale);
    }

    DrawFilterBar(app, layout);
    DrawInspector(app, layout);
    DrawStatusBar(app, layout);

    // Last, so they cover everything and their click targets are the first ones
    // HitTest finds (it walks the list backwards). At most one is open.
    GroupMenuDraw(app, layout, app.palette);
    GroupMenuDraw(app, layout, app.goTo);
    // Il pannello dei valori sta attaccato all'intestazione da cui e' nato, e si
    // disegna per ultimo: e' l'unico che deve coprire la lista.
    GroupMenuDraw(app, layout, app.columnMenu);
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
    return MenuPanel(app, layout, menu).Contains(x, y) ? MenuAction::None : MenuAction::Close;
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

void ScrollBy(AppState& app, const Layout& layout, float delta)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    const float content = ViewContentHeight(app, layout);
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
    const Rect rect = ItemRect(app, layout, rowIndex, app.scroll);
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

    const Rect item = ItemRect(app, layout, rowIndex, app.scroll);
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

                // Il pannello dell'operazione in corso e' modale, e sta sopra a
                // tutto: la rotella non scorre niente sotto di lui.
                if (OperationPanelVisible(*app))
                    return 0;

                // Il pannello delle proprieta' e' modale: la rotella lo scorre e
                // basta.
                if (app->propertiesOpen)
                {
                    const int steps = static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                    app->propertiesScroll -= static_cast<float>(steps) * 44.0f;
                    if (app->propertiesScroll < 0.0f)
                        app->propertiesScroll = 0.0f;
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }

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

                // La stessa cosa per una lista a gruppi: la rotella muove il
                // cursore invece di scorrere la lista sotto, che e' coperta.
                if (GroupMenu* menu = OpenMenu(*app))
                {
                    // Rotella in su (delta positivo) muove il cursore in su.
                    const int steps = static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                    GroupMenuMove(*menu, -steps);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }

                const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;

                // Sopra il testo la rotella scorre il testo, e con Ctrl lo ingrandisce:
                // e' il gesto di chi sta leggendo, ed e' l'unico modo di arrivare in
                // fondo a un file lungo senza toccare la lista sotto.
                POINT pointer{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &pointer);
                if (app->textDocument.isText &&
                    app->textViewerRect.Contains(static_cast<float>(pointer.x), static_cast<float>(pointer.y)))
                {
                    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                        RunCommand(*app, steps > 0.0f ? CmdTextZoomIn : CmdTextZoomOut);
                    else
                    {
                        const float lineHeight = (app->monoFontOk ? app->monoFont : app->smallFont)
                                                     .LineHeight() * layout.scale * app->textZoom;
                        app->textScroll -= steps * 3.0f * lineHeight;
                    }
                    return 0;
                }

                const ViewMetrics metrics = ComputeViewMetrics(*app, layout);

                // Ctrl+rotella sopra la griglia: la cella si ingrandisce e si
                // stringe di continuo. Il nome della vista segue la misura, cosi'
                // il pie' di pagina e la palette dicono quello che si sta guardando
                // invece di quello che si e' chiuso l'ultima volta.
                if (metrics.grid && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    const float wanted = (std::max)(AppState::kGridIconMin,
                                                    (std::min)(AppState::kGridIconMax,
                                                               app->gridIconSize + steps * 16.0f));
                    if (wanted != app->gridIconSize)
                    {
                        app->gridIconSize = wanted;
                        app->viewMode = wanted <= AppState::kGridIconSplit ? ViewMode::Icons
                                                                          : ViewMode::Tiles;
                        app->viewDirty = true;
                    }
                    return 0;
                }

                const float stride = metrics.grid ? metrics.cellHeight : metrics.rowHeight;
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

            // Con un pannello a gruppi aperto il tasto destro non apre niente: la
            // finestra sotto e' coperta, e un menu contestuale dietro un pannello
            // non si potrebbe ne' vedere ne' usare.
            if (OpenMenu(*app) != nullptr)
                return 0;

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
            // Il trascinamento di un tab: si arma al primo movimento oltre la
            // soglia, e da li' in poi ogni movimento aggiorna dove finirebbe.
            if (app != nullptr)
            {
                const float tx = static_cast<float>(GET_X_LPARAM(lparam));
                const float ty = static_cast<float>(GET_Y_LPARAM(lparam));
                if (app->tabPress >= 0 && app->dragTab < 0 &&
                    std::fabs(tx - app->tabPressX) > 6.0f &&
                    app->tabPress < static_cast<int>(app->tabs.size()))
                {
                    app->dragTab  = app->tabs[static_cast<size_t>(app->tabPress)].id;
                    app->tabPress = -1;
                }
                if (app->dragTab >= 0)
                {
                    UpdateTabDrop(*app, tx, ty);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }

            // Il trascinamento di uno slider delle opzioni: finche' il pulsante e'
            // giu', la posizione del puntatore e' il valore. Se il pulsante e' stato
            // lasciato fuori dalla finestra, il trascinamento finisce qui invece di
            // riprendere al movimento successivo.
            if (app != nullptr && app->optionSliderDrag >= 0)
            {
                if ((wparam & MK_LBUTTON) == 0)
                {
                    app->optionSliderDrag = -1;
                }
                else
                {
                    const Layout layout = ComputeLayout(*app);
                    const float x = static_cast<float>(GET_X_LPARAM(lparam));
                    const Rect panel = MenuPanel(*app, layout, app->options);
                    for (GroupMenuItem& item : app->options.items)
                    {
                        if (item.id != app->optionSliderDrag || item.control != OptionControl::Slider)
                            continue;
                        const float scale  = layout.scale;
                        const float width  = OptionControlWidth(item, scale);
                        const float height = 22.0f * scale;
                        const float left   = panel.right() - 6.0f * scale - width - 14.0f * scale;
                        const float knob   = height * 0.5f;
                        const float track  = (std::max)(1.0f, width - knob * 2.0f);
                        float fraction = (x - (left + knob)) / track;
                        fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
                        item.fraction = fraction;
                        ApplyOptionFromItem(*app, item);
                        break;
                    }
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }

            // Il tempo del sovrappasso riparte quando il puntatore si muove: fermo
            // sulla stessa riga, il conto scorre; in movimento, no.
            if (app != nullptr)
            {
                const float mx = static_cast<float>(GET_X_LPARAM(lparam));
                const float my = static_cast<float>(GET_Y_LPARAM(lparam));
                if (std::fabs(mx - app->pointerX) > 2.0f || std::fabs(my - app->pointerY) > 2.0f)
                {
                    app->hoverStart = NowSeconds();
                    app->pointerX   = mx;
                    app->pointerY   = my;
                }
            }

            // Il trascinamento di un bordo: la colonna cresce e la vicina si
            // stringe, e nessuna delle due scende sotto la larghezza leggibile.
            if (app != nullptr && app->columnDrag > 0)
            {
                const Layout layout = ComputeLayout(*app);
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const int index = app->columnDrag;
                const float moved = app->columnDragStartFraction +
                                    (x - app->columnDragStartX) / (std::max)(1.0f, layout.header.w);
                const float minimum = kMinColumnWidth * layout.scale /
                                      (std::max)(1.0f, layout.header.w);
                const float left  = app->columns[index - 1].fraction + minimum;
                const float right = (index + 1 < kColumnCount ? app->columns[index + 1].fraction
                                                              : 1.0f) - minimum;
                app->columns[index].fraction = (std::min)(right, (std::max)(left, moved));
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
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
                        size_t dragged = paths.size();
                        app->dropReported = false;

                        // Da una cartella che non e' un percorso si trascina
                        // **l'oggetto della shell**, non un nome: il nome di una voce
                        // del cestino e' il percorso del file dentro `$Recycle.Bin`, e
                        // trascinare quello significherebbe trascinare un altro file.
                        if (ShellLocation::IsNamespace(app->directory))
                        {
                            const std::vector<const BYTE*> pidls = SelectedPidls(*app);
                            dragged = pidls.size();
                            app->draggingNamespaceItems = true;
                            if (IDataObject* data = DragDrop::DataObjectForPidls(window, pidls))
                            {
                                DragDrop::DragOutData(window, data, pidls.size(), &effect);
                                data->Release();
                            }
                            else
                            {
                                ReportStatus(*app, TrW(L"La shell non sa preparare queste voci per il "
                                                       L"trascinamento: si gestiscono col menu "
                                                       L"contestuale.",
                                                       L"The shell cannot prepare these items for "
                                                       L"dragging: use the context menu."));
                            }
                            app->draggingNamespaceItems = false;
                        }
                        else
                        {
                            DragDrop::DragOut(window, paths, &effect);
                        }

                        // Spostati davvero: gli originali non ci sono piu' e la
                        // vista li sta ancora mostrando.
                        if ((effect & DROPEFFECT_MOVE) != 0 && dragged > 0)
                        {
                            // Il messaggio lo da' chi ha eseguito la caduta, se e'
                            // stato uno di noi: sa dove sono finiti i file, e
                            // "Spostati 3 elementi" senza dire dove e' meno di
                            // "Buttati via 3 elementi nel cestino". Qui si parla
                            // solo se la destinazione era un'altra applicazione,
                            // che non ha modo di dirlo.
                            //
                            // Se l'ha eseguita questa finestra l'operazione sta
                            // ancora girando e la vista si rifa' quando finisce:
                            // rifarla adesso la svuoterebbe per poi rimetterla, che
                            // e' un lampo e non un'informazione.
                            if (!app->dropReported)
                            {
                                ReloadKeeping(*app, /*keep=*/{}, /*preferCursor=*/{}, app->scroll);
                                ReportStatus(*app, VerbItems(dragged, L"Spostato ", L"Spostati ",
                                                             L"Moved "));
                            }
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
                app->hoveredSection = -1;
                app->hoveredNav = -1;

                if (const ClickTarget* nav = HitTest(*app, x, y, ClickTarget::Kind::NavButton))
                    app->hoveredNav = nav->index;
                if (HitTest(*app, x, y, ClickTarget::Kind::Menu))              app->hoveredChrome = 0;
                else if (HitTest(*app, x, y, ClickTarget::Kind::TabNew))       app->hoveredChrome = 1;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMinimize)) app->hoveredChrome = 2;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowMaximize)) app->hoveredChrome = 3;
                else if (HitTest(*app, x, y, ClickTarget::Kind::WindowClose))    app->hoveredChrome = 4;

                const int wasHovered = app->hovered;
                if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::Tab))
                    app->hoveredTab = tab->index;
                else if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                    app->hovered = row->index;
                if (app->hovered != wasHovered)
                    app->hoverStart = NowSeconds();
                else if (const ClickTarget* place = HitTest(*app, x, y, ClickTarget::Kind::Place))
                    app->hoveredPlace = place->index;
                else if (const ClickTarget* header = HitTest(*app, x, y, ClickTarget::Kind::SidebarHeader))
                    app->hoveredSection = static_cast<int>(header->rect.y);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));

                // L'operazione in corso e' modale come un menu: prende il puntatore
                // prima di tutto il resto. Un clic fuori dal pannello non fa niente:
                // chiudere con un clic sarebbe un annullamento per sbaglio, ed e'
                // l'ultima cosa che si vuole mentre qualcosa si sta muovendo.
                if (OperationPanelVisible(*app))
                {
                    const Layout layout = ComputeLayout(*app);
                    const Rect panel = OperationPanelRect(*app, layout);
                    if (OperationCancelButton(*app, layout, panel).Contains(x, y))
                    {
                        app->operation->Cancel();
                        ReportStatus(*app, TrW(L"Annullamento...", L"Cancelling..."));
                    }
                    return 0;
                }

                // Il pannello delle proprieta' e' modale: il clic o cade sul suo
                // pulsante, o lo chiude, o non fa altro.
                if (app->propertiesOpen)
                {
                    const Layout layout = ComputeLayout(*app);
                    const Rect panel = PropertiesPanelRect(*app, layout);
                    if (PropertiesWindowsButton(*app, layout, panel).Contains(x, y))
                    {
                        app->propertiesOpen = false;
                        const std::vector<std::wstring> paths = SelectedPaths(*app);
                        if (paths.empty() || ShowProperties(app->window, paths) == 0)
                            ReportStatus(*app,
                                         TrW(L"Proprietà non disponibili per la selezione.",
                                             L"Properties unavailable for the selection."));
                    }
                    else if (!panel.Contains(x, y))
                    {
                        app->propertiesOpen = false;
                    }
                    return 0;
                }

                // Un bordo d'intestazione si prende e si trascina. La presa viene
                // prima di tutto il resto: mentre si trascina, la lista sotto non
                // deve rispondere ai clic.
                if (const ClickTarget* edge = HitTest(*app, x, y, ClickTarget::Kind::ColumnEdge))
                {
                    app->columnDrag = edge->index;
                    app->columnDragStartX = x;
                    app->columnDragStartFraction = app->columns[edge->index].fraction;
                    SetCapture(window);
                    return 0;
                }

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
                    const Layout layout = ComputeLayout(*app);
                    // Il pannello delle opzioni calcola da se' la riga e il
                    // controllo sotto il puntatore: non lascia bersagli nella
                    // lista, perche' la geometria ha una sola definizione.
                    MenuAction action = (menu == &app->options)
                                            ? OptionsClick(*app, layout, *menu, x, y)
                                            : GroupMenuClick(*app, layout, *menu, x, y);
                    if (action == MenuAction::Close)
                    {
                        // Chiudere il pannello mentre si scrive in un campo non deve
                        // lasciare la scrittura appesa: senza questo, il primo
                        // carattere digitato dopo sarebbe finito in un campo chiuso.
                        if (menu == &app->options)
                            CancelOptionEdit(*app);
                        // E chiudere l'editor dei tasti non deve lasciare appesa una
                        // cattura: un clic fuori dal pannello mentre si aspetta il
                        // tasto nuovo e' un modo normale di ripensarci.
                        if (menu == &app->keyMap)
                            CloseKeyMap(*app);
                        else
                            menu->open = false;
                    }
                    else if (action == MenuAction::Activate)
                    {
                        if (menu == &app->palette)
                            RunPaletteSelection(*app);
                        else if (menu == &app->goTo)
                            RunGoToSelection(*app);
                        else if (menu == &app->optionsChoice)
                            RunOptionsChoiceSelection(*app);
                        else if (menu == &app->keyMap)
                            RunKeyMapSelection(*app);
                        else if (menu == &app->pinMenu)
                            RunPinMenuSelection(*app);
                        else
                            RunColumnMenuSelection(*app);
                    }
                    return 0;
                }

                // La linguetta del filtro: apre l'elenco dei valori di quella
                // colonna, o richiude quello che c'e' gia'.
                if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::ColumnFilter))
                {
                    // Il nome e' la prima colonna, e il suo filtro non e' un elenco di
                    // valori: la linguetta accende la casella di testo in fondo, che
                    // e' il filtro del nome che serve davvero.
                    if (tab->index == 0)
                    {
                        app->columnMenu.open = false;
                        app->columnPanel     = -1;
                        app->filterActive    = true;
                    }
                    else if (app->columnPanel == tab->index && app->columnMenu.open)
                    {
                        app->columnMenu.open = false;
                        app->columnPanel     = -1;
                    }
                    else
                    {
                        BuildColumnMenu(*app, tab->index);
                    }
                    return 0;
                }

                // I selettori di vista e lo stepper delle anteprime, nella barra in
                // fondo. Cambiare vista da qui e' l'unico gesto col puntatore: prima
                // c'era solo la palette.
                if (const ClickTarget* v = HitTest(*app, x, y, ClickTarget::Kind::ViewSelect))
                {
                    app->viewMode  = static_cast<ViewMode>(v->index);
                    app->viewDirty = true;
                    return 0;
                }
                if (HitTest(*app, x, y, ClickTarget::Kind::PreviewDown))
                {
                    if (app->previewStep > 0)
                        --app->previewStep;
                    RememberSettings(*app);
                    return 0;
                }
                if (HitTest(*app, x, y, ClickTarget::Kind::PreviewUp))
                {
                    if (app->previewStep + 1 < AppState::kPreviewStepCount)
                        ++app->previewStep;
                    RememberSettings(*app);
                    return 0;
                }

                if (const ClickTarget* nav = HitTest(*app, x, y, ClickTarget::Kind::NavButton))
                {
                    // I pulsanti non fanno niente di nuovo: eseguono i comandi che
                    // esistono gia', quelli della tastiera e della palette. Una sola
                    // implementazione per ogni azione, o prima o poi divergono.
                    static const int kNavCommands[5] = {CmdBack, CmdForward, CmdGoUp, CmdRefresh,
                                                        CmdToggleBookmark};
                    if (nav->index >= 0 && nav->index < 5)
                        RunCommand(*app, kNavCommands[nav->index]);
                }
                else if (const ClickTarget* closeTab = HitTest(*app, x, y, ClickTarget::Kind::TabClose))
                {
                    CloseTab(*app, closeTab->index);
                }
                else if (const ClickTarget* tab = HitTest(*app, x, y, ClickTarget::Kind::Tab))
                {
                    ActivateTab(*app, tab->index);
                    // Il gesto puo' diventare un trascinamento, ma solo se il mouse
                    // si muove: un clic non deve spostare niente.
                    app->tabPress  = tab->index;
                    app->tabPressX = x;
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
                else if (const ClickTarget* box = HitTest(*app, x, y, ClickTarget::Kind::Checkbox))
                {
                    // Sceglie e basta: non apre, non arma un trascinamento, e non
                    // tocca il cursore. E' tutto quello che serve a una casella.
                    ToggleSelection(*app, box->index);
                    return 0;
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

                    // `ActOnPress`: con il clic singolo acceso, agire *premendo*
                    // invece che mollando. E' l'unica differenza fra le due chiavi.
                    if (app->actOnPress &&
                        SingleClickActivates(*app, app->entries[static_cast<size_t>(row->index)]))
                    {
                        app->dragArmed = false;   // il gesto e' finito qui: non parte un trascinamento
                        ActivateSelection(*app);
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
                else if (const ClickTarget* section = HitTest(*app, x, y,
                                                             ClickTarget::Kind::SidebarHeader))
                {
                    // Ricordare la sezione chiusa per nome e non per posizione: le
                    // righe si spostano quando i segnalibri cambiano, e una sezione
                    // chiusa non deve riaprirsi da sola perche' ne e' arrivata
                    // un'altra sopra.
                    if (app->collapsedSections.count(section->label) != 0)
                        app->collapsedSections.erase(section->label);
                    else
                        app->collapsedSections.insert(section->label);
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
            if (app != nullptr && app->optionSliderDrag >= 0)
            {
                // Fine del trascinamento di uno slider: il valore e' gia' stato
                // applicato a ogni movimento, qui si lascia solo la presa.
                app->optionSliderDrag = -1;
                return 0;
            }
            if (app != nullptr && app->dragTab >= 0)
            {
                ApplyTabDrop(*app);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (app != nullptr)
                app->tabPress = -1;
            if (app != nullptr && app->columnDrag > 0)
            {
                app->columnDrag = -1;
                ReleaseCapture();
                return 0;
            }
            if (app != nullptr)
            {
                // `SingleClick`: mollare su una riga la apre, se non e' partito un
                // trascinamento. `dragArmed` e' ancora vero solo se il mouse non si
                // e' mosso abbastanza da armarlo — e allora il gesto era un clic,
                // non un trascinamento.
                const bool wasClick = app->dragArmed;
                app->dragArmed = false;

                if (wasClick && app->singleClick && !app->actOnPress)
                {
                    const float x = static_cast<float>(GET_X_LPARAM(lparam));
                    const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                    if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                    {
                        const Entry& entry = app->entries[static_cast<size_t>(row->index)];
                        if (SingleClickActivates(*app, entry))
                            ActivateSelection(*app);
                    }
                }

                FinishRectSelection(*app);
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            // Doppio clic su un bordo: la colonna torna come era nata. E' il modo di
            // disfare un trascinamento senza doverlo indovinare a rovescio.
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                if (const ClickTarget* edge = HitTest(*app, x, y, ClickTarget::Kind::ColumnEdge))
                {
                    app->columns[edge->index].fraction = kColumns[edge->index].fraction;
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }
            if (app != nullptr)
            {
                const float x = static_cast<float>(GET_X_LPARAM(lparam));
                const float y = static_cast<float>(GET_Y_LPARAM(lparam));
                if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
                {
                    SelectOnly(*app, row->index);
                    ActivateSelection(*app);
                }
                else if (app->doubleClickToParent &&
                         ComputeLayout(*app).list.Contains(x, y))
                {
                    // `DoubleClickToParent`: il doppio clic sul fondo va su di un
                    // livello. E' il gesto che Windows chiama "doppio clic per
                    // salire", e vale solo sul fondo: su una riga il doppio clic
                    // apre, che e' un'altra cosa.
                    RunCommand(*app, CmdGoUp);
                }
            }
            return 0;

        case WM_KEYDOWN:
            if (app != nullptr)
            {
                // Il pannello dell'operazione in corso e' modale come un menu, e sta
                // sopra a tutto: prende la tastiera prima di ogni altra cosa.
                if (OperationPanelVisible(*app))
                {
                    OperationPanelKey(*app, wparam);
                    return 0;
                }

                // Il pannello delle proprieta' e' modale come un menu: prende la
                // tastiera prima di tutto il resto, e non lascia passare niente.
                if (app->propertiesOpen)
                {
                    PropertiesPanelKey(*app, wparam);
                    return 0;
                }

                const Layout layout = ComputeLayout(*app);

                // An open menu is modal, so it takes the keyboard first. Returning
                // here does not stop TranslateMessage from producing WM_CHAR, which
                // is where printable characters are routed to the menu filter.
                if (GroupMenu* menu = OpenMenu(*app))
                {
                    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const bool shift   = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                    // Il campo di testo di un'opzione, mentre si scrive, prende la
                    // tastiera come la rinomina: Backspace cancella una lettera del
                    // nome, non del filtro.
                    if (app->optionEditActive)
                    {
                        switch (wparam)
                        {
                            case VK_RETURN: CommitOptionEdit(*app); break;
                            case VK_ESCAPE: CancelOptionEdit(*app); break;
                            case VK_BACK:
                                if (!app->optionEditText.empty())
                                    app->optionEditText.pop_back();
                                break;
                            default: break;
                        }
                        return 0;
                    }

                    // La cattura del tasto sta **prima** di ogni altra lettura: mentre
                    // e' aperta il tasto premuto e' il dato che si sta scrivendo, non
                    // un comando — altrimenti `Ctrl+P` aprirebbe il GoTo invece di
                    // diventare il tasto di un comando.
                    if (app->keyCaptureCommand >= 0)
                    {
                        KeyMapKeyDown(*app, wparam);
                        return 0;
                    }

                    // Ogni menu si richiude col suo tasto. Le scorciatoie sono
                    // quelle del riferimento: Ctrl+P e F4 al GoTo, Ctrl+Maiusc+P ai
                    // comandi, Ctrl+, alle opzioni.
                    const bool toggling = control &&
                        ((menu == &app->goTo    && wparam == 'P' && !shift) ||
                         (menu == &app->palette && wparam == 'P' && shift)  ||
                         ((menu == &app->options || menu == &app->keyMap) &&
                          wparam == VK_OEM_COMMA));
                    if (toggling)
                    {
                        if (menu == &app->options)
                            CancelOptionEdit(*app);
                        // Dall'editor si **torna alle opzioni**, perche' e' da li' che si
                        // e' aperto: il tasto che le apre le riapre, invece di
                        // chiudere tutto e lasciare il posto dov'era.
                        const bool backToOptions = (menu == &app->keyMap);
                        menu->open = false;
                        if (backToOptions)
                            BuildOptionsMenu(*app);
                        return 0;
                    }

                    // I tasti del **controllo** di una riga di opzione: Invio,
                    // Spazio e frecce. Su/Giu'/Home/Fine/Esc restano al componente.
                    if (menu == &app->options && OptionsKeyDown(*app, *menu, wparam))
                        return 0;

                    const MenuAction action = GroupMenuKeyDown(*menu, wparam);
                    if (action == MenuAction::Close)
                    {
                        if (menu == &app->options)
                            CancelOptionEdit(*app);
                        if (menu == &app->keyMap)
                            CloseKeyMap(*app);
                        else
                            menu->open = false;
                    }
                    else if (action == MenuAction::Activate)
                    {
                        if (menu == &app->palette)
                            RunPaletteSelection(*app);
                        else if (menu == &app->goTo)
                            RunGoToSelection(*app);
                        else if (menu == &app->optionsChoice)
                            RunOptionsChoiceSelection(*app);
                        else if (menu == &app->keyMap)
                            RunKeyMapSelection(*app);
                        else if (menu == &app->pinMenu)
                            RunPinMenuSelection(*app);
                        else
                            RunColumnMenuSelection(*app);
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

                // Prima di tutto il resto: **che comando e' questo tasto**? La
                // risposta la da' la mappa (un dato, che si puo' cambiare); i tasti
                // che non sono comandi — le frecce, l'Invio, `Ctrl+A` che seleziona
                // tutto, `Ctrl+rotella` — restano qui sotto, perche' sono
                // comportamenti dell'input e non voci di un menu.
                {
                    const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    const bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
                    const int command = app->keys.CommandFor(static_cast<unsigned>(wparam), ctrl, shift, alt);
                    if (command >= 0)
                    {
                        // **Lo stesso esecutore della palette e dei menu.** Prima il
                        // tasto e il comando erano due strade con lo stesso corpo
                        // scritto due volte — `case 'C': DoCopyToClipboard(...)` qui e
                        // `case CmdCopy:` la' — e due copie divergono sempre.
                        RunCommand(*app, command);
                        return 0;
                    }
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
                            // Sale di un livello, e da una radice di volume va a
                            // "This PC": la stessa strada del comando, non una
                            // seconda che puo' divergere.
                            RunCommand(*app, CmdGoUp);
                        }
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
                    case 'G':
                        // GoTo ha preso Ctrl+P e F4, come il riferimento. Ctrl+G
                        // resta libero: ciclare il raggruppamento sta su
                        // Ctrl+Maiusc+G, che e' l'unico uso rimasto.
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                            (GetKeyState(VK_SHIFT) & 0x8000) != 0)
                        {
                            app->groupMode = static_cast<GroupMode>((static_cast<int>(app->groupMode) + 1) % 6);
                            app->viewDirty = true;
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
                // La cattura di un tasto vuole **il tasto**, non il carattere che ne
                // esce: senza questo, premendo `C` per rimappare Copia il `c`
                // finirebbe nel filtro dell'elenco sotto.
                if (app->keyCaptureCommand >= 0)
                    return 0;

                // Il pannello dell'operazione in corso e' modale anche per i
                // caratteri: senza questo, una lettera digitata andrebbe nel filtro
                // della lista sotto il pannello.
                if (OperationPanelVisible(*app))
                    return 0;

                // Il pannello delle proprieta' e' modale, e i suoi tasti li ha gia'
                // presi `WM_KEYDOWN`: senza questo, un carattere digitato andrebbe
                // nel filtro della lista **sotto** il pannello.
                if (app->propertiesOpen)
                    return 0;

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
                    else if (app->optionEditActive)
                    {
                        // Il campo di testo di un'opzione: il carattere va nel
                        // nome, non nel filtro del pannello che lo contiene.
                        const std::wstring wide(1, character);
                        app->optionEditText += ToUtf8(wide);
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
            // La cattura prende anche i tasti di sistema: `Alt` li rende tali, e senza
            // questo `Alt+qualcosa` non si potrebbe assegnare — e `Alt+F4` chiuderebbe
            // la finestra mentre si sta scegliendo un tasto.
            if (app != nullptr && app->keyCaptureCommand >= 0)
            {
                KeyMapKeyDown(*app, wparam);
                return 0;
            }

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
            // Alt+Su sale di un livello, e da una radice di volume porta a "This
            // PC". Arriva come tasto di sistema come Alt+freccia laterale.
            if (app != nullptr && wparam == VK_UP)
            {
                RunCommand(*app, CmdGoUp);
                return 0;
            }
            break;

        case WM_SYSCHAR:
            // `Alt`+lettera produce anche questo, oltre a `WM_SYSKEYDOWN`: senza
            // rispondere, la cattura di un tasto con `Alt` suonerebbe.
            if (app != nullptr && app->keyCaptureCommand >= 0)
                return 0;
            break;

        case WM_SETCURSOR:
            // Su un bordo d'intestazione il puntatore dice che li' si trascina: senza
            // questo, la striscia di presa esiste e non si vede.
            if (app != nullptr && LOWORD(lparam) == HTCLIENT)
            {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                const Layout layout = ComputeLayout(*app);
                if (layout.header.Contains(static_cast<float>(point.x), static_cast<float>(point.y)) &&
                    HitTest(*app, static_cast<float>(point.x), static_cast<float>(point.y),
                            ClickTarget::Kind::ColumnEdge) != nullptr)
                {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;

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
    // `Spacing` e' una preferenza: l'altezza della riga di lista la segue, e per
    // questo e' un valore derivato e non un 24 scritto a mano.
    layout.rowHeight  = (app.rowSpacing > 0.0f ? app.rowSpacing : 24.0f) * scale;
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

void ActivateSelection(AppState& app)
{
    const int cursorIndex = CursorIndex(app);
    if (cursorIndex < 0)
        return;

    const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
    const std::wstring full = EntryLocation(app, entry);
    if (entry.isDirectory && entry.navigable)
    {
        LoadDirectory(app, full);
    }
    else if (entry.isDirectory)
    {
        // Dentro il cestino il nome di una cartella e' il percorso del file
        // riciclato: aprirlo mostrerebbe il blob, non quello che si vede. Li' una
        // cartella si ripristina.
        ReportStatus(app, L"\"" + entry.wideName +
                              TrW(L"\" non si apre da qui: si ripristina.",
                                  L"\" does not open from here: restore it."));
    }
    else if (!entry.location.empty())
    {
        // Un file dentro il cestino non si apre: si ripristina o si elimina, e
        // quei comandi sono del menu contestuale della shell. Dirlo e' meglio che
        // far finta di averlo aperto.
        ReportStatus(app, L"\"" + entry.wideName +
                              TrW(L"\" non si apre da qui: usa il menu contestuale.",
                                  L"\" does not open from here: use the context menu."));
    }
    else
    {
        ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
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
                if (app.stats.TryGet(JoinPath(app.directory, item.wideName), &stats))
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
            ReportStatus(app, TrW(L"File non creato: ", L"File not created: ") + error);
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

        // In una cartella dello spazio dei nomi il genitore lo sa la shell: sopra
        // il cestino c'e' il desktop, e `ParentOf` su "::{...}" non troverebbe
        // niente da tagliare.
        //
        // Da una **radice di volume** invece `ParentOf` non sale: `C:\` e' padre di
        // se stesso, e salire da li' non faceva niente. Sopra un disco c'e' "This
        // PC", che e' una cartella di spazio dei nomi: la stessa lista di unita'
        // della barra laterale, raggiungibile col gesto che ci si aspetta.
        case CmdGoUp:
            if (ShellLocation::IsNamespace(app.directory))
            {
                const std::wstring up = ShellLocation::ParentOf(app.directory);
                if (!up.empty())
                    LoadDirectory(app, up);
            }
            else
            {
                const std::wstring up = ParentOf(app.directory);
                if (up != app.directory)
                    LoadDirectory(app, up);
                else if (!app.thisPcPath.empty() && app.directory != app.thisPcPath)
                    LoadDirectory(app, app.thisPcPath);
            }
            break;
        case CmdBack:      NavigateBack(app); break;
        case CmdForward:   NavigateForward(app); break;
        case CmdGoTo:      BuildGoToMenu(app); break;
        case CmdPalette:   BuildPaletteMenu(app); break;
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
            // Dentro una cartella dello spazio dei nomi `/select` non vale, e non
            // serve: si apre la cartella stessa, che e' quello che si vede.
            if (ShellLocation::IsNamespace(app.directory))
            {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", app.directory.c_str(), nullptr,
                              SW_SHOWNORMAL);
            }
            else if (cursorIndex >= 0)
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

        case CmdToggleSidebar:
            app.sidebarVisible = !app.sidebarVisible;
            RememberSettings(app);
            break;
        // Una preferenza si cambia e si **ricorda**: e' quello che l'autore ha
        // chiesto per la lingua, e vale per tutte quelle che si scelgono qui.
        case CmdToggleHoverToSelect:    app.hoverToSelect    = !app.hoverToSelect;    RememberSettings(app); break;
        case CmdToggleUnderlineOnHover: app.underlineOnHover = !app.underlineOnHover; RememberSettings(app); break;
        case CmdToggleTooltips:         app.showTooltips     = !app.showTooltips;     RememberSettings(app); break;
        case CmdSingleClick:            app.singleClick            = !app.singleClick;            RememberSettings(app); break;
        case CmdSingleClickFoldersOnly: app.singleClickFoldersOnly = !app.singleClickFoldersOnly; RememberSettings(app); break;
        case CmdActOnPress:             app.actOnPress             = !app.actOnPress;             RememberSettings(app); break;
        case CmdDoubleClickToParent:    app.doubleClickToParent    = !app.doubleClickToParent;    RememberSettings(app); break;
        case CmdShowCheckboxes:         app.showCheckboxes         = !app.showCheckboxes;         RememberSettings(app); break;
        case CmdLanguageItalian:
            gLanguage = Language::Italian;
            RememberSettings(app);
            ReportStatus(app, TrW(L"Impostazioni salvate in ", L"Settings saved to ") + SettingsPath());
            break;
        case CmdLanguageEnglish:
            gLanguage = Language::English;
            RememberSettings(app);
            ReportStatus(app, TrW(L"Impostazioni salvate in ", L"Settings saved to ") + SettingsPath());
            break;
        case CmdShowSettingsPath:
            ReportStatus(app, SettingsPath().empty()
                                 ? TrW(L"Impostazioni non disponibili", L"Settings unavailable")
                                 : TrW(L"Impostazioni in ", L"Settings in ") + SettingsPath());
            break;
        // Le tre dell'aspetto, che sono anche le tre chiavi dello spec. Sono la via
        // rapida per cambiare tavolozza: dal pannello delle opzioni servono tre passaggi
        // — aprire, cercare, scegliere — e qui uno.
        case CmdColorSchemeLight:
            SetColorScheme(app, ColorScheme::Light);
            ReportStatus(app, TrW(L"Tema: chiaro", L"Theme: light"));
            break;
        case CmdColorSchemeDark:
            SetColorScheme(app, ColorScheme::Dark);
            ReportStatus(app, TrW(L"Tema: scuro", L"Theme: dark"));
            break;
        case CmdSystemColorScheme:
            app.systemColorScheme = !app.systemColorScheme;
            ApplyTheme(app);
            RememberSettings(app);
            ReportStatus(app, app.systemColorScheme
                                 ? TrW(L"Tema: segue il sistema", L"Theme: follows the system")
                                 : TrW(L"Tema: scelto a mano", L"Theme: chosen by hand"));
            break;
        case CmdOpenOptions:
            BuildOptionsMenu(app);
            break;
        case CmdEditKeyMap:
            BuildKeyMapMenu(app);
            break;
        case CmdPinTo:   BuildPinMenu(app, /*removing=*/false); break;
        case CmdUnpin:   BuildPinMenu(app, /*removing=*/true);  break;
        case CmdProperties:
            OpenPropertiesPanel(app);
            break;
        case CmdPropertiesWindows:
        {
            const std::vector<std::wstring> paths = SelectedPaths(app);
            if (paths.empty() || ShowProperties(app.window, paths) == 0)
                ReportStatus(app, TrW(L"Proprietà non disponibili per la selezione.",
                                      L"Properties unavailable for the selection."));
            break;
        }
        case CmdCloseInTree:
            CloseTabTree(app, app.tabs[static_cast<size_t>(app.activeTab)].id, /*wholeTree=*/false);
            break;
        case CmdCloseAllInTree:
            CloseTabTree(app, app.tabs[static_cast<size_t>(app.activeTab)].id, /*wholeTree=*/true);
            break;
        case CmdDetachTab:
        {
            // "Stacca" riporta il tab alla radice: e' l'unico modo di disfare un
            // annidamento senza cercarne un altro con il mouse.
            Tab& tab = app.tabs[static_cast<size_t>(app.activeTab)];
            tab.parentId = -1;
            break;
        }
        case CmdZoomIn:    app.zoom = (std::min)(2.0f, app.zoom + 0.1f); break;

        // Le quattro opzioni del visualizzatore. Cambiano il disegno del testo, non
        // la lista: la riga da rifare e' quella delle righe visibili, e si rifa' da
        // sola perche' il calcolo si accorge che l'andare a capo e' cambiato.
        case CmdTextWhitespace:  app.textShowWhitespace = !app.textShowWhitespace; break;
        case CmdTextLineNumbers: app.textLineNumbers = !app.textLineNumbers; break;
        case CmdTextMinimap:     app.textMinimap = !app.textMinimap; break;
        case CmdTextWrap:
            app.textWrap = !app.textWrap;
            app.textRowsDirty = true;
            break;
        case CmdTextZoomIn:
            app.textZoom = (std::min)(2.4f, app.textZoom + 0.15f);
            app.textRowsDirty = true;
            break;
        case CmdTextZoomOut:
            app.textZoom = (std::max)(0.6f, app.textZoom - 0.15f);
            app.textRowsDirty = true;
            break;

        // L'ingrandimento dell'anteprima e' **relativo all'adattamento**: il
        // decoder la ridimensiona a 1024 px al massimo, quindi "1:1 rispetto
        // all'originale" non e' una cosa che si possa promettere.
        case CmdImageZoomIn:  app.imageZoom = (std::min)(4.0f, app.imageZoom + 0.25f); break;
        case CmdImageZoomOut: app.imageZoom = (std::max)(0.25f, app.imageZoom - 0.25f); break;
        case CmdImageZoomFit: app.imageZoom = 1.0f; break;
        // La griglia dietro l'anteprima dell'immagine: si vede dove finisce lo sfondo
        // dell'immagine e comincia quello del pannello. Si ricorda, come le altre
        // preferenze.
        case CmdToggleImageGrid:
            app.inspectorHideGrid = !app.inspectorHideGrid;
            RememberSettings(app);
            break;
        case CmdZoomOut:   app.zoom = (std::max)(0.6f, app.zoom - 0.1f); break;
        case CmdZoomReset: app.zoom = app.dpiScale; break;
        case CmdToggleDetails: app.palette.detailed = !app.palette.detailed; break;
        // Cambiando vista le celle si spostano, e l'indice della riga sotto il
        // puntatore non vuol piu' dire niente: il sovrappasso si azzera e lo
        // ricalcola il prossimo movimento. Senza, il suggerimento resta appeso a
        // una voce che non e' piu' quella sotto il mouse.
        case CmdViewDetails: SetViewMode(app, ViewMode::Details); break;
        case CmdViewList:    SetViewMode(app, ViewMode::List);    break;
        case CmdViewTiles:   SetViewMode(app, ViewMode::Tiles);   break;
        case CmdViewIcons:   SetViewMode(app, ViewMode::Icons);   break;
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
        else if (!ShellLocation::IsNamespace(place->path))
            target.verb = DragDrop::DropVerb::DropInto;
        // "Questo PC", o un'altra cartella di spazio dei nomi: non si sa dove
        // mettere i file. Rifiutare e' meglio che promettere e non fare.
        return target;
    }

    if (const ClickTarget* row = HitTest(*app, x, y, ClickTarget::Kind::Row))
    {
        const Entry& entry = app->entries[static_cast<size_t>(row->index)];
        if (!entry.isDirectory)
        {
            // Sopra un file la destinazione e' la cartella che lo contiene, come
            // in Esplora risorse: li' un file non riceve niente, ma la lista si'.
            target.path = app->directory;
            target.verb = DragDrop::DropVerb::DropInto;
            return target;
        }
        // Una cartella va aperta per lasciarci qualcosa dentro: la riga mostra il
        // nome, la destinazione e' il percorso.
        target.path    = JoinPath(app->directory, entry.wideName);
        target.verb    = DragDrop::DropVerb::DropInto;
        target.ontoRow = true;
        return target;
    }

    const Layout layout = ComputeLayout(*app);
    if (layout.list.Contains(x, y))
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

bool Drop(HWND window, const std::vector<std::wstring>& sources, const DragDrop::Target& target,
          bool move)
{
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app == nullptr || sources.empty())
        return false;

    const bool discard = target.verb == DragDrop::DropVerb::Discard;

    const bool started =
        BeginOperation(*app,
                       discard ? FileOps::Job::StartDelete(window, sources, /*permanently=*/false)
                               : FileOps::Job::StartTransfer(window, sources, target.path, move),
                       OperationSource::Drop);

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
                // Un nome di spazio dei nomi non ha attributi da leggere: si
                // riconosce da come e' scritto, ed e' l'unico modo di aprire Bolt
                // direttamente sul cestino.
                else if (ShellLocation::IsNamespace(arguments[1]))
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
        if (!glRenderer.Initialize(REA_SHADER_DIR, &error))
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

    const std::wstring initialDirectory = startDirectory.empty()
        ? (app.places.empty() ? L"C:\\" : app.places[0].path)
        : startDirectory;

    app.tabs.push_back(Tab{});
    app.activeTab = 0;
    LoadDirectory(app, initialDirectory);

    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    // La sorveglianza delle modifiche esterne. Se non parte si va avanti lo
    // stesso — il programma funziona, e' l'aggiornamento automatico che manca —
    // ma si dice: un aggiornamento che non arriva sembra un difetto dell'app e
    // non lo e'.
    if (!app.watcher.Start(window, kDirectoryChanged))
        DiagLog("--- sorveglianza delle cartelle non disponibile");
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
        if (app.viewDirty ||
            (app.groupMode != GroupMode::None && statsRevision != app.viewStatsRevision &&
             now - app.lastViewRebuild > 0.25))
        {
            RebuildView(app);
            app.viewStatsRevision = statsRevision;
            app.lastViewRebuild   = now;
        }

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
