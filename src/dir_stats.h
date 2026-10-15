// ---------------------------------------------------------------------------
// Recursive folder statistics.
//
// One folder's numbers are cheap; a folder tree's numbers are not, so this is a
// background service rather than a function. The shape follows the machine
// recovered from the reference binary (analysis/out/reverse/FINDINGS.md):
//
//   * a job per folder, not a synchronous call;
//   * a time budget per work slice, so one huge tree cannot monopolise a worker;
//   * partial results published while the job runs, so numbers grow on screen
//     instead of appearing only at the end;
//   * a generation per folder: invalidating a folder makes every in-flight job
//     for it stale, which is the reference application's `DAT_140249268`
//     comparison.
//
// The concurrency is split by volume profile, because the right amount of
// parallelism depends on the device (the reference application has
// Setting_CalcFolderSizesSSD, _HDD and _Network). The profile is taken from
// GetDriveTypeW plus the storage device's seek-penalty property, which is the
// documented way to tell an SSD from a spinning disk, and is the reason the
// reference binary imports DeviceIoControl.
//
// Enumeration asks the folder itself, in batches, with
// GetFileInformationByHandleEx(FileIdBothDirectoryInfo): that is the only shape
// that carries AllocationSize — the bytes the disk has actually reserved, which
// is not the same number as the file's length, and which FindFirstFileExW does
// not expose at all. One call per folder instead of one per file (which is what
// GetCompressedFileSizeW would cost). Where that interface is not answered —
// volumes that are not NTFS, some network shares — the enumeration falls back to
// FindFirstFileExW and the allocated number is then **not published**, instead of
// being guessed from the logical one.
//
// Reparse points are not followed: which one is a link is stated in the findings;
// following it would double count the target and can loop.
//
// Known limitation: hard links are counted once per name. Deduplicating them needs
// a file identifier, and while the batch interface does return one, keeping a set
// of identifiers for a tree of millions is a cost this service will not pay for a
// case it has never been asked to fix.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct DirStats
{
    uint64_t files        = 0;
    uint64_t folders      = 0;
    uint64_t logicalBytes = 0;

    // I byte che il disco ha davvero riservato — la differenza fra «quanto e'
    // lungo questo file» e «quanto spazio occupa». |allocatedKnown| e' falso
    // quando almeno una cartella dell'albero non ha saputo rispondere: allora il
    // numero e' un **minimo**, e chi lo mostra deve dirlo invece di farlo passare
    // per un totale.
    uint64_t allocatedBytes = 0;
    bool     allocatedKnown = false;

    bool     present      = false;   // an entry exists for this folder
    bool     partial      = false;   // still being counted, the numbers grow
    bool     complete     = false;   // reached the end of the tree
};

enum class VolumeProfile
{
    Ssd,       // fixed disk without seek penalty
    Hdd,       // fixed disk with seek penalty
    Network,   // remote or mapped
    Removable, // USB stick, memory card
    Unknown,
};

class DirStatsService
{
public:
    DirStatsService();
    ~DirStatsService();

    DirStatsService(const DirStatsService&) = delete;
    DirStatsService& operator=(const DirStatsService&) = delete;

    void Start();
    void Stop();

    // Render thread. Queues the folder when nothing is known about it yet.
    // Cheap and safe to call every frame: a folder already queued, already
    // counting, or already complete is left alone.
    void Request(const std::wstring& path);

    // Render thread. Copies the current snapshot. False when nothing is known.
    bool TryGet(const std::wstring& path, DirStats* out) const;

    // Drops a cached result and makes any in-flight job for it stale.
    void Invalidate(const std::wstring& path);

    // La stessa cosa, ma **per il percorso e per ogni suo genitore**: serve a chi
    // scopre che qualcosa e' cambiato dentro una cartella. Il totale di una
    // cartella contiene quello delle sue sottocartelle, quindi un file cambiato in
    // fondo rende vecchi i numeri di tutto quello che lo contiene — e sono quelli
    // che si vedono dal genitore.
    void InvalidateIncludingParents(const std::wstring& path);

    // Forgets every result and drops all queued work. Used by refresh.
    void ClearAll();

    // Drops queued and in-flight work but keeps completed results. Called when
    // the panel changes folder, so leaving a large folder does not leave the
    // workers counting trees that are no longer on screen.
    void CancelQueued();

    int  ActiveJobs() const;
    bool Busy() const { return ActiveJobs() > 0; }

    // Increases on every published result, partial or final. The view uses it to
    // notice that a grouping which depends on the statistics may need rebuilding.
    uint64_t Revision() const;

    static VolumeProfile ProfileFor(const std::wstring& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
