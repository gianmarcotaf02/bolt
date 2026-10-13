#include "text_view.h"

#include "language.h"

#include <windows.h>

#include <algorithm>

namespace TextView
{
namespace
{

// Oltre questo si legge solo l'inizio. Non e' un limite tecnico ma una scelta: una
// lettura sincrona di un file enorme blocca la finestra, e il visualizzatore
// dell'inspector non e' un editor.
constexpr uint64_t kReadLimit = 1024 * 1024;

// Un file con questi caratteri in mezzo non e' testo. Si contano su un campione:
// il due per cento di caratteri di controllo e' rumore per un testo, e' normale per
// un formato binario.
bool LooksLikeBinary(const std::wstring& text)
{
    if (text.empty())
        return false;

    size_t control = 0;
    for (const wchar_t c : text)
    {
        if (c == L'\t' || c == L'\n' || c == L'\r')
            continue;
        if (c < 0x20 || c == 0x7F)
            ++control;
    }
    return control * 50 > text.size();   // oltre il 2%
}

// UTF-16 senza marcatore d'ordine: si riconosce dall'alternanza. In un testo
// prevalentemente ASCII ogni secondo byte e' zero, e in un binario no.
bool LooksLikeUtf16Le(const std::vector<char>& raw, bool* bigEndian)
{
    if (raw.size() < 4)
        return false;

    const size_t sample = (std::min)(raw.size(), static_cast<size_t>(4096)) & ~static_cast<size_t>(1);
    size_t oddZeros = 0;
    size_t pairs = 0;
    for (size_t i = 0; i + 1 < sample; i += 2)
    {
        ++pairs;
        if (raw[i + 1] == 0 && raw[i] != 0)
            ++oddZeros;
    }
    if (pairs > 0 && oddZeros * 10 > pairs * 8)
    {
        *bigEndian = false;
        return true;
    }

    size_t evenZeros = 0;
    for (size_t i = 0; i + 1 < sample; i += 2)
    {
        if (raw[i] == 0 && raw[i + 1] != 0)
            ++evenZeros;
    }
    if (pairs > 0 && evenZeros * 10 > pairs * 8)
    {
        *bigEndian = true;
        return true;
    }
    return false;
}

std::wstring WideFromUtf8(const std::vector<char>& raw, size_t offset, bool* valid)
{
    const int length = static_cast<int>(raw.size() - offset);
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data() + offset,
                                           length, nullptr, 0);
    if (needed <= 0)
    {
        *valid = false;
        return std::wstring();
    }

    std::wstring text(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data() + offset, length, text.data(),
                        needed);
    *valid = true;
    return text;
}

std::wstring WideFromAnsi(const std::vector<char>& raw, size_t offset)
{
    const int length = static_cast<int>(raw.size() - offset);
    if (length <= 0)
        return std::wstring();

    const int needed = MultiByteToWideChar(CP_ACP, 0, raw.data() + offset, length, nullptr, 0);
    std::wstring text(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_ACP, 0, raw.data() + offset, length, text.data(), needed);
    return text;
}

std::wstring WideFromUtf16(const std::vector<char>& raw, size_t offset, bool bigEndian)
{
    std::wstring text;
    text.reserve((raw.size() - offset) / 2);
    for (size_t i = offset; i + 1 < raw.size(); i += 2)
    {
        const unsigned char low  = static_cast<unsigned char>(raw[i]);
        const unsigned char high = static_cast<unsigned char>(raw[i + 1]);
        const wchar_t unit = bigEndian
                                 ? static_cast<wchar_t>((low << 8) | high)
                                 : static_cast<wchar_t>((high << 8) | low);
        text.push_back(unit);
    }
    return text;
}

std::string Utf8FromWide(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed,
                        nullptr, nullptr);
    return out;
}

} // namespace

const char* EncodingName(Encoding encoding)
{
    switch (encoding)
    {
        case Encoding::Utf8:     return "UTF-8";
        case Encoding::Utf8Bom:  return "UTF-8 (BOM)";
        case Encoding::Utf16Le:  return "UTF-16 LE";
        case Encoding::Utf16Be:  return "UTF-16 BE";
        case Encoding::Ansi:     return "ANSI";
        case Encoding::Binary:   return Tr("binario", "binary");
    }
    return "?";
}

Document Load(const std::wstring& path)
{
    Document document;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return document;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == FALSE || size.QuadPart == 0)
    {
        CloseHandle(file);
        return document;
    }
    document.byteSize  = static_cast<uint64_t>(size.QuadPart);
    document.truncated = document.byteSize > kReadLimit;

    const DWORD toRead = static_cast<DWORD>((std::min)(document.byteSize, kReadLimit));
    std::vector<char> raw(toRead);
    DWORD read = 0;
    const BOOL ok = ReadFile(file, raw.data(), toRead, &read, nullptr);
    CloseHandle(file);
    if (ok == FALSE)
        return document;
    raw.resize(read);

    // --- la codifica --------------------------------------------------------
    size_t offset = 0;
    bool bigEndian = false;
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF)
    {
        document.encoding = Encoding::Utf8Bom;
        offset = 3;
    }
    else if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF &&
             static_cast<unsigned char>(raw[1]) == 0xFE)
    {
        document.encoding = Encoding::Utf16Le;
        offset = 2;
    }
    else if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE &&
             static_cast<unsigned char>(raw[1]) == 0xFF)
    {
        document.encoding = Encoding::Utf16Be;
        offset = 2;
    }
    else if (LooksLikeUtf16Le(raw, &bigEndian))
    {
        document.encoding = bigEndian ? Encoding::Utf16Be : Encoding::Utf16Le;
    }

    // --- il testo -----------------------------------------------------------
    std::wstring text;
    if (document.encoding == Encoding::Utf16Le || document.encoding == Encoding::Utf16Be)
    {
        text = WideFromUtf16(raw, offset, document.encoding == Encoding::Utf16Be);
    }
    else
    {
        bool valid = false;
        text = WideFromUtf8(raw, offset, &valid);
        if (!valid)
        {
            // Non e' UTF-8: si prova la codifica locale, che e' quello che fa un
            // editor quando gli si dice "apri e basta".
            text = WideFromAnsi(raw, offset);
            document.encoding = Encoding::Ansi;
        }
    }

    if (LooksLikeBinary(text))
    {
        document.encoding = Encoding::Binary;
        return document;
    }

    // --- le righe -----------------------------------------------------------
    // Tre terminatori, e possono convivere: si contano per dirlo, e si spezza su
    // tutti e tre.
    size_t crlf = 0;
    size_t lf = 0;
    size_t cr = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'\r')
        {
            if (i + 1 < text.size() && text[i + 1] == L'\n')
            {
                ++crlf;
                ++i;
            }
            else
            {
                ++cr;
            }
        }
        else if (text[i] == L'\n')
        {
            ++lf;
        }
    }
    if (crlf > 0 && lf == 0 && cr == 0)      document.endings = "CRLF";
    else if (lf > 0 && crlf == 0 && cr == 0) document.endings = "LF";
    else if (cr > 0 && crlf == 0 && lf == 0) document.endings = "CR";
    else if (crlf + lf + cr > 0)             document.endings = Tr("misto", "mixed");

    const std::string utf8 = Utf8FromWide(text);
    size_t start = 0;
    for (size_t i = 0; i <= utf8.size(); ++i)
    {
        const bool end = (i == utf8.size());
        if (!end && utf8[i] != '\n' && utf8[i] != '\r')
            continue;

        if (!end && utf8[i] == '\r' && i + 1 < utf8.size() && utf8[i + 1] == '\n')
            ++i;
        document.lines.push_back(utf8.substr(start, (end ? i : i) - start));
        start = i + 1;
    }

    // Un file che finisce con un terminatore non ha una riga vuota in fondo: quella
    // e' la riga vuota che tutti gli editor mostrano, e mostrarla anche qui sarebbe
    // contare una riga che non c'e'.
    if (document.lines.size() > 1 && document.lines.back().empty())
        document.lines.pop_back();

    document.isText = true;
    return document;
}

} // namespace TextView
