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

#include <string>
#include <vector>

#include "app_ui.h"
#include "language.h"
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
    if (ReadSetting(text, "monoFace", &value) && !value.empty())
        app.monoFace = Widen(value);

    const auto readFlag = [&](const char* key, bool* target) {
        std::string v;
        if (ReadSetting(text, key, &v))
            *target = (v == "1");
    };
    readFlag("showTooltips", &app.showTooltips);
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
    text += "monoFace=" + ToUtf8(app.monoFace) + "\n";

    const auto writeFlag = [&](const char* key, bool value) {
        text += std::string(key) + "=" + (value ? "1" : "0") + "\n";
    };
    writeFlag("showTooltips", app.showTooltips);
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
