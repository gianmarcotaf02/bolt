#include "view_sidebar.h"

#include "app_ui.h"
#include "shell_location.h"

namespace Bolt
{
// Riempie l'elenco: le sezioni, e dentro ognuna le voci.
void BuildSidebarRows(const AppState& app, std::vector<SidebarRow>* rows)
{
    const auto section = [&](const char* key, const char* title, const std::vector<Place>& places) {
        SidebarRow header;
        header.header = true;
        header.key    = key;
        header.title  = title;
        rows->push_back(std::move(header));

        if (app.collapsedSections.count(key) != 0)
            return;
        for (const Place& place : places)
        {
            SidebarRow row;
            row.label        = place.label;
            row.path         = place.path;
            row.icon         = place.icon;
            row.freeText     = place.freeText;
            row.usedFraction = place.usedFraction;
            row.volume       = !place.freeText.empty();
            rows->push_back(std::move(row));
        }
    };

    // QUICK ACCESS si mostra solo se ha qualcosa dentro: una sezione vuota qui non
    // vuol dire "non ne hai fissata nessuna", vuol dire che la cartella della shell
    // non si e' letta — e una sezione vuota senza spiegazione non si capisce.
    if (!app.quickAccess.empty())
        section("QUICK ACCESS", Tr("ACCESSO RAPIDO", "QUICK ACCESS"), app.quickAccess);

    // I recenti: le cartelle visitate in questa sessione, la piu' recente per
    // prima. E' il ranking dei recenti (M4) nella sua forma minima — solo cartelle,
    // e solo per la sessione (`StoreRecents: Session`, `docs/06`).
    {
        std::vector<Place> recents;
        for (const std::wstring& path : app.recents)
        {
            if (path.empty() || path == app.directory)
                continue;   // dove si e' gia' non e' un "recente" da riproporre
            // Il nome di una cartella, non il percorso: di un percorso la parte che
            // identifica e' la coda. Per una cartella di spazio dei nomi non c'e' una
            // coda da tagliare, e il nome lo da' la shell.
            const std::wstring label = ShellLocation::IsNamespace(path) ? LocationLabel(app, path)
                                                                       : LeafName(path);
            Place place;
            place.label = ToUtf8(label);
            place.path  = path;
            place.icon  = path;
            recents.push_back(std::move(place));
        }
        if (!recents.empty())
            section("RECENTS", Tr("RECENTI", "RECENTS"), recents);
    }

    std::vector<Place> marks;
    marks.reserve(app.bookmarks.size());
    for (const std::wstring& path : app.bookmarks)
    {
        Place place;
        place.label = ToUtf8(LeafName(path));
        place.path  = path;
        place.icon  = path;
        marks.push_back(std::move(place));
    }
    section("BOOKMARKS", Tr("PREFERITI", "BOOKMARKS"), marks);

    section("STORAGE", Tr("UNITÀ", "STORAGE"), app.volumes);
    // "Places" sono le cartelle standard — This PC, la cartella personale, le
    // cartelle di sistema, il cestino — quelle che ci sono sempre. "Posizioni" e'
    // il nome che usa l'ambiente GTK per lo stesso elenco, ed e' l'unico che dice
    // "dove si puo' andare" senza promettere che siano preferiti o cartelle.
    section("PLACES", Tr("POSIZIONI", "PLACES"), app.places);
}


void DrawSidebar(AppState& app, const Layout& layout)
{
    if (!app.sidebarVisible)
        return;

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.Draw(UIDraw::Fill(layout.sidebar, t.panelBg, 6.0f * scale));

    std::vector<SidebarRow> rows;
    BuildSidebarRows(app, &rows);

    float y = layout.sidebar.y + 8.0f * scale;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const SidebarRow& item = rows[i];
        const float rowHeight = (item.header ? 24.0f : (item.volume ? 46.0f : 26.0f)) * scale;
        const Rect row{layout.sidebar.x + 5.0f * scale, y, layout.sidebar.w - 10.0f * scale,
                       rowHeight};

        if (item.header)
        {
            // Chiusa o aperta si legge dalla **chiave**, non dal nome: il nome e'
            // tradotto, e una sezione chiusa in italiano deve restare chiusa anche
            // in inglese. Con il nome nel test, la chiave non si trovava mai e la
            // linguetta restava ferma — che e' il modo in cui un difetto diventa una
            // "freccia che non cambia mai", e nessuno lo chiama difetto.
            const bool collapsed = app.collapsedSections.count(item.key) != 0;
            if (static_cast<int>(i) == app.hoveredSection)
                r.FillRect(row, t.rowHover, 4.0f * scale);

            // Il verso del triangolino e' lo stato: chiuso punta a destra, aperto in
            // giu'. E' l'unico segno che dice che la sezione si puo' chiudere.
            //
            // E' un **triangolo disegnato**, non il carattere `>` o `v`: un carattere
            // non e' un elemento — cambia forma col carattere scelto nelle opzioni,
            // non si allinea con niente, e a questa misura e' un grumo. La misura e'
            // derivata dall'altezza della riga, cosi' resta giusta a ogni ingrandimento.
            const float arrow = 10.0f * scale;
            r.Draw(DisclosureArrow(Rect{row.x + 5.0f * scale, row.y + (rowHeight - arrow) * 0.5f,
                                        arrow, arrow},
                                   t.textDim, /*pointsDown=*/!collapsed));
            app.smallFont.Draw(r, row.x + 20.0f * scale, row.y + 5.0f * scale, item.title,
                               t.textDim, scale);

            ClickTarget target;
            target.rect  = row;
            target.kind  = ClickTarget::Kind::SidebarHeader;
            target.label = item.key;
            app.clickTargets.push_back(target);
            y += rowHeight;
            continue;
        }

        const bool active = (item.path == app.directory);
        if (active)
            r.FillRect(row, Color::FromHex(0x2E3138), 4.0f * scale);
        else if (static_cast<int>(i) == app.hoveredPlace)
            r.FillRect(row, t.rowHover, 4.0f * scale);

        // La voce su cui e' in corso una caduta: lo stesso tratteggio delle righe,
        // perche' e' la stessa cosa detta in un altro posto.
        if (static_cast<int>(i) == app.dropTargetSidebarRow)
            r.Draw(UIDraw::DashedBorder(row, t.accent, 2.0f * scale, 6.0f * scale, 4.0f * scale,
                                        4.0f * scale));

        // L'icona la disegna la shell, e non si tinge: e' la sua, ed e' quella che
        // Esplora risorse mostra per lo stesso oggetto — un disco per `C:`, un
        // computer per "Questo PC", un cestino per il cestino. Tingere quella a
        // colori con un colore del tema dava oro spento, e non era ne' l'icona
        // della shell ne' un glifo monocromatico.
        const ShellIconAtlas::IconUV* uv =
            item.icon.empty() ? nullptr : app.icons.GetForObject(item.icon);
        if (uv == nullptr)
            uv = app.icons.GetFor("", true);
        if (uv != nullptr)
        {
            const Rect icon{row.x + 8.0f * scale, row.y + 5.0f * scale, 16.0f * scale, 16.0f * scale};
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }
        app.font.Draw(r, row.x + 30.0f * scale, row.y + 5.0f * scale, item.label,
                      active ? t.text : t.textDim, scale);

        if (item.volume)
        {
            // Barra di occupazione e spazio libero: due informazioni in un posto
            // solo, come le mostra Esplora risorse. La barra si colora quando il
            // disco e' quasi pieno, perche' e' quello il momento in cui la si guarda.
            const Rect track{row.x + 30.0f * scale, row.y + 26.0f * scale,
                             row.w - 42.0f * scale, 4.0f * scale};
            r.FillRect(track, Color::FromHex(0x2A2D34), 2.0f * scale);
            const Color fill = item.usedFraction > 0.9f ? Color::FromHex(0xE06C5A)
                                                       : t.accent;
            r.FillRect(Rect{track.x, track.y, track.w * (std::min)(1.0f, item.usedFraction),
                            track.h},
                       fill, 2.0f * scale);
            app.smallFont.Draw(r, row.x + 30.0f * scale, row.y + 31.0f * scale,
                               item.freeText + Tr(" liberi", " free"), t.textDim, scale);
        }

        ClickTarget target;
        target.rect = row;
        target.kind = ClickTarget::Kind::Place;
        target.index = static_cast<int>(i);
        target.path = item.path;
        app.clickTargets.push_back(target);

        y += rowHeight;
    }
}

} // namespace Bolt
