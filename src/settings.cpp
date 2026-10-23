// ---------------------------------------------------------------------------
// Le impostazioni: leggere, scrivere, e ricordare.
//
// Le preferenze si **ricordano**. `docs/06` chiedeva che la persistenza fosse
// esplicita e scelta dall'utente, e l'autore l'ha chiesta cosi': «la lingua deve
// impostarsi in maniera fissa». Quindi il file c'e' — e non e' nascosto:
//
//   %APPDATA%/Bolt/settings.ini
//
// e' testo semplice con una riga per preferenza, si legge e si cancella a mano,
// cancellarlo riporta tutto ai valori di partenza, e l'applicazione **dice dove
// l'ha scritto**. Non contiene nomi di file, cartelle visitate o altre tracce di cosa
// si e' guardato: solo come si vuole che il programma si comporti.
//
// Uscito da `main.cpp` il 2026-10-18: era il pezzo con meno dipendenze rimasto li'
// (nessuna funzione della vista, nessun comando), e la cucitura perdeva una voce.
// ---------------------------------------------------------------------------
#include "settings.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app_ui.h"
#include "language.h"
#include "navigation.h"
#include "shell_location.h"
#include "theme.h"
#include "view_statusbar.h"

namespace Bolt
{
namespace
{
// Una riga per preferenza, "chiave=valore". Niente librerie e niente formato
// binario: si apre col Blocco note e si capisce.
bool ReadSetting(const std::string& text, const char* key, std::string* value)
{
    const std::string wanted = std::string(key) + "=";
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.compare(0, wanted.size(), wanted) == 0)
        {
            *value = line.substr(wanted.size());
            return true;
        }
        at = end + 1;
    }
    return false;
}
} // namespace

std::wstring SettingsPath()
{
    PWSTR roaming = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) &&
        roaming != nullptr)
    {
        path = roaming;
        CoTaskMemFree(roaming);
    }
    if (path.empty())
        return path;
    path += L"\\Bolt";
    CreateDirectoryW(path.c_str(), nullptr);
    return path + L"\\settings.ini";
}

void LoadSettings(AppState& app)
{
    const std::wstring path = SettingsPath();
    if (path.empty())
        return;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;   // primo avvio: i valori di partenza sono quelli giusti

    std::string text;
    char buffer[1024];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        text.append(buffer, read);
    CloseHandle(file);

    std::string value;
    if (ReadSetting(text, "language", &value))
        gLanguage = (value == "english") ? Language::English : Language::Italian;

    // I nomi dei caratteri sono testo: si leggono cosi' come si scrivono.
    if (ReadSetting(text, "fontFace", &value) && !value.empty())
        app.fontFace = Widen(value);
    // La pagina di avvio. **Una cartella**, e solo quella: e' la preferenza che chi
    // usa il programma ha scelto, non un elenco di dove e' stato (`docs/06`).
    // Il file e' testo che si puo' scrivere a mano, quindi la barra in avanti capita:
    // si normalizza qui, dove entra, e non in ogni punto che poi la usa.
    if (ReadSetting(text, "startupFolder", &value))
        app.startupFolder = NormalizePath(Widen(value));

    if (ReadSetting(text, "monoFace", &value) && !value.empty())
        app.monoFace = Widen(value);

    const auto readFlag = [&](const char* key, bool* target) {
        std::string v;
        if (ReadSetting(text, key, &v))
            *target = (v == "1");
    };
    readFlag("showTooltips", &app.showTooltips);
    readFlag("showRecents", &app.showRecents);
    readFlag("showFileExtension", &app.showFileExtension);
    // Le due colonne delle date: spente se il file non ne parla, che e' il valore di
    // partenza anche per un'installazione nuova.
    readFlag("showCreatedDate", &app.showCreatedDate);
    readFlag("showAccessedDate", &app.showAccessedDate);
    readFlag("compactPaths", &app.compactPaths);
    readFlag("roundCorners", &app.roundCorners);
    readFlag("animations", &app.animations);
    readFlag("longPaths", &app.longPaths);
    {
        // L'opacita' dei pannelli inattivi e' un numero, non un interruttore: sta
        // con `rowSpacing` e `zoom`, che sono float e si leggono allo stesso modo.
        std::string v;
        if (ReadSetting(text, "inactiveOpacity", &v))
            app.inactiveOpacity = std::clamp(static_cast<float>(atof(v.c_str())), 0.2f, 1.0f);
    }
    // Il modo dell'elenco: si ricorda come le altre preferenze della vista. Nessun
    // nome di file finisce in `settings.ini` — e' la regola di `docs/06` — qui si
    // scrive solo un interruttore.
    // `LoadOnStartup = LastSession` del riferimento, ma **a scelta e spenta**: con
    // questa accesa le schede aperte si ritrovano al prossimo avvio, e per farlo
    // bisogna pur scrivere da qualche parte quali cartelle erano aperte. E' l'unica
    // cosa di `docs/06` che il programma scrive su disco e non e' una preferenza:
    // sta in un file **suo** (`session.ini`), si cancella a mano, e spegnendo
    // l'interruttore il file **sparisce** invece di restare li'.
    readFlag("rememberTabs", &app.rememberTabs);
    {
        // La larghezza dell'ispettore: un numero, e si tiene dentro limiti sensati
        // anche se qualcuno scrive il file a mano.
        std::string v;
        if (ReadSetting(text, "inspectorWidth", &v))
            app.inspectorWidth = std::clamp(static_cast<float>(atof(v.c_str())), 200.0f, 900.0f);
    }

    // I comandi preferiti: un elenco di **id**, non di percorsi. `std::set` invece di
    // un vettore perche' l'ordine non conta niente — la palette li mette in cima e li
    // ordina lei.
    if (ReadSetting(text, "favorites", &value) && !value.empty())
    {
        size_t at = 0;
        while (at <= value.size())
        {
            const size_t comma = value.find(',', at);
            const std::string piece =
                value.substr(at, (comma == std::string::npos ? value.size() : comma) - at);
            if (!piece.empty())
                app.favoriteCommands.insert(std::atoi(piece.c_str()));
            if (comma == std::string::npos)
                break;
            at = comma + 1;
        }
    }

    // Il layout salvato: la **forma** della finestra. Nessun percorso dentro, quindi
    // sta qui e non nel file della sessione.
    {
        std::string v;
        if (ReadSetting(text, "layout.on", &v) && v == "1")
        {
            AppState::SavedLayout& layout = app.savedLayout;
            layout.defined = true;

            const auto readInt = [&](const char* key, int* target) {
                std::string n;
                if (ReadSetting(text, key, &n))
                    *target = std::atoi(n.c_str());
            };
            const auto readFloat = [&](const char* key, float* target) {
                std::string n;
                if (ReadSetting(text, key, &n))
                    *target = static_cast<float>(atof(n.c_str()));
            };
            const auto readBool = [&](const char* key, bool* target) {
                std::string n;
                if (ReadSetting(text, key, &n))
                    *target = (n == "1");
            };

            readInt("layout.viewMode", &layout.viewMode);
            readInt("layout.previewStep", &layout.previewStep);
            readFloat("layout.zoom", &layout.zoom);
            readInt("layout.sort", &layout.sortColumn);
            readBool("layout.ascending", &layout.sortAscending);
            readInt("layout.group", &layout.groupMode);
            readBool("layout.sidebar", &layout.sidebarVisible);
            readBool("layout.subfolders", &layout.showSubfolders);

            // Una riga per colonna: larghezza e se e' chiusa. I **filtri** per valore
            // non si salvano: un elenco di cartelle tenuto in un file e' una traccia di
            // cosa si e' guardato, e questo file non ne contiene.
            // Un carattere per colonna, `0` = accesa, `1` = chiusa: **quali** colonne
            // erano, non quanto erano larghe. Le larghezze salvate in una vista si
            // ritrovavano nell'altra, e due intestazioni una sopra l'altra
            // (`Data modifica` dentro `Cartelle`) erano il risultato: la larghezza si
            // riadatta al contenuto quando il layout si carica. I **filtri** per valore
            // non si salvano — un elenco di valori tenuto in un file e' una traccia di
            // cosa si e' guardato, e questo file non ne contiene.
            std::string bits;
            if (ReadSetting(text, "layout.columns", &bits))
            {
                for (int i = 0; i < kColumnCount && i < static_cast<int>(bits.size()); ++i)
                    layout.columns[static_cast<size_t>(i)].hidden =
                        (bits[static_cast<size_t>(i)] == '1');
            }
        }
    }
    readFlag("sortRecents", &app.sortRecentsOnTop);
    readFlag("hoverToSelect", &app.hoverToSelect);
    readFlag("underlineOnHover", &app.underlineOnHover);
    readFlag("singleClick", &app.singleClick);
    readFlag("singleClickFoldersOnly", &app.singleClickFoldersOnly);
    readFlag("actOnPress", &app.actOnPress);
    readFlag("doubleClickToParent", &app.doubleClickToParent);
    readFlag("showCheckboxes", &app.showCheckboxes);
    readFlag("sidebarVisible", &app.sidebarVisible);
    readFlag("alternatingRows", &app.alternatingRows);
    readFlag("inspectorHideGrid", &app.inspectorHideGrid);
    // Due chiavi per l'aspetto: l'interruttore "segui il sistema" e la tavolozza
    // scelta. Si leggono **tutte e due** anche quando e' il sistema a comandare, o
    // spegnendo l'interruttore si perderebbe la tavolozza che c'era prima.
    readFlag("systemColorScheme", &app.systemColorScheme);
    if (ReadSetting(text, "colorScheme", &value))
        app.colorScheme = (value == "light") ? ColorScheme::Light : ColorScheme::Dark;

    std::string grid;
    if (ReadSetting(text, "gridIconSize", &grid))
        app.gridIconSize = static_cast<float>(atof(grid.c_str()));
    if (app.gridIconSize < AppState::kGridIconMin || app.gridIconSize > AppState::kGridIconMax)
        app.gridIconSize = 64.0f;

    // Le misure si validano: un file modificato a mano non deve poter chiedere un
    // carattere da zero punti o una riga alta un pixel.
    const auto readInt = [&](const char* key, int* target, int low, int high) {
        std::string v;
        if (ReadSetting(text, key, &v))
        {
            const int parsed = atoi(v.c_str());
            *target = (parsed < low) ? low : (parsed > high ? high : parsed);
        }
    };
    readInt("uiFontSize", &app.uiFontSize, AppState::kUiFontSizeMin, AppState::kUiFontSizeMax);
    readInt("monoFontSize", &app.monoFontSize, AppState::kMonoFontSizeMin, AppState::kMonoFontSizeMax);
    readInt("previewStep", &app.previewStep, 0, AppState::kPreviewStepCount - 1);

    std::string spacing;
    if (ReadSetting(text, "rowSpacing", &spacing))
    {
        app.rowSpacing = static_cast<float>(atof(spacing.c_str()));
        if (app.rowSpacing < AppState::kRowSpacingMin)
            app.rowSpacing = AppState::kRowSpacingMin;
        if (app.rowSpacing > AppState::kRowSpacingMax)
            app.rowSpacing = AppState::kRowSpacingMax;
    }
}

bool SaveSettings(const AppState& app)
{
    const std::wstring path = SettingsPath();
    if (path.empty())
        return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    std::string text =
        "# Bolt — impostazioni\n"
        "# Le scrive Bolt quando cambi una preferenza, e le rilegge a ogni avvio.\n"
        "# Si cancellano a mano: cancellandole, tutto torna come al primo avvio.\n"
        "# Non contengono nomi di file ne' cartelle visitate.\n";
    text += std::string("language=") +
            (gLanguage == Language::English ? "english" : "italian") + "\n";
    text += "fontFace=" + ToUtf8(app.fontFace) + "\n";
    // Vuota non si scrive: nel file, la chiave assente vuol dire "Questo PC", e cosi'
    // il file resta corto quando la preferenza e' quella di partenza.
    if (!app.startupFolder.empty())
        text += "startupFolder=" + ToUtf8(app.startupFolder) + "\n";

    text += "monoFace=" + ToUtf8(app.monoFace) + "\n";

    const auto writeFlag = [&](const char* key, bool value) {
        text += std::string(key) + "=" + (value ? "1" : "0") + "\n";
    };
    writeFlag("showTooltips", app.showTooltips);
    writeFlag("showRecents", app.showRecents);
    writeFlag("showFileExtension", app.showFileExtension);
    writeFlag("showCreatedDate", app.showCreatedDate);
    writeFlag("showAccessedDate", app.showAccessedDate);
    writeFlag("compactPaths", app.compactPaths);
    writeFlag("roundCorners", app.roundCorners);
    writeFlag("animations", app.animations);
    writeFlag("longPaths", app.longPaths);
    char opacity[32];
    std::snprintf(opacity, sizeof(opacity), "inactiveOpacity=%.2f\n", app.inactiveOpacity);
    text += opacity;
    writeFlag("showSubfolders", app.showSubfolders);
    writeFlag("rememberTabs", app.rememberTabs);
    char inspector[48];
    std::snprintf(inspector, sizeof(inspector), "inspectorWidth=%.0f\n",
                  static_cast<double>(app.inspectorWidth));
    text += inspector;
    if (!app.favoriteCommands.empty())
    {
        std::string favorites;
        for (int command : app.favoriteCommands)
            favorites += (favorites.empty() ? "" : ",") + std::to_string(command);
        text += "favorites=" + favorites + "\n";
    }
    writeFlag("sortRecents", app.sortRecentsOnTop);
    writeFlag("hoverToSelect", app.hoverToSelect);
    writeFlag("underlineOnHover", app.underlineOnHover);
    writeFlag("singleClick", app.singleClick);
    writeFlag("singleClickFoldersOnly", app.singleClickFoldersOnly);
    writeFlag("actOnPress", app.actOnPress);
    writeFlag("doubleClickToParent", app.doubleClickToParent);
    writeFlag("showCheckboxes", app.showCheckboxes);
    writeFlag("sidebarVisible", app.sidebarVisible);
    writeFlag("alternatingRows", app.alternatingRows);
    writeFlag("inspectorHideGrid", app.inspectorHideGrid);
    writeFlag("systemColorScheme", app.systemColorScheme);
    text += std::string("colorScheme=") +
            (app.colorScheme == ColorScheme::Light ? "light" : "dark") + "\n";
    text += "gridIconSize=" + std::to_string(static_cast<int>(app.gridIconSize)) + "\n";
    text += "uiFontSize=" + std::to_string(app.uiFontSize) + "\n";
    text += "monoFontSize=" + std::to_string(app.monoFontSize) + "\n";
    text += "rowSpacing=" + std::to_string(static_cast<int>(app.rowSpacing)) + "\n";
    text += "previewStep=" + std::to_string(app.previewStep) + "\n";

    // Il layout salvato, quando c'e'. `layout.on` e' la riga che dice se il resto
    // vale: senza di lei un file vecchio o scritto a mano non riempirebbe campi a
    // caso.
    if (app.savedLayout.defined)
    {
        const AppState::SavedLayout& layout = app.savedLayout;
        text += "layout.on=1\n";
        text += "layout.viewMode=" + std::to_string(layout.viewMode) + "\n";
        text += "layout.previewStep=" + std::to_string(layout.previewStep) + "\n";
        char zoom[32];
        std::snprintf(zoom, sizeof(zoom), "layout.zoom=%.3f\n", static_cast<double>(layout.zoom));
        text += zoom;
        text += "layout.sort=" + std::to_string(layout.sortColumn) + "\n";
        text += std::string("layout.ascending=") + (layout.sortAscending ? "1" : "0") + "\n";
        text += "layout.group=" + std::to_string(layout.groupMode) + "\n";
        text += std::string("layout.sidebar=") + (layout.sidebarVisible ? "1" : "0") + "\n";
        text += std::string("layout.subfolders=") + (layout.showSubfolders ? "1" : "0") + "\n";
        std::string bits;
        for (int i = 0; i < kColumnCount; ++i)
            bits += layout.columns[static_cast<size_t>(i)].hidden ? '1' : '0';
        text += "layout.columns=" + bits + "\n";
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == text.size();
}

void RememberSettings(AppState& app)
{
    if (!SaveSettings(app))
        ReportStatus(app, TrW(L"Impostazioni non salvate: il file non si e' potuto scrivere",
                              L"Settings not saved: the file could not be written"));
}
} // namespace Bolt
namespace Bolt
{
namespace
{
// Legge un file di testo UTF-8 tutto in una volta. Falso se non c'e'.
bool ReadWholeFile(const std::wstring& path, std::string* out)
{
    out->clear();
    if (path.empty())
        return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    char buffer[8192];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        out->append(buffer, read);
    CloseHandle(file);
    return true;
}

bool WriteWholeFile(const std::wstring& path, const std::string& text)
{
    if (path.empty())
        return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == text.size();
}
} // namespace

std::wstring SessionPath()
{
    const std::wstring base = SettingsPath();   // ...\Bolt\settings.ini
    if (base.empty())
        return std::wstring();
    const size_t slash = base.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return std::wstring();
    return base.substr(0, slash + 1) + L"session.ini";
}

void SaveSession(const AppState& app)
{
    const std::wstring path = SessionPath();
    if (path.empty())
        return;

    // Spento vuol dire **nessuna traccia**: il file si cancella, non resta li' con
    // dentro l'ultima sessione. E' la differenza fra "non lo faccio piu'" e "l'ho
    // fatto finora e lo tengo".
    if (!app.rememberTabs)
    {
        DeleteFileW(path.c_str());
        return;
    }

    std::string text =
        "# Bolt — i pannelli e le schede aperti l'ultima volta.\n"
        "# Scritto solo perche' 'Riapri le schede' e' acceso; cancellabile a mano.\n"
        "# Nessun altro dato: solo cartelle (che sono la sessione) e la forma della\n"
        "# finestra. `docs/06`.\n";

    const int paneCount = static_cast<int>(app.panes.size());
    // La **forma** della finestra: l'asse e dove sta il divisore, piu' quale pannello
    // aveva il fuoco. Sono le due cose che servono a ritrovarla com'era e non sono
    // percorsi: niente che riguardi l'utente. Con un pannello solo non si scrivono.
    if (paneCount >= 2)
    {
        text += std::string("split=") + (app.splitColumn ? "column" : "row") + "\n";
        char fraction[32] = {};
        std::snprintf(fraction, sizeof(fraction), "splitFraction=%.3f", app.splitFraction);
        text += std::string(fraction) + "\n";
        text += "activePane=" + std::to_string(app.activePane) + "\n";
    }

    // Le schede si scrivono **per pannello**, con `pane=` davanti a ogni gruppo: le file
    // sono due, e un elenco unico le rimetterebbe tutte nella prima.
    //
    // La scheda **attiva** di ogni pannello ha la cartella nello stato vivo, non nel suo
    // `Tab`: la struct si riempie quando si cambia scheda (`SaveActiveTab`), e quella li'
    // non e' passata. Chiedere solo a `tab.directory` scriveva un file senza nessuna
    // scheda — visto: `active=0` e nient'altro. Vale per il pannello attivo; l'altro ha
    // le voci gia' nel tab, perche' e' cosi' che si disegna.
    for (int pane = 0; pane < paneCount; ++pane)
    {
        const Pane& data = app.panes[static_cast<size_t>(pane)];
        text += "pane=" + std::to_string(pane) + "\n";
        text += "paneActive=" + std::to_string(data.activeTab) + "\n";
        for (size_t i = 0; i < data.tabs.size(); ++i)
        {
            const bool live = (pane == app.activePane && static_cast<int>(i) == data.activeTab);
            const std::wstring& directory = live ? app.directory : data.tabs[i].directory;
            if (directory.empty())
                continue;
            text += "tab=" + ToUtf8(directory) + "\n";
        }
    }
    WriteWholeFile(path, text);
}

bool RestoreSession(AppState& app)
{
    if (!app.rememberTabs)
        return false;

    std::string text;
    if (!ReadWholeFile(SessionPath(), &text))
        return false;

    // Un elenco di cartelle **per pannello**, con la scheda accesa di ognuno. Le file
    // sono due e vanno ricostruite una per una: un elenco unico non direbbe quale
    // scheda finiva dove.
    std::vector<std::vector<std::wstring>> paneTabs;
    std::vector<int>                       paneActive;
    const auto ensurePane = [&paneTabs, &paneActive](int index) {
        while (static_cast<int>(paneTabs.size()) <= index)
        {
            paneTabs.push_back({});
            paneActive.push_back(0);
        }
    };

    int  pane        = 0;      // il gruppo di `tab=` che si sta leggendo
    int  activePane   = 0;
    bool splitColumn  = true;
    float splitFraction = 0.5f;
    size_t at = 0;
    while (at < text.size())
    {
        const size_t end = text.find('\n', at);
        const std::string line = text.substr(at, (end == std::string::npos ? text.size() : end) - at);
        at = (end == std::string::npos) ? text.size() : end + 1;
        if (line.empty() || line[0] == '#')
            continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key   = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "pane")
        {
            pane = (std::max)(0, std::atoi(value.c_str()));
            ensurePane(pane);
        }
        else if (key == "paneActive")
        {
            ensurePane(pane);
            paneActive[static_cast<size_t>(pane)] = std::atoi(value.c_str());
        }
        else if (key == "split")
        {
            splitColumn = (value == "column");
        }
        else if (key == "splitFraction")
        {
            splitFraction = static_cast<float>(std::atof(value.c_str()));
        }
        else if (key == "activePane")
        {
            activePane = std::atoi(value.c_str());
        }
        else if (key == "tab" && !value.empty())
        {
            ensurePane(pane);
            const std::wstring path = Widen(value);
            // Una cartella sparita si salta: riaprire una scheda su niente non aiuta
            // nessuno. Le posizioni di spazio dei nomi non hanno attributi da leggere.
            if (ShellLocation::IsNamespace(path))
            {
                paneTabs[static_cast<size_t>(pane)].push_back(path);
                continue;
            }
            const DWORD attributes = GetFileAttributesW(IOPath(path, app.longPaths).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                paneTabs[static_cast<size_t>(pane)].push_back(path);
        }
    }

    if (paneTabs.empty() || paneTabs[0].empty())
        return false;

    // La prima cartella va nella scheda che c'e' gia' (chi chiama ha appena creato la
    // prima tab), le altre sono schede nuove. `NewTab` carica la cartella, quindi lo
    // stato vivo finisce per essere l'ultima: alla fine si attiva quella che era
    // attiva, e la vista torna dove era rimasta.
    LoadDirectory(app, paneTabs[0][0], /*recordHistory=*/false);
    for (size_t i = 1; i < paneTabs[0].size(); ++i)
        NewTab(app, paneTabs[0][i]);
    if (paneActive[0] >= 0 &&
        paneActive[0] < static_cast<int>(app.panes[static_cast<size_t>(app.activePane)].tabs.size()))
        ActivateTab(app, paneActive[0]);

    // Il secondo pannello, se c'era: si divide **dopo** aver rimesso a posto il primo,
    // perche' dividerlo prima vorrebbe dire ricostruire a mano quello che `SplitPane`
    // fa gia'. `SplitPane` lascia nel pannello nuovo una copia della cartella di quello
    // di partenza: la sua prima scheda si riporta dove era.
    if (paneTabs.size() > 1 && !paneTabs[1].empty())
    {
        SplitPane(app, splitColumn);
        LoadDirectory(app, paneTabs[1][0], /*recordHistory=*/false);
        for (size_t i = 1; i < paneTabs[1].size(); ++i)
            NewTab(app, paneTabs[1][i]);
        if (paneActive.size() > 1 && paneActive[1] >= 0 &&
            paneActive[1] < static_cast<int>(app.panes[static_cast<size_t>(app.activePane)].tabs.size()))
            ActivateTab(app, paneActive[1]);

        app.splitFraction = std::clamp(splitFraction, 0.25f, 0.75f);
        // Il fuoco dov'era. `SplitPane` l'ha portato sul pannello nuovo, e se era sul
        // primo si torna li' — con il suo stato, che `ActivatePane` sa salvare.
        if (activePane == 0)
            ActivatePane(app, 0);
    }
    return true;
}
} // namespace Bolt
