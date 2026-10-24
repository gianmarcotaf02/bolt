// ---------------------------------------------------------------------------
// Il pannello delle opzioni: i controlli tipizzati e le azioni che li cambiano.
//
// Era in `main.cpp`, dentro il namespace anonimo, ed e' l'ultima **superficie**
// rimasta li': la barra laterale, la barra del titolo, la lista, l'ispettore, la
// barra di stato e i menu a gruppi erano gia' usciti. Il disegno non e' qui —
// `GroupMenuDraw` lo fa gia' per ogni menu a gruppi — e nemmeno la geometria delle
// righe: qui stanno la **costruzione** dell'elenco delle preferenze, il passaggio
// dello stato alla riga e ritorno, la modifica in linea del valore, e i tasti e i
// clic che il pannello riceve.
//
// Perche' e' un modulo e non una parte di `view_menu`: il pannello delle opzioni
// conosce `AppState` — quale tavolozza, quale lingua, quale misura — e `view_menu`
// no. La direzione delle dipendenze resta quella degli altri moduli di vista.
//
// Le funzioni che `WindowProc` chiama sono dichiarate in `view_options.h`; le
// altre sono `static` nel senso di sempre: chiuse dentro il namespace anonimo di
// questo file.
// ---------------------------------------------------------------------------
#include "view_options.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app_actions.h"
#include "commands.h"
#include "app_ui.h"
#include "language.h"
#include "settings.h"
#include "theme.h"
#include "navigation.h"   // `ReloadKeeping`: il filtro dei nascosti si applica subito
#include "view_list.h"   // `SortEntries`: cambiando `SortRecents` la lista si riordina subito
#include "view_statusbar.h"

namespace Bolt
{
namespace
{
// --- il pannello delle opzioni (M9) -----------------------------------------
//
// Il riferimento non mette le preferenze dentro la lista dei comandi: ha un
// **pannello di opzioni**, una lista cercabile in cui ogni riga porta tre cose —
// il nome, la ragione e il controllo del tipo giusto. Il componente c'e' gia':
// `GroupMenu` filtra, raggruppa, naviga con Su/Giu' e si chiude con Esc. Quello
// che gli mancava sono i **controlli tipizzati**, ed e' quello che si aggiunge
// qui.
//
// La verita' di un'opzione sta in `AppState`: `RefreshOptionItem` e' l'unico
// posto che la porta nella riga, e `ApplyOptionFromItem` l'unico che la riporta
// indietro. Cosi' non esistono due copie di "i suggerimenti sono accesi".
enum OptionId
{
    OptFont, OptFontSize, OptInspectorFont, OptInspectorFontSize,
    OptSpacing, OptZoom, OptAnimations, OptRoundCorners, OptSystemScheme,
    OptColorScheme, OptAlternatingRows, OptInactiveOpacity, OptFileExtension,
    OptLongPaths, OptCompactPaths,
    // Cosa **entra** nella lista, non come si disegna: due attributi di Windows,
    // due interruttori, spenti di partenza (`docs/03`, `ShowHiddenFiles` e
    // `ShowSystemFiles`).
    OptShowHiddenFiles, OptShowSystemFiles,
    // La pagina di avvio: un percorso, vuoto per "Questo PC".
    OptStartup,
    OptTooltips, OptHoverToSelect, OptUnderlineOnHover, OptSingleClick,
    OptSingleClickFoldersOnly, OptActOnPress, OptDoubleClickToParent,
    OptShowCheckboxes, OptSidebar, OptGridIconSize, OptLanguage,
    // Le due colonne che di partenza sono spente: la creazione e l'ultimo accesso.
    // La data di modifica resta, e non e' un'opzione.
    OptShowCreatedColumn, OptShowAccessedColumn,
    // `ShowRecents` e `SortRecents`: le due opzioni dei recenti che si vedono
    // nella lista — una li spegne, l'altra li porta in cima.
    OptShowRecents, OptSortRecents,
    // Cosa si registra nei recenti (`StoreRecents`). Non e' una preferenza come
    // le altre: il modo decide quali cartelle la sezione `RECENTS` puo' mostrare,
    // e nessuno dei tre scrive su disco (`docs/06`).
    OptStoreRecents,
    // La riga che apre l'editor della mappa dei tasti: e' un'**azione**, non una
    // preferenza — non c'e' un valore da leggere, e il posto in cui si legge il
    // tasto di un comando e' l'elenco, non questa riga.
    OptKeyMap,
};

// L'altezza di una riga del pannello: due linee di testo, e **non** `Spacing`.

// Un nome di carattere esiste? `CreateFontW` sostituisce in silenzio quello che
// non conosce, quindi la domanda non e' se la chiamata riesce: e' se la faccia
// che torna e' quella chiesta. Senza questo controllo, un nome sbagliato
// cambierebbe il carattere di tutta l'interfaccia senza dirlo.

bool FontInstalled(const std::wstring& face)
{
    if (face.empty())
        return false;
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
        return false;
    HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, face.c_str());
    bool installed = false;
    if (font != nullptr)
    {
        HGDIOBJ previous = SelectObject(dc, font);
        wchar_t actual[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, actual);
        installed = _wcsicmp(actual, face.c_str()) == 0;
        SelectObject(dc, previous);
        DeleteObject(font);
    }
    DeleteDC(dc);
    return installed;
}

// Le tre parole di `StoreRecents`, in una funzione sola: la riga del pannello e le
// sue scelte devono dire la stessa cosa, o il controllo mostrerebbe un valore che
// non e' quello fra le voci.
const char* StoreModeLabel(StoreRecents mode)
{
    switch (mode)
    {
        case StoreRecents::Persistent: return Tr("Persistente", "Persistent");
        case StoreRecents::LocalDisk:  return Tr("Disco locale", "Local Disk");
        default:                       return Tr("Sessione", "Session");
    }
}

StoreRecents StoreModeFromLabel(const std::string& label)
{
    if (label == StoreModeLabel(StoreRecents::Persistent))
        return StoreRecents::Persistent;
    if (label == StoreModeLabel(StoreRecents::LocalDisk))
        return StoreRecents::LocalDisk;
    return StoreRecents::Session;
}

void RefreshOptionItem(const AppState& app, GroupMenuItem& item)
{
    item.enabled = true;
    switch (static_cast<OptionId>(item.id))
    {
        case OptFont:
            item.control   = OptionControl::Text;
            item.valueText = ToUtf8(app.fontFace);
            break;
        case OptFontSize:
            item.control   = OptionControl::Stepper;
            item.valueText = std::to_string(app.uiFontSize);
            break;
        case OptInspectorFont:
            item.control   = OptionControl::Text;
            item.valueText = ToUtf8(app.monoFace);
            break;
        case OptInspectorFontSize:
            item.control   = OptionControl::Stepper;
            item.valueText = std::to_string(app.monoFontSize);
            break;
        case OptSpacing:
            item.control  = OptionControl::Slider;
            item.fraction = (app.rowSpacing - AppState::kRowSpacingMin) /
                            (AppState::kRowSpacingMax - AppState::kRowSpacingMin);
            item.valueText = std::to_string(static_cast<int>(app.rowSpacing + 0.5f));
            break;
        case OptZoom:
            item.control  = OptionControl::Slider;
            item.fraction = (app.zoom - 0.6f) / 1.4f;
            item.valueText = std::to_string(static_cast<int>(app.zoom * 100.0f + 0.5f)) + "%";
            break;
        case OptAlternatingRows:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.alternatingRows;
            break;
        case OptTooltips:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.showTooltips;
            break;
        // `ShowRecents`. La riga mostra lo stato; **quanto** e' spenta una voce lo
        // decide il suo punteggio, non questa casella.
        case OptShowRecents:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.showRecents;
            break;
        case OptSortRecents:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.sortRecentsOnTop;
            break;
        case OptHoverToSelect:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.hoverToSelect;
            break;
        case OptUnderlineOnHover:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.underlineOnHover;
            break;
        case OptSingleClick:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.singleClick;
            break;
        case OptSingleClickFoldersOnly:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.singleClickFoldersOnly;
            item.enabled = app.singleClick;   // da sola non vuol dire niente
            break;
        case OptActOnPress:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.actOnPress;
            break;
        case OptDoubleClickToParent:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.doubleClickToParent;
            break;
        case OptShowCheckboxes:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.showCheckboxes;
            break;
        case OptSidebar:
            item.control = OptionControl::Toggle;
            item.toggleOn = app.sidebarVisible;
            break;
        case OptGridIconSize:
            item.control  = OptionControl::Slider;
            item.fraction = (app.gridIconSize - AppState::kGridIconMin) /
                            (AppState::kGridIconMax - AppState::kGridIconMin);
            item.valueText = std::to_string(static_cast<int>(app.gridIconSize));
            break;
        case OptLanguage:
            item.control   = OptionControl::Choice;
            item.valueText = (gLanguage == Language::English) ? "English" : "Italiano";
            break;
        // Il modo dei recenti si legge come lo mostra la riga: la stessa parola che
        // sta fra le scelte, o il controllo direbbe una cosa e ne farebbe un'altra.
        case OptStoreRecents:
            item.control   = OptionControl::Choice;
            item.valueText = StoreModeLabel(app.recents.StoreMode());
            break;
        // Non e' una preferenza con un valore: e' un pulsante che apre l'editor. Il
        // testo del pulsante dice **cosa fa**, non com'e' messa: non c'e' niente da
        // leggere in una riga che non ha uno stato.
        case OptKeyMap:
            item.control   = OptionControl::Action;
            item.valueText = Tr("Modifica...", "Edit...");
            break;
        // Le opzioni che il riferimento ha e che qui non ci sono. Si mostrano
        // **spente**: un'opzione che non si puo' applicare sta in vista e dice
        // che non si tocca, invece di sparire e far credere che non esista.
        // `Animations`: i pannelli compaiono in dissolvenza. E' l'unica animazione
        // che c'e' — lo scorrimento e il cursore di testo non lo sono.
        case OptAnimations:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.animations;
            break;
        // `LongPathsSupport` adesso si applica: `IOPath` mette il prefisso nei punti
        // di I/O quando serve, e da spenta il comportamento e' quello di prima.
        case OptLongPaths:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.longPaths;
            break;
        // La pagina di avvio: la riga **mostra** la preferenza, e si modifica
        // scrivendoci un percorso. Vuota vuol dire **Home**, ed e' il valore di
        // partenza: nessuna cartella scelta, nessun file che se la ricorda.
        case OptStartup:
            item.control   = OptionControl::Text;
            item.valueText = ToUtf8(app.startupFolder);
            break;
        // `RoundCorners` adesso si applica: e' uno stato di disegno del fotogramma.
        case OptRoundCorners:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.roundCorners;
            break;
        // Le due dell'aspetto invece **si applicano**. L'interruttore dice se la
        // tavolozza la decide il sistema; la scelta mostra quale tavolozza si sta
        // usando adesso, anche quando e' il sistema a deciderla: un controllo che
        // dichiara qualcosa di diverso da quello che si vede e' peggio di un
        // controllo che non c'e'.
        case OptSystemScheme:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.systemColorScheme;
            break;
        case OptColorScheme:
            item.control   = OptionControl::Choice;
            item.valueText = SchemeLabel(SchemeInUse(app));
            // Col sistema a comandare questa scelta non fa niente, quindi si mostra
            // **spenta** — continuando a dire qual e' la tavolozza in uso.
            item.enabled   = !app.systemColorScheme;
            break;
        case OptInactiveOpacity:
            // Il pannello senza fuoco si attenua; `AppState::focusArea` dice quale
            // ha il fuoco, e si aggiorna dove si clicca. Lo slider va da 0,2 a 1,0:
            // sotto un quinto un pannello non si legge piu', e non e' una scelta.
            item.control   = OptionControl::Slider;
            item.fraction  = (app.inactiveOpacity - 0.2f) / 0.8f;
            item.valueText = std::to_string(static_cast<int>(app.inactiveOpacity * 100.0f + 0.5f)) + "%";
            break;
        // Due opzioni che adesso **si applicano**. Il nome mostrato senza estensione
        // e il percorso che si stringe invece di uscire dal riquadro: sono
        // comportamento, non decorazione, e si vedono subito.
        case OptFileExtension:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.showFileExtension;
            break;
        case OptCompactPaths:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.compactPaths;
            break;
        case OptShowHiddenFiles:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.showHiddenFiles;
            break;
        case OptShowSystemFiles:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.showSystemFiles;
            break;
        case OptShowCreatedColumn:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.showCreatedDate;
            break;
        case OptShowAccessedColumn:
            item.control  = OptionControl::Toggle;
            item.toggleOn = app.showAccessedDate;
            break;
        default:
            break;
    }
}

void RefreshOptionItems(const AppState& app, GroupMenu& menu)
{
    for (GroupMenuItem& item : menu.items)
        RefreshOptionItem(app, item);
}

// Il rettangolo della riga |index|. Le righe del pannello scivolano di pixel in
// pixel, quindi il rettangolo si ricalcola ogni volta dalla stessa vista che usa
// il disegno: due calcoli della stessa riga sono due modi di sbagliare il clic.
Rect OptionRowRect(const Layout& layout, const GroupMenu& menu, const Rect& panel, int index)
{
    const float rowHeight  = MenuRowHeight(layout, menu);
    const float listTop    = MenuListTop(layout, panel, menu);
    const float listBottom = MenuListBottom(layout, panel);
    const MenuScrollView view =
        GroupMenuScrollViewOf(menu, listTop, listBottom, rowHeight);
    return Rect{panel.x + 6.0f * layout.scale,
                listTop + static_cast<float>(index - view.first) * rowHeight - view.offset,
                panel.w - 12.0f * layout.scale, rowHeight};
}

// La riga sotto un punto, dentro il pannello delle opzioni.
int OptionRowAt(const Layout& layout, const GroupMenu& menu, const Rect& panel, float y)
{
    const float rowHeight  = MenuRowHeight(layout, menu);
    const float listTop    = MenuListTop(layout, panel, menu);
    const float listBottom = MenuListBottom(layout, panel);
    if (y < listTop || y >= listBottom)
        return -1;
    const MenuScrollView view =
        GroupMenuScrollViewOf(menu, listTop, listBottom, rowHeight);
    const int visible = (std::max)(1, static_cast<int>((listBottom - listTop) / rowHeight));
    // Lo scorrimento sposta le righe sotto il puntatore: si toglie prima di
    // dividere, o il clic cade sulla riga che si vedeva prima dello scorrimento.
    const int index = view.first + static_cast<int>((y - listTop + view.offset) / rowHeight);
    if (index >= view.first + visible || index < 0 || index >= static_cast<int>(menu.rows.size()))
        return -1;
    return index;
}


void BeginOptionEdit(AppState& app, const GroupMenuItem& item)
{
    app.optionEditActive = true;
    app.optionEditId     = item.id;
    app.optionEditText   = item.valueText;
}

void OpenOptionChoice(AppState& app, const GroupMenuItem& item)
{
    std::vector<GroupMenuItem> items;
    for (const std::pair<std::string, int>& choice : item.choices)
        items.push_back(GroupMenuItem{"", choice.first, "", choice.second});

    app.optionsChoiceParent = item.id;
    GroupMenuOpen(app.optionsChoice, item.label, Tr("Filtra i valori...", "Filter values..."),
                  std::move(items));
    app.optionsChoice.richRows = false;
    app.optionsChoice.hint =
        Tr("Invio sceglie   Esc torna al pannello", "Enter chooses   Esc goes back");
}

// Un controllo `Action` e' una riga che apre un'altra cosa invece di avere un
// valore. Cosa apre lo dice il suo id — oggi c'e' solo la mappa dei tasti, che e' la
// riga da cui si apre e non ha un secondo significato da inventare.
void RunOptionAction(AppState& app, const GroupMenuItem& item)
{
    if (static_cast<OptionId>(item.id) == OptKeyMap)
        BuildKeyMapMenu(app);
}
} // namespace


void ApplyOptionFromItem(AppState& app, GroupMenuItem& item)
{
    const OptionId id = static_cast<OptionId>(item.id);
    // `BuildOptionsMenu` ricostruisce l'elenco, quindi ogni riferimento a `item`
    // scade da li' in poi: l'id si prende subito.
    const int changedId = item.id;
    bool fontsChanged = false;
    bool saved        = true;

    const auto clampInt = [](int value, int low, int high) {
        return value < low ? low : (value > high ? high : value);
    };

    switch (id)
    {
        case OptFont:
        case OptInspectorFont:
        {
            const std::wstring face = Widen(item.valueText);
            if (!FontInstalled(face))
            {
                ReportStatus(app, TrW(L"Carattere inesistente: il nome non cambia",
                                      L"No such font: the name is unchanged"));
                saved = false;
                break;
            }
            if (id == OptFont) app.fontFace = face;
            else                app.monoFace = face;
            fontsChanged = true;
            break;
        }
        case OptFontSize:
        {
            const int size = clampInt(atoi(item.valueText.c_str()),
                                      AppState::kUiFontSizeMin, AppState::kUiFontSizeMax);
            if (size != app.uiFontSize)
            {
                app.uiFontSize = size;
                fontsChanged   = true;
            }
            break;
        }
        case OptInspectorFontSize:
        {
            const int size = clampInt(atoi(item.valueText.c_str()),
                                      AppState::kMonoFontSizeMin, AppState::kMonoFontSizeMax);
            if (size != app.monoFontSize)
            {
                app.monoFontSize = size;
                fontsChanged     = true;
            }
            break;
        }
        case OptSpacing:
        {
            const float value = AppState::kRowSpacingMin +
                                item.fraction * (AppState::kRowSpacingMax - AppState::kRowSpacingMin);
            app.rowSpacing = std::round(value);
            break;
        }
        case OptInactiveOpacity:
            app.inactiveOpacity = 0.2f + item.fraction * 0.8f;
            break;
        case OptZoom:
            // A passi di 5%: uno zoom che cambia di un millesimo non si vede,
            // e riempirebbe il file di impostazioni di cifre inutili.
            app.zoom = std::round((0.6f + item.fraction * 1.4f) * 20.0f) / 20.0f;
            break;
        case OptGridIconSize:
            app.gridIconSize = std::round((AppState::kGridIconMin +
                                           item.fraction * (AppState::kGridIconMax - AppState::kGridIconMin)) /
                                          2.0f) * 2.0f;
            break;
        case OptAlternatingRows: app.alternatingRows = item.toggleOn; break;
        case OptRoundCorners:    app.roundCorners    = item.toggleOn; break;
        case OptAnimations:      app.animations      = item.toggleOn; break;
        case OptLongPaths:       app.longPaths       = item.toggleOn; break;
        case OptStartup:
        {
            const std::wstring voleva = Widen(item.valueText);
            if (voleva.empty())
            {
                app.startupFolder.clear();   // vuota = Home
                break;
            }
            // "Questo PC" non e' una cartella e non ha attributi: si riconosce da
            // come e' scritto, ed e' una scelta di avvio legittima quanto un
            // percorso.
            if (ShellLocation::IsNamespace(voleva))
            {
                app.startupFolder = voleva;
                break;
            }
            const std::wstring probe = IOPath(voleva, app.longPaths);
            const DWORD attributes  = GetFileAttributesW(probe.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                // Un percorso che non e' una cartella non diventa l'avvio di nessuno:
                // la riga resta com'era e la barra di stato dice perche'.
                ReportStatus(app, TrW(L"Cartella inesistente: l'avvio non cambia",
                                      L"No such folder: the startup folder is unchanged"));
                saved = false;
                break;
            }
            app.startupFolder = voleva;
            break;
        }
        case OptCompactPaths:    app.compactPaths      = item.toggleOn; break;
        // Il nome mostrato e' una cosa che si ridisegna: senza `viewDirty` la lista
        // resterebbe com'era fino al prossimo evento, e sembrerebbe che l'opzione
        // non faccia niente.
        case OptFileExtension:
            app.showFileExtension = item.toggleOn;
            app.viewDirty         = true;
            break;
        // `ShowHiddenFiles` / `ShowSystemFiles`: non e' il disegno che cambia, e'
        // **cosa c'e'** nella cartella. Quindi la cartella si rilegge, tenendo il
        // posto: una preferenza non deve portare via la selezione. E anche
        // l'altro pannello, se sta sulla stessa cartella, o le due meta' della
        // finestra direbbero due elenchi diversi.
        case OptShowHiddenFiles:
        case OptShowSystemFiles:
        {
            if (id == OptShowHiddenFiles) app.showHiddenFiles = item.toggleOn;
            else                          app.showSystemFiles = item.toggleOn;
            ReloadKeeping(app, app.selection, app.cursor, app.scroll);
            ReloadPaneShowing(app, app.directory);
            app.viewDirty = true;
            break;
        }
        // Le due colonne: `SetColumnVisible` fa tutto quello che serve oltre al
        // disegno — togliere il filtro di quella colonna, e riportare l'ordine al nome
        // se era lei a ordinare.
        case OptShowCreatedColumn:
            SetColumnVisible(app, ColumnOfSort(SortColumn::Created), item.toggleOn);
            break;
        case OptShowAccessedColumn:
            SetColumnVisible(app, ColumnOfSort(SortColumn::Accessed), item.toggleOn);
            break;
        case OptTooltips:        app.showTooltips     = item.toggleOn; break;
        case OptShowRecents:     app.showRecents      = item.toggleOn; break;
        case OptSortRecents:
            app.sortRecentsOnTop = item.toggleOn;
            SortEntries(app);   // l'ordine si vede subito, appena chiuso il pannello
            break;
        case OptHoverToSelect:   app.hoverToSelect    = item.toggleOn; break;
        case OptUnderlineOnHover: app.underlineOnHover = item.toggleOn; break;
        case OptSingleClick:     app.singleClick      = item.toggleOn; break;
        case OptSingleClickFoldersOnly: app.singleClickFoldersOnly = item.toggleOn; break;
        case OptActOnPress:      app.actOnPress       = item.toggleOn; break;
        case OptDoubleClickToParent: app.doubleClickToParent = item.toggleOn; break;
        case OptShowCheckboxes:  app.showCheckboxes   = item.toggleOn; break;
        case OptSidebar:         app.sidebarVisible   = item.toggleOn; break;
        case OptSystemScheme:
            app.systemColorScheme = item.toggleOn;
            ApplyTheme(app);
            break;
        case OptColorScheme:
            app.colorScheme = (item.valueText == SchemeLabel(ColorScheme::Light))
                                  ? ColorScheme::Light
                                  : ColorScheme::Dark;
            ApplyTheme(app);
            break;
        case OptLanguage:
            // Le due lingue sono due voci, non un interruttore: la scelta mostra
            // sempre quale delle due e' accesa.
            gLanguage = (item.valueText == "English") ? Language::English : Language::Italian;
            break;
        case OptStoreRecents:
            // Il valore non si salva: il modo vale per la sessione, e la riga lo
            // dice. Scriverlo in `settings.ini` sarebbe una preferenza presa di
            // nascosto su quali cartelle il programma ricorda (`docs/06`).
            app.recents.SetStoreMode(StoreModeFromLabel(item.valueText));
            saved = false;
            break;
        default:
            saved = false;   // una riga spenta non ha niente da applicare
            break;
    }

    if (fontsChanged)
        ApplyFonts(app);

    // Le etichette e le descrizioni del pannello sono scritte in una lingua:
    // cambiandola, le righe costruite prima restano nella lingua vecchia. Si rifa'
    // l'elenco e si rimette il cursore dove era, invece di riportarlo in cima.
    if (id == OptLanguage)
    {
        const std::wstring filter = app.options.filter;
        // Lo scorrimento e' la posizione di chi sta leggendo: ricostruire
        // l'elenco non e' un motivo per riportarlo in cima alla lista.
        const float scroll = app.options.scrollPx;
        const float target = app.options.scrollTargetPx;
        BuildOptionsMenu(app);
        app.options.scrollPx       = scroll;
        app.options.scrollTargetPx = target;
        app.options.scrollFromPx   = scroll;
        app.options.scrollStartedAt = NowSeconds();
        app.options.filter = filter;
        app.options.dirty  = true;
        GroupMenuRebuild(app.options);
        for (int i = 0; i < static_cast<int>(app.options.rows.size()); ++i)
        {
            const GroupMenuRow& row = app.options.rows[static_cast<size_t>(i)];
            if (!row.header && app.options.items[static_cast<size_t>(row.item)].id == changedId)
            {
                app.options.selected = i;
                break;
            }
        }
    }
    else
    {
        RefreshOptionItems(app, app.options);
    }

    if (saved)
        RememberSettings(app);
    app.viewDirty = true;
}

// Il pannello vero e proprio. Una lista piatta, come nel riferimento: il campo in
// alto filtra, e i gruppi sarebbero una seconda gerarchia sopra una lista che si
// cerca gia' per nome.
void BuildOptionsMenu(AppState& app)
{
    struct OptionRow
    {
        // Il nome del riferimento, in inglese: e' la **chiave** della riga, ed e' per
        // questo che resta scritto per esteso invece di essere tradotto e basta.
        const char*  label;
        // E la stessa riga in italiano. Sta accanto e non in una funzione a parte
        // perche' **questa** tabella e' il posto dove le due lingue si leggono una
        // sotto l'altra, come per le descrizioni qui sotto.
        const char*  labelIt;
        const char*  descriptionIt;
        const char*  descriptionEn;
        OptionId     id;
        bool         notApplied;   // il riferimento la ha, qui non e' applicata
    };
    // Dove la riga di un'opzione e' anche un **comando** della palette, la parola
    // italiana e' quella del comando: `Show Tooltips` si chiama «Mostra i
    // suggerimenti» in tutti e due i posti, o sarebbero due nomi per la stessa
    // preferenza.
    static const OptionRow kRows[] = {
        {"Font", "Carattere",
         "Il nome di un carattere installato, applicato a tutta l'interfaccia.",
         "The system-installed font name that is applied to the entire UI.", OptFont, false},
        {"Font Size", "Misura del carattere",
         "La misura del testo dell'interfaccia.",
         "Defines the size of the text in the interface.", OptFontSize, false},
        {"Inspector Font", "Carattere dell'ispettore",
         "Il carattere usato per il testo nell'inspector.",
         "The system-installed font used when previewing text contents in the inspector.", OptInspectorFont, false},
        {"Inspector Font Size", "Misura del carattere nell'ispettore",
         "La misura del testo nell'inspector.",
         "Specifies the font size used to display text contents in the inspector.", OptInspectorFontSize, false},
        {"Spacing", "Spaziatura", "Lo spazio fra le voci di una lista.",
         "Defines the space between items in a list.", OptSpacing, false},
        {"Zoom", "Ingrandimento", "Ingrandisce o rimpicciolisce tutta l'interfaccia.",
         "Scales the whole interface up or down.", OptZoom, false},
        {"Animations", "Animazioni",
         "Animazioni delle aperture, dello scorrimento e del cursore di testo.",
         "Applies animations to new popups, view scrolling, text cursor, etc.", OptAnimations, false},
        {"Round Corners", "Angoli arrotondati",
         "Arrotonda gli angoli di pannelli, pulsanti e selezioni.",
         "Applies rounding to the corners of UI elements such as popups, buttons, and selections.", OptRoundCorners, false},
        {"System Light / Dark Scheme", "Tema chiaro o scuro secondo il sistema",
         "La tavolozza la decide il tema chiaro o scuro di Windows.",
         "Automatically adapt between light and dark color schemes based on system settings.", OptSystemScheme, false},
        {"Color Scheme", "Tavolozza",
         "Applica una tavolozza di colori a tutta l'interfaccia.",
         "Applies a color palette to all UI elements.", OptColorScheme, false},
        {"Alternating Row Colors", "Righe alternate",
         "Colora a righe alterne per distinguere righe vicine.",
         "Applies alternating row colors to improve readability and distinguish adjacent rows.", OptAlternatingRows, false},
        {"Inactive Panel Content Opacity", "Opacità del pannello inattivo",
         "Attenua il contenuto del pannello che non ha il fuoco.",
         "Applies dimming to inactive panel contents to make the active panel stand out.", OptInactiveOpacity, false},
        {"Show File Extension", "Mostra l'estensione nei nomi",
         "Mostra o nasconde l'estensione nei nomi dei file.",
         "Toggle the visibility of file extensions.", OptFileExtension, false},
        {"Show Hidden Files", "Mostra i file nascosti",
         "Mostra i file con l'attributo nascosto di Windows.",
         "Shows files with the hidden attribute.", OptShowHiddenFiles, false},
        {"Show System Files", "Mostra i file di sistema",
         "Mostra i file con l'attributo di sistema. Un file con tutt'e due gli attributi vuole tutt'e due accesi.",
         "Shows files with the system attribute.", OptShowSystemFiles, false},
        {"Long Paths Support", "Percorsi lunghi", "Percorsi oltre 260 caratteri.",
         "Enable support for paths longer than 260 characters.", OptLongPaths, false},
        {"Startup", "Avvio",
         "La cartella che si apre all'avvio. Vuota: Questo PC.",
         "The folder opened at startup. Empty means This PC.", OptStartup, false},
        {"Display Compact Paths", "Percorsi compatti",
         "Accorcia i percorsi lunghi nella barra del percorso.",
         "Displays compact paths in the toolbar.", OptCompactPaths, false},
        {"Show Tooltips", "Mostra i suggerimenti",
         "Mostra un suggerimento quando il puntatore si ferma su una riga.",
         "Shows a hint when the pointer rests on a row.", OptTooltips, false},
        {"Highlight Recents", "Evidenzia i recenti",
         "Le cartelle gia' visitate si vedono in tono piu' spento, tanto piu' quanto meno valgono.",
         "Folders already visited are drawn faded, fainter for a lower score.", OptShowRecents, false},
        {"Sort Recents On Top", "Recenti in cima",
         "Le cartelle e i file gia' aperti stanno in cima alla lista, prima di tutto il resto.",
         "Folders and files already opened go on top of the list, before everything else.",
         OptSortRecents, false},
        {"Hover To Select", "Il passaggio seleziona",
         "Seleziona la riga sotto il puntatore dopo una pausa.",
         "Selects the row under the pointer after a short pause.", OptHoverToSelect, false},
        {"Underline On Hover", "Sottolinea al passaggio",
         "Sottolinea il nome della riga sotto il puntatore.",
         "Underlines the name of the row under the pointer.", OptUnderlineOnHover, false},
        {"Single Click", "Un clic apre", "Apre con un clic invece che con due.",
         "Opens an item with one click instead of two.", OptSingleClick, false},
        {"Single Click Folders Only", "Un clic: solo le cartelle",
         "Con l'apertura a un clic, sui file ne servono ancora due.",
         "With single click, still requires two clicks for files.", OptSingleClickFoldersOnly, false},
        {"Act On Press", "Agisci premendo",
         "Agisce appena il pulsante scende, non quando risale.",
         "Acts as soon as the button goes down, not when it comes up.", OptActOnPress, false},
        {"Double Click On Empty Space Goes Up", "Doppio clic sul fondo: su di un livello",
         "Doppio clic sul fondo: va alla cartella superiore.",
         "Goes to the parent folder when the empty space is double clicked.", OptDoubleClickToParent, false},
        {"Show Checkboxes", "Mostra le caselle di scelta",
         "Una casella di scelta su ogni riga.",
         "Shows a checkbox on every row.", OptShowCheckboxes, false},
        {"Show Creation Date Column", "Mostra la data di creazione",
         "La colonna della data di creazione. Di partenza e' spenta: resta la data di modifica.",
         "The creation date column. Off by default: only the modified date stays.",
         OptShowCreatedColumn, false},
        {"Show Last Access Date Column", "Mostra la data di ultimo accesso",
         "La colonna dell'ultimo accesso. Di partenza e' spenta: resta la data di modifica.",
         "The last access date column. Off by default: only the modified date stays.",
         OptShowAccessedColumn, false},
        {"Show The Sidebar", "Mostra la barra laterale",
         "Mostra il riquadro dei segnalibri e dei posti a sinistra.",
         "Shows the bookmark and place panel on the left.", OptSidebar, false},
        {"Grid Icon Size", "Misura delle anteprime",
         "La misura di una cella nelle viste a icone e a riquadri.",
         "The size of a cell in the icon and tile views.", OptGridIconSize, false},
        {"Language", "Lingua", "La lingua dell'interfaccia.",
         "The language of the interface. Applied immediately.", OptLanguage, false},
        {"Store Recents", "Salva i recenti",
         "Cosa entra nella sezione dei recenti: vale per questa sessione e non si scrive su disco.",
         "What goes into the recents section: lasts for this session and is not written to disk.",
         OptStoreRecents, false},
        {"Key Map", "Mappa dei tasti",
         "Il tasto di ogni comando. Vale per questa sessione: la mappa non si scrive su disco.",
         "The key of every command. Lasts for this session: the map is not written to disk.",
         OptKeyMap, false},
    };
    // Le righe che il riferimento ha e che qui non sono applicate: si mostrano
    // spente: un'opzione che non si puo' applicare sta in vista e dice che non si
    // tocca, invece di sparire e far credere che non esista. La ragione e' scritta
    // accanto alla descrizione, dove si legge.
    const char* notAppliedNote = Tr(" (non ancora applicata in Bolt)",
                                    " (not yet applied in Bolt)");

    std::vector<GroupMenuItem> items;
    items.reserve(sizeof(kRows) / sizeof(kRows[0]));
    for (const OptionRow& row : kRows)
    {
        GroupMenuItem item;
        item.group       = "";          // lista piatta, come il riferimento
        item.label       = Tr(row.labelIt, row.label);
        item.description = std::string(Tr(row.descriptionIt, row.descriptionEn)) +
                           (row.notApplied ? notAppliedNote : "");
        item.id          = row.id;
        if (row.id == OptLanguage)
        {
            item.choices.push_back({"Italiano", 0});
            item.choices.push_back({"English", 1});
        }
        else if (row.id == OptColorScheme)
        {
            // Le due voci sono le due tavolozze, con il nome tradotto: la scelta si
            // rilegge da `SchemeLabel`, che e' la stessa stringa che mostra la riga.
            item.choices.push_back({SchemeLabel(ColorScheme::Dark), 0});
            item.choices.push_back({SchemeLabel(ColorScheme::Light), 1});
        }
        else if (row.id == OptStoreRecents)
        {
            item.choices.push_back({StoreModeLabel(StoreRecents::Session), 0});
            item.choices.push_back({StoreModeLabel(StoreRecents::Persistent), 1});
            item.choices.push_back({StoreModeLabel(StoreRecents::LocalDisk), 2});
        }
        RefreshOptionItem(app, item);
        items.push_back(std::move(item));
    }

    CloseKeyMap(app);
    ClosePinMenu(app);
    app.palette.open = false;
    app.goTo.open    = false;
    app.optionsChoice.open = false;
    GroupMenuOpen(app.options, Tr("OPZIONI", "OPTIONS"),
                  Tr("Scegli un'opzione...", "Select an option..."), std::move(items));
    app.options.richRows = true;
    // La testata: le preferenze sono un comando come gli altri, e dicono come si apre.
    app.options.caption     = Tr("Le preferenze del programma.", "The program's preferences.");
    app.options.command     = Tr("Apri le opzioni", "Open options");
    app.options.commandKeys = app.keys.LabelsFor(CmdOpenOptions);
    app.options.hint     = Tr("Su Giu' per navigare   Invio per usare   Esc per chiudere",
                              "Up Down to navigate   Enter to use   Escape to dismiss");
}

void CancelOptionEdit(AppState& app)
{
    app.optionEditActive = false;
    app.optionEditId     = -1;
    app.optionEditText.clear();
    RefreshOptionItems(app, app.options);
}

void CommitOptionEdit(AppState& app)
{
    if (!app.optionEditActive)
        return;
    const int id = app.optionEditId;
    std::string text = app.optionEditText;
    app.optionEditActive = false;
    app.optionEditId     = -1;
    app.optionEditText.clear();

    // Gli spazi ai bordi non fanno parte di un nome di carattere.
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.erase(text.begin());
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.pop_back();

    for (GroupMenuItem& item : app.options.items)
    {
        if (item.id == id)
        {
            item.valueText = text;
            ApplyOptionFromItem(app, item);
            return;
        }
    }
}

void RunOptionsChoiceSelection(AppState& app)
{
    const int parent = app.optionsChoiceParent;
    const int value  = GroupMenuSelectedId(app.optionsChoice);
    app.optionsChoice.open = false;
    if (parent < 0 || value < 0)
        return;

    for (GroupMenuItem& item : app.options.items)
    {
        if (item.id != parent)
            continue;
        for (const std::pair<std::string, int>& choice : item.choices)
            if (choice.second == value)
                item.valueText = choice.first;
        ApplyOptionFromItem(app, item);
        return;
    }
}

// I tasti che riguardano il **controllo** della riga scelta. Ritorna vero se il
// tasto e' stato consumato: Su/Giu'/Home/Fine restano al componente.
bool OptionsKeyDown(AppState& app, GroupMenu& menu, WPARAM key)
{
    if (menu.selected < 0 || menu.selected >= static_cast<int>(menu.rows.size()))
        return false;
    const GroupMenuRow& row = menu.rows[static_cast<size_t>(menu.selected)];
    if (row.header || row.item < 0)
        return false;
    GroupMenuItem& item = menu.items[static_cast<size_t>(row.item)];

    if (!item.enabled)
        return key == VK_RETURN || key == VK_SPACE;

    switch (key)
    {
        case VK_RETURN:
        case VK_SPACE:
            if (item.control == OptionControl::Toggle)
            {
                item.toggleOn = !item.toggleOn;
                ApplyOptionFromItem(app, item);
            }
            else if (item.control == OptionControl::Choice)
            {
                OpenOptionChoice(app, item);
            }
            else if (item.control == OptionControl::Text)
            {
                BeginOptionEdit(app, item);
            }
            else if (item.control == OptionControl::Action)
            {
                RunOptionAction(app, item);
            }
            // Uno stepper e uno slider non hanno un "invio": si muovono con le
            // frecce, che sono il gesto di chi cambia un numero.
            return true;

        case VK_LEFT:
        case VK_RIGHT:
        {
            const int delta = (key == VK_RIGHT) ? 1 : -1;
            if (item.control == OptionControl::Stepper)
            {
                item.valueText = std::to_string(atoi(item.valueText.c_str()) + delta);
                ApplyOptionFromItem(app, item);
                return true;
            }
            if (item.control == OptionControl::Slider)
            {
                float fraction = item.fraction + delta * 0.05f;
                fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
                item.fraction = fraction;
                ApplyOptionFromItem(app, item);
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// Un clic dentro il pannello delle opzioni. Non chiude il pannello: si cambiano
// piu' preferenze di seguito, ed e' tutto il motivo per cui e' un pannello e non
// un singolo comando.
MenuAction OptionsClick(AppState& app, const Layout& layout, GroupMenu& menu, float x, float y)
{
    if (!menu.open)
        return MenuAction::None;

    const Rect panel = MenuPanel(app, layout, menu);
    if (!panel.Contains(x, y))
        return MenuAction::Close;

    const int index = OptionRowAt(layout, menu, panel, y);
    if (index < 0 || menu.rows[static_cast<size_t>(index)].header)
        return MenuAction::None;
    const GroupMenuRow& row = menu.rows[static_cast<size_t>(index)];
    if (row.item < 0)
        return MenuAction::None;

    menu.selected = index;
    // Un clic e' una scelta: il cursore si e' mosso, e se la riga sta a meta'
    // del bordo l'elenco la porta dentro. Non cambia niente su una riga che si
    // vede gia' intera.
    menu.revealSelection = true;
    GroupMenuItem& item = menu.items[static_cast<size_t>(row.item)];
    if (!item.enabled)
        return MenuAction::None;

    const Rect rowRect = OptionRowRect(layout, menu, panel, index);
    const Rect control = OptionControlRect(layout, item, rowRect);

    // Cambiare una preferenza vuole il puntatore **sul controllo**. Un clic sul
    // resto della riga la sceglie e basta: sulla riga di `Spacing` un clic a
    // sinistra avrebbe messo lo slider a zero, che non e' quello che si chiede
    // cliccando un'etichetta.
    if (!control.Contains(x, y))
        return MenuAction::None;

    switch (item.control)
    {
        case OptionControl::Toggle:
            item.toggleOn = !item.toggleOn;
            ApplyOptionFromItem(app, item);
            break;
        case OptionControl::Text:
            BeginOptionEdit(app, item);
            break;
        case OptionControl::Choice:
            OpenOptionChoice(app, item);
            break;
        case OptionControl::Action:
            RunOptionAction(app, item);
            break;
        case OptionControl::Stepper:
            item.valueText = std::to_string(atoi(item.valueText.c_str()) +
                                            (x < control.x + control.w * 0.5f ? -1 : 1));
            ApplyOptionFromItem(app, item);
            break;
        case OptionControl::Slider:
        {
            const float knob  = control.h * 0.5f;
            const float track = (std::max)(1.0f, control.w - knob * 2.0f);
            float fraction = (x - (control.x + knob)) / track;
            fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
            item.fraction = fraction;
            app.optionSliderDrag = item.id;   // il trascinamento continua nel movimento
            ApplyOptionFromItem(app, item);
            break;
        }
        default:
            break;
    }
    return MenuAction::None;
}
} // namespace Bolt
