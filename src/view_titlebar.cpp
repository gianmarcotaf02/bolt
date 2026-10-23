#include "view_titlebar.h"

#include "app_ui.h"
#include "home.h"
#include "tabs.h"


namespace Bolt
{
namespace
{
// La fila delle schede di **un** pannello, dentro |strip| e fra |left| e |right|.
// Estratta perche' i due posti che la disegnano — la barra del titolo quando il
// pannello e' uno, e la striscia sopra ogni pannello quando sono due — devono
// mostrare la stessa cosa: due copie di questo disegno divergono al primo ritocco, e
// la seconda a divergere sarebbe quella che si vede di rado.
void DrawTabRow(AppState& app, const Layout& layout, const Rect& strip, int pane,
                float left, float right)
{
    if (pane < 0 || pane >= static_cast<int>(app.panes.size()))
        return;

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    // Cosa si sta guardando: il pannello **e** la sua scheda accesa. Un pannello che
    // non ha il fuoco non ne accende nessuna — e' cosi' che si vede dov'e' il fuoco,
    // adesso che la testata col percorso non c'e' piu'.
    const bool paneActive = (pane == app.activePane);
    const Pane& data = app.panes[static_cast<size_t>(pane)];

    float x = left;
    // La fila si ricava dall'albero: un figlio si disegna subito dopo il genitore,
    // rientrato. L'ordine di nascita non conta piu'.
    for (const std::pair<int, int>& node : TabOrder(app, pane))
    {
        const int i     = node.first;
        const int depth = node.second;
        const Tab& tab = data.tabs[static_cast<size_t>(i)];
        // Il fuoco: il pannello **e** la sua scheda accesa. Un pannello che non lo ha
        // non ne accende nessuna, ed e' cosi' che si vede dov'e' il fuoco adesso che la
        // testata col percorso non c'e' piu'.
        const bool active  = paneActive && (i == data.activeTab);
        const bool hovered = (i == app.hoveredTab && pane == app.hoveredTabPane);
        // La scheda accesa ha lo snapshot **vuoto** finche' e' in vita: le sue voci sono
        // state spostate fuori, e solo i campi vivi sono aggiornati, quindi il nome deve
        // venire da li'. Vale per ogni pannello, non solo per quello col fuoco: chi
        // disegna un pannello inattivo ne ha appena scambiato lo stato in vita
        // (`SwapLiveWithPane`), quindi `app.directory` e' **la sua**.
        const std::wstring& source = (i == data.activeTab) ? app.directory : tab.directory;
        const bool named = ShellLocation::IsNamespace(source) || IsHome(source);
        // Il tab mostra il **nome della cartella**, non tutto il percorso: un
        // percorso lungo non ci sta e diventa illeggibile proprio nella parte che
        // identifica. Il pezzo che viene prima si mostra in piccolo e spento, e solo
        // se avanza spazio.
        // Anche per una cartella normale il nome e' quello che Windows mostra:
        // "Documenti" per `...\Documents`, come nella barra laterale e nella Home.
        std::wstring leafW = named ? LocationLabel(app, source) : FriendlyName(source);
        // `Toggle Subfolders`: la scheda dice che la lista contiene **piu'** di quello
        // che la cartella contiene, con lo stesso segno del riferimento (`sandbox
        // [...]`). Solo per la scheda attiva: le altre sono ferme com'erano quando le
        // si e' lasciate, e il segno lo porta il loro stato salvato quando ci si torna.
        if (i == data.activeTab && app.showSubfolders && !named)
            leafW += L" [...]";
        std::wstring parentW;
        if (!named)
        {
            const size_t slash = source.find_last_of(L'\\');
            if (slash != std::wstring::npos && slash + 1 < source.size())
                parentW = source.substr(0, slash + 1);
        }
        const std::string leaf   = ToUtf8(leafW);
        const std::string parent = ToUtf8(parentW);

        // La larghezza si misura sul **nome**, piu' un margine per il pezzo di
        // percorso che precede (fino a un tetto): un nome corto in una cartella
        // profonda resta un tab stretto, e il percorso non lo allunga oltre il tetto.
        const float parentExtra = (std::min)(app.smallFont.Measure(parent, scale), 110.0f * scale);
        const float width = (std::min)(200.0f * scale,
                                       (std::max)(110.0f * scale,
                                                  app.font.Measure(leaf, scale) + 66.0f * scale +
                                                      parentExtra));
        if (x + width > right)
            break;   // no overflow menu yet: extra tabs are simply not drawn

        const Rect tabRect{x, strip.y + 5.0f * scale, width, strip.h - 5.0f * scale};

        if (active)
        {
            r.FillRect(tabRect, t.panelBg, 6.0f * scale);
            r.Draw(UIDraw::Border(tabRect, t.panelBorder, 1.0f, 6.0f * scale));
        }
        else if (hovered)
        {
            r.FillRect(tabRect, t.rowHover, 6.0f * scale);
        }

        // Il rientro di un figlio, con la riga che lo lega al genitore: senza, un
        // tab annidato e' indistinguibile da uno spostato.
        const float indent = static_cast<float>(depth) * 12.0f * scale;
        if (depth > 0)
        {
            r.FillRect(Rect{tabRect.x + indent - 8.0f * scale, tabRect.y + 6.0f * scale,
                            1.0f * scale, tabRect.h - 12.0f * scale},
                       t.separator);
        }

        // L'icona e' quella dell'oggetto che il tab mostra: un disco per `C:`, un
        // cestino per il cestino, una cartella per una cartella. Non si tinge: e' la
        // sua, come nel riquadro di navigazione.
        // La Home ha la **casa** di Esplora risorse; le altre posizioni l'icona
        // dell'oggetto (un disco, un cestino); una cartella quella generica.
        const ShellIconAtlas::IconUV* tabIcon =
            IsHome(source) ? app.icons.GetForObject(kHomeIcon) : app.icons.GetForObject(source);
        if (tabIcon == nullptr)
            tabIcon = app.icons.GetFor("", true);
        if (tabIcon != nullptr)
        {
            const Rect icon{tabRect.x + 10.0f * scale + indent, tabRect.y + 6.0f * scale,
                            16.0f * scale, 16.0f * scale};
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                                 tabIcon->u0, tabIcon->v0, tabIcon->u1, tabIcon->v1));
        }

        // Il nome in evidenza; il percorso che precede, in piccolo e spento, solo
        // se c'e' spazio per leggerlo. Se non ce n'e', sparisce invece di stringere
        // il nome.
        const float contentX = tabRect.x + 32.0f * scale + indent;
        const float contentW = tabRect.w - 56.0f * scale - indent;
        float leafX = contentX;
        if (!parent.empty())
        {
            const float leafWidth = app.font.Measure(leaf, scale);
            const float room = contentW - leafWidth - 12.0f * scale;
            if (room > 40.0f * scale)
            {
                const std::string shownParent = FitTextTail(app.smallFont, parent, scale, room);
                app.smallFont.Draw(r, contentX, TextY(layout, tabRect, app.smallFont),
                                   shownParent,
                                   active ? t.textDim : t.textDim.WithAlpha(0.6f), scale);
                leafX = contentX + app.smallFont.Measure(shownParent, scale) + 8.0f * scale;
            }
        }
        app.font.Draw(r, leafX, TextY(layout, tabRect, app.font),
                      FitText(app.font, leaf, scale,
                              tabRect.right() - 28.0f * scale - leafX),
                      active ? t.text : t.textDim, scale);

        const Rect close{tabRect.right() - 24.0f * scale, tabRect.y + 5.0f * scale,
                         18.0f * scale, tabRect.h - 10.0f * scale};
        // La croce e' disegnata, non il carattere `x`: come la linguetta, un
        // carattere cambia col carattere scelto e non si allinea con niente.
        const float cross = 9.0f * scale;
        r.Draw(UIDraw::Cross(
            Rect{close.x + (close.w - cross) * 0.5f, close.y + (close.h - cross) * 0.5f, cross, cross},
            hovered ? t.text : t.textDim, 1.4f * scale));

        ClickTarget tabTarget;
        tabTarget.rect  = tabRect;
        tabTarget.kind  = ClickTarget::Kind::Tab;
        tabTarget.index = i;
        // La fila di cui questa scheda fa parte: con due pannelli l'indice da solo
        // direbbe la scheda 2 di tutti e due.
        tabTarget.pane  = pane;
        app.clickTargets.push_back(tabTarget);

        // Il marcatore del trascinamento: dove finirebbe il tab che si sta
        // spostando. La riga dice "prima o dopo", il tab intero dice "dentro" — e
        // non promette niente che il rilascio rifiuterebbe: dentro se stesso o
        // dentro un proprio discendente non si va, o l'albero prende un anello.
        // Solo nella fila da cui il gesto e' partito: un tab non si trascina ancora
        // nell'altro pannello, e un marcatore li' prometterebbe quello che non c'e'.
        if (paneActive && app.dragTab >= 0 && app.tabDropTarget == i)
        {
            const bool refused = TabIsDescendantOf(app, tab.id, app.dragTab);
            if (app.tabDropInto)
            {
                if (!refused)
                    r.Draw(UIDraw::Fill(tabRect, t.accent.WithAlpha(0.18f), 6.0f * scale));
            }
            else
            {
                const float lineX = app.tabDropAfter ? tabRect.right() : tabRect.x;
                r.FillRect(Rect{lineX - 1.0f * scale, tabRect.y, 2.0f * scale, tabRect.h},
                           t.accent, 1.0f * scale);
            }
        }

        ClickTarget closeTarget;
        closeTarget.rect  = close;
        closeTarget.kind  = ClickTarget::Kind::TabClose;
        closeTarget.index = i;
        closeTarget.pane  = pane;
        app.clickTargets.push_back(closeTarget);

        x += width + 3.0f * scale;
    }

    const Rect add{x, strip.y + 5.0f * scale, 30.0f * scale, strip.h - 5.0f * scale};
    if (add.right() <= right)
    {
        // Il `+` si accende quello sotto il puntatore, non tutti e due: con due pannelli
        // `hoveredChrome` da solo direbbe "il pulsante delle schede nuove" due volte.
        const bool addHot = app.hoveredChrome == 1 && add.Contains(app.pointerX, app.pointerY);
        r.FillRect(add, addHot ? t.rowHover : t.headerBg, 5.0f * scale);
        const float plus = 11.0f * scale;
        r.Draw(UIDraw::Plus(
            Rect{add.x + (add.w - plus) * 0.5f, add.y + (add.h - plus) * 0.5f, plus, plus},
            t.text, 1.4f * scale));

        ClickTarget target;
        target.rect = add;
        target.kind = ClickTarget::Kind::TabNew;
        target.pane = pane;
        app.clickTargets.push_back(target);
    }
}
} // namespace

void DrawTitleBar(AppState& app, const Layout& layout)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect bar = layout.titleBar;

    r.FillRect(bar, t.headerBg);
    r.FillRect(Rect{bar.x, bar.bottom() - 1.0f, bar.w, 1.0f}, t.separator);

    const float buttonWidth = 44.0f * scale;
    const float stripLeft   = 80.0f * scale;
    const float stripRight  = bar.right() - buttonWidth * 3.0f;

    // Il pulsante ☰ apre il **menu dell'applicazione** (`BuildAppMenu`), come nel
    // riferimento. La barra laterale ha il suo, accanto: erano la stessa cosa finche' il
    // menu non c'era, e adesso che c'e' non hanno piu' niente in comune.
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

    // Il riquadro della barra laterale: due rettangoli accostati, quello di sinistra
    // pieno quando la barra si vede. E' una forma e non un carattere (AGENTS.md §8).
    const Rect panel{bar.x + 42.0f * scale, bar.y + 7.0f * scale, 30.0f * scale,
                     bar.h - 14.0f * scale};
    r.FillRect(panel, app.hoveredChrome == 5 ? t.rowHover : t.panelBg, 5.0f * scale);
    {
        const float box = 12.0f * scale;
        const Rect frame{panel.x + (panel.w - box) * 0.5f, panel.y + (panel.h - box) * 0.5f, box,
                         box};
        r.Draw(UIDraw::Border(frame, t.textDim, 1.0f, 2.0f * scale));
        if (app.sidebarVisible)
            r.FillRect(Rect{frame.x + 1.0f, frame.y + 1.0f, frame.w * 0.38f, frame.h - 2.0f},
                       t.textDim);
    }
    {
        ClickTarget target;
        target.rect = panel;
        target.kind = ClickTarget::Kind::SidebarToggle;
        app.clickTargets.push_back(target);
    }

    // La fila delle schede sta qui **finche' il pannello e' uno**: quando sono due,
    // ognuno ha la sua sopra il suo riquadro (`DrawPaneTabs`), e questa striscia
    // resta al menu e ai pulsanti di finestra. Non e' un gusto: spostare una fila che
    // sta bene dov'e' per fare posto a una seconda che non c'e' cambierebbe
    // l'interfaccia a chi non ha diviso niente.
    if (app.panes.size() < 2)
        DrawTabRow(app, layout, bar, app.activePane, stripLeft, stripRight);

    const bool maximized = (app.window != nullptr) && IsZoomed(app.window) != FALSE;
    for (int i = 0; i < 3; ++i)
    {
        const Rect button{stripRight + i * buttonWidth, bar.y, buttonWidth, bar.h};
        const bool hot = (app.hoveredChrome == 2 + i);
        if (hot)
            r.FillRect(button, i == 2 ? t.closeHover : t.rowHover);

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
            // Anche la croce di chiusura e' una forma, come le altre due del gruppo.
            const float cross = 10.0f * scale;
            r.Draw(UIDraw::Cross(Rect{centreX - cross * 0.5f, centreY - cross * 0.5f, cross, cross},
                                 glyph, 1.4f * scale));
        }

        ClickTarget target;
        target.rect = button;
        target.kind = (i == 0) ? ClickTarget::Kind::WindowMinimize
                    : (i == 1) ? ClickTarget::Kind::WindowMaximize
                               : ClickTarget::Kind::WindowClose;
        app.clickTargets.push_back(target);
    }
}

// La fila delle schede di un pannello, sopra il suo riquadro. Prende il posto della
// testata che c'era prima sui pannelli inattivi (`DrawInactivePaneChrome`, con il
// percorso e il numero di schede): le schede dicono gia' tutto quello che diceva, e
// meglio — **quale** cartella e' (il nome sulla scheda), quante ce ne sono (quante se
// ne vedono) e quale pannello ha il fuoco (la scheda accesa). Aggiungere una testata
// sopra a una fila che dice le stesse cose vorrebbe dire togliere righe alla lista per
// ripetere.
void DrawPaneTabs(AppState& app, const Layout& layout, int pane)
{
    if (pane < 0 || pane >= static_cast<int>(app.panes.size()))
        return;

    const Rect strip = layout.tabs;
    if (strip.h <= 0.0f)
        return;   // un pannello solo: la fila e' quella della barra del titolo

    const float scale = layout.scale;
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;

    r.FillRect(strip, t.headerBg);
    r.FillRect(Rect{strip.x, strip.bottom() - 1.0f, strip.w, 1.0f}, t.separator);

    // Il bordo attorno a tutto il riquadro, e **solo** per il pannello che non si sta
    // guardando: insieme alle schede spente e' quello che dice dov'e' il fuoco. Il
    // pannello attivo non ha bordo, come non l'aveva prima dei pannelli.
    if (pane != app.activePane)
    {
        r.BorderRect(Rect{strip.x - 1.0f, strip.y - 1.0f, strip.w + 2.0f,
                          layout.filterBar.bottom() - strip.y + 2.0f},
                     t.panelBorder, 1.0f, 0.0f);
    }

    DrawTabRow(app, layout, strip, pane, strip.x + 6.0f * scale, strip.right() - 6.0f * scale);
}

} // namespace Bolt
