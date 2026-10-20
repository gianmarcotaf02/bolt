// ---------------------------------------------------------------------------
// Il ranking dei recenti: la formula sta scritta in `recents.h`, qui c'e' il
// codice che la applica. Nessuna scrittura su disco, in nessun modo: `docs/06`.
// ---------------------------------------------------------------------------
#include "recents.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>

#include "app_ui.h"
#include "shell_location.h"

namespace Bolt
{
namespace
{
// L'identita' del volume di un percorso: se e' un disco fisso, il numero di
// serie, e la radice di rete. Sono i tre campi che il riferimento tiene nella
// voce, e servono a non confondere `D:\Foto` sul disco di serie 1234 con
// `D:\Foto` su un disco esterno che ha preso la stessa lettera.
struct Volume
{
    bool         fixed  = false;
    uint32_t     serial = 0;
    std::wstring uncRoot;
};

Volume IdentifyVolume(const std::wstring& path)
{
    Volume volume;
    if (path.empty() || ShellLocation::IsNamespace(path))
        return volume;

    wchar_t root[MAX_PATH * 4] = {};
    if (!GetVolumePathNameW(path.c_str(), root, ARRAYSIZE(root)))
        return volume;

    const UINT type = GetDriveTypeW(root);
    volume.fixed    = (type == DRIVE_FIXED);

    // Il numero di serie c'e' anche per i volumi rimovibili e per le condivisioni
    // di rete: chiederlo sempre costa una syscall e toglie un'ambiguita'.
    DWORD serial = 0;
    if (GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
        volume.serial = serial;

    // `\\server\share\` e' la radice di una condivisione: la parte che identifica
    // un volume di rete e' quella, non il percorso intero.
    if (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        size_t i = 2;
        int    separators = 0;
        while (i < path.size() && separators < 2)
        {
            if (path[i] == L'\\')
                ++separators;
            ++i;
        }
        volume.uncRoot = path.substr(0, i);
        while (!volume.uncRoot.empty() && volume.uncRoot.back() == L'\\')
            volume.uncRoot.pop_back();
    }
    return volume;
}
}   // namespace

float RecentOpacity(int score)
{
    const int top = Recents::kMaxScore;
    const int clamped = (score < 0) ? 0 : (score > top ? top : score);
    return (static_cast<float>(clamped) / static_cast<float>(top)) * 0.75f + 0.25f;
}

std::wstring FoldCase(const std::wstring& text)
{
    std::wstring folded = text;
    if (!folded.empty())
        CharLowerBuffW(folded.data(), static_cast<DWORD>(folded.size()));
    return folded;
}

std::vector<std::wstring> Recents::NamesIn(const std::wstring& directory) const
{
    std::vector<std::wstring> names;
    for (const RecentEntry& entry : entries)
        if (SamePath(ParentOf(entry.path), directory))
            names.push_back(FoldCase(LeafName(entry.path)));
    return names;
}

int Recents::ScoreFor(const std::wstring& path) const
{
    for (const RecentEntry& entry : entries)
        if (SamePath(entry.path, path))
            return entry.score;
    return -1;
}

bool Recents::SamePath(const std::wstring& a, const std::wstring& b)
{    if (a.size() != b.size())
        return false;
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool Recents::SameParent(const std::wstring& a, const std::wstring& b)
{
    return SamePath(ParentOf(a), ParentOf(b));
}

RecentEntry* Recents::FindMutable(const std::wstring& path)
{
    for (RecentEntry& entry : entries)
        if (SamePath(entry.path, path))
            return &entry;
    return nullptr;
}

void Recents::Touch(const std::wstring& path, int weight, bool isDirectory)
{
    if (path.empty() || weight <= 0)
        return;

    const Volume volume = IdentifyVolume(path);
    // `LocalDisk` registra solo i volumi locali fissi: e' il nome del modo.
    if (storeMode == StoreRecents::LocalDisk && !volume.fixed)
        return;

    const uint16_t gain = static_cast<uint16_t>(std::min(weight, static_cast<int>(kMaxScore)));

    RecentEntry* touched = FindMutable(path);
    if (touched == nullptr)
    {
        RecentEntry entry;
        entry.path        = path;
        entry.isDirectory = isDirectory;
        entry.uncRoot     = volume.uncRoot;
        entry.driveFixed  = volume.fixed;
        entry.driveSerial = volume.serial;
        entry.score       = gain;   // una voce nuova vale quanto la visita
        entry.decay       = 0;
        entry.ordinal     = ++ordinalCounter;
        entries.push_back(std::move(entry));
        touched = &entries.back();
    }
    else
    {
        // La visita riusa la voce: la somma satura a 10, il decadimento riparte da
        // zero, e l'ordinale dice che e' la visita piu' recente.
        touched->score       = static_cast<uint16_t>(std::min<int>(kMaxScore, touched->score + gain));
        touched->decay       = 0;
        touched->ordinal     = ++ordinalCounter;
        touched->isDirectory = isDirectory;
    }

    // I fratelli: stessa cartella, non la voce appena toccata. Prima si consuma il
    // credito di decadimento (dieci passaggi), poi ogni passaggio in piu' costa un
    // punto di punteggio. Una cartella che si smette di usare scende, ma non
    // sparisce.
    for (RecentEntry& entry : entries)
    {
        if (&entry == touched || !SameParent(entry.path, touched->path))
            continue;
        if (entry.decay < kMaxDecay)
            entry.decay = static_cast<uint16_t>(entry.decay + 1);
        else if (entry.score > 0)
            entry.score = static_cast<uint16_t>(entry.score - 1);
    }

    // Il tetto all'elenco: si butta la voce che vale meno, non quella arrivata per
    // ultima. `Ordinal` decide a pari punteggio, che e' la regola dell'ordine.
    if (entries.size() > kMaxEntries)
    {
        const std::vector<RecentEntry> ranked = Ranked();
        entries.assign(ranked.begin(), ranked.begin() + static_cast<ptrdiff_t>(kMaxEntries));
    }
}

std::vector<RecentEntry> Recents::Ranked() const
{
    std::vector<RecentEntry> ranked = entries;
    std::sort(ranked.begin(), ranked.end(), [](const RecentEntry& a, const RecentEntry& b)
    {
        if (a.score != b.score)
            return a.score > b.score;
        return a.ordinal > b.ordinal;
    });
    return ranked;
}

void Recents::Forget(const std::wstring& path)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&path](const RecentEntry& entry)
                                 { return SamePath(entry.path, path); }),
                  entries.end());
}

void Recents::Clear()
{
    entries.clear();
    ordinalCounter = 0;
}
}   // namespace Bolt
