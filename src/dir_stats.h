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
// Enumeration uses FindFirstFileExW with FindExInfoBasic and
// FIND_FIRST_EX_LARGE_FETCH, falling back to FindFirstFileW on providers that
// reject the large fetch. Reparse points are not followed: which one is a link
// is stated in the findings; following it would double count the target and can
// loop.
//
// Known limitation: hard links are counted once per name, because deduplicating
// them needs a file identifier per entry and the directory enumeration does not
// provide one.
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
