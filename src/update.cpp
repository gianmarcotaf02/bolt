#include "update.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "language.h"

#ifndef BOLT_VERSION
#define BOLT_VERSION "0.1.0"
#endif

namespace Bolt
{
namespace Update
{
// Il lavoro condiviso fra il thread e chi disegna. La forma e' quella di
// `FileOps::Job`: il thread riceve **questo**, non il `Job`, cosi' non dipende da
// quanto il chiamante tiene in vita l'oggetto.
struct JobState
{
    std::atomic<bool>     running{true};
    std::atomic<bool>     cancel{false};
    std::atomic<int>      phase{static_cast<int>(Phase::Checking)};
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> total{0};

    Result       result = Result::Failed;
    std::wstring error;
    ReleaseInfo  release;
};

namespace
{
// La repo e' questa: il comando esiste per aggiornare *questo* programma.
constexpr const wchar_t* kApiUrl =
    L"https://api.github.com/repos/gianmarcotaf02/bolt/releases/latest";

// **Solo per il banco di prova**: `BOLT_UPDATE_API` sposta l'indirizzo del foglio
// delle release su un server locale, cosi' il percorso download → SHA-256 → rifiuto
// si puo' provare senza pubblicare una release vera. Non cambia niente d'altro: il
// setup passa comunque dal confronto dell'impronta.
std::wstring ApiUrl()
{
    wchar_t buffer[1024]{};
    const DWORD length = GetEnvironmentVariableW(L"BOLT_UPDATE_API", buffer, 1024);
    if (length > 0 && length < 1024)
        return std::wstring(buffer, length);
    return kApiUrl;
}

// Un tetto per le risposte piccole (JSON e impronte): una risposta che supera
// questo non e' quella che ci si aspetta, e non va tenuta in memoria.
constexpr size_t kMaxSmallResponse = 4u * 1024u * 1024u;

constexpr DWORD kChunkBytes = 64u * 1024u;

struct HttpRequest
{
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;

    ~HttpRequest()
    {
        if (request != nullptr)
            WinHttpCloseHandle(request);
        if (connect != nullptr)
            WinHttpCloseHandle(connect);
        if (session != nullptr)
            WinHttpCloseHandle(session);
    }
};

bool IsCancelled(JobState* state)
{
    return state->cancel.load(std::memory_order_relaxed);
}

void SetPhase(JobState* state, Phase phase)
{
    state->phase.store(static_cast<int>(phase), std::memory_order_relaxed);
}

std::wstring LastErrorText(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0,
                                        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text;
    if (length != 0 && buffer != nullptr)
    {
        text.assign(buffer, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
            text.pop_back();
    }
    if (buffer != nullptr)
        LocalFree(buffer);
    return text;
}

// Apre una richiesta GET per |url| e prepara la risposta. Non e' il posto in cui si
// legge il corpo: due chiamanti lo vogliono in due modi — in memoria e su file — e
// le due cose che hanno in comune sono queste righe.
bool OpenRequest(const std::wstring& url, HttpRequest* http, DWORD* status,
                 uint64_t* contentLength, std::wstring* error)
{
    URL_COMPONENTS parts{};
    parts.dwStructSize     = sizeof(parts);
    parts.dwSchemeLength   = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength  = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (WinHttpCrackUrl(url.c_str(), 0, 0, &parts) == FALSE)
    {
        *error = TrW(L"L'indirizzo dell'aggiornamento non e' valido.",
                     L"The update address is not valid.");
        return false;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0 && parts.lpszExtraInfo != nullptr)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    const std::wstring agent = L"Bolt/" + CurrentVersion();
    http->session = WinHttpOpen(agent.c_str(),
                                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (http->session == nullptr)
    {
        *error = TrW(L"Connessione a GitHub non riuscita (errore ", L"Could not reach GitHub (error ")
                 + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    // Cinque secondi per connettersi, quindici per una risposta: un server che non
    // risponde non deve lasciare il comando appeso per sempre.
    WinHttpSetTimeouts(http->session, 5000, 5000, 15000, 15000);

    // Gli asset delle release rispondono con un rimando a un altro host: senza
    // questo, il download scaricherebbe la pagina del rimando.
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(http->session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    http->connect = WinHttpConnect(http->session, host.c_str(),
                                   parts.nPort, 0);
    if (http->connect == nullptr)
    {
        *error = TrW(L"Connessione a GitHub non riuscita (errore ", L"Could not reach GitHub (error ")
                 + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    http->request = WinHttpOpenRequest(http->connect, L"GET", path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (http->request == nullptr)
    {
        *error = TrW(L"Richiesta a GitHub non riuscita (errore ", L"GitHub request failed (error ")
                 + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    // User-Agent obbligatorio per l'API di GitHub, e il tipo di risposta che sappiamo
    // leggere. Nessun altro dato: non c'e' niente di nostro da mandare.
    const std::wstring headers = L"User-Agent: Bolt/" + CurrentVersion() +
                                 L"\r\nAccept: application/vnd.github+json\r\n";
    if (WinHttpSendRequest(http->request, headers.c_str(), static_cast<DWORD>(-1L),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE ||
        WinHttpReceiveResponse(http->request, nullptr) == FALSE)
    {
        *error = TrW(L"GitHub non ha risposto (errore ", L"GitHub did not answer (error ")
                 + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    DWORD code   = 0;
    DWORD size   = sizeof(code);
    if (WinHttpQueryHeaders(http->request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &size,
                            WINHTTP_NO_HEADER_INDEX) == FALSE)
    {
        *error = TrW(L"GitHub ha risposto in un modo che non si legge.",
                     L"GitHub answered in a way this program cannot read.");
        return false;
    }
    *status = code;

    *contentLength = 0;
    wchar_t lengthText[64]{};
    DWORD lengthSize = sizeof(lengthText);
    if (WinHttpQueryHeaders(http->request, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize,
                            WINHTTP_NO_HEADER_INDEX) != FALSE)
    {
        *contentLength = _wcstoui64(lengthText, nullptr, 10);
    }
    return true;
}

// Una risposta piccola, in memoria. |status| e' 0 se la richiesta non e' partita.
bool GetBytes(const std::wstring& url, size_t maxBytes, std::string* body, DWORD* status,
              std::wstring* error, JobState* state)
{
    HttpRequest http;
    uint64_t contentLength = 0;
    if (!OpenRequest(url, &http, status, &contentLength, error))
        return false;

    body->clear();
    if (*status != 200)
        return true;   // il chiamante decide cosa significa quel codice

    if (contentLength > maxBytes)
    {
        *error = TrW(L"La risposta di GitHub e' troppo grande.",
                     L"GitHub's answer is too large.");
        return false;
    }

    std::vector<char> buffer(kChunkBytes);
    uint64_t received = 0;
    for (;;)
    {
        if (IsCancelled(state))
        {
            *error = TrW(L"Annullato.", L"Cancelled.");
            return false;
        }

        DWORD read = 0;
        if (WinHttpReadData(http.request, buffer.data(), static_cast<DWORD>(buffer.size()),
                            &read) == FALSE)
        {
            *error = TrW(L"Lettura da GitHub non riuscita (errore ",
                         L"Reading from GitHub failed (error ")
                     + std::to_wstring(GetLastError()) + L").";
            return false;
        }
        if (read == 0)
            break;

        received += read;
        if (received > maxBytes)
        {
            *error = TrW(L"La risposta di GitHub e' troppo grande.",
                         L"GitHub's answer is too large.");
            return false;
        }
        body->append(buffer.data(), read);
    }
    return true;
}

// Il file, a pezzi, aggiornando il conto per chi disegna. Il file si crea qui e non
// si tocca il resto della cartella temporanea.
bool DownloadFile(const std::wstring& url, const std::wstring& path, Phase phase,
                  std::wstring* error, JobState* state)
{
    HttpRequest http;
    DWORD status = 0;
    uint64_t contentLength = 0;
    if (!OpenRequest(url, &http, &status, &contentLength, error))
        return false;

    if (status != 200)
    {
        *error = TrW(L"Il download dell'aggiornamento ha risposto ",
                     L"The update download answered ")
                 + std::to_wstring(status) + L".";
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        *error = TrW(L"Non si riesce a scrivere il file scaricato (errore ",
                     L"Cannot write the downloaded file (error ")
                 + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    SetPhase(state, phase);
    state->total.store(contentLength, std::memory_order_relaxed);
    state->received.store(0, std::memory_order_relaxed);

    std::vector<char> buffer(kChunkBytes);
    uint64_t received = 0;
    bool ok = true;
    for (;;)
    {
        if (IsCancelled(state))
        {
            *error = TrW(L"Annullato.", L"Cancelled.");
            ok = false;
            break;
        }

        DWORD read = 0;
        if (WinHttpReadData(http.request, buffer.data(), static_cast<DWORD>(buffer.size()),
                            &read) == FALSE)
        {
            *error = TrW(L"Lettura da GitHub non riuscita (errore ",
                         L"Reading from GitHub failed (error ")
                     + std::to_wstring(GetLastError()) + L").";
            ok = false;
            break;
        }
        if (read == 0)
            break;

        DWORD written = 0;
        if (WriteFile(file, buffer.data(), read, &written, nullptr) == FALSE || written != read)
        {
            *error = TrW(L"Scrittura del file scaricato non riuscita (errore ",
                         L"Writing the downloaded file failed (error ")
                     + std::to_wstring(GetLastError()) + L").";
            ok = false;
            break;
        }

        received += read;
        state->received.store(received, std::memory_order_relaxed);
    }

    CloseHandle(file);

    if (!ok)
    {
        DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

// --- JSON: il minimo che serve, e nient'altro ---------------------------------
//
// Non c'e' una libreria JSON qui, e non vale la pena aggiungerne una per tre campi
// di una risposta che conosciamo. Quello che si legge e' **una stringa dopo una
// chiave**, che e' l'unica forma che questa API usa per i dati che ci servono.

std::string Utf8FromCodePoint(unsigned int code)
{
    std::string out;
    if (code < 0x80)
    {
        out.push_back(static_cast<char>(code));
    }
    else if (code < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    return out;
}

// |quote| e' la posizione della virgoletta aperta.
std::string ReadJsonString(const std::string& json, size_t quote)
{
    std::string out;
    for (size_t i = quote + 1; i < json.size(); ++i)
    {
        const char c = json[i];
        if (c == '"')
            break;
        if (c != '\\')
        {
            out.push_back(c);
            continue;
        }
        if (++i >= json.size())
            break;
        const char escaped = json[i];
        if (escaped == 'u' && i + 4 < json.size())
        {
            const std::string hex = json.substr(i + 1, 4);
            const unsigned int code =
                static_cast<unsigned int>(std::strtoul(hex.c_str(), nullptr, 16));
            out += Utf8FromCodePoint(code);
            i += 4;
        }
        else if (escaped == '/' || escaped == '\\' || escaped == '"')
        {
            out.push_back(escaped);
        }
        else if (escaped == 'n')
        {
            out.push_back('\n');
        }
        else if (escaped == 't')
        {
            out.push_back('\t');
        }
    }
    return out;
}

bool StringField(const std::string& json, size_t keyPosition, std::string* value)
{
    const size_t colon = json.find(':', keyPosition);
    if (colon == std::string::npos)
        return false;
    const size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos)
        return false;
    *value = ReadJsonString(json, quote);
    return true;
}

bool EndsWith(const std::string& text, const std::string& suffix)
{
    if (text.size() < suffix.size())
        return false;
    return std::equal(suffix.begin(), suffix.end(), text.end() - static_cast<ptrdiff_t>(suffix.size()),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

struct Asset
{
    std::string name;
    std::string url;
};

// Gli asset che ci servono: il setup e le impronte. Il resto della release (sorgenti,
// zip portatile) non si scarica: il comando aggiorna **l'installazione**, e il
// pacchetto e' quello che sa farlo.
void FindAssets(const std::string& json, Asset* setup, Asset* sums)
{
    const std::string key = "\"browser_download_url\"";
    for (size_t pos = json.find(key); pos != std::string::npos; pos = json.find(key, pos + 1))
    {
        std::string url;
        if (!StringField(json, pos, &url))
            continue;

        const size_t namePos = json.rfind("\"name\"", pos);
        if (namePos == std::string::npos)
            continue;
        std::string name;
        if (!StringField(json, namePos, &name))
            continue;

        if (setup->url.empty() && EndsWith(name, "-setup.exe"))
        {
            setup->name = name;
            setup->url  = url;
        }
        else if (sums->url.empty() && (name == "SHA256SUMS" || name == "SHA256SUMS.txt"))
        {
            sums->name = name;
            sums->url  = url;
        }
    }
}

// L'impronta del setup, dal file delle impronte. Il formato e' quello di `sha256sum`:
// esadecimale, spazio, nome del file. Il nome puo' avere un asterisco davanti.
bool ExpectedHash(const std::string& sums, const std::string& assetName, std::string* hash)
{
    size_t start = 0;
    while (start < sums.size())
    {
        size_t end = sums.find('\n', start);
        if (end == std::string::npos)
            end = sums.size();
        std::string line = sums.substr(start, end - start);
        start = end + 1;

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const size_t space = line.find(' ');
        if (space == std::string::npos)
            continue;
        std::string digest = line.substr(0, space);
        std::string name   = line.substr(space + 1);
        while (!name.empty() && (name.front() == ' ' || name.front() == '*'))
            name.erase(name.begin());

        if (_stricmp(name.c_str(), assetName.c_str()) != 0)
            continue;
        if (digest.size() != 64)
            continue;
        *hash = digest;
        return true;
    }
    return false;
}

// --- le versioni -------------------------------------------------------------

std::vector<int> VersionParts(std::wstring version)
{
    while (!version.empty() && (version.front() == L'v' || version.front() == L'V'))
        version.erase(version.begin());

    std::vector<int> parts;
    int  current = 0;
    bool digits  = false;
    for (const wchar_t c : version)
    {
        if (c >= L'0' && c <= L'9')
        {
            current = current * 10 + (c - L'0');
            digits  = true;
        }
        else if (c == L'.' || c == L'-' || c == L'+')
        {
            parts.push_back(digits ? current : 0);
            current = 0;
            digits  = false;
        }
        else
        {
            break;
        }
    }
    parts.push_back(digits ? current : 0);
    return parts;
}

bool VersionGreater(const std::wstring& candidate, const std::wstring& current)
{
    const std::vector<int> a = VersionParts(candidate);
    const std::vector<int> b = VersionParts(current);
    const size_t count = (std::max)(a.size(), b.size());
    for (size_t i = 0; i < count; ++i)
    {
        const int left  = i < a.size() ? a[i] : 0;
        const int right = i < b.size() ? b[i] : 0;
        if (left != right)
            return left > right;
    }
    return false;
}

// --- SHA-256 -----------------------------------------------------------------

bool Sha256File(const std::wstring& path, std::string* hex, std::wstring* error)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash     = nullptr;
    std::vector<BYTE> object;
    BYTE digest[32]{};
    bool ok = false;

    do
    {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        {
            *error = TrW(L"SHA-256 non disponibile su questo sistema.",
                         L"SHA-256 is not available on this system.");
            break;
        }

        DWORD objectSize = 0;
        DWORD written    = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                              &written, 0) < 0)
        {
            *error = TrW(L"SHA-256 non disponibile su questo sistema.",
                         L"SHA-256 is not available on this system.");
            break;
        }

        object.resize(objectSize);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0)
        {
            *error = TrW(L"SHA-256 non disponibile su questo sistema.",
                         L"SHA-256 is not available on this system.");
            break;
        }

        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            *error = TrW(L"Il file scaricato non si apre (errore ",
                         L"The downloaded file cannot be opened (error ")
                     + std::to_wstring(GetLastError()) + L").";
            break;
        }

        std::vector<BYTE> buffer(kChunkBytes);
        bool readOk = true;
        for (;;)
        {
            DWORD read = 0;
            if (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == FALSE)
            {
                readOk = false;
                break;
            }
            if (read == 0)
                break;
            if (BCryptHashData(hash, buffer.data(), read, 0) < 0)
            {
                readOk = false;
                break;
            }
        }
        CloseHandle(file);

        if (!readOk)
        {
            *error = TrW(L"Lettura del file scaricato non riuscita.",
                         L"Reading the downloaded file failed.");
            break;
        }

        if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0)
        {
            *error = TrW(L"Il calcolo dell'impronta non e' riuscito.",
                         L"Computing the hash failed.");
            break;
        }

        static const char* digits = "0123456789abcdef";
        hex->clear();
        hex->reserve(64);
        for (const BYTE byte : digest)
        {
            hex->push_back(digits[byte >> 4]);
            hex->push_back(digits[byte & 0x0F]);
        }
        ok = true;
    } while (false);

    if (hash != nullptr)
        BCryptDestroyHash(hash);
    if (algorithm != nullptr)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

std::wstring UpdateDirectory()
{
    wchar_t temp[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, temp);
    if (length == 0 || length >= MAX_PATH)
        return std::wstring();

    std::wstring directory(temp, length);
    if (!directory.empty() && directory.back() != L'\\')
        directory += L'\\';
    directory += L"Bolt-update";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

// Il nome del file locale: quello dell'asset, ma senza separatori. Un nome che
// arrivasse da fuori con una barra dentro scriverebbe fuori dalla propria cartella.
std::wstring LocalNameOf(const std::string& assetName)
{
    std::wstring name(assetName.begin(), assetName.end());
    for (wchar_t& c : name)
        if (c == L'\\' || c == L'/')
            c = L'_';
    return name;
}

std::wstring WidenAscii(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

// --- il lavoro ---------------------------------------------------------------

void RunJob(const std::shared_ptr<JobState>& state)
{
    auto fail = [&state](const std::wstring& message) {
        state->error  = message;
        state->result = Result::Failed;
        state->running.store(false, std::memory_order_release);
    };

    SetPhase(state.get(), Phase::Checking);

    std::string body;
    DWORD status = 0;
    std::wstring error;
    if (!GetBytes(ApiUrl(), kMaxSmallResponse, &body, &status, &error, state.get()))
    {
        if (IsCancelled(state.get()))
        {
            state->result = Result::Cancelled;
            state->running.store(false, std::memory_order_release);
            return;
        }
        fail(error);
        return;
    }

    if (IsCancelled(state.get()))
    {
        state->result = Result::Cancelled;
        state->running.store(false, std::memory_order_release);
        return;
    }

    if (status == 404)
    {
        fail(TrW(L"Nessuna release e' stata pubblicata per gianmarcotaf02/bolt.",
                 L"No release has been published for gianmarcotaf02/bolt."));
        return;
    }
    if (status != 200)
    {
        fail(TrW(L"GitHub ha risposto ", L"GitHub answered ") + std::to_wstring(status) + L".");
        return;
    }

    std::wstring version;
    const size_t tagKey = body.find("\"tag_name\"");
    if (tagKey != std::string::npos)
    {
        std::string tag;
        if (StringField(body, tagKey, &tag))
            version = WidenAscii(tag);
    }
    if (version.empty())
    {
        fail(TrW(L"La release non dice la sua versione.",
                 L"The release does not say its version."));
        return;
    }

    Asset setup;
    Asset sums;
    FindAssets(body, &setup, &sums);
    if (setup.url.empty())
    {
        fail(TrW(L"La release non ha un setup da installare.",
                 L"The release has no setup to install."));
        return;
    }
    if (sums.url.empty())
    {
        fail(TrW(L"La release non pubblica le impronte SHA-256: non si installa niente.",
                 L"The release does not publish SHA-256 checksums: nothing is installed."));
        return;
    }

    if (!VersionGreater(version, CurrentVersion()))
    {
        state->release.version = version;
        state->result = Result::UpToDate;
        state->running.store(false, std::memory_order_release);
        return;
    }

    SetPhase(state.get(), Phase::Checksums);
    std::string sumsBody;
    DWORD sumsStatus = 0;
    if (!GetBytes(WidenAscii(sums.url), kMaxSmallResponse, &sumsBody, &sumsStatus, &error,
                  state.get()) ||
        sumsStatus != 200)
    {
        if (IsCancelled(state.get()))
        {
            state->result = Result::Cancelled;
            state->running.store(false, std::memory_order_release);
            return;
        }
        fail(error.empty() ? TrW(L"Il file delle impronte non si scarica.",
                                 L"The checksums file cannot be downloaded.")
                           : error);
        return;
    }

    std::string expected;
    if (!ExpectedHash(sumsBody, setup.name, &expected))
    {
        fail(TrW(L"Le impronte non contengono questo setup.",
                 L"The checksums do not cover this setup."));
        return;
    }

    const std::wstring directory = UpdateDirectory();
    if (directory.empty())
    {
        fail(TrW(L"Non c'e' una cartella temporanea dove scaricare.",
                 L"There is no temporary folder to download into."));
        return;
    }
    const std::wstring path = directory + L"\\" + LocalNameOf(setup.name);

    if (!DownloadFile(WidenAscii(setup.url), path, Phase::Downloading, &error, state.get()))
    {
        if (IsCancelled(state.get()))
        {
            state->result = Result::Cancelled;
            state->running.store(false, std::memory_order_release);
            return;
        }
        fail(error);
        return;
    }

    SetPhase(state.get(), Phase::Verifying);
    std::string actual;
    if (!Sha256File(path, &actual, &error))
    {
        DeleteFileW(path.c_str());
        fail(error);
        return;
    }

    if (_stricmp(actual.c_str(), expected.c_str()) != 0)
    {
        DeleteFileW(path.c_str());
        fail(TrW(L"L'impronta SHA-256 del setup non corrisponde a quella pubblicata: "
                 L"il file e' stato cancellato e non si installa.",
                 L"The setup's SHA-256 does not match the published one: "
                 L"the file was deleted and will not be installed."));
        return;
    }

    state->release.version   = version;
    state->release.setupPath = path;
    state->result = Result::Ready;
    state->running.store(false, std::memory_order_release);
}
} // namespace

// --- Job ---------------------------------------------------------------------

Job::Job(std::shared_ptr<JobState> state) : state_(std::move(state))
{
    std::shared_ptr<JobState> shared = state_;
    thread_ = std::thread([shared] { RunJob(shared); });
}

Job::~Job()
{
    if (state_ != nullptr)
        state_->cancel.store(true, std::memory_order_relaxed);
    if (thread_.joinable())
        thread_.join();
}

std::unique_ptr<Job> Job::Start()
{
    auto state = std::make_shared<JobState>();
    return std::unique_ptr<Job>(new Job(std::move(state)));
}

bool Job::Running() const
{
    return state_ != nullptr && state_->running.load(std::memory_order_acquire);
}

Snapshot Job::Progress() const
{
    Snapshot snapshot;
    if (state_ == nullptr)
        return snapshot;
    snapshot.phase    = static_cast<Phase>(state_->phase.load(std::memory_order_relaxed));
    snapshot.received = state_->received.load(std::memory_order_relaxed);
    snapshot.total    = state_->total.load(std::memory_order_relaxed);
    return snapshot;
}

void Job::Cancel()
{
    if (state_ != nullptr)
        state_->cancel.store(true, std::memory_order_relaxed);
}

Result Job::Finish(ReleaseInfo* release, std::wstring* error)
{
    if (thread_.joinable())
        thread_.join();

    if (state_ == nullptr)
        return Result::Failed;

    if (release != nullptr)
        *release = state_->release;
    if (error != nullptr)
        *error = state_->error;

    const Result result = state_->result;
    state_.reset();
    return result;
}

std::wstring CurrentVersion()
{
    return WidenAscii(BOLT_VERSION);
}
} // namespace Update
} // namespace Bolt
