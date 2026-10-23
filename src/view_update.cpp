// ---------------------------------------------------------------------------
// La finestra dell'aggiornamento — vedi `view_update.h` per il perche' e `docs/04` M13
// per la decisione. Qui c'e' solo la forma: dove sta, cosa dice, cosa si preme.
//
// La geometria e' **centrata e derivata** dalla misura della finestra, come il pannello
// delle proprieta': a che misura si legge dipende dai caratteri, e i caratteri li
// misura `app.font`.
// ---------------------------------------------------------------------------
#include "view_update.h"

#include <windows.h>

#include <algorithm>
#include <string>

#include "app_actions.h"
#include "app_ui.h"
#include "language.h"

namespace Bolt
{
namespace
{
// La finestra: una misura sola per tutti gli stati, perche' non deve saltare mentre il
// download avanza. In altezza ci sta il titolo, due righe di testo, la barra e i
// pulsanti.
constexpr float kDialogWidth  = 520.0f;
constexpr float kDialogHeight = 196.0f;

std::string FitSmall(AppState& app, const std::string& text, float width, float scale)
{
    return FitText(app.smallFont, text, scale, width);
}

std::string FitBig(AppState& app, const std::string& text, float width, float scale)
{
    return FitText(app.font, text, scale, width);
}

// Quante parti di barra riempire: frazione **vera** solo quando il totale si sa.
float ProgressFraction(const Update::Snapshot& snapshot)
{
    if (snapshot.total == 0)
        return 0.0f;
    const double fraction = static_cast<double>(snapshot.received) /
                            static_cast<double>(snapshot.total);
    return static_cast<float>((std::max)(0.0, (std::min)(1.0, fraction)));
}
} // namespace

bool UpdateDialogVisible(const AppState& app)
{
    return app.updateDialogOpen;
}

std::wstring UpdateProgressLine(const Update::Snapshot& snapshot)
{
    switch (snapshot.phase)
    {
        case Update::Phase::Checking:
            return TrW(L"Controllo gli aggiornamenti...", L"Checking for updates...");
        case Update::Phase::Checksums:
            return TrW(L"Scarico le impronte SHA-256...", L"Downloading SHA-256 checksums...");
        case Update::Phase::Verifying:
            return TrW(L"Verifico l'impronta SHA-256...", L"Verifying the SHA-256 checksum...");
        case Update::Phase::Downloading:
        default:
            return TrW(L"Scarico l'aggiornamento...", L"Downloading the update...");
    }
}

Rect UpdateDialogRect(const AppState& app, const Layout& layout)
{
    const float scale = layout.scale;
    const float width  = kDialogWidth * scale;
    const float height = kDialogHeight * scale;
    const float x = (static_cast<float>(app.viewportWidth) - width) * 0.5f;
    // Un filo sopra il centro: al centro esatto la finestra sembra caduta giu'.
    const float y = (static_cast<float>(app.viewportHeight) - height) * 0.5f - 30.0f * scale;
    return Rect{(std::max)(8.0f * scale, x), (std::max)(8.0f * scale, y), width, height};
}

namespace
{
// Il pulsante si misura sul testo: `Annulla` e `Riavvia ora` non sono larghi uguale, e
// un pulsante largo uguale per tutti e due lascia un vuoto da una parte e un testo
// stretto dall'altra.
float ButtonWidth(AppState& app, const char* label, float scale)
{
    return (app.smallFont.Measure(label, scale) + 26.0f * scale);
}

Rect ButtonAt(const AppState& app, const Layout& layout, const Rect& dialog, bool primary)
{
    const float scale = layout.scale;
    const float height = 26.0f * scale;
    const float y = dialog.bottom() - height - 16.0f * scale;

    std::string primaryLabel = Tr("Chiudi", "Close");
    if (app.updateInstalling)
        primaryLabel = Tr("Installazione...", "Installing...");
    else if (!app.updateDone)
        primaryLabel = Tr("Annulla", "Cancel");
    else if (app.updateResult == Update::Result::Ready)
        primaryLabel = Tr("Riavvia ora", "Restart now");

    AppState& mutableApp = const_cast<AppState&>(app);
    const float primaryWidth = ButtonWidth(mutableApp, primaryLabel.c_str(), scale);

    if (primary)
        return Rect{dialog.right() - primaryWidth - 16.0f * scale, y, primaryWidth, height};

    if (app.updateDone && app.updateResult == Update::Result::Ready)
    {
        const char* label = Tr("Piu' tardi", "Later");
        const float width = ButtonWidth(mutableApp, label, scale);
        return Rect{dialog.right() - primaryWidth - width - 30.0f * scale, y, width, height};
    }

    // Nessun secondo pulsante: il rettangolo e' vuoto e nessun clic lo incontra.
    return Rect{0.0f, 0.0f, 0.0f, 0.0f};
}
} // namespace

Rect UpdateDialogPrimaryButton(const AppState& app, const Layout& layout, const Rect& dialog)
{
    return ButtonAt(app, layout, dialog, /*primary=*/true);
}

Rect UpdateDialogSecondaryButton(const AppState& app, const Layout& layout, const Rect& dialog)
{
    return ButtonAt(app, layout, dialog, /*primary=*/false);
}

void DrawUpdateDialog(AppState& app, const Layout& layout)
{
    if (!app.updateDialogOpen || app.renderer == nullptr)
        return;

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect dialog = UpdateDialogRect(app, layout);

    // Il velo: la finestra di sotto resta visibile e **smette di rispondere** — e' il
    // segno che quello che si sta guardando e' un'altra cosa.
    r.FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                    static_cast<float>(app.viewportHeight)}, Color{0.0f, 0.0f, 0.0f, 0.45f});
    r.Draw(UIDraw::Shadow(Rect{dialog.x + 3.0f * scale, dialog.y + 6.0f * scale,
                               dialog.w, dialog.h},
                          t.shadow, 22.0f * scale, 10.0f * scale));
    r.Draw(UIDraw::Fill(dialog, t.panelBg, 10.0f * scale));
    r.Draw(UIDraw::Border(dialog, t.panelBorder, 1.0f, 10.0f * scale));

    const float left  = dialog.x + 18.0f * scale;
    const float width = dialog.w - 36.0f * scale;

    app.smallFont.Draw(r, left, dialog.y + 12.0f * scale, Tr("Aggiornamento", "Update"),
                       t.textDim, scale);

    // La riga grande: cosa sta succedendo, o com'e' andata.
    std::wstring headline;
    std::wstring detail;
    // Una seconda riga, per le frasi che a questa larghezza non ci stanno: troncare
    // l'istruzione che dice cosa succede sarebbe come non scriverla.
    std::wstring detail2;
    if (!app.updateDone)
    {
        headline = UpdateProgressLine(app.updateJob != nullptr
                                          ? app.updateJob->Progress()
                                          : Update::Snapshot{});
    }
    else
    {
        switch (app.updateResult)
        {
            case Update::Result::Ready:
                headline = TrW(L"Bolt ", L"Bolt ") + app.updateVersion +
                           TrW(L" scaricato e verificato.", L" downloaded and verified.");
                // Due righe, e corte: a cinquecento pixel una frase lunga si tronca, e
                // l'istruzione che dice cosa succede e' l'unica cosa che non si puo'
                // perdere.
                if (app.updateInstalling)
                {
                    detail = TrW(L"Installo e riavvio Bolt...", L"Installing and restarting Bolt...");
                }
                else
                {
                    detail  = TrW(L"Riavvia Bolt per installarlo.", L"Restart Bolt to install it.");
                    detail2 = TrW(L"Bolt si chiude, il setup installa e Bolt si riapre.",
                                  L"Bolt closes, the setup installs and Bolt reopens.");
                }
                break;
            case Update::Result::UpToDate:
                headline = TrW(L"Bolt e' aggiornato (", L"Bolt is up to date (") +
                           Update::CurrentVersion() + L").";
                break;
            case Update::Result::Cancelled:
                headline = TrW(L"Aggiornamento annullato.", L"Update cancelled.");
                break;
            case Update::Result::Failed:
            default:
                headline = TrW(L"Aggiornamento non riuscito.", L"The update failed.");
                detail   = app.updateError;
                break;
        }
    }

    app.font.Draw(r, left, dialog.y + 34.0f * scale,
                  FitBig(app, ToUtf8(headline), width, scale), t.text, scale);

    float after = dialog.y + 34.0f * scale + app.font.LineHeight() * scale + 6.0f * scale;
    for (const std::wstring* line : {&detail, &detail2})
    {
        if (line->empty())
            continue;
        app.smallFont.Draw(r, left, after,
                           FitSmall(app, ToUtf8(*line), width, scale), t.textDim, scale);
        after += app.smallFont.LineHeight() * scale + 4.0f * scale;
    }

    // La barra: solo mentre si scarica, perche' e' l'unica fase che ha una durata.
    // Prima e dopo non c'e' niente da misurare, e una barra ferma a meta' sarebbe una
    // bugia comoda.
    if (!app.updateDone && app.updateJob != nullptr)
    {
        const Update::Snapshot snapshot = app.updateJob->Progress();
        if (snapshot.phase == Update::Phase::Downloading)
        {
            const Rect track{left, after + 10.0f * scale, width, 8.0f * scale};
            r.Draw(UIDraw::Fill(track, t.volumeTrack, 4.0f * scale));

            const float fraction = ProgressFraction(snapshot);
            if (fraction > 0.0f)
                r.Draw(UIDraw::Fill(Rect{track.x, track.y, track.w * fraction, track.h},
                                    t.accent, 4.0f * scale));

            // I byte, e la percentuale quando il totale si sa. `>=` e non `>`: una
            // risposta senza lunghezza dichiarata mostrerebbe "0 MB", che non e' vero.
            std::string measured;
            if (snapshot.received >= 1024)
                measured = HumanBytes(snapshot.received);
            if (snapshot.total > 0)
            {
                measured += (measured.empty() ? "" : " / ") + HumanBytes(snapshot.total) +
                            " (" + std::to_string(static_cast<int>(fraction * 100.0f)) + "%)";
            }
            app.smallFont.Draw(r, left, track.bottom() + 6.0f * scale, measured, t.text, scale);
        }
    }

    // I pulsanti. Il primario e' sempre acceso; il secondario e' un'uscita, non una
    // scelta, e resta spento come le righe che non fanno niente.
    const Rect primary = UpdateDialogPrimaryButton(app, layout, dialog);
    const Rect secondary = UpdateDialogSecondaryButton(app, layout, dialog);

    const std::string primaryLabel =
        app.updateInstalling               ? Tr("Installazione...", "Installing...")
        : !app.updateDone                  ? Tr("Annulla", "Cancel")
        : app.updateResult == Update::Result::Ready ? Tr("Riavvia ora", "Restart now")
                                                   : Tr("Chiudi", "Close");

    const bool ready = app.updateDone && app.updateResult == Update::Result::Ready;
    const Color primaryFill = app.updateInstalling ? t.volumeTrack : (ready ? t.accent : t.rowHover);
    const Color primaryText = app.updateInstalling ? t.textDim : t.text;

    r.Draw(UIDraw::Fill(primary, primaryFill, 5.0f * scale));
    r.Draw(UIDraw::Border(primary, t.panelBorder, 1.0f, 5.0f * scale));
    app.smallFont.Draw(r, primary.x + 13.0f * scale, TextY(layout, primary, app.smallFont),
                       primaryLabel, primaryText, scale);

    if (secondary.w > 0.0f)
    {
        r.Draw(UIDraw::Fill(secondary, t.rowHover, 5.0f * scale));
        r.Draw(UIDraw::Border(secondary, t.panelBorder, 1.0f, 5.0f * scale));
        app.smallFont.Draw(r, secondary.x + 13.0f * scale,
                           TextY(layout, secondary, app.smallFont), Tr("Piu' tardi", "Later"),
                           t.text, scale);
    }

    // Cosa fa la tastiera, detto una volta e in piccolo: e' la stessa riga che il
    // pannello dell'operazione scrive sotto i suoi pulsanti.
    app.smallFont.Draw(r, left, dialog.bottom() - 24.0f * scale,
                       app.updateDone ? Tr("Esc chiude", "Esc closes")
                                      : Tr("Esc annulla", "Esc cancels"),
                       t.textDim, scale);
}

void UpdateDialogClick(AppState& app, float x, float y)
{
    if (!app.updateDialogOpen)
        return;

    const Layout layout = ComputeLayout(app);
    const Rect dialog = UpdateDialogRect(app, layout);

    if (UpdateDialogPrimaryButton(app, layout, dialog).Contains(x, y))
    {
        UpdateDialogPrimary(app);
        return;
    }
    if (UpdateDialogSecondaryButton(app, layout, dialog).Contains(x, y))
    {
        UpdateDialogSecondary(app);
        return;
    }

    // Fuori dai pulsanti non succede niente, e nemmeno sopra il velo: una finestra
    // modale che si chiude cliccando dove capita e' una finestra che si perde per
    // sbaglio.
    (void)dialog;
}

void UpdateDialogKey(AppState& app, WPARAM key)
{
    if (!app.updateDialogOpen)
        return;

    if (key == VK_ESCAPE)
    {
        // Con il lavoro in corso `Esc` e' l'annullamento, come nel pannello
        // dell'operazione: chiudere la finestra e lasciare il download a girare
        // vorrebbe dire aver perso il modo di fermarlo.
        if (!app.updateDone && app.updateJob != nullptr)
        {
            app.updateJob->Cancel();
        }
        else
        {
            app.updateDialogOpen = false;
        }
        return;
    }

    if (key == VK_RETURN)
        UpdateDialogPrimary(app);
}

void UpdateDialogPrimary(AppState& app)
{
    if (!app.updateDialogOpen || app.updateInstalling)
        return;

    if (!app.updateDone)
    {
        if (app.updateJob != nullptr)
            app.updateJob->Cancel();
        return;
    }

    if (app.updateResult == Update::Result::Ready)
    {
        // L'ultima parola e' dell'autore: si chiude **adesso**, e il setup che sta
        // gia' aspettando prende il posto dell'eseguibile (`docs/04` M13).
        BeginInstallUpdate(app);
        return;
    }

    app.updateDialogOpen = false;
}

void UpdateDialogSecondary(AppState& app)
{
    // `Piu' tardi` chiude solo la finestra: il setup verificato resta dov'e', e il
    // comando rifatto riapre questo stesso stato invece di riscaricare.
    app.updateDialogOpen = false;
}
} // namespace Bolt
