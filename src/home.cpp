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

Entry MakeEntry(const std::wstring& path, int band)
{
    Entry entry;
    // `wideName` e' l'**identita'** della riga: cursore e selezione si ricordano
    // questo, quindi deve essere unico. Il percorso lo e', la coda no — due
    // cartelle possono chiamarsi "documenti" in posti diversi.
    entry.wideName    = path;
    // Il nome come lo chiama Windows: una cartella nota si chiama "Documenti" anche
    // se sul disco e' "Documents", ed e' il nome che la barra laterale mostra gia'.
    entry.name        = ToUtf8(FriendlyName(path));
    entry.isDirectory = true;
    entry.navigable   = true;
    entry.location    = path;
    entry.typeText    = ShellTypeName("", /*isDirectory=*/true);
    entry.band        = band;

    // Le date e la dimensione, chieste al percorso come le chiede una cartella
    // qualunque (`FindFirstFile`). Senza, la Home era una tabella con le colonne
    // della data **vuote**: una colonna che c'e' e non dice niente sembra un guasto,
    // ed e' la ragione per cui questa riga esiste.
    if (!ShellLocation::IsNamespace(path))
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        {
            entry.modified = data.ftLastWriteTime;
            entry.created  = data.ftCreationTime;
            entry.accessed = data.ftLastAccessTime;
            entry.bytes    = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            entry.modifiedText = FormatFileTime(entry.modified, /*withTime=*/false);
            entry.modifiedFull = FormatFileTime(entry.modified, /*withTime=*/true);
        }
    }
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

    const auto add = [&entries, &seen](const std::wstring& path, int band)
    {
        if (path.empty())
            return;
        // La Home non si mette dentro la Home: una riga che porta a se stessa e'
        // un giro su se stessi in mezzo alle destinazioni.
        if (IsHome(path))
            return;
        if (!seen.insert(KeyOf(path)).second)
            return;
        entries.push_back(MakeEntry(path, band));
    };

    // 0 — i fissaggi, nell'ordine in cui l'utente li ha messi.
    for (const std::wstring& path : app.pins)
        add(path, 0);

    // 1 — i recenti, per punteggio (`Recents::Ranked`). Solo cartelle: un file
    // aperto non e' una destinazione dove andare. Anche dove si sta gia' non e' un
    // "recente" da riproporre, ma sulla Home la cartella aperta e' la Home stessa,
    // quindi non c'e' niente da escludere.
    for (const RecentEntry& recent : app.recents.Ranked())
    {
        if (!recent.isDirectory)
            continue;
        add(recent.path, 1);
    }

    // 2 — le cartelle note: "Questo PC", la cartella personale, il cestino.
    for (const Place& place : app.places)
        add(place.path, 2);

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
