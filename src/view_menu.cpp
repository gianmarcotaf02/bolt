// ---------------------------------------------------------------------------
// I menu: il componente a gruppi (palette, GoTo, opzioni, valori di una colonna) e
// il menu contestuale.
//
// Era in `main.cpp`, dove il componente stava in mezzo ai comandi che esegue. Qui
// c'e' il **widget**: lo stato, la geometria, il disegno e la tastiera. Non c'e'
// cosa significa una voce scelta — un comando, un percorso, un'opzione — perche'
// quello lo sa chi ha costruito il menu: `GroupMenuClick` restituisce un'azione, e
// chi l'ha chiamata la esegue.
//
// La separazione e' quella della lista: il disegno sa dove sono le righe, l'input
// sa cosa fanno. E il componente era gia' scritto cosi' — la funzione che riceve il
// clic restituisce `MenuAction` e non esegue niente — quindi si e' potuto spostare
// senza toccarne la logica.
//
// Restano in `main.cpp`, e non sono il widget: cosa va **dentro** un menu (la
// tavolozza dei comandi, le opzioni, i livelli del menu contestuale coi verbi della
// shell) e cosa fa una voce scelta (`ContextMenuActivate`, `RunCommand`).
// ---------------------------------------------------------------------------
#include "view_menu.h"

#include "app_ui.h"
#include "language.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Bolt
{
namespace
{
// Fa combaciare un termine di ricerca con l'etichetta di una voce: tutti i termini
// devono comparire, in qualunque ordine. Lo usa solo il filtro del menu.
bool MenuMatch(const std::string& text, const std::wstring& filter)
{
    if (filter.empty())
        return true;

    std::string lower = text;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    size_t start = 0;
    for (;;)
    {
        size_t space = filter.find(L' ', start);
        if (space == std::wstring::npos)
            space = filter.size();
        if (space > start)
        {
            std::string term = ToUtf8(filter.substr(start, space - start));
            for (char& c : term)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find(term) == std::string::npos)
                return false;
        }
        if (space >= filter.size())
            break;
        start = space + 1;
    }
    return true;
}

// Il nome di un gruppo come si **legge**. La chiave resta quella del riferimento —
// `View`, `Sort`, `GroupBy` — ed e' con la chiave che si ricorda se un gruppo e'
// chiuso: tradurre la chiave invece del nome vorrebbe dire riaprire, cambiando lingua,
// quello che l'utente aveva chiuso. Quello che si legge si traduce, e la regola e' la
// stessa delle sezioni della barra laterale e delle etichette dei comandi.
//
// Sta qui e non in `main.cpp` perche' il gruppo lo disegna **questo** file, e perche'
// i gruppi non sono solo quelli della palette: `Path`, `Places`, `QuickAccess`,
// `Bookmarks` e `Recents` sono le bande del GoTo, e la mappa dei tasti usa le stesse della palette.
//
// Le bande della **lista** (`Oggi`, `Gigantesco`) hanno la loro tabella, in
// `view_list.cpp`: sono parole diverse, e le scrive chi disegna quelle.
const char* MenuGroupLabelIt(const std::string& key)
{
    if (key == "View")        return "Vista";
    if (key == "Language")    return "Lingua";
    if (key == "Appearance")  return "Aspetto";
    if (key == "Tabs")        return "Schede";
    if (key == "Folder")      return "Cartella";
    if (key == "GoTo")        return "Vai a";
    if (key == "Interaction") return "Interazione";
    if (key == "Inspector")   return "Ispettore";
    if (key == "Sort")        return "Ordina";
    if (key == "GroupBy")     return "Raggruppa";
    if (key == "Text")        return "Testo";
    if (key == "Scroll")      return "Scorrimento";
    if (key == "Bookmarks")   return "Segnalibri";
    if (key == "QuickAccess") return "Accesso rapido";
    if (key == "Recents")     return "Recenti";
    if (key == "Options")     return "Opzioni";
    if (key == "Places")      return "Posti";
    if (key == "Path")        return "Percorso";
    // `File` resta `File`: non c'e' niente da tradurre, e una voce in piu' qui
    // sarebbe la stessa parola scritta due volte.
    return nullptr;
}

std::string MenuGroupLabel(const std::string& key)
{
    const char* italian = MenuGroupLabelIt(key);
    if (gLanguage != Language::English && italian != nullptr)
        return italian;
    return key;
}

} // namespace

// --- the group menu component -----------------------------------------------
void GroupMenuOpen(GroupMenu& menu, std::string title, std::string placeholder,
                   std::vector<GroupMenuItem> items)
{
    menu.title       = std::move(title);
    menu.placeholder = std::move(placeholder);
    menu.items       = std::move(items);
    menu.filter.clear();
    menu.selected = 0;
    menu.dirty    = true;
    menu.open     = true;
    menu.openedAt = NowSeconds();   // da qui parte l'animazione di apertura
    GroupMenuRebuild(menu);   // so the selection is valid before the first frame
}

// Una riga su cui il cursore puo' posarsi: una voce, e accesa. Le righe di gruppo
// non sono voci, e una voce spenta non fa niente — quindi non si sceglie.
namespace
{
bool GroupMenuRowUsable(const GroupMenu& menu, int row)
{
    if (row < 0 || row >= static_cast<int>(menu.rows.size()))
        return false;
    const GroupMenuRow& candidate = menu.rows[static_cast<size_t>(row)];
    if (candidate.header)
        return false;
    return menu.items[static_cast<size_t>(candidate.item)].enabled;
}
} // namespace

bool GroupMenuItemEnabled(const GroupMenu& menu, int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(menu.items.size()))
        return false;
    return menu.items[static_cast<size_t>(itemIndex)].enabled;
}

bool GroupMenuSelectedEnabled(const GroupMenu& menu)
{
    if (menu.selected < 0 || menu.selected >= static_cast<int>(menu.rows.size()))
        return false;
    const GroupMenuRow& row = menu.rows[static_cast<size_t>(menu.selected)];
    if (row.header)
        return false;
    return GroupMenuItemEnabled(menu, row.item);
}

void GroupMenuRebuild(GroupMenu& menu)
{
    menu.rows.clear();
    menu.groups.clear();
    menu.matchCount = 0;
    menu.dirty = false;

    // Items arrive already ordered by group, so a group is a run of consecutive
    // items and one pass is enough.
    std::string current;
    int groupIndex = -1;

    for (int i = 0; i < static_cast<int>(menu.items.size()); ++i)
    {
        const GroupMenuItem& item = menu.items[static_cast<size_t>(i)];
        if (!MenuMatch(item.label, menu.filter))
            continue;

        ++menu.matchCount;

        if (item.group != current)
        {
            current = item.group;
            menu.groups.push_back(current);
            groupIndex = static_cast<int>(menu.groups.size()) - 1;
            menu.rows.push_back(GroupMenuRow{true, groupIndex, -1});
        }

        if (menu.collapsed.count(current) != 0)
            continue;
        menu.rows.push_back(GroupMenuRow{false, groupIndex, i});
    }

    // Keep the cursor on a real entry: una riga di gruppo non e' una voce, e una voce
    // spenta non si sceglie. Il cursore si posa sulla prima che si puo' davvero
    // scegliere, avanti e poi indietro — su un menu di due righe puo' capitare che
    // l'unica accesa sia l'ultima.
    if (menu.selected < 0 || menu.selected >= static_cast<int>(menu.rows.size()) ||
        menu.rows[static_cast<size_t>(menu.selected)].header)
        menu.selected = 0;

    if (!GroupMenuSelectedEnabled(menu))
    {
        int forward = menu.selected;
        while (forward < static_cast<int>(menu.rows.size()) &&
               !GroupMenuRowUsable(menu, forward))
            ++forward;

        if (forward < static_cast<int>(menu.rows.size()))
        {
            menu.selected = forward;
        }
        else
        {
            int backward = (std::min)(menu.selected, static_cast<int>(menu.rows.size()) - 1);
            while (backward >= 0 && !GroupMenuRowUsable(menu, backward))
                --backward;
            if (backward >= 0)
                menu.selected = backward;
        }
    }
}

void GroupMenuMove(GroupMenu& menu, int delta)
{
    const int count = static_cast<int>(menu.rows.size());
    if (count == 0 || delta == 0)
        return;

    int target = menu.selected + delta;
    if (target < 0) target = 0;
    if (target >= count) target = count - 1;

    const int step = (delta > 0) ? 1 : -1;
    while (target >= 0 && target < count && !GroupMenuRowUsable(menu, target))
        target += step;

    if (target >= 0 && target < count)
        menu.selected = target;
}

// First or last entry row, skipping the group band that may sit on the edge.
void GroupMenuSelectEdge(GroupMenu& menu, bool last)
{
    const int count = static_cast<int>(menu.rows.size());
    if (count == 0)
        return;

    int index = last ? count - 1 : 0;
    const int step = last ? -1 : 1;
    while (index >= 0 && index < count && !GroupMenuRowUsable(menu, index))
        index += step;

    if (index >= 0 && index < count)
        menu.selected = index;
}

int GroupMenuSelectedId(const GroupMenu& menu)
{
    const int index = menu.selected;
    if (index < 0 || index >= static_cast<int>(menu.rows.size()))
        return -1;
    const GroupMenuRow& row = menu.rows[static_cast<size_t>(index)];
    if (row.header)
        return -1;
    return menu.items[static_cast<size_t>(row.item)].id;
}

// Keys the component owns. The caller checks its own toggle key before calling.
MenuAction GroupMenuKeyDown(GroupMenu& menu, WPARAM key)
{
    switch (key)
    {
        case VK_ESCAPE: return MenuAction::Close;
        case VK_RETURN: return GroupMenuSelectedEnabled(menu) ? MenuAction::Activate
                                                              : MenuAction::None;
        case VK_UP:     GroupMenuMove(menu, -1); break;
        case VK_DOWN:   GroupMenuMove(menu, 1);  break;
        case VK_NEXT:   GroupMenuMove(menu, 10); break;
        case VK_PRIOR:  GroupMenuMove(menu, -10); break;
        case VK_HOME:   GroupMenuSelectEdge(menu, false); break;
        case VK_END:    GroupMenuSelectEdge(menu, true);  break;
        case VK_BACK:
            if (!menu.filter.empty())
            {
                menu.filter.pop_back();
                menu.dirty = true;
            }
            break;
        default:
            break;
    }
    return MenuAction::None;
}

void GroupMenuChar(GroupMenu& menu, wchar_t character)
{
    if (character >= 0x20 && character != 0x7F)
    {
        menu.filter.push_back(character);
        menu.dirty = true;
    }
}

GroupMenu* OpenMenu(AppState& app)
{
    // Il piu' profondo per primo: un controllo `Choice` aperto sta sopra le
    // opzioni, e finche' c'e' e' lui a prendere tastiera e clic.
    if (app.optionsChoice.open) return &app.optionsChoice;
    // Le destinazioni del fissaggio stanno sopra tutto: si aprono da una voce del menu
    // contestuale o dalla palette, che si chiudono nell'aprirsi, quindi sotto non c'e'
    // piu' niente — l'ordine serve a dire chi vince se mai ci finissero.
    if (app.pinMenu.open)       return &app.pinMenu;
    // L'editor dei tasti sta **sopra** le opzioni: si apre da una loro riga, e
    // mentre e' aperto e' lui la cosa su cui si sta agendo. Aprendosi le chiude,
    // quindi le due insieme non si vedono mai; l'ordine serve a dire quale vince
    // se mai ci finissero.
    if (app.keyMap.open)        return &app.keyMap;
    if (app.options.open)       return &app.options;
    if (app.palette.open)       return &app.palette;
    if (app.goTo.open)          return &app.goTo;
    if (app.columnMenu.open)    return &app.columnMenu;
    return nullptr;
}
// Se seguisse la preferenza che sta comandando, lo slider si tirerebbe dietro le
// proprie righe e il bersaglio si sposterebbe mentre lo si trascina.
constexpr float kOptionRowHeight = 46.0f;

float MenuRowHeight(const Layout& layout, const GroupMenu& menu)
{
    return menu.richRows ? kOptionRowHeight * layout.scale : layout.rowHeight;
}

// La prima riga visibile: la stessa formula per il disegno e per il clic, o le
// due non sarebbero d'accordo su cosa sta sotto il puntatore.
int GroupMenuFirstVisible(const GroupMenu& menu, float listTop, float listBottom, float rowHeight)
{
    const int visible = (std::max)(1, static_cast<int>((listBottom - listTop) / rowHeight));
    int first = 0;
    if (menu.selected >= first + visible)
        first = menu.selected - visible + 1;
    if (menu.selected < first)
        first = menu.selected;
    return first;
}
// Quanto e' largo il controllo di un tipo. La larghezza non dipende dalla riga:
// e' la stessa per il disegno, per il clic e per il trascinamento di uno slider.
float OptionControlWidth(const GroupMenuItem& item, float scale)
{
    switch (item.control)
    {
        case OptionControl::Text:    return 200.0f * scale;
        case OptionControl::Choice:  return 170.0f * scale;
        // Un pulsante che apre un'altra cosa: la larghezza e' quella che serve a
        // scriverci dentro una parola come "Modifica...".
        case OptionControl::Action:  return 130.0f * scale;
        case OptionControl::Stepper: return 128.0f * scale;
        case OptionControl::Slider:  return 150.0f * scale;
        case OptionControl::Toggle:  return 40.0f * scale;
        default:                     return 0.0f;
    }
}

// Dove sta il controllo di una riga. Una funzione sola, perche' disegno e clic
// devono scegliere lo stesso rettangolo.
Rect OptionControlRect(const Layout& layout, const GroupMenuItem& item, const Rect& rowRect)
{
    const float scale = layout.scale;
    const float height = 22.0f * scale;
    const float width  = OptionControlWidth(item, scale);
    return Rect{rowRect.right() - width - 14.0f * scale,
                rowRect.y + (rowRect.h - height) * 0.5f, width, height};
}
Rect GroupMenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu)
{
    const float scale = layout.scale;
    // Il pannello delle opzioni ha bisogno di piu' larghezza: una descrizione
    // accanto a un controllo, dentro 560 px, non ci sta.
    const float maxWidth = menu.richRows ? 760.0f : 560.0f;
    const float width  = (std::min)(maxWidth * scale, static_cast<float>(app.viewportWidth) - 60.0f * scale);
    const float height = (std::min)((menu.richRows ? 560.0f : 420.0f) * scale,
                                    static_cast<float>(app.viewportHeight) - 90.0f * scale);
    return Rect{(static_cast<float>(app.viewportWidth) - width) * 0.5f, 70.0f * scale, width, height};
}

// Il pannello dei valori di una colonna: piu' stretto degli altri, appoggiato sotto
// l'intestazione e alto quanto basta ai suoi valori, invece di 420 px di vuoto.
Rect ColumnMenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu)
{
    const float scale = layout.scale;
    const float width = 260.0f * scale;
    const float wanted = 84.0f * scale + static_cast<float>(menu.rows.size()) * layout.rowHeight;
    const float room = static_cast<float>(app.viewportHeight) - menu.anchor.bottom() - 20.0f * scale;
    const float height = (std::min)((std::min)(520.0f * scale, wanted), (std::max)(160.0f * scale, room));
    float x = menu.anchor.x;
    if (x + width > static_cast<float>(app.viewportWidth) - 10.0f * scale)
        x = static_cast<float>(app.viewportWidth) - width - 10.0f * scale;
    return Rect{x, menu.anchor.bottom() + 2.0f * scale, width, height};
}

Rect MenuPanel(const AppState& app, const Layout& layout, const GroupMenu& menu)
{
    return menu.anchored ? ColumnMenuPanel(app, layout, menu) : GroupMenuPanel(app, layout, menu);
}
// Il disegno del controllo dentro una riga di opzione. Il tipo decide la forma,
// e una opzione spenta si vede spenta invece di sparire: e' la lezione del
// riferimento, dove `System Light / Dark Scheme` sta li' ma non si tocca.
void DrawOptionControl(AppState& app, const Layout& layout, const GroupMenuItem& item,
                       const Rect& control)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Color ink  = item.enabled ? t.text : t.textDim.WithAlpha(0.55f);
    const Color line = item.enabled ? t.panelBorder : t.separator;

    switch (item.control)
    {
        case OptionControl::Toggle:
        {
            const Color track = item.enabled ? (item.toggleOn ? t.accent : t.panelBorder) : t.separator;
            r.Draw(UIDraw::Fill(control, track, control.h * 0.5f));
            const float knob = control.h - 4.0f * scale;
            const float x = item.toggleOn ? control.right() - knob - 2.0f * scale
                                          : control.x + 2.0f * scale;
            r.Draw(UIDraw::Fill(Rect{x, control.y + 2.0f * scale, knob, knob},
                                item.enabled ? Color{0.98f, 0.99f, 1.0f, 1.0f}
                                             : t.textDim.WithAlpha(0.55f), knob * 0.5f));
            break;
        }
        case OptionControl::Stepper:
        {
            r.Draw(UIDraw::Fill(control, t.windowBg, 4.0f * scale));
            r.Draw(UIDraw::Border(control, line, 1.0f, 4.0f * scale));
            const float box = control.w / 3.0f;
            const float textY = TextY(layout, control, app.font);
            app.font.Draw(r, control.x + box * 0.42f, textY, "-", ink, scale);
            app.font.Draw(r, control.right() - box * 0.58f, textY, "+", ink, scale);
            const float w = app.font.Measure(item.valueText, scale);
            app.font.Draw(r, control.x + (control.w - w) * 0.5f, textY, item.valueText, ink, scale);
            break;
        }
        case OptionControl::Slider:
        {
            const float knob   = control.h * 0.5f;
            const float trackH = 4.0f * scale;
            const Rect  track{control.x + knob, control.y + (control.h - trackH) * 0.5f,
                              (std::max)(1.0f, control.w - knob * 2.0f), trackH};
            r.Draw(UIDraw::Fill(track, item.enabled ? t.panelBorder : t.separator, trackH * 0.5f));
            float fraction = item.fraction;
            fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
            const float filled = track.w * fraction;
            if (filled > 0.0f)
                r.Draw(UIDraw::Fill(Rect{track.x, track.y, filled, track.h},
                                    item.enabled ? t.accent : t.separator, trackH * 0.5f));
            r.Draw(UIDraw::Fill(Rect{track.x + filled - knob, control.y, knob * 2.0f, knob * 2.0f},
                                item.enabled ? Color{0.95f, 0.96f, 1.0f, 1.0f}
                                             : t.textDim.WithAlpha(0.55f), knob));
            break;
        }
        case OptionControl::Text:
        {
            const bool editing = app.optionEditActive && app.optionEditId == item.id;
            r.Draw(UIDraw::Fill(control, t.windowBg, 4.0f * scale));
            r.Draw(UIDraw::Border(control, editing ? t.accent : line, 1.0f, 4.0f * scale));
            const std::string shown = editing ? app.optionEditText : item.valueText;
            const float textY = TextY(layout, control, app.font);
            app.font.Draw(r, control.x + 8.0f * scale, textY, shown, ink, scale);
            if (editing)
            {
                const float caretX = control.x + 8.0f * scale + app.font.Measure(shown, scale) + 1.0f * scale;
                r.FillRect(Rect{caretX, control.y + 4.0f * scale, 1.0f, control.h - 8.0f * scale}, t.text);
            }
            break;
        }
        case OptionControl::Choice:
        {
            r.Draw(UIDraw::Fill(control, t.windowBg, 4.0f * scale));
            r.Draw(UIDraw::Border(control, line, 1.0f, 4.0f * scale));
            const float textY = TextY(layout, control, app.smallFont);
            app.smallFont.Draw(r, control.x + 10.0f * scale, textY, item.valueText, ink, scale);
            app.smallFont.Draw(r, control.right() - 16.0f * scale,
                               TextY(layout, control, app.smallFont), ">", ink, scale);
            break;
        }
        case OptionControl::Action:
        {
            // Un pulsante, non un valore: la stessa forma della scelta senza la
            // freccia, perche' non c'e' un elenco da aprire ma un'altra cosa. Il
            // testo sta al centro, che e' come si legge un pulsante.
            r.Draw(UIDraw::Fill(control, t.windowBg, 4.0f * scale));
            r.Draw(UIDraw::Border(control, line, 1.0f, 4.0f * scale));
            const float textY = TextY(layout, control, app.smallFont);
            const float width = app.smallFont.Measure(item.valueText, scale);
            app.smallFont.Draw(r, control.x + (control.w - width) * 0.5f, textY,
                               item.valueText, ink, scale);
            break;
        }
        default:
            break;
    }
}
void GroupMenuDraw(AppState& app, const Layout& layout, GroupMenu& menu)
{
    if (!menu.open)
        return;

    // `Animations`: il pannello compare in dissolvenza. Il fattore si applica a
    // **tutto** quello che il pannello disegna — velo, ombra, fondo, testo — perche'
    // e' uno stato del renderer e non un colore da correggere in cinquanta punti.
    const float appear = PanelAppear(menu.openedAt, app.animations);
    app.renderer->SetOpacity(appear);

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect panel = MenuPanel(app, layout, menu);

    r.FillRect(Rect{0.0f, 0.0f, static_cast<float>(app.viewportWidth),
                    static_cast<float>(app.viewportHeight)}, Color{0.0f, 0.0f, 0.0f, 0.45f});

    r.Draw(UIDraw::Shadow(Rect{panel.x + 3.0f * scale, panel.y + 6.0f * scale, panel.w, panel.h},
                          t.shadow, 22.0f * scale, 10.0f * scale));
    r.Draw(UIDraw::Fill(panel, t.panelBg, 10.0f * scale));
    r.Draw(UIDraw::Border(panel, t.panelBorder, 1.0f, 10.0f * scale));

    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.y + 12.0f * scale,
                  menu.title, t.textDim, scale);

    const std::string query = menu.filter.empty() ? menu.placeholder : ToUtf8(menu.filter);
    app.font.Draw(r, panel.x + 16.0f * scale, panel.y + 32.0f * scale, query,
                  menu.filter.empty() ? t.textDim : t.text, scale);
    if (!menu.filter.empty())
    {
        const float caretX = panel.x + 16.0f * scale + app.font.Measure(query, scale) + 1.0f * scale;
        r.FillRect(Rect{caretX, panel.y + 32.0f * scale, 1.0f, app.font.LineHeight() * scale}, t.text);
    }

    const float listTop    = panel.y + 58.0f * scale;
    const float listBottom = panel.bottom() - 26.0f * scale;
    const float rowHeight  = MenuRowHeight(layout, menu);
    r.FillRect(Rect{panel.x + 1.0f, listTop - 4.0f * scale, panel.w - 2.0f, 1.0f}, t.separator);

    const int firstVisible =
        GroupMenuFirstVisible(menu, listTop, listBottom, rowHeight);

    for (int i = firstVisible; i < static_cast<int>(menu.rows.size()); ++i)
    {
        const float y = listTop + (i - firstVisible) * rowHeight;
        if (y + rowHeight > listBottom)
            break;

        const GroupMenuRow& row = menu.rows[static_cast<size_t>(i)];
        const Rect rowRect{panel.x + 6.0f * scale, y, panel.w - 12.0f * scale, rowHeight};

        if (row.header)
        {
            const std::string& name = menu.groups[static_cast<size_t>(row.group)];
            // Chiuso o aperto lo dice la **chiave**, quello che si legge lo dice la
            // lingua: sono due cose diverse, ed e' per questo che sono due variabili.
            const bool collapsed = menu.collapsed.count(name) != 0;
            r.FillRect(rowRect, t.headerBg, 3.0f * scale);
            const float arrow = 10.0f * scale;
            r.Draw(DisclosureArrow(Rect{rowRect.x + 7.0f * scale,
                                        rowRect.y + (rowRect.h - arrow) * 0.5f, arrow, arrow},
                                   t.textDim, /*pointsDown=*/!collapsed));
            app.smallFont.Draw(r, rowRect.x + 22.0f * scale, TextY(layout, rowRect, app.smallFont),
                          MenuGroupLabel(name), t.text, scale);

            ClickTarget target;
            target.rect  = rowRect;
            target.kind  = ClickTarget::Kind::MenuHeader;
            target.label = name;
            app.clickTargets.push_back(target);
            continue;
        }

        const GroupMenuItem& item = menu.items[static_cast<size_t>(row.item)];
        if (i == menu.selected)
            r.FillRect(rowRect, t.rowSelected, 3.0f * scale);

        // La riga del pannello delle opzioni: nome, descrizione e controllo. Non
        // lascia un bersaglio da clic perche' la geometria la rifa' `OptionsClick`:
        // due calcoli della stessa riga sono due occasioni di non essere d'accordo.
        if (menu.richRows)
        {
            const Color nameInk = item.enabled ? t.text : t.textDim.WithAlpha(0.6f);
            const Color descInk = item.enabled ? t.textDim : t.textDim.WithAlpha(0.45f);
            app.font.Draw(r, rowRect.x + 14.0f * scale, rowRect.y + 7.0f * scale,
                          item.label, nameInk, scale);
            const Rect control = OptionControlRect(layout, item, rowRect);
            const float descMax = control.x - (rowRect.x + 14.0f * scale) - 16.0f * scale;
            if (descMax > 24.0f)
                app.smallFont.Draw(r, rowRect.x + 14.0f * scale, rowRect.y + 27.0f * scale,
                                   FitText(app.smallFont, item.description, scale, descMax),
                                   descInk, scale);
            DrawOptionControl(app, layout, item, control);
            continue;
        }

        // L'etichetta e il dettaglio si dividono la riga. Il dettaglio, quando c'e',
        // prende al massimo poco piu' di meta' riga e si taglia **davanti** — di un
        // percorso la parte che dice dove si va e' la coda; l'etichetta prende quello
        // che resta e si taglia in fondo. Con un carattere largo scelto dalle opzioni
        // i due si sovrapponevano, e due testi uno sopra l'altro non si leggono.
        std::string label = item.label;
        if (menu.detailed)
        {
            // Il dettaglio prende quello che avanza dopo l'etichetta, e si taglia
            // **davanti** — di un percorso la parte che dice dove si va e' la coda.
            // Se non avanza niente, si tiene una scheggia e si accorcia l'etichetta:
            // meglio due testi troncati che due testi sovrapposti.
            //
            // Quando il dettaglio manca, il ripiego e' il gruppo, ma **solo** dove il
            // menu lo chiede: vedi `GroupMenu::detailFallsBackToGroup`. Vuoto vuol dire
            // vuoto, ed e' un'informazione anche quella.
            const std::string& raw = (item.detail.empty() && menu.detailFallsBackToGroup)
                                         ? item.group
                                         : item.detail;
            const float labelNeed = app.font.Measure(label, scale);
            const float detailRoom = (std::max)(60.0f * scale,
                                                rowRect.w - labelNeed - 34.0f * scale);
            const std::string detail = FitTextTail(app.smallFont, raw, scale, detailRoom);
            const float width = app.smallFont.Measure(detail, scale);
            app.smallFont.Draw(r, rowRect.right() - width - 12.0f * scale,
                               TextY(layout, rowRect, app.smallFont), detail,
                               item.enabled ? t.textDim : t.textDim.WithAlpha(0.45f), scale);
            const float room = rowRect.right() - width - 20.0f * scale - (rowRect.x + 14.0f * scale);
            label = FitText(app.font, label, scale, (std::max)(0.0f, room));
        }
        else
        {
            label = FitText(app.font, label, scale, rowRect.w - 28.0f * scale);
        }

        // Una voce spenta si vede spenta: il nome si spegne come nel pannello delle
        // opzioni, che era l'unico posto a saperlo fare.
        app.font.Draw(r, rowRect.x + 14.0f * scale, TextY(layout, rowRect, app.font), label,
                      item.enabled ? t.text : t.textDim.WithAlpha(0.55f), scale);

        ClickTarget target;
        target.rect  = rowRect;
        target.kind  = ClickTarget::Kind::MenuRow;
        target.index = row.item;
        app.clickTargets.push_back(target);
    }

    // La barra di scorrimento, quando le righe non ci stanno tutte: con venti
    // opzioni il cursore va giu' ma il pollice e' l'unica cosa che dice quanto
    // lungo sia l'elenco.
    if (menu.richRows)
    {
        const float total = static_cast<float>(menu.rows.size()) * rowHeight;
        const float room  = listBottom - listTop;
        if (total > room)
        {
            const float barW = 4.0f * scale;
            const Rect  track{panel.right() - 10.0f * scale, listTop, barW, room};
            r.Draw(UIDraw::Fill(track, t.separator, barW * 0.5f));
            const float thumbH = (std::max)(24.0f * scale, room * room / total);
            const float maxOffset = (std::max)(1.0f, total - room);
            const float offset = (firstVisible * rowHeight) / maxOffset;
            r.Draw(UIDraw::Fill(Rect{track.x, track.y + (room - thumbH) * offset, barW, thumbH},
                                t.scrollThumb, barW * 0.5f));
        }
    }

    char footerText[96];
    std::snprintf(footerText, sizeof(footerText), Tr("%d di %zu", "%d of %zu"),
                  menu.matchCount, menu.items.size());
    const std::string footer = std::string(footerText) + "   " + menu.hint;
    app.smallFont.Draw(r, panel.x + 16.0f * scale, panel.bottom() - 20.0f * scale,
                  footer, t.textDim, scale);

    // L'animazione vale per questo pannello e per nient'altro: quello che si disegna
    // dopo (il menu contestuale, il pannello dell'operazione) torna pieno.
    app.renderer->SetOpacity(1.0f);
}
} // namespace Bolt
