// ---------------------------------------------------------------------------
// La barra di stato, e la riga che sta in fondo alla finestra.
//
// E' il pezzo piu' piccolo uscito da `main.cpp`, ed e' uscito per la stessa ragione
// degli altri: era una funzione di disegno in mezzo a un file che fa altro. Qui ci
// sono due cose che sembrano una sola e non lo sono:
//
//   * l'**esito** di quello che si e' appena fatto — "Incollato 1 elemento" — che
//     prende il posto di tutto il resto per quattro secondi, perche' chi ha appena
//     premuto un tasto guarda li';
//   * i **numeri** — quante voci, quante scelte, dove si e' nella lista — e i
//     selettori della barra in fondo: vista, misura delle anteprime, ingrandimento.
//
// I selettori registrano i loro rettangoli come bersagli di clic (`ClickTarget`) e
// non rispondono da soli: chi li preme e' l'input, che sta ancora in `main.cpp` e
// conosce i comandi. E' la stessa divisione della lista: il disegno sa dove sono le
// cose, l'input sa cosa fanno.
// ---------------------------------------------------------------------------
#include "view_statusbar.h"

#include "app_ui.h"
#include "diag.h"
#include "language.h"

#include <cstdio>
#include <string>

namespace Bolt
{
void DrawStatusBar(AppState& app, const Layout& layout)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    // Un esito recente prende il posto di tutto il resto. Chi ha appena premuto un
    // tasto guarda li' per sapere com'e' andata, e i numeri non rispondono alla sua
    // domanda.
    const bool showingResult = !app.statusMessage.empty() &&
                               NowSeconds() - app.statusMessageTime < 4.0;
    if (showingResult)
    {
        app.smallFont.Draw(r, 12.0f * scale, layout.statusBar.y + 4.0f * scale,
                           ToUtf8(app.statusMessage), t.accent, scale);
        return;
    }

    // A sinistra quante cose ci sono e quante sono scelte; a destra dove si e' nella
    // lista, come si sta guardando, e quanto e' ingrandita. Erano contatori del
    // motore — fotogrammi, quadrilateri, lavori in corso — che servono a chi scrive
    // il programma e non a chi lo usa: adesso stanno dietro `BOLT_DEV_HUD=1`.
    char counts[96];
    std::snprintf(counts, sizeof(counts),
                  app.entries.size() == 1 ? Tr("%zu voce", "%zu item")
                                          : Tr("%zu voci", "%zu items"),
                  app.entries.size());
    app.smallFont.Draw(r, 12.0f * scale, layout.statusBar.y + 4.0f * scale, counts, t.textDim, scale);

    if (!app.selection.empty())
    {
        char chosen[96];
        std::snprintf(chosen, sizeof(chosen),
                      app.selection.size() == 1 ? Tr("%zu selezionato", "%zu selected")
                                                : Tr("%zu selezionati", "%zu selected"),
                      app.selection.size());
        const float x = 12.0f * scale + app.smallFont.Measure(counts, scale) + 16.0f * scale;
        app.smallFont.Draw(r, x, layout.statusBar.y + 4.0f * scale, chosen, t.text, scale);
    }

    if (DevHudEnabled())
    {
        char dev[192];
        std::snprintf(dev, sizeof(dev), "%.0f fps | %.0f quads | %d icons, %d previews | %d jobs",
                      static_cast<double>(app.framesPerSecond),
                      static_cast<double>(app.renderer->QuadCount()), app.icons.IconCount(),
                      app.previews.ReadyCount(), app.stats.ActiveJobs());
        const float x = 12.0f * scale + app.font.Measure("0000 items", scale) +
                        app.smallFont.Measure("000 selected", scale) + 40.0f * scale;
        app.smallFont.Draw(r, x, layout.statusBar.y + 4.0f * scale, dev, t.textDim, scale);
    }

    // A destra, al contrario: la posizione nella lista. Il conto e' fra le voci
    // **visibili** — i gruppi aggiungono righe che non sono voci, e un filtro ne
    // toglie — ed e' quello che serve a orientarsi.
    int visible = 0;
    int position = 0;
    for (const Row& row : app.rows)
    {
        if (row.header)
            continue;
        ++visible;
        const Entry& entry = app.entries[static_cast<size_t>(row.entry)];
        if (position == 0 && entry.wideName == app.cursor)
            position = visible;
    }

    // --- la barra in fondo: viste, anteprime, ingrandimento --------------------
    //
    // Il riferimento mette qui i selettori di vista e la percentuale delle
    // anteprime. Da noi la riga e' una sola — la barra di stato — e ci stanno
    // tutti, costruiti da destra verso sinistra: ingrandimento, anteprime, viste,
    // posizione.
    const float textY = layout.statusBar.y + 4.0f * scale;
    float penX = layout.statusBar.right() - 14.0f * scale;

    const std::string zoomText =
        std::to_string(static_cast<int>(app.zoom * 100.0f /
                                        (app.dpiScale > 0.0f ? app.dpiScale : 1.0f))) + "%";
    penX -= app.smallFont.Measure(zoomText, scale);
    app.smallFont.Draw(r, penX, textY, zoomText, t.textDim, scale);
    penX -= 18.0f * scale;

    // Lo stepper delle anteprime: etichetta, −, valore, +.
    {
        const std::string percent =
            std::to_string(static_cast<int>(app.PreviewZoom() * 100.0f)) + "%";
        const float valueWidth = app.smallFont.Measure(percent, scale);
        const float button     = 14.0f * scale;
        const float plusX  = penX - button;
        const float valueX = plusX - 8.0f * scale - valueWidth;
        const float minusX = valueX - 8.0f * scale - button;
        app.smallFont.Draw(r, plusX,  textY, "+", t.text, scale);
        app.smallFont.Draw(r, valueX, textY, percent, t.text, scale);
        app.smallFont.Draw(r, minusX, textY, "-", t.text, scale);
        const std::string label = Tr("Anteprime", "Previews");
        const float labelX = minusX - 10.0f * scale - app.smallFont.Measure(label, scale);
        app.smallFont.Draw(r, labelX, textY, label, t.textDim, scale);

        ClickTarget down;
        down.rect  = Rect{minusX - 5.0f * scale, layout.statusBar.y, button + 10.0f * scale,
                          layout.statusBar.h};
        down.kind  = ClickTarget::Kind::PreviewDown;
        app.clickTargets.push_back(down);
        ClickTarget up;
        up.rect = Rect{plusX - 5.0f * scale, layout.statusBar.y, button + 10.0f * scale,
                       layout.statusBar.h};
        up.kind = ClickTarget::Kind::PreviewUp;
        app.clickTargets.push_back(up);

        penX = labelX - 18.0f * scale;
    }

    // I quattro selettori di vista. E' l'unico posto dove si cambiano col
    // puntatore, invece di passare dalla palette.
    {
        struct Choice { ViewMode mode; const char* it; const char* en; };
        static const Choice kChoices[4] = {
            {ViewMode::Details, "Dettagli", "Details"},
            {ViewMode::List,    "Elenco",   "List"},
            {ViewMode::Tiles,   "Riquadri", "Tiles"},
            {ViewMode::Icons,   "Icone",    "Icons"},
        };
        for (int i = 3; i >= 0; --i)
        {
            const std::string label = Tr(kChoices[i].it, kChoices[i].en);
            const float width = app.smallFont.Measure(label, scale) + 14.0f * scale;
            const Rect chip{penX - width, layout.statusBar.y + 2.0f * scale, width,
                            layout.statusBar.h - 4.0f * scale};
            const bool active = (app.viewMode == kChoices[i].mode);
            if (active)
                r.FillRect(chip, t.accent.WithAlpha(0.22f), 3.0f * scale);
            app.smallFont.Draw(r, chip.x + 7.0f * scale, TextY(layout, chip, app.smallFont),
                               label, active ? t.text : t.textDim, scale);
            ClickTarget target;
            target.rect  = chip;
            target.kind  = ClickTarget::Kind::ViewSelect;
            target.index = static_cast<int>(kChoices[i].mode);
            app.clickTargets.push_back(target);
            penX -= width + 4.0f * scale;
        }
        penX -= 14.0f * scale;
    }

    if (position > 0)
    {
        const std::string pos = std::to_string(position) + " / " + std::to_string(visible);
        const float width = app.smallFont.Measure(pos, scale);
        app.smallFont.Draw(r, penX - width, textY, pos, t.textDim, scale);
    }
}
// Un'operazione che fallisce in silenzio e' peggio di una che non parte: senza
// un messaggio, "Sposta" su un file in uso sembrerebbe non aver fatto nulla.
void ReportStatus(AppState& app, const std::wstring& message)
{
    app.statusMessage     = message;
    app.statusMessageTime = NowSeconds();
}

} // namespace Bolt
