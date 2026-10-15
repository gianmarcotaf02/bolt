#include "diag.h"

#include <windows.h>

#include <mutex>
#include <string>

bool DiagEnabled()
{
    static const bool enabled = []()
    {
        wchar_t value[8]{};
        return GetEnvironmentVariableW(L"BOLT_DIAG", value, 8) > 0;
    }();
    return enabled;
}

bool DevHudEnabled()
{
    static const bool enabled = []()
    {
        wchar_t value[8]{};
        return GetEnvironmentVariableW(L"BOLT_DEV_HUD", value, 8) > 0;
    }();
    return enabled;
}

void DiagLog(const std::string& line)
{
    if (!DiagEnabled())
        return;

    // Le righe le scrivono ormai **due thread** — quello della finestra e quello che
    // sorveglia le cartelle — e il buffer e' uno solo: senza questa protezione due
    // chiamate che si sovrappongono scrivono nello stesso `std::string` mentre
    // l'altro lo legge, che e' un difetto silenzioso e ogni tanto un guasto.
    // Il file si apre e si riscrive dentro la stessa sezione critica, cosi' due
    // thread non si contendono nemmeno il file.
    static std::mutex guard;
    std::lock_guard<std::mutex> lock(guard);

    static std::string buffer;
    buffer += line;
    buffer += "\r\n";

    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0)
        return;

    std::wstring file = modulePath;
    const size_t slash = file.find_last_of(L'\\');
    file = (slash == std::wstring::npos) ? L"bolt-diag.log"
                                         : file.substr(0, slash + 1) + L"bolt-diag.log";

    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &written, nullptr);
    CloseHandle(handle);
}

std::string DiagClock()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char text[16]{};
    sprintf_s(text, "%02u:%02u:%02u.%03u", time.wHour, time.wMinute, time.wSecond,
              time.wMilliseconds);
    return text;
}
