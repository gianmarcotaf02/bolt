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

    // Il conto delle scelte e' quello delle voci che si **vedono**. Il filtro non
    // toglie niente dalla selezione — le operazioni scartano gia' le voci nascoste,
    // `docs/02` §7.55 — ma «3 selezionati» davanti a una riga sola e' un numero che
    // sullo schermo non si trova: la barra di stato dice quante cose si vedono
    // scelte, come il conto della posizione a destra conta le righe visibili.
    std::size_t chosenCount = 0;
    for (const Row& row : app.rows)
    {
        if (row.header)
            continue;
        if (app.selection.count(app.entries[static_cast<size_t>(row.entry)].wideName) != 0)
            ++chosenCount;
    }

    if (chosenCount != 0)
    {
        char chosen[96];
        std::snprintf(chosen, sizeof(chosen),
                      chosenCount == 1 ? Tr("%zu selezionato", "%zu selected")
                                       : Tr("%zu selezionati", "%zu selected"),
                      chosenCount);
        const float x = 12.0f * scale + app.smallFont.Measure(counts, scale) + 16.0f * scale;
        app.smallFont.Draw(r, x, layout.statusBar.y + 4.0f * scale, chosen, t.text, scale);
    }

    if (DevHudEnabled())
    {
        char dev[192];
        std::snprintf(dev, sizeof(dev), "%.0f fps | %zu draws, %.0f quads | %d icons, %d previews | %d jobs",
                      static_cast<double>(app.framesPerSecond), app.renderer->DrawCalls(),
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

    // Cartelle e file, e quante sono scelte: le stesse due pastiglie che il
    // riferimento tiene in fondo a destra. Con una selezione il numero davanti e' quello
    // delle scelte — `2/4` vuol dire "due cartelle scelte su quattro" — e senza
    // selezione non c'e' niente da mettere davanti, quindi si legge quanti sono.
    {
        int folders = 0;
        int files = 0;
        int chosenFolders = 0;
        int chosenFiles = 0;
        // Con `Toggle Subfolders` acceso i due numeri vogliono dire un'altra cosa, ed e'
        // quella del riferimento: **dirette / tutte**. `4/5` cartelle vuol dire quattro
        // nella cartella aperta e una dentro una sottocartella; senza, il primo numero
        // e' quello delle scelte, e senza nemmeno quelle si scrive il totale.
        int directFolders = 0;
        int directFiles = 0;
        for (const Entry& entry : app.entries)
        {
            const bool chosen = app.selection.count(entry.wideName) != 0;
            const bool direct = entry.parentHint.empty();
            if (entry.isDirectory)
            {
                ++folders;
                chosenFolders += chosen ? 1 : 0;
                directFolders += direct ? 1 : 0;
            }
            else
            {
                ++files;
                chosenFiles += chosen ? 1 : 0;
                directFiles += direct ? 1 : 0;
            }
        }

        std::string folderText;
        std::string fileText;
        if (app.showSubfolders)
        {
            folderText = std::to_string(directFolders) + "/" + std::to_string(folders);
            fileText   = std::to_string(directFiles) + "/" + std::to_string(files);
        }
        else if (chosenCount != 0)
        {
            folderText = std::to_string(chosenFolders) + "/" + std::to_string(folders);
            fileText   = std::to_string(chosenFiles) + "/" + std::to_string(files);
        }
        else
        {
            folderText = std::to_string(folders) + " " + Tr("cartelle", "folders");
            fileText   = std::to_string(files) + " " + Tr("file", "files");
        }

        // Due pastiglie, da destra: prima i file, poi le cartelle.
        const std::string texts[2] = {fileText, folderText};
        for (const std::string& text : texts)
        {
            const float width = app.smallFont.Measure(text, scale) + 14.0f * scale;
            app.smallFont.Draw(r, penX - width + 7.0f * scale, textY, text, t.textDim, scale);
            penX -= width + 4.0f * scale;
        }

        // Il pulsante della **cartella nuova**, dove il riferimento lo mette: accanto ai
        // conteggi. Un comando che esiste solo da tastiera e' un comando che meta' delle
        // persone non trova.
        const float side = layout.statusBar.h - 8.0f * scale;
        const Rect button{penX - side - 8.0f * scale, layout.statusBar.y + 4.0f * scale, side, side};
        r.FillRect(button, t.headerBg, 3.0f * scale);
        app.smallFont.Draw(r,
                           button.x + (side - app.smallFont.Measure("+", scale)) * 0.5f,
                           TextY(layout, button, app.smallFont), "+", t.text, scale);
        ClickTarget target;
        target.rect = button;
        target.kind = ClickTarget::Kind::NewFolder;
        app.clickTargets.push_back(target);
        penX -= side + 14.0f * scale;
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
