#include "dir_stats.h"

#include <windows.h>
#include <winioctl.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kProfileCount = 5;

// Workers and time slice per volume profile. An SSD tolerates many concurrent
// readers because seeks are free; a spinning disk wants exactly one, or the
// reads turn into random seeks; on a network the per-entry latency dominates, so
// fewer workers and a longer slice avoid flooding the provider.
struct LaneConfig
{
    int workers;
    int sliceMs;
};

constexpr LaneConfig kLaneConfig[kProfileCount] = {
    {3,  8},   // Ssd
    {1, 25},   // Hdd
    {2, 40},   // Network
    {1, 20},   // Removable
    {1, 16},   // Unknown
};

struct DirStatsJob
{
    std::wstring  path;
    uint64_t      token   = 0;
    VolumeProfile profile = VolumeProfile::Unknown;

    // Resume state, carried across slices. |pending| is the stack of directories
    // still to visit; LIFO keeps the traversal depth-first, so a partial result
    // covers whole subtrees rather than a slice through the whole tree.
    std::vector<std::wstring> pending;
    uint64_t files        = 0;
    uint64_t folders      = 0;
    uint64_t logicalBytes = 0;
    bool     started      = false;
};

int ProfileIndex(VolumeProfile profile)
{
    switch (profile)
    {
        case VolumeProfile::Ssd:       return 0;
        case VolumeProfile::Hdd:       return 1;
        case VolumeProfile::Network:   return 2;
        case VolumeProfile::Removable: return 3;
        default:                       return 4;
    }
}

double SecondsSince(const LARGE_INTEGER& start, const LARGE_INTEGER& frequency)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - start.QuadPart) /
           static_cast<double>(frequency.QuadPart);
}

// The storage device's own answer to "do you pay for seeking". This is the
// documented mechanism and the reason the reference binary imports
// DeviceIoControl; asking the volume cache, not the disk, keeps it cheap.
bool VolumeIncursSeekPenalty(const std::wstring& root)
{
    HANDLE volume = CreateFileW(root.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE)
        return false;

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType  = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
    DWORD returned = 0;
    bool penalty = false;
    if (DeviceIoControl(volume, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                        &descriptor, sizeof(descriptor), &returned, nullptr) != FALSE)
        penalty = descriptor.IncursSeekPenalty != FALSE;

    CloseHandle(volume);
    return penalty;
}

} // namespace

struct DirStatsService::Impl
{
    struct Lane
    {
        std::deque<DirStatsJob>  queue;
        std::mutex               mutex;
        std::condition_variable  cv;
        std::vector<std::thread> threads;
    };

    std::array<Lane, kProfileCount> lanes;

    mutable std::mutex mapMutex;
    std::unordered_map<std::wstring, DirStats> entries;
    // Presence of a path here means "queued or being counted". The value is the
    // token of the job that owns it; a job whose token no longer matches is
    // stale and must not publish.
    std::unordered_map<std::wstring, uint64_t> tokens;
    uint64_t nextToken = 1;

    std::atomic<int>  active{0};
    std::atomic<bool> stopping{false};
    std::atomic<uint64_t> revision{0};

    void WorkerLoop(int laneIndex);
    bool RunSlice(DirStatsJob& job, int sliceMs, bool* stale);
    bool IsStale(const DirStatsJob& job);
    void PublishPartial(const DirStatsJob& job);
    void PublishFinal(const DirStatsJob& job);
};

void DirStatsService::Impl::WorkerLoop(int laneIndex)
{
    Lane& lane = lanes[laneIndex];
    const int sliceMs = kLaneConfig[laneIndex].sliceMs;

    for (;;)
    {
        DirStatsJob job;
        {
            std::unique_lock<std::mutex> lock(lane.mutex);
            lane.cv.wait(lock, [this, &lane] { return stopping.load() || !lane.queue.empty(); });
            if (stopping.load() && lane.queue.empty())
                break;
            job = std::move(lane.queue.front());
            lane.queue.pop_front();
        }

        bool stale = false;
        const bool finished = RunSlice(job, sliceMs, &stale);

        if (stale)
        {
            --active;
            continue;
        }

        if (finished)
        {
            PublishFinal(job);
            --active;
            continue;
        }

        // Budget exhausted: keep the state and go to the back of the queue, so
        // other folders make progress before this one resumes.
        {
            std::lock_guard<std::mutex> lock(lane.mutex);
            lane.queue.push_back(std::move(job));
        }
        lane.cv.notify_one();
    }
}

bool DirStatsService::Impl::IsStale(const DirStatsJob& job)
{
    std::lock_guard<std::mutex> lock(mapMutex);
    const auto it = tokens.find(job.path);
    return it == tokens.end() || it->second != job.token;
}

bool DirStatsService::Impl::RunSlice(DirStatsJob& job, int sliceMs, bool* stale)
{
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const double budget = sliceMs / 1000.0;

    if (!job.started)
    {
        job.pending.push_back(job.path);
        job.started = true;
    }

    while (!job.pending.empty())
    {
        std::wstring directory = std::move(job.pending.back());
        job.pending.pop_back();

        const std::wstring pattern = directory + L"\\*";
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                       FindExSearchNameMatch, nullptr,
                                       FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE)
        {
            // Not every provider accepts the large-fetch hint.
            find = FindFirstFileW(pattern.c_str(), &data);
        }

        if (find != INVALID_HANDLE_VALUE)
        {
            unsigned sincePublish = 0;
            do
            {
                const wchar_t* name = data.cFileName;
                if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0')))
                    continue;

                const DWORD attributes = data.dwFileAttributes;

                // A reparse point is a link. Following it would count the target
                // twice, and a cycle would never end, so it is counted as neither
                // a file nor a folder.
                if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    continue;

                if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    ++job.folders;
                    std::wstring child = directory;
                    child += L'\\';
                    child += name;
                    job.pending.push_back(std::move(child));
                }
                else
                {
                    ++job.files;
                    job.logicalBytes += (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
                                        data.nFileSizeLow;
                }

                // A single directory can hold hundreds of thousands of entries,
                // which would be a long silence with a per-directory budget
                // check. Publishing inside the loop keeps the numbers moving.
                if ((++sincePublish & 0xFFFu) == 0)
                {
                    if (IsStale(job))
                    {
                        *stale = true;
                        return false;
                    }
                    PublishPartial(job);
                }
            }
            while (FindNextFileW(find, &data) != FALSE);

            FindClose(find);
        }

        if (IsStale(job))
        {
            *stale = true;
            return false;
        }

        if (SecondsSince(start, frequency) >= budget)
        {
            PublishPartial(job);
            return false;
        }
    }

    return true;
}

void DirStatsService::Impl::PublishPartial(const DirStatsJob& job)
{
    std::lock_guard<std::mutex> lock(mapMutex);
    const auto it = tokens.find(job.path);
    if (it == tokens.end() || it->second != job.token)
        return;

    DirStats stats;
    stats.files        = job.files;
    stats.folders      = job.folders;
    stats.logicalBytes = job.logicalBytes;
    stats.present      = true;
    stats.partial      = true;
    stats.complete     = false;
    entries[job.path] = stats;
    ++revision;
}

void DirStatsService::Impl::PublishFinal(const DirStatsJob& job)
{
    std::lock_guard<std::mutex> lock(mapMutex);
    const auto it = tokens.find(job.path);
    if (it == tokens.end() || it->second != job.token)
        return;

    DirStats stats;
    stats.files        = job.files;
    stats.folders      = job.folders;
    stats.logicalBytes = job.logicalBytes;
    stats.present      = true;
    stats.partial      = false;
    stats.complete     = true;
    entries[job.path] = stats;
    tokens.erase(it);
    ++revision;
}

DirStatsService::DirStatsService() : impl_(std::make_unique<Impl>()) {}

DirStatsService::~DirStatsService()
{
    Stop();
}

void DirStatsService::Start()
{
    impl_->stopping = false;
    for (int lane = 0; lane < kProfileCount; ++lane)
    {
        for (int worker = 0; worker < kLaneConfig[lane].workers; ++worker)
            impl_->lanes[lane].threads.emplace_back([this, lane] { impl_->WorkerLoop(lane); });
    }
}

void DirStatsService::Stop()
{
    impl_->stopping = true;
    for (auto& lane : impl_->lanes)
        lane.cv.notify_all();

    for (auto& lane : impl_->lanes)
    {
        for (auto& thread : lane.threads)
        {
            if (thread.joinable())
                thread.join();
        }
        lane.threads.clear();
    }

    for (auto& lane : impl_->lanes)
    {
        std::lock_guard<std::mutex> lock(lane.mutex);
        lane.queue.clear();
    }
}

void DirStatsService::Request(const std::wstring& path)
{
    std::lock_guard<std::mutex> lock(impl_->mapMutex);

    if (impl_->tokens.count(path) != 0)
        return;

    const auto existing = impl_->entries.find(path);
    if (existing != impl_->entries.end() && existing->second.complete)
        return;

    const uint64_t token = impl_->nextToken++;
    impl_->tokens[path] = token;
    ++impl_->active;

    DirStatsJob job;
    job.path    = path;
    job.token   = token;
    job.profile = DirStatsService::ProfileFor(path);

    const int laneIndex = ProfileIndex(job.profile);
    {
        std::lock_guard<std::mutex> laneLock(impl_->lanes[laneIndex].mutex);
        impl_->lanes[laneIndex].queue.push_back(std::move(job));
    }
    impl_->lanes[laneIndex].cv.notify_one();
}

bool DirStatsService::TryGet(const std::wstring& path, DirStats* out) const
{
    std::lock_guard<std::mutex> lock(impl_->mapMutex);
    const auto it = impl_->entries.find(path);
    if (it == impl_->entries.end() || !it->second.present)
        return false;
    if (out != nullptr)
        *out = it->second;
    return true;
}

void DirStatsService::Invalidate(const std::wstring& path)
{
    std::lock_guard<std::mutex> lock(impl_->mapMutex);
    impl_->tokens.erase(path);
    impl_->entries.erase(path);
}

void DirStatsService::ClearAll()
{
    {
        std::lock_guard<std::mutex> lock(impl_->mapMutex);
        impl_->entries.clear();
    }
    CancelQueued();
}

void DirStatsService::CancelQueued()
{
    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mapMutex);
        impl_->tokens.clear();

        // A partial entry whose job is gone would stay underlined for ever; it
        // is dropped, while a completed result is kept for when the user returns.
        for (auto it = impl_->entries.begin(); it != impl_->entries.end();)
        {
            if (it->second.partial && !it->second.complete)
                it = impl_->entries.erase(it);
            else
                ++it;
        }
    }

    for (auto& lane : impl_->lanes)
    {
        std::lock_guard<std::mutex> lock(lane.mutex);
        dropped += lane.queue.size();
        lane.queue.clear();
    }

    impl_->active -= static_cast<int>(dropped);
    if (impl_->active.load() < 0)
        impl_->active = 0;
}

int DirStatsService::ActiveJobs() const
{
    return impl_->active.load();
}

uint64_t DirStatsService::Revision() const
{
    return impl_->revision.load();
}

VolumeProfile DirStatsService::ProfileFor(const std::wstring& path)
{
    // The answer depends only on the drive letter, and asking costs a handle and
    // an ioctl, so it is cached per drive for the life of the process.
    static std::mutex cacheMutex;
    static std::unordered_map<wchar_t, VolumeProfile> cache;

    if (path.size() < 2 || path[1] != L':')
        return VolumeProfile::Unknown;

    const wchar_t drive = static_cast<wchar_t>(towupper(path[0]));

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto it = cache.find(drive);
        if (it != cache.end())
            return it->second;
    }

    const wchar_t root[4] = {drive, L':', L'\\', L'\0'};
    VolumeProfile profile = VolumeProfile::Unknown;
    switch (GetDriveTypeW(root))
    {
        case DRIVE_REMOTE:
            profile = VolumeProfile::Network;
            break;
        case DRIVE_REMOVABLE:
            profile = VolumeProfile::Removable;
            break;
        case DRIVE_FIXED:
            profile = VolumeIncursSeekPenalty(root) ? VolumeProfile::Hdd : VolumeProfile::Ssd;
            break;
        default:
            profile = VolumeProfile::Unknown;
            break;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);
    cache[drive] = profile;
    return profile;
}
