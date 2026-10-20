#include "home.h"

#include <unordered_set>

#include "app_ui.h"
#include "language.h"
#include "recents.h"
#include "shell_location.h"

namespace Bolt
{
namespace
{
// La stessa cartella scritta in due modi non deve comparire due volte: il percorso
// che arriva da un pin, da un recente e da una cartella nota puo' avere la barra
// finale di troppo o una maiuscola diversa.
std::wstring KeyOf(const std::wstring& path)
{
    std::wstring key = path;
    while (key.size() > 3 && (key.back() == L'\\' || key.back() == L'/'))
        key.pop_back();
    return FoldCase(key);
}

// Il nome da mostrare: la coda del percorso, o il nome che da' la shell per una
// cartella di spazio dei nomi (una coda non ce l'ha).
std::wstring NameOf(const AppState& app, const std::wstring& path)
{
    return ShellLocation::IsNamespace(path) ? LocationLabel(app, path) : LeafName(path);
}

Entry MakeEntry(const AppState& app, const std::wstring& path, int band)
{
    Entry entry;
    // `wideName` e' l'**identita'** della riga: cursore e selezione si ricordano
    // questo, quindi deve essere unico. Il percorso lo e', la coda no — due
    // cartelle possono chiamarsi "documenti" in posti diversi.
    entry.wideName    = path;
    entry.name        = ToUtf8(NameOf(app, path));
    entry.isDirectory = true;
    entry.navigable   = true;
    entry.location    = path;
    entry.typeText    = ShellTypeName("", /*isDirectory=*/true);
    entry.band        = band;
    return entry;
}
} // namespace

bool IsHome(const std::wstring& location)
{
    return location == kHomeLocation;
}

std::vector<Entry> BuildHomeEntries(const AppState& app)
{
    std::vector<Entry> entries;
    entries.reserve(app.pins.size() + app.recents.Ranked().size() + app.places.size());

    // Le chiavi gia' usate, in ordine di banda: quello che si e' gia' visto in una
    // banda piu' importante non torna in una meno importante.
    std::unordered_set<std::wstring> seen;

    const auto add = [&entries, &seen](const AppState& state, const std::wstring& path, int band)
    {
        if (path.empty())
            return;
        // La Home non si mette dentro la Home: una riga che porta a se stessa e'
        // un giro su se stessi in mezzo alle destinazioni.
        if (IsHome(path))
            return;
        if (!seen.insert(KeyOf(path)).second)
            return;
        entries.push_back(MakeEntry(state, path, band));
    };

    // 0 — i fissaggi, nell'ordine in cui l'utente li ha messi.
    for (const std::wstring& path : app.pins)
        add(app, path, 0);

    // 1 — i recenti, per punteggio (`Recents::Ranked`). Solo cartelle: un file
    // aperto non e' una destinazione dove andare. Anche dove si sta gia' non e' un
    // "recente" da riproporre, ma sulla Home la cartella aperta e' la Home stessa,
    // quindi non c'e' niente da escludere.
    for (const RecentEntry& recent : app.recents.Ranked())
    {
        if (!recent.isDirectory)
            continue;
        add(app, recent.path, 1);
    }

    // 2 — le cartelle note: "Questo PC", la cartella personale, il cestino.
    for (const Place& place : app.places)
        add(app, place.path, 2);

    return entries;
}

const char* HomeBandLabel(int band)
{
    switch (band)
    {
        case 0: return Tr("Fissaggi", "Pinned");
        case 1: return Tr("Recenti", "Recent");
        default: return Tr("Cartelle note", "Known folders");
    }
}
} // namespace Bolt
