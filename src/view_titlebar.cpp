#include "view_titlebar.h"

#include "app_ui.h"
#include "tabs.h"


namespace Bolt
{
void DrawTitleBar(AppState& app, const Layout& layout)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect bar = layout.titleBar;

    r.FillRect(bar, t.headerBg);
    r.FillRect(Rect{bar.x, bar.bottom() - 1.0f, bar.w, 1.0f}, t.separator);

    const float buttonWidth = 44.0f * scale;
    const float stripLeft   = 44.0f * scale;
    const float stripRight  = bar.right() - buttonWidth * 3.0f;

    // Menu button. The reference opens the application menu here; the menu
    // component arrives with the command palette, so for now it toggles the one
    // thing the shell already has.
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

    float x = stripLeft;
    // La fila si ricava dall'albero: un figlio si disegna subito dopo il genitore,
    // rientrato. L'ordine di nascita non conta piu'.
    for (const std::pair<int, int>& node : TabOrder(app))
    {
        const int i     = node.first;
        const int depth = node.second;
        const Tab& tab = app.tabs[static_cast<size_t>(i)];
        // The active tab's snapshot is empty while it is live: its entries were
        // moved out and only the live fields are current, so the title has to come
        // from there.
        const std::wstring& source = (i == app.activeTab) ? app.directory : tab.directory;
        // Il tab mostra il **nome della cartella**, non tutto il percorso: un
        // percorso lungo non ci sta e diventa illeggibile proprio nella parte che
        // identifica. Il pezzo che viene prima si mostra in piccolo e spento, e solo
        // se avanza spazio.
        const std::wstring leafW = ShellLocation::IsNamespace(source)
                                       ? LocationLabel(app, source)
                                       : LeafName(source);
        std::wstring parentW;
        if (!ShellLocation::IsNamespace(source))
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
        if (x + width > stripRight)
            break;   // no overflow menu yet: extra tabs are simply not drawn

        const Rect tabRect{x, bar.y + 5.0f * scale, width, bar.h - 5.0f * scale};
        const bool active = (i == app.activeTab);

        if (active)
        {
            r.FillRect(tabRect, t.panelBg, 6.0f * scale);
            r.Draw(UIDraw::Border(tabRect, t.panelBorder, 1.0f, 6.0f * scale));
        }
        else if (i == app.hoveredTab)
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
        const ShellIconAtlas::IconUV* tabIcon = app.icons.GetForObject(source);
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
            i == app.hoveredTab ? t.text : t.textDim, 1.4f * scale));

        ClickTarget tabTarget;
        tabTarget.rect  = tabRect;
        tabTarget.kind  = ClickTarget::Kind::Tab;
        tabTarget.index = i;
        app.clickTargets.push_back(tabTarget);

        // Il marcatore del trascinamento: dove finirebbe il tab che si sta
        // spostando. La riga dice "prima o dopo", il tab intero dice "dentro" — e
        // non promette niente che il rilascio rifiuterebbe: dentro se stesso o
        // dentro un proprio discendente non si va, o l'albero prende un anello.
        if (app.dragTab >= 0 && app.tabDropTarget == i)
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
        app.clickTargets.push_back(closeTarget);

        x += width + 3.0f * scale;
    }

    const Rect add{x, bar.y + 5.0f * scale, 30.0f * scale, bar.h - 5.0f * scale};
    if (add.right() <= stripRight)
    {
        r.FillRect(add, app.hoveredChrome == 1 ? t.rowHover : t.headerBg, 5.0f * scale);
        const float plus = 11.0f * scale;
        r.Draw(UIDraw::Plus(
            Rect{add.x + (add.w - plus) * 0.5f, add.y + (add.h - plus) * 0.5f, plus, plus},
            t.text, 1.4f * scale));

        ClickTarget target;
        target.rect = add;
        target.kind = ClickTarget::Kind::TabNew;
        app.clickTargets.push_back(target);
    }

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

} // namespace Bolt
