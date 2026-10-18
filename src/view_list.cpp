// ---------------------------------------------------------------------------
// La lista: le righe, le colonne, la griglia, i gruppi, il filtro.
//
// Era in `main.cpp`, dentro il namespace anonimo, dove nessun altro file poteva
// usarla: e' il pezzo di vista piu' grosso uscito da li'. Il conto che porta qui e'
// `RebuildView` — dalla cartella alle righe che si vedono — e il disegno e'
// `DrawList`, che le righe le riceve gia' fatte.
//
// La direzione delle dipendenze e' quella degli altri moduli: questo file include
// `app_state.h` e `app_ui.h` e non conosce ne' i tab ne' l'inspector. Quello che
// l'input deve sapere — dove cade una riga, che percorso ha una voce — lo dice
// questo file, invece di ricalcolarlo: due conti della stessa geometria divergono
// sempre, e il secondo sarebbe quello invisibile.
// ---------------------------------------------------------------------------
#include "view_list.h"

#include "app_ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Bolt
{
namespace
{
// --- filtro: i termini, e chi li fa combaciare ------------------------------
std::wstring ToLowerWide(const std::wstring& text)
{
    std::wstring out = text;
    for (wchar_t& c : out)
        c = static_cast<wchar_t>(towlower(c));
    return out;
}

// '*' matches any run, '?' one character. Iterative with backtracking on the
// last star, which is the classic linear-time formulation.
bool GlobMatch(const std::wstring& pattern, const std::wstring& text)
{
    size_t p = 0, t = 0;
    size_t star = std::wstring::npos;
    size_t resume = 0;

    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == L'?' || pattern[p] == text[t]))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == L'*')
        {
            star = p++;
            resume = t;
        }
        else if (star != std::wstring::npos)
        {
            p = star + 1;
            t = ++resume;
        }
        else
        {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == L'*')
        ++p;
    return p == pattern.size();
}

bool TermMatches(const Entry& entry, const std::wstring& term)
{
    // A term that starts with a dot, or contains a wildcard, asks about the
    // type rather than the whole name: ".png" and "*.png" mean the same thing.
    // This is the filter-by-type path of the reference key table
    // (FilterFileTypes / FilterTypes) expressed in the same box as free text.
    if (!term.empty() && term[0] == L'.' && term.find(L'*') == std::wstring::npos)
    {
        if (entry.isDirectory)
            return false;
        return ToLowerWide(Widen(entry.extension)) == term.substr(1);
    }

    const std::wstring name = ToLowerWide(entry.wideName);
    if (term.find(L'*') != std::wstring::npos || term.find(L'?') != std::wstring::npos)
        return GlobMatch(term, name);
    return name.find(term) != std::wstring::npos;
}

// Space separated terms, all of which must match. An empty filter matches all.
bool FilterMatch(const Entry& entry, const std::wstring& filter)
{
    if (filter.empty())
        return true;

    size_t start = 0;
    for (;;)
    {
        size_t space = filter.find(L' ', start);
        if (space == std::wstring::npos)
            space = filter.size();
        if (space > start && !TermMatches(entry, filter.substr(start, space - start)))
            return false;
        if (space >= filter.size())
            break;
        start = space + 1;
    }
    return true;
}

// Una colonna senza valori tolti non filtra niente, e le colonne si sommano: chi
// cerca "documenti di testo modificati oggi" toglie valori in due colonne diverse.
bool ColumnFiltersMatch(const AppState& app, const Entry& entry)
{
    for (int i = 0; i < kColumnCount; ++i)
    {
        if (app.columns[static_cast<size_t>(i)].excluded.empty())
            continue;
        const std::string value = ColumnValueOf(app, entry, i);
        if (app.columns[static_cast<size_t>(i)].excluded.count(value) != 0)
            return false;
    }
    return true;
}

bool EntryVisible(const AppState& app, const Entry& entry)
{
    return FilterMatch(entry, app.filter) && ColumnFiltersMatch(app, entry);
}

// --- gruppi: le bande in cui si raggruppa l'elenco -------------------------
uint64_t LocalDayIndex(const FILETIME& fileTime)
{
    FILETIME local{};
    if (FileTimeToLocalFileTime(&fileTime, &local) == FALSE)
        return 0;
    const uint64_t ticks = (static_cast<uint64_t>(local.dwHighDateTime) << 32) | local.dwLowDateTime;
    return ticks / (10000000ULL * 60ULL * 60ULL * 24ULL);
}

uint64_t CurrentDayIndex()
{
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return LocalDayIndex(now);
}

bool VolumeUsage(const std::wstring& path, int* percentFull)
{
    if (path.size() < 2 || path[1] != L':')
        return false;
    const wchar_t root[4] = {path[0], L':', L'\\', L'\0'};
    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(root, &available, &total, &freeBytes) == FALSE || total.QuadPart == 0)
        return false;
    *percentFull = static_cast<int>((total.QuadPart - freeBytes.QuadPart) * 100 / total.QuadPart);
    return true;
}

// The band names come from the reference key table (GroupByLabel_*). The
// cut-offs do not: docs/03 records them as still unknown, so the values here are
// a reconstruction and are deliberately easy to find and change.
void GroupKeyFor(const Entry& entry, GroupMode mode, uint64_t bytes, uint64_t items,
                 bool statsKnown, int percentFull, int* outOrder, std::string* outLabel)
{
    switch (mode)
    {
        case GroupMode::Name:
        {
            const wchar_t c = entry.wideName.empty()
                ? L'\0'
                : static_cast<wchar_t>(towupper(entry.wideName[0]));
            if (c >= L'0' && c <= L'9')      { *outOrder = 0; *outLabel = "09"; }
            else if (c >= L'A' && c <= L'H') { *outOrder = 1; *outLabel = "AH"; }
            else if (c >= L'I' && c <= L'P') { *outOrder = 2; *outLabel = "IP"; }
            else if (c >= L'Q' && c <= L'Z') { *outOrder = 3; *outLabel = "QZ"; }
            else                             { *outOrder = 4; *outLabel = "Other"; }
            break;
        }
        case GroupMode::Date:
        {
            const uint64_t now  = CurrentDayIndex();
            const uint64_t then = LocalDayIndex(entry.modified);
            const uint64_t age  = (now >= then) ? (now - then) : 0;
            if (age == 0)        { *outOrder = 0; *outLabel = "Today"; }
            else if (age == 1)   { *outOrder = 1; *outLabel = "Yesterday"; }
            else if (age <= 6)   { *outOrder = 2; *outLabel = "EarlierThisWeek"; }
            else if (age <= 13)  { *outOrder = 3; *outLabel = "LastWeek"; }
            else if (age <= 30)  { *outOrder = 4; *outLabel = "EarlierThisMonth"; }
            else if (age <= 60)  { *outOrder = 5; *outLabel = "LastMonth"; }
            else if (age <= 364) { *outOrder = 6; *outLabel = "EarlierThisYear"; }
            else if (age <= 729) { *outOrder = 7; *outLabel = "LastYear"; }
            else                 { *outOrder = 8; *outLabel = "Older"; }
            break;
        }
        case GroupMode::Size:
        {
            if (entry.isDirectory && !statsKnown) { *outOrder = 7; *outLabel = "Unspecified"; break; }
            if (bytes == 0)                          { *outOrder = 0; *outLabel = "Empty"; }
            else if (bytes < 16ULL * 1024)           { *outOrder = 1; *outLabel = "Tiny"; }
            else if (bytes < 1024ULL * 1024)         { *outOrder = 2; *outLabel = "Small"; }
            else if (bytes < 16ULL * 1024 * 1024)    { *outOrder = 3; *outLabel = "Medium"; }
            else if (bytes < 128ULL * 1024 * 1024)   { *outOrder = 4; *outLabel = "Large"; }
            else if (bytes < 1024ULL * 1024 * 1024)  { *outOrder = 5; *outLabel = "Huge"; }
            else                                     { *outOrder = 6; *outLabel = "Gigantic"; }
            break;
        }
        case GroupMode::Count:
        {
            if (!entry.isDirectory || !statsKnown) { *outOrder = 8; *outLabel = "Unspecified"; break; }
            if (items == 0)          { *outOrder = 0; *outLabel = "Empty"; }
            else if (items <= 5)     { *outOrder = 1; *outLabel = "Few"; }
            else if (items <= 50)    { *outOrder = 2; *outLabel = "Dozens"; }
            else if (items <= 500)   { *outOrder = 3; *outLabel = "Hundreds"; }
            else if (items <= 5000)  { *outOrder = 4; *outLabel = "Thousands"; }
            else if (items <= 50000) { *outOrder = 5; *outLabel = "TensOfThousands"; }
            else if (items <= 500000){ *outOrder = 6; *outLabel = "HundredsOfThousands"; }
            else                     { *outOrder = 7; *outLabel = "Millions"; }
            break;
        }
        case GroupMode::Volume:
        default:
        {
            if (percentFull < 0) { *outOrder = 11; *outLabel = "Unspecified"; break; }
            const int band = (percentFull >= 100) ? 10 : percentFull / 10;
            *outOrder = band;
            if (band >= 10)
            {
                *outLabel = "100%";
            }
            else
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%d-%d%%", band * 10, band * 10 + 9);
                *outLabel = buffer;
            }
            break;
        }
    }
}

const char* GroupModeName(GroupMode mode)
{
    switch (mode)
    {
        case GroupMode::Name:   return Tr("Nome", "Name");
        case GroupMode::Date:   return Tr("Data", "Date");
        case GroupMode::Size:   return Tr("Dimensione", "Size");
        case GroupMode::Count:  return Tr("Conteggio", "Count");
        case GroupMode::Volume: return Tr("Volume", "Volume");
        default:                return Tr("No", "None");
    }
}

// Il nome di un gruppo si legge in due lingue. La **chiave** resta quella inglese,
// perche' e' con quella che si ricorda se il gruppo e' chiuso: tradurre la chiave
// vorrebbe dire riaprire da soli i gruppi che l'utente aveva chiuso, al cambio di
// lingua. Una fascia di percentuale non e' una parola e resta com'e'.
const char* GroupLabelIt(const std::string& key)
{
    struct Label { const char* key; const char* italian; };
    static const Label kLabels[] = {
        {"Other", "Altro"},
        {"Today", "Oggi"},
        {"Yesterday", "Ieri"},
        {"EarlierThisWeek", "Inizio settimana"},
        {"LastWeek", "Settimana scorsa"},
        {"EarlierThisMonth", "Inizio mese"},
        {"LastMonth", "Mese scorso"},
        {"EarlierThisYear", "Inizio anno"},
        {"LastYear", "Anno scorso"},
        {"Older", "Pi\xc3\xb9 vecchi"},
        {"Unspecified", "Non calcolato"},
        {"Empty", "Vuoto"},
        {"Tiny", "Minuscolo"},
        {"Small", "Piccolo"},
        {"Medium", "Medio"},
        {"Large", "Grande"},
        {"Huge", "Enorme"},
        {"Gigantic", "Gigantesco"},
        {"Few", "Pochi"},
        {"Dozens", "Decine"},
        {"Hundreds", "Centinaia"},
        {"Thousands", "Migliaia"},
        {"TensOfThousands", "Decine di migliaia"},
        {"HundredsOfThousands", "Centinaia di migliaia"},
        {"Millions", "Milioni"},
    };
    for (const Label& label : kLabels)
    {
        if (key == label.key)
            return Tr(label.italian, label.key);
    }
    return nullptr;
}

// Quello che si legge in testa a un gruppo: la traduzione se c'e', altrimenti la
// chiave — che per una fascia di percentuale e' gia' quello che si vuole leggere.
std::string GroupLabel(const std::string& key)
{
    const char* italian = GroupLabelIt(key);
    return italian != nullptr ? std::string(italian) : key;
}

// Dove punta un collegamento. Un ".lnk" da solo non dice cosa apre, e il bersaglio
// accanto al nome e' quello che si vuole sapere prima di aprirlo: e' quello che fa
// File Pilot, ed e' l'unica informazione del collegamento che serva davvero.
//
// La lettura apre il file, quindi si fa una volta per percorso e si tiene: la riga
// si disegna a ogni fotogramma.
const std::wstring& ShortcutTarget(AppState& app, const Entry& entry)
{
    static const std::wstring empty;

    if (entry.extension != "lnk" || !entry.location.empty())
        return empty;   // fuori da una cartella normale un collegamento non si risolve

    // Il percorso: qui la voce e' per forza di una cartella normale, quindi il nome
    // dentro la cartella aperta e' tutto quello che serve.
    const std::wstring path = JoinPath(app.directory, entry.wideName);
    const auto cached = app.shortcutTargets.find(path);
    if (cached != app.shortcutTargets.end())
        return cached->second;

    std::wstring target;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, reinterpret_cast<void**>(&link))) &&
        link != nullptr)
    {
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file))) &&
            file != nullptr)
        {
            if (SUCCEEDED(file->Load(path.c_str(), STGM_READ)))
            {
                wchar_t buffer[MAX_PATH]{};
                if (SUCCEEDED(link->GetPath(buffer, MAX_PATH, nullptr, SLGP_RAWPATH)))
                    target = buffer;

                // Un collegamento non punta sempre a un file: puo' puntare a una
                // cartella dello spazio dei nomi, e allora l'identificatore e' il
                // PIDL, non un percorso.
                if (target.empty())
                {
                    PIDLIST_ABSOLUTE pidl = nullptr;
                    if (SUCCEEDED(link->GetIDList(&pidl)) && pidl != nullptr)
                    {
                        PWSTR name = nullptr;
                        if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name)) &&
                            name != nullptr)
                        {
                            target = name;
                            CoTaskMemFree(name);
                        }
                        CoTaskMemFree(pidl);
                    }
                }
            }
            file->Release();
        }
        link->Release();
    }

    return app.shortcutTargets.emplace(path, std::move(target)).first->second;
}

// La geometria della griglia, **calcolata una volta** e usata da tutti.
//
// Prima ogni indice aveva la sua formula (`indice % colonne`, `indice / colonne`),
// e con le bande di gruppo quella formula non vale piu': una banda a piena
// larghezza spezza il flusso, e la riga di celle sotto di lei ricomincia da
// sinistra. Rifare il conto a ogni domanda vorrebbe dire farlo per ogni cella
// disegnata, per ogni clic e per ogni scorrimento, e tre conti che devono
// coincidere prima o poi non coincidono.
//
// Non c'e' lo scorrimento dentro questi rettangoli: chi li usa lo sottrae, cosi'
// la geometria non va rifatta a ogni rotella.
const std::vector<Rect>& GridRects(AppState& app, const Layout& layout)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    if (app.gridRectsRows == app.rows.size() && app.gridRectsWidth == layout.list.w &&
        app.gridRectsView == static_cast<int>(app.viewMode) &&
        app.gridRectsGroup == static_cast<int>(app.groupMode) && app.gridRectsScale == layout.scale &&
        app.gridRectsIcon == app.gridIconSize)
        return app.gridRects;

    app.gridRects.clear();
    app.gridRects.reserve(app.rows.size());

    const float left = layout.list.x + metrics.padding;
    float x = left;
    float y = layout.list.y + metrics.padding;
    int column = 0;

    for (const Row& row : app.rows)
    {
        if (row.header)
        {
            // Una cella a meta' riga non si lascia dietro una banda: la riga in
            // corso si chiude prima, e la banda parte da sinistra.
            if (column != 0)
            {
                y += metrics.cellHeight;
                column = 0;
                x = left;
            }
            app.gridRects.push_back(Rect{layout.list.x + 1.0f, y, layout.list.w - 2.0f,
                                         metrics.bandHeight});
            y += metrics.bandHeight;
            continue;
        }

        app.gridRects.push_back(Rect{x, y, metrics.cellWidth, metrics.cellHeight});
        x += metrics.cellWidth;
        if (++column >= metrics.columns)
        {
            column = 0;
            x = left;
            y += metrics.cellHeight;
        }
    }

    app.gridRectsRows  = app.rows.size();
    app.gridRectsWidth = layout.list.w;
    app.gridRectsView  = static_cast<int>(app.viewMode);
    app.gridRectsGroup = static_cast<int>(app.groupMode);
    app.gridRectsScale = layout.scale;
    app.gridRectsIcon  = app.gridIconSize;
    return app.gridRects;
}

// Splits a name into at most |maxLines| lines that fit, ellipsising the last one.
// Character based: this is a label, and the glyph atlas is ASCII.
std::vector<std::string> WrapName(const FontAtlas& font, const std::string& text, float scale,
                                  float maxWidth, int maxLines)
{
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0.0f || maxLines < 1)
        return lines;

    std::string current;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (font.Measure(current + c, scale) > maxWidth && !current.empty())
        {
            lines.push_back(current);
            if (static_cast<int>(lines.size()) == maxLines - 1)
            {
                current = text.substr(i);   // the rest goes on the last line
                break;
            }
            current.clear();
            if (c == ' ')
                continue;                    // do not start a line with a space
        }
        current.push_back(c);
    }
    if (!current.empty())
        lines.push_back(current);

    if (!lines.empty())
        lines.back() = FitText(font, lines.back(), scale, maxWidth);
    return lines;
}

// La casella di scelta, quando `ShowCheckboxes` e' acceso. Riflette la selezione:
// una casella accanto a una riga non selezionata sarebbe una seconda verita' da
// tenere allineata alla prima, e prima o poi le due divergono.
//
// Si preme per scegliere: la casella prende il clic, la riga sotto no. E' il gesto
// che serve a scegliere senza aprire, che e' tutto il motivo per cui le caselle
// esistono accanto al clic singolo.
float DrawCheckbox(AppState& app, const Rect& row, bool selected, float scale,
                   float padding, int entryIndex)
{
    const Theme& t = app.theme;
    const float side = 14.0f * scale;
    const Rect box{row.x + padding, row.y + (row.h - side) * 0.5f, side, side};
    app.renderer->Draw(UIDraw::Fill(box, selected ? t.accent : t.windowBg, 3.0f * scale));
    // Il bordo di una casella vuota e' in tono spento ma **visibile**: su una riga
    // scura un bordo del colore del pannello sparisce, e una casella che non si vede
    // e' una casella che non c'e'.
    app.renderer->Draw(UIDraw::Border(box, selected ? t.accent : t.textDim, 1.0f, 3.0f * scale));
    if (selected)
    {
        // Il segno di spunta e' un quadrato pieno: nell'atlante dei glifi non c'e'
        // un carattere di spunta, e scriverne uno fuori dall'insieme rasterizzato
        // darebbe un buco (§7.32).
        const float inner = 6.0f * scale;
        app.renderer->Draw(UIDraw::Fill(Rect{box.x + (side - inner) * 0.5f,
                                            box.y + (side - inner) * 0.5f, inner, inner},
                                       t.windowBg, 1.0f * scale));
    }

    ClickTarget target;
    target.rect  = Rect{box.x - 3.0f * scale, row.y, box.w + 6.0f * scale, row.h};
    target.kind  = ClickTarget::Kind::Checkbox;
    target.index = entryIndex;
    app.clickTargets.push_back(target);
    return box.w + padding;
}

// One cell of a grid view: thumbnail on top, name underneath, centred.
void DrawEntryCell(AppState& app, const Layout& layout, const ViewMetrics& metrics,
                   const Rect& cell, const Row& view, int cursorIndex)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Entry& entry = app.entries[static_cast<size_t>(view.entry)];

    const Rect card = cell.Inset(metrics.padding * 0.5f, metrics.padding * 0.5f);
    // Nella griglia il contenuto e' centrato, quindi la casella sta nell'angolo
    // senza spostare niente.
    const bool checkboxes = app.showCheckboxes;
    // La selezione vince sul sovrappasso: la riga sotto il mouse resta leggibile
    // anche quando e' selezionata, invece di cambiare colore sotto il puntatore.
    if (IsEntrySelected(app, entry))
        r.FillRect(card, t.rowSelected, 4.0f * scale);
    else if (view.entry == app.hovered)
        r.FillRect(card, t.rowHover, 4.0f * scale);
    if (view.entry == cursorIndex)
        r.Draw(UIDraw::Border(card, t.accent.WithAlpha(0.9f), 1.0f, 4.0f * scale));

    if (checkboxes)
        DrawCheckbox(app, cell, IsEntrySelected(app, entry), scale, metrics.padding * 0.5f,
                     view.entry);

    // La destinazione di una caduta: tratteggio, cosi' si distingue sia dal
    // riempimento della selezione sia dalla linea continua del cursore.
    if (!app.dropTarget.empty() && app.dropTargetIsRow &&
        entry.wideName == LeafName(app.dropTarget))
        r.Draw(UIDraw::DashedBorder(card, t.accent, 2.0f * scale, 6.0f * scale, 4.0f * scale,
                                    6.0f * scale));

    ClickTarget target;
    target.rect  = card;
    target.kind  = ClickTarget::Kind::Row;
    target.index = view.entry;
    app.clickTargets.push_back(target);

    const float lineHeight = app.font.LineHeight() * scale;
    const float labelTop   = card.bottom() - lineHeight * 2.0f;
    const float iconArea   = (std::max)(16.0f, labelTop - card.y - 4.0f * scale);
    const float iconSize   = (std::min)(metrics.iconSize, iconArea);
    const Rect icon{card.x + (card.w - iconSize) * 0.5f,
                    card.y + (iconArea - iconSize) * 0.5f,
                    iconSize, iconSize};

    ShellPreviewCache::IconUV preview;
    ShellPreviewCache::IconUV largeIcon;
    // Fuori da una cartella normale un'anteprima non c'e': `Query` riceve il nome
    // di spazio dei nomi, se c'e', e risponde "niente" invece di leggere un
    // percorso che non esiste.
    const ShellPreviewCache::State state =
        app.previews.Query(EntryLocation(app, entry), entry.extension, &preview);

    // La misura delle anteprime: 0 le spegne (resta l'icona), 50 le disegna a
    // meta' nella loro casella. Oltre la casella non si va, o la miniatura
    // finirebbe sopra l'etichetta.
    const float previewZoom = app.PreviewZoom();
    const Rect  previewIcon{icon.x + icon.w * (1.0f - previewZoom) * 0.5f,
                            icon.y + icon.h * (1.0f - previewZoom) * 0.5f,
                            icon.w * previewZoom, icon.h * previewZoom};

    if (state == ShellPreviewCache::State::Ready && previewZoom > 0.0f)
    {
        r.Draw(UIDraw::Image(previewIcon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                             preview.u0, preview.v0, preview.u1, preview.v1));
    }
    // Oltre la misura dell'atlante — che rasterizza a `SM_CXICON`, 32 px — l'icona
    // si chiede **grande alla shell** invece di essere ingrandita: e' li' che la
    // vista a icone grandi si sgranava. La misura la da' la shell, quindi regge
    // anche a 128 px. Il ripiego resta quello piccolo: mentre si aspetta, e se la
    // shell non risponde, un'icona un po' morbida e' meglio di un buco.
    else if (iconSize > static_cast<float>(app.icons.IconPixels()) * scale &&
             app.previews.QueryIcon(entry.extension, entry.isDirectory,
                                    EntryLocation(app, entry), &largeIcon) ==
                 ShellPreviewCache::State::Ready)
    {
        r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                             largeIcon.u0, largeIcon.v0, largeIcon.u1, largeIcon.v1));
    }
    else if (const ShellIconAtlas::IconUV* uv = IconFor(app, entry))
    {
        r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                             uv->u0, uv->v0, uv->u1, uv->v1));
    }

    const std::vector<std::string> lines =
        WrapName(app.font, entry.name, scale, card.w - 8.0f * scale, 2);
    float textY = labelTop;
    float lastX = card.x;
    float lastWidth = 0.0f;
    for (const std::string& line : lines)
    {
        const float width = app.font.Measure(line, scale);
        lastX = card.x + (card.w - width) * 0.5f;
        lastWidth = width;
        app.font.Draw(r, lastX, textY, line, t.text, scale);
        textY += lineHeight;
    }

    // La sottolineatura va sotto l'ultima riga del nome, che e' l'ultima che si
    // legge: sotto la prima sarebbe sotto una riga di testo che continua.
    if (app.underlineOnHover && view.entry == app.hovered && !lines.empty())
        r.FillRect(Rect{lastX, textY - lineHeight + app.font.LineHeight() * scale - 1.0f * scale,
                        lastWidth, 1.0f * scale},
                   t.text);
}

} // namespace

// --- modello: dall'elenco delle voci alle righe che si vedono ---------------
bool SortEntries(AppState& app)
{
    const bool ascending = app.sortAscending;
    const SortColumn column = app.sortColumn;

    // A statistics column needs the service, which costs a lock per comparison.
    // That is acceptable here because sorting happens on demand, not per frame.
    auto statsFor = [&app](const Entry& entry, DirStats* out) -> bool
    {
        if (!entry.isDirectory)
            return false;
        return app.stats.TryGet(JoinPath(app.directory, entry.wideName), out);
    };

    auto statValue = [&statsFor](const Entry& entry, SortColumn which) -> uint64_t
    {
        if (!entry.isDirectory)
            return (which == SortColumn::Size) ? entry.bytes : 0;

        DirStats stats;
        if (!statsFor(entry, &stats))
            return 0;
        switch (which)
        {
            case SortColumn::Folders: return stats.folders;
            case SortColumn::Files:   return stats.files;
            default:                  return stats.logicalBytes;
        }
    };

    std::stable_sort(app.entries.begin(), app.entries.end(),
                     [column, ascending, &statValue](const Entry& left, const Entry& right)
                     {
                         // Folders always lead, matching the FoldersFirst default.
                         if (left.isDirectory != right.isDirectory)
                             return left.isDirectory;

                         int order = 0;
                         switch (column)
                         {
                             case SortColumn::Size:
                             case SortColumn::Folders:
                             case SortColumn::Files:
                             {
                                 const uint64_t a = statValue(left, column);
                                 const uint64_t b = statValue(right, column);
                                 order = (a < b) ? -1 : (a > b) ? 1 : 0;
                                 break;
                             }
                             case SortColumn::Modified:
                             {
                                 const uint64_t a = FileTimeToUint64(left.modified);
                                 const uint64_t b = FileTimeToUint64(right.modified);
                                 order = (a < b) ? -1 : (a > b) ? 1 : 0;
                                 break;
                             }
                             case SortColumn::Type:
                                 order = left.typeText.compare(right.typeText);
                                 break;
                             case SortColumn::Name:
                             default:
                                 order = _wcsicmp(left.wideName.c_str(), right.wideName.c_str());
                                 break;
                         }

                         if (order == 0)
                             order = _wcsicmp(left.wideName.c_str(), right.wideName.c_str());

                         return ascending ? (order < 0) : (order > 0);
                     });
    return true;
}


// Il valore di una colonna per una voce, **come si vede**: la stessa stringa che
// disegna la cella. Un filtro che parla una lingua diversa da quella della colonna
// e' un filtro che non si capisce.
//
// La dimensione fa eccezione ed e' dichiarata: un valore per file non filtra
// niente, quindi li' il valore e' una **fascia** ("< 1 KB"), che e' quello che
// chiede chi cerca "i file piccoli".
std::string ColumnValueOf(const AppState& app, const Entry& entry, int column)
{
    switch (column)
    {
        case 1: return entry.typeText;
        case 2: return entry.modifiedText.empty() ? std::string("--") : entry.modifiedText;
        case 3:
        case 4:
        {
            if (!entry.isDirectory || !entry.location.empty())
                return "--";
            DirStats stats;
            if (!app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats))
                return "--";
            return HumanCount(column == 3 ? stats.folders : stats.files);
        }
        case 5:
        {
            uint64_t bytes = entry.bytes;
            if (entry.isDirectory)
            {
                if (entry.location.empty())
                {
                    DirStats stats;
                    if (!app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats))
                        return "--";
                    bytes = stats.logicalBytes;
                }
                else
                {
                    return "--";
                }
            }
            if (bytes == 0)
                return "0 B";
            if (bytes < 1024ull)
                return "< 1 KB";
            if (bytes < 1024ull * 1024ull)
                return "< 1 MB";
            if (bytes < 1024ull * 1024ull * 1024ull)
                return "< 1 GB";
            return ">= 1 GB";
        }
        default: return std::string();
    }
}

void RebuildView(AppState& app)
{
    app.rows.clear();
    app.groups.clear();
    app.matchCount = 0;
    app.viewDirty = false;

    // Le bande di gruppo adesso stanno anche nella griglia: la geometria e' un
    // impaginatore a flusso che sa spezzarsi, e la usano tutti — vedi `GridRects`.
    if (app.groupMode == GroupMode::None)
    {
        for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
        {
            if (EntryVisible(app, app.entries[static_cast<size_t>(i)]))
                app.rows.push_back(Row{false, i, -1});
        }
        app.matchCount = static_cast<int>(app.rows.size());
        return;
    }

    int percentFull = -1;
    if (app.groupMode == GroupMode::Volume)
    {
        int percent = 0;
        if (VolumeUsage(app.directory, &percent))
            percentFull = percent;
    }

    struct Bucket
    {
        int              order = 0;
        std::string      label;
        std::vector<int> entries;
    };

    std::vector<Bucket> buckets;
    std::unordered_map<int, int> bucketIndex;

    const auto addToBucket = [&buckets, &bucketIndex](int order, const std::string& label, int entryIndex)
    {
        const auto found = bucketIndex.find(order);
        int index = 0;
        if (found == bucketIndex.end())
        {
            index = static_cast<int>(buckets.size());
            bucketIndex[order] = index;
            buckets.push_back(Bucket{order, label, {}});
        }
        else
        {
            index = found->second;
        }
        buckets[static_cast<size_t>(index)].entries.push_back(entryIndex);
    };

    for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
    {
        const Entry& entry = app.entries[static_cast<size_t>(i)];
        if (!EntryVisible(app, entry))
            continue;

        uint64_t bytes = entry.isDirectory ? 0 : entry.bytes;
        uint64_t items = 0;
        bool statsKnown = true;
        if (entry.isDirectory)
        {
            DirStats stats;
            statsKnown = app.stats.TryGet(JoinPath(app.directory, entry.wideName), &stats);
            if (statsKnown)
            {
                bytes = stats.logicalBytes;
                items = stats.files + stats.folders;
            }
        }

        int order = 0;
        std::string label;
        GroupKeyFor(entry, app.groupMode, bytes, items, statsKnown, percentFull, &order, &label);
        addToBucket(order, label, i);
        ++app.matchCount;
    }

    std::sort(buckets.begin(), buckets.end(),
              [](const Bucket& a, const Bucket& b) { return a.order < b.order; });

    for (const Bucket& bucket : buckets)
    {
        app.groups.push_back(Group{bucket.label, static_cast<int>(bucket.entries.size())});
        const int groupIndex = static_cast<int>(app.groups.size()) - 1;
        app.rows.push_back(Row{true, -1, groupIndex});

        if (app.collapsedGroups.count(bucket.label) != 0)
            continue;
        for (const int entryIndex : bucket.entries)
            app.rows.push_back(Row{false, entryIndex, -1});
    }
}

// La larghezza di partenza e' quella di `kColumns`. Si chiama una volta, quando
// l'applicazione nasce: dopo, la larghezza e' quella che l'utente ha scelto.
void InitColumns(AppState& app)
{
    for (int i = 0; i < kColumnCount; ++i)
        app.columns[i].fraction = kColumns[i].fraction;
}

void SetViewMode(AppState& app, ViewMode mode)
{
    if (app.viewMode == mode)
        return;
    app.viewMode    = mode;
    app.viewDirty   = true;
    app.hovered     = -1;
    app.hoverStart  = NowSeconds();

    // I due comandi a griglia portano la misura ai due punti noti: sono scorciatoie,
    // non due modalita' separate.
    if (mode == ViewMode::Icons)
        app.gridIconSize = 64.0f;
    else if (mode == ViewMode::Tiles)
        app.gridIconSize = 128.0f;
}

// --- le voci: la domanda "dove sta", in un posto solo -----------------------
// Il nome assoluto di una voce: quello che si e' letto per le cartelle dello
// spazio dei nomi, o il percorso dentro la cartella aperta per tutto il resto.
//
// Un solo posto decide, cosi' aprire, selezionare e chiedere un verbo alla shell
// non possono divergere sul significato di "dove sta questa voce".
std::wstring EntryLocation(const AppState& app, const Entry& entry)
{
    return entry.location.empty() ? JoinPath(app.directory, entry.wideName) : entry.location;
}

// L'icona di una voce della lista. Per quasi tutte e' quella della **sua
// estensione**, che la shell risolve senza toccare il disco: la lista puo' avere
// migliaia di righe e l'atlante non ha sfratto. Per un'unita' invece l'icona dipende
// dal disco — un disco fisso, uno rimovibile, uno di rete — e si chiede guardandola;
// l'insieme e' chiuso (una per lettera di unita'), quindi si puo' fare.
const ShellIconAtlas::IconUV* IconFor(AppState& app, const Entry& entry)
{
    if (entry.volumeRoot)
    {
        if (const ShellIconAtlas::IconUV* uv = app.icons.GetForObject(entry.location))
            return uv;
    }
    return app.icons.GetFor(entry.extension, entry.isDirectory);
}

int FindEntryByName(const AppState& app, const std::wstring& name)
{
    for (int i = 0; i < static_cast<int>(app.entries.size()); ++i)
    {
        if (app.entries[static_cast<size_t>(i)].wideName == name)
            return i;
    }
    return -1;
}

int CursorIndex(const AppState& app)
{
    return app.cursor.empty() ? -1 : FindEntryByName(app, app.cursor);
}

bool IsEntrySelected(const AppState& app, const Entry& entry)
{
    return app.selection.count(entry.wideName) != 0;
}

// --- geometria: dove cade una riga -------------------------------------------
ViewMetrics ComputeViewMetrics(const AppState& app, const Layout& layout)
{
    const float scale = layout.scale;
    // The cell height is derived from the icon and the label rather than fixed,
    // so a two line name can never overlap the picture above it.
    const float labelHeight = app.font.LineHeight() * scale * 2.0f;

    ViewMetrics metrics;
    metrics.padding = 6.0f * scale;

    switch (app.viewMode)
    {
        case ViewMode::List:
            metrics.rowHeight = 20.0f * scale;
            metrics.iconSize  = 14.0f * scale;
            break;

        // Le due viste a griglia condividono la misura: quello che cambia e' solo
        // quanto e' grande. Tutto il resto si **deriva** da li' — la cella deve
        // contenere l'icona e stare comoda per l'etichetta, e un numero derivato
        // non puo' entrare in disaccordo con le sue parti.
        case ViewMode::Icons:
        case ViewMode::Tiles:
            metrics.grid       = true;
            metrics.iconSize   = app.gridIconSize * scale;
            metrics.cellWidth  = (app.gridIconSize + 40.0f) * scale;
            metrics.cellHeight = metrics.iconSize + labelHeight + 16.0f * scale;
            break;

        case ViewMode::Details:
        default:
            metrics.rowHeight = layout.rowHeight;
            metrics.iconSize  = 16.0f * scale;
            break;
    }

    if (metrics.grid)
    {
        const float usable = layout.list.w - metrics.padding * 2.0f;
        metrics.columns = (std::max)(1, static_cast<int>(usable / metrics.cellWidth));
        // Spread the leftover width so the cells tile the panel exactly.
        metrics.cellWidth = usable / static_cast<float>(metrics.columns);
        // Una banda di gruppo e' alta come una riga dell'elenco: e' la stessa cosa
        // detta in un'altra vista, e deve leggersi come la stessa cosa.
        metrics.bandHeight = layout.rowHeight;
    }
    return metrics;
}

float ViewContentHeight(AppState& app, const Layout& layout)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    if (!metrics.grid)
        return static_cast<float>(app.rows.size()) * metrics.rowHeight;
    const std::vector<Rect>& rects = GridRects(app, layout);
    if (rects.empty())
        return metrics.padding * 2.0f;
    return rects.back().bottom() - layout.list.y + metrics.padding;
}

Rect ItemRect(AppState& app, const Layout& layout, int rowIndex, float scroll)
{
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    Rect rect;
    if (!metrics.grid)
    {
        rect = Rect{layout.list.x + 1.0f,
                    layout.list.y + static_cast<float>(rowIndex) * metrics.rowHeight,
                    layout.list.w - 2.0f, metrics.rowHeight};
    }
    else
    {
        const std::vector<Rect>& rects = GridRects(app, layout);
        if (rowIndex < 0 || rowIndex >= static_cast<int>(rects.size()))
            return Rect{};
        rect = rects[static_cast<size_t>(rowIndex)];
    }
    rect.y -= scroll;
    return rect;
}

// --- disegno: la lista, le colonne e il filtro -------------------------------
void DrawHeader(AppState& app, const Layout& layout, float* columnX)
{
    if (app.viewMode == ViewMode::Tiles || app.viewMode == ViewMode::Icons)
        return;   // nothing to head, and the list already has the space

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.FillRect(layout.header, t.headerBg, 4.0f * scale);

    // A small pulse beside the statistics headers while jobs are running: the
    // numbers are still moving, and that is worth saying out loud.
    const float pulse = 0.25f + 0.75f * static_cast<float>(std::fabs(std::sin(app.lastFrameTime * 3.0)));

    for (int i = 0; i < kColumnCount; ++i)
    {
        const float x = layout.header.x + 10.0f * scale + layout.header.w * app.columns[i].fraction;
        columnX[i] = x;

        const bool sorted = (app.sortColumn == kColumns[i].sort);
        std::string label = ColumnLabel(i);
        if (sorted)
            label += app.sortAscending ? " ^" : " v";

        // Il titolo si accorcia se non ci sta. Con il carattere di partenza ci
        // stava per fortuna; con un carattere piu' largo scelto dalle opzioni (o
        // con una colonna stretta trascinata) i titoli si sovrapponevano, e un
        // titolo sopra un altro e' peggio di un titolo troncato.
        const float nextX = layout.header.x + 10.0f * scale + layout.header.w *
                            (i + 1 < kColumnCount ? app.columns[static_cast<size_t>(i) + 1].fraction
                                                  : 1.0f);
        const float room = nextX - x - (kColumns[i].filterable ? 26.0f * scale : 8.0f * scale);
        if (room > 10.0f * scale)
            label = FitText(app.smallFont, label, scale, room);

        app.smallFont.Draw(r, x, TextY(layout, layout.header, app.smallFont), label,
                      sorted ? t.text : t.textDim, scale);

        if (i >= 3 && app.stats.Busy())
        {
            const float dotX = x + app.smallFont.Measure(label, scale) + 6.0f * scale;
            const float dotY = layout.header.y + layout.header.h * 0.5f - 2.0f * scale;
            r.FillRect(Rect{dotX, dotY, 4.0f * scale, 4.0f * scale},
                       t.accent.WithAlpha(pulse), 2.0f * scale);
        }

        const float width = (i < kColumnCount - 1)
            ? layout.header.w * (app.columns[i + 1].fraction - app.columns[i].fraction)
            : layout.header.w * (1.0f - app.columns[i].fraction);
        ClickTarget target;
        target.rect = Rect{x - 10.0f * scale, layout.header.y, width, layout.header.h};
        target.kind = ClickTarget::Kind::Column;
        target.index = i;
        app.clickTargets.push_back(target);

        // La linguetta del filtro: tre barre di larghezza decrescente, che e' il
        // segno che si legge come "imbuto". In colore d'accento quando la colonna
        // filtra davvero, spenta quando no.
        if (kColumns[i].filterable)
        {
            const float right = layout.header.x + layout.header.w *
                                (i + 1 < kColumnCount ? app.columns[i + 1].fraction : 1.0f);
            const float glyphX = right - 16.0f * scale;
            const float glyphY = layout.header.y + layout.header.h * 0.5f;
            const bool  filtered = (i == 0) ? (!app.filter.empty() || app.filterActive)
                                            : !app.columns[static_cast<size_t>(i)].excluded.empty();
            const Color ink = filtered ? t.accent : t.textDim;
            const float widths[3] = {8.0f, 5.5f, 3.0f};
            for (int bar = 0; bar < 3; ++bar)
            {
                const float w = widths[bar] * scale;
                r.FillRect(Rect{glyphX + (8.0f * scale - w) * 0.5f,
                                glyphY - 4.0f * scale + bar * 3.0f * scale, w, 1.5f * scale},
                           ink, 0.5f * scale);
            }

            ClickTarget filterTarget;
            filterTarget.rect  = Rect{glyphX - 3.0f * scale, layout.header.y, 16.0f * scale,
                                      layout.header.h};
            filterTarget.kind  = ClickTarget::Kind::ColumnFilter;
            filterTarget.index = i;
            app.clickTargets.push_back(filterTarget);
        }

        // Il bordo, che si trascina. Sta a **sinistra** del nome della colonna: cosi'
        // il clic sul nome resta un clic per ordinare, e la striscia di presa non
        // copre niente che non sia gia' una zona morta.
        if (i > 0)
        {
            const float edge = layout.header.x + layout.header.w * app.columns[i].fraction;
            ClickTarget grip;
            grip.rect = Rect{edge - 3.0f * scale, layout.header.y, 6.0f * scale,
                             layout.header.h};
            grip.kind = ClickTarget::Kind::ColumnEdge;
            grip.index = i;
            app.clickTargets.push_back(grip);
        }
    }

    r.FillRect(Rect{layout.header.x, layout.header.bottom() - 1.0f, layout.header.w, 1.0f}, t.separator);
}

void DrawFilterBar(AppState& app, const Layout& layout)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const Rect bar = layout.filterBar;

    r.FillRect(bar, t.headerBg, 4.0f * scale);
    r.Draw(UIDraw::Border(bar, app.filterActive ? t.accent.WithAlpha(0.75f) : t.panelBorder,
                          1.0f, 4.0f * scale));

    const std::string text =
        app.filter.empty()
            ? (std::string(Tr("Filtra ", "Filter ")) + std::to_string(app.entries.size()) +
               Tr(" file...", " files..."))
            : ToUtf8(app.filter);

    const float textX = bar.x + 10.0f * scale;
    app.font.Draw(r, textX, TextY(layout, bar, app.font), text,
                  app.filter.empty() ? t.textDim : t.text, scale);

    if (app.filterActive)
    {
        const float caretX = textX + app.font.Measure(text, scale) + 1.0f * scale;
        r.FillRect(Rect{caretX, bar.y + 5.0f * scale, 1.0f, bar.h - 10.0f * scale}, t.text);
    }

    // The count is how many entries the filter matched, not how many rows are on
    // screen: folding a group away must not look like the filter changed.
    const std::string matchText = std::to_string(app.matchCount) + " / " +
                                  std::to_string(app.entries.size());

    std::string groupText = std::string(Tr("Raggruppa: ", "Group: ")) + GroupModeName(app.groupMode);
    const float matchWidth = app.smallFont.Measure(matchText, scale);
    const float groupWidth = app.smallFont.Measure(groupText, scale);
    const float rightEdge  = bar.right() - 10.0f * scale;

    app.smallFont.Draw(r, rightEdge - matchWidth, TextY(layout, bar, app.smallFont),
                  matchText, t.textDim, scale);

    const Rect groupRect{rightEdge - matchWidth - groupWidth - 20.0f * scale, bar.y,
                         groupWidth + 12.0f * scale, bar.h};
    // Il comando si accende quando il raggruppamento sta facendo qualcosa: adesso
    // anche nella griglia, che non e' piu' l'eccezione.
    app.smallFont.Draw(r, groupRect.x, TextY(layout, bar, app.smallFont), groupText,
                  app.groupMode != GroupMode::None ? t.accent : t.textDim, scale);

    ClickTarget target;
    target.rect = groupRect;
    target.kind = ClickTarget::Kind::GroupMode;
    app.clickTargets.push_back(target);
}

void DrawList(AppState& app, const Layout& layout, const float* columnX)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const int cursorIndex = CursorIndex(app);
    const ViewMetrics metrics = ComputeViewMetrics(app, layout);
    const float rowHeight = metrics.rowHeight;

    r.Draw(UIDraw::Fill(layout.list, t.panelBg, 6.0f * scale));

    // Il fondo della lista riceve la caduta nella cartella aperta: si evidenzia
    // l'area, perche' li' sotto non c'e' nessuna riga da tratteggiare. Il
    // tratteggio e' lo stesso della riga, cosi' le due destinazioni si leggono
    // come la stessa cosa detta in due punti diversi.
    if (!app.dropTarget.empty() && !app.dropTargetIsRow && app.dropTargetSidebarRow < 0)
        r.Draw(UIDraw::DashedBorder(layout.list.Inset(1.0f, 1.0f), t.accent, 2.0f * scale,
                                    6.0f * scale, 4.0f * scale, 6.0f * scale));

    if (!app.errorText.empty())
    {
        app.font.Draw(r, layout.list.x + 16.0f * scale, layout.list.y + 16.0f * scale,
                      app.errorText, t.textDim, scale);
        return;
    }

    // A grid packs several rows of the view into one visual row, so the window of
    // interesting rows is computed in items, not in visual rows.
    int first = 0;
    int last  = static_cast<int>(app.rows.size());
    if (metrics.grid)
    {
        // Con le bande di gruppo non c'e' piu' una corrispondenza fra indice e
        // posizione, quindi la finestra non si calcola: si guarda la geometria, e
        // le righe fuori dal riquadro si saltano con lo stesso `continue` di
        // sempre. Non si disegnano e non lasciano bersagli.
        first = 0;
        last  = static_cast<int>(app.rows.size());
    }
    else
    {
        first = (std::max)(0, static_cast<int>(app.scroll / rowHeight) - 1);
        last  = (std::min)(last, first + static_cast<int>(layout.list.h / rowHeight) + 3);
    }

    for (int rowIndex = first; rowIndex < last; ++rowIndex)
    {
        const Row& view = app.rows[static_cast<size_t>(rowIndex)];
        const Rect row = ItemRect(app, layout, rowIndex, app.scroll);

        if (row.y < layout.list.y - row.h || row.y > layout.list.bottom())
            continue;

        // A group band is a row like any other: it scrolls, it is hit tested, and
        // clicking it folds the group away. Folding is remembered by label, so it
        // survives a rebuild of the view.
        if (view.header)
        {
            const Group& group = app.groups[static_cast<size_t>(view.group)];
            const bool collapsed = app.collapsedGroups.count(group.label) != 0;

            r.FillRect(row, t.headerBg);
            // La linguetta e' un triangolo disegnato, come quelle della barra
            // laterale: era il carattere `>` o `v` scritto in testa al titolo.
            const float arrow = 10.0f * scale;
            r.Draw(DisclosureArrow(Rect{row.x + 7.0f * scale, row.y + (row.h - arrow) * 0.5f,
                                        arrow, arrow},
                                   t.textDim, /*pointsDown=*/!collapsed));
            app.smallFont.Draw(r, row.x + 22.0f * scale, TextY(layout, row, app.smallFont),
                          GroupLabel(group.label), t.text, scale);

            const std::string count =
                std::to_string(group.count) +
                (group.count == 1 ? Tr(" voce", " item") : Tr(" voci", " items"));
            app.smallFont.Draw(r, row.right() - app.smallFont.Measure(count, scale) - 10.0f * scale,
                          TextY(layout, row, app.smallFont), count, t.textDim, scale);

            ClickTarget target;
            target.rect  = row;
            target.kind  = ClickTarget::Kind::GroupHeader;
            target.index = view.group;
            target.label = group.label;
            app.clickTargets.push_back(target);
            continue;
        }

        if (metrics.grid)
        {
            DrawEntryCell(app, layout, metrics, row, view, cursorIndex);
            continue;
        }

        const Entry& entry = app.entries[static_cast<size_t>(view.entry)];
        // L'ordine e' deliberato: selezione, poi sovrappasso, poi righe alternate.
        // Il sovrappasso deve stare PRIMA dell'alternanza, o sarebbe visibile
        // solo sulle righe pari e a chiazze.
        if (IsEntrySelected(app, entry))
            r.FillRect(row, t.rowSelected, 3.0f * scale);
        else if (view.entry == app.hovered)
            r.FillRect(row, t.rowHover, 3.0f * scale);
        else if (app.alternatingRows && rowIndex % 2 == 1)
            r.FillRect(row, t.rowOdd, 3.0f * scale);

        // The selection has a fill; the cursor has an outline. The reference theme
        // keeps them as separate tokens (ContentSelection, OutlineSelection) for
        // exactly this reason: with more than one row selected the user still has
        // to be able to see which one the keyboard is on.
        if (view.entry == cursorIndex)
            r.Draw(UIDraw::Border(row.Inset(1.0f, 1.0f), t.accent.WithAlpha(0.9f), 1.0f, 2.0f * scale));

        // La destinazione di una caduta in corso.
        if (!app.dropTarget.empty() && app.dropTargetIsRow &&
            entry.wideName == LeafName(app.dropTarget))
            r.Draw(UIDraw::DashedBorder(row.Inset(1.0f, 1.0f), t.accent, 2.0f * scale,
                                        6.0f * scale, 4.0f * scale, 6.0f * scale));

        ClickTarget target;
        target.rect = row;
        target.kind = ClickTarget::Kind::Row;
        target.index = view.entry;
        app.clickTargets.push_back(target);

        // Dopo la riga: `HitTest` cammina all'indietro, quindi la casella vince sul
        // clic che aprirebbe.
        if (app.showCheckboxes)
            DrawCheckbox(app, row, IsEntrySelected(app, entry), scale, 6.0f * scale, view.entry);

        // A real thumbnail replaces the type icon when one is ready. Asking is
        // free: an unavailable type never reaches the worker, and the request is
        // de-duplicated while a decode is in flight.
        const Rect icon{row.x + 6.0f * scale +
                            (app.showCheckboxes ? 22.0f * scale : 0.0f),
                        row.y + (rowHeight - metrics.iconSize) * 0.5f,
                        metrics.iconSize, metrics.iconSize};
        ShellPreviewCache::IconUV preview;
        const ShellPreviewCache::State previewState =
            app.previews.Query(EntryLocation(app, entry), entry.extension, &preview);

        const float previewZoom = app.PreviewZoom();
        const Rect  previewIcon{icon.x + icon.w * (1.0f - previewZoom) * 0.5f,
                                icon.y + icon.h * (1.0f - previewZoom) * 0.5f,
                                icon.w * previewZoom, icon.h * previewZoom};
        if (previewState == ShellPreviewCache::State::Ready && previewZoom > 0.0f)
        {
            r.Draw(UIDraw::Image(previewIcon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                                 preview.u0, preview.v0, preview.u1, preview.v1));
        }
        else if (const ShellIconAtlas::IconUV* uv = IconFor(app, entry))
        {
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }

        const float textY = TextY(layout, row, app.font);
        const std::wstring fullPath = EntryLocation(app, entry);

        // Numeric columns are right aligned so the digits line up down the
        // column, and underlined while the number is still growing.
        const auto columnRight = [&](int column) -> float
        {
            const float end = (column < kColumnCount - 1) ? columnX[column + 1]
                                                          : layout.header.right();
            return end - 8.0f * scale;
        };
        const auto drawRight = [&](int column, const std::string& text, Color color, bool growing)
        {
            const float right = columnRight(column);
            const float width = app.font.Measure(text, scale);
            app.font.Draw(r, right - width, textY, text, color, scale);
            if (growing)
                r.FillRect(Rect{right - width, row.bottom() - 5.0f * scale, width, 1.0f},
                           color.WithAlpha(0.6f));
        };

        // Con le caselle accese l'icona e il nome si spostano: la casella non
        // copre niente, si prende il suo posto.
        const float nameX   = row.x + 28.0f * scale +
                              (app.showCheckboxes ? 22.0f * scale : 0.0f);
        const float nameMax = columnX[1] - nameX - 12.0f * scale;

        if (app.renameActive && entry.wideName == app.renameOriginal)
        {
            // Il campo si disegna SOPRA la riga invece di sostituirne il
            // contenuto: la lista non cambia impaginazione mentre si scrive,
            // quindi niente si sposta sotto le dita di chi sta digitando.
            const Rect field{nameX - 3.0f * scale, row.y + 1.0f * scale,
                             nameMax + 6.0f * scale, row.h - 2.0f * scale};
            r.Draw(UIDraw::Fill(field, t.windowBg, 2.0f * scale));
            r.Draw(UIDraw::Border(field, t.accent, 1.0f, 2.0f * scale));

            const std::string shown =
                FitText(app.font, app.renameText, scale, field.w - 8.0f * scale);
            app.font.Draw(r, nameX + 1.0f * scale, textY, shown, t.text, scale);

            // Il cursore va dopo il testo VISIBILE, non dopo quello completo: se
            // il nome e' piu' lungo del campo, un cursore sul fondo del campo
            // sembrerebbe fermo mentre si scrive.
            const float caretX =
                nameX + 1.0f * scale + app.font.Measure(shown, scale) + 1.0f * scale;
            r.FillRect(Rect{caretX, row.y + 3.0f * scale, 1.0f * scale,
                            row.h - 6.0f * scale},
                       t.text);
        }
        else
        {
            const std::string shown = FitText(app.font, entry.name, scale, nameMax);
            app.font.Draw(r, nameX, textY, shown, t.text, scale);

            // `UnderlineOnHover`: la sottolineatura sta **sotto il nome**, e sotto
            // quello visibile quando il nome e' troncato — una riga lunga quanto
            // una parola che non c'e' sarebbe una bugia.
            if (app.underlineOnHover && view.entry == app.hovered)
                r.FillRect(Rect{nameX, textY + app.font.LineHeight() * scale - 1.0f * scale,
                                app.font.Measure(shown, scale), 1.0f * scale},
                           t.text);

            // Il bersaglio di un collegamento, dopo il nome e in tono spento: sta
            // nella stessa colonna, quindi prende lo spazio che resta — e se non ne
            // resta abbastanza non si scrive, invece di scrivere tre lettere e un
            // puntino.
            const std::wstring& linkTarget = ShortcutTarget(app, entry);
            if (!linkTarget.empty())
            {
                const float used = app.font.Measure(shown, scale);
                const float from = nameX + used + 14.0f * scale;
                const float room = columnX[1] - from - 8.0f * scale;
                if (room > 60.0f * scale)
                {
                    const std::string text = ToUtf8(linkTarget);
                    const float arrowWidth = app.font.Measure("> ", scale);
                    app.font.Draw(r, from, textY, "> ", t.textDim, scale);
                    app.font.Draw(r, from + arrowWidth, textY,
                                  FitText(app.font, text, scale, room - arrowWidth), t.textDim,
                                  scale);
                }
            }
        }

        const float typeMax = columnX[2] - columnX[1] - 12.0f * scale;
        app.font.Draw(r, columnX[1], textY, FitText(app.font, entry.typeText, scale, typeMax), t.textDim, scale);
        app.font.Draw(r, columnX[2], textY, entry.modifiedText, t.textDim, scale);

        // Files answer immediately; folders are answered by the statistics
        // service, which fills them in progressively. Requesting here means only
        // rows that are actually on screen get counted.
        // Sopra una cartella che non e' un percorso non c'e' niente da contare: le
        // statistiche ricorsive camminano il file system, e li' non ce n'e' uno.
        if (entry.isDirectory && entry.location.empty())
        {
            app.stats.Request(fullPath);

            DirStats stats;
            const bool known = app.stats.TryGet(fullPath, &stats);
            const Color numberColor = known ? t.text : t.textDim;
            const bool growing = known && stats.partial;

            drawRight(3, known ? HumanCount(stats.folders) : "--", numberColor, growing);
            drawRight(4, known ? HumanCount(stats.files) : "--", numberColor, growing);
            drawRight(5, known ? HumanBytes(stats.logicalBytes) : "--", numberColor, growing);
        }
        else
        {
            drawRight(3, "--", t.textDim, false);
            drawRight(4, "--", t.textDim, false);
            drawRight(5, entry.sizeText, t.textDim, false);
        }
    }

    // Scrollbar.
    const float contentHeight = ViewContentHeight(app, layout);
    if (contentHeight > layout.list.h)
    {
        const float trackHeight = layout.list.h - 8.0f;
        const float thumbHeight = (std::max)(24.0f * scale, trackHeight * (layout.list.h / contentHeight));
        const float maxScroll = (std::max)(1.0f, contentHeight - layout.list.h);
        const float thumbY = layout.list.y + 4.0f + (trackHeight - thumbHeight) * (app.scroll / maxScroll);

        const Rect track{layout.list.right() - 7.0f, layout.list.y + 4.0f, 4.0f, trackHeight};
        r.FillRect(track, t.separator, 2.0f * scale);
        r.FillRect(Rect{track.x, thumbY, track.w, thumbHeight}, t.scrollThumb, 2.0f * scale);
    }
}


// --- operazioni su file (M7) ------------------------------------------------
// Percorsi completi di cio' che e' selezionato **e visibile**, nell'ordine della
// lista.
//
// Filtrare puo' nascondere righe che restano nella selezione: la selezione e'
// per nome e sopravvive al cambio di filtro. Senza questo controllo il menu
// contestuale offriva "Elimina definitivamente" su un file che l'utente non
// vedeva piu', e un'azione su qualcosa che non si vede e' esattamente cio' che
// non deve poter succedere. La regola e' quella che Ctrl+A gia' segue: il filtro
// fa parte della domanda, quindi fa parte della risposta.
std::vector<std::wstring> SelectedPaths(const AppState& app)
{
    std::vector<std::wstring> paths;
    for (const Row& row : app.rows)
    {
        if (row.header)
            continue;
        const Entry& entry = app.entries[static_cast<size_t>(row.entry)];
        if (app.selection.count(entry.wideName) != 0)
            paths.push_back(EntryLocation(app, entry));
    }
    return paths;
}


// La selezione come PIDL, nello stesso ordine dei percorsi. Le voci normali non ne
// hanno uno, e va bene: per quelle il percorso basta ed e' la forma che le
// operazioni su file si aspettano. Le voci di spazio dei nomi invece il PIDL ce
// l'hanno per forza, ed e' l'unico modo di chiedere alla shell il loro menu.
std::vector<const BYTE*> SelectedPidls(const AppState& app)
{
    std::vector<const BYTE*> pidls;
    for (const Row& row : app.rows)
    {
        if (row.header)
            continue;
        const Entry& entry = app.entries[static_cast<size_t>(row.entry)];
        if (app.selection.count(entry.wideName) != 0 && !entry.pidl.empty())
            pidls.push_back(entry.pidl.data());
    }
    return pidls;
}

} // namespace Bolt
