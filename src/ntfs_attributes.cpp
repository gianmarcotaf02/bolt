#include "ntfs_attributes.h"

#include <winioctl.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace NtfsAttributes
{
namespace
{

// La firma di un record del MFT: i quattro byte "FILE". Un parser che non la
// controlla salterebbe il primo record corrotto letto male, ed e' per questo che
// l'assenza di questo immediato nel binario del riferimento e' la prova che il
// programma **non** scansiona il volume (`docs/01`).
constexpr uint32_t kRecordSignature = 0x454C4946;

constexpr uint32_t kAttributeEnd = 0xFFFFFFFF;

// Un settore logico di NTFS: la update sequence array protegge gli ultimi due byte
// di ognuno.
constexpr size_t kSector = 512;

// Il record minimo: testa del file record (0x30) piu' il marcatore di fine.
constexpr size_t kMinimumRecord = 0x30;

uint16_t U16(const uint8_t* data, size_t offset)
{
    return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t U32(const uint8_t* data, size_t offset)
{
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint64_t U64(const uint8_t* data, size_t offset)
{
    return static_cast<uint64_t>(U32(data, offset)) |
           (static_cast<uint64_t>(U32(data, offset + 4)) << 32);
}

// Da UTF-16LE a UTF-8: i nomi degli stream sono rari e corti, quindi la conversione
// si fa a mano invece di passare da `WideCharToMultiByte`, che vuole un buffer di
// dimensione calcolata.
std::string FromUtf16(const uint8_t* data, size_t characters)
{
    std::string out;
    for (size_t index = 0; index < characters; ++index)
    {
        const uint32_t unit = U16(data, index * 2);
        if (unit < 0x80)
        {
            out.push_back(static_cast<char>(unit));
        }
        else if (unit < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
    }
    return out;
}

} // namespace

std::string NameOf(uint32_t code)
{
    // La tabella `AT_*` del riferimento, in ordine di codice: 0x10, 0x20, ... 0x100.
    static const char* const kNames[] = {
        "Standard Information",   // 0x10
        "Attribute List",         // 0x20
        "File Name",              // 0x30
        "Object ID",              // 0x40
        "Security Descriptor",    // 0x50
        "Volume Name",            // 0x60
        "Volume Information",     // 0x70
        "Data",                   // 0x80
        "Index Root",             // 0x90
        "Index Allocation",       // 0xA0
        "Bitmap",                 // 0xB0
        "Reparse Point",          // 0xC0
        "EA Information",         // 0xD0  (l'unico che il riferimento non nomina)
        "EA",                     // 0xE0
        "Property Set",           // 0xF0
        "Logged Utility Stream",  // 0x100
    };

    // `AT_IndexRoot` e' l'elemento 8 della tabella del riferimento, ed e' 0x90:
    // l'indice e' `(codice >> 4) - 1`.
    if (code >= 0x10 && code <= 0x100 && (code & 0xF) == 0)
        return kNames[(code >> 4) - 1];

    char text[8];
    std::snprintf(text, sizeof(text), "0x%02X", code);
    return text;
}

bool ParseRecord(const uint8_t* record, size_t size, std::vector<Attribute>* out)
{
    out->clear();
    if (record == nullptr || size < kMinimumRecord)
        return false;

    // Il record si corregge in una copia: la update sequence array va rimessa al suo
    // posto, e il buffer che arriva da `DeviceIoControl` e' di chi lo passa.
    std::vector<uint8_t> bytes(record, record + size);

    if (U32(bytes.data(), 0) != kRecordSignature)
        return false;

    const size_t usaOffset = U16(bytes.data(), 0x04);
    const size_t usaCount  = U16(bytes.data(), 0x06);
    const size_t firstAttr = U16(bytes.data(), 0x14);
    const size_t usedSize  = U32(bytes.data(), 0x18);

    // La testa promette piu' di quanto sia arrivato: si usa quello che c'e'.
    size_t limit = size;
    if (usedSize >= kMinimumRecord && usedSize < limit)
        limit = usedSize;

    if (firstAttr < kMinimumRecord || firstAttr >= limit)
        return false;

    // Update sequence array: `usaCount` comprende la firma (che sta li' come
    // controllo), quindi i settori protetti sono `usaCount - 1`, e ognuno finisce
    // con i due byte che stanno qui.
    if (usaCount >= 2 && usaOffset + usaCount * 2 <= bytes.size())
    {
        for (size_t sectorIndex = 1; sectorIndex < usaCount; ++sectorIndex)
        {
            const size_t target = sectorIndex * kSector - 2;
            // L'IOCTL restituisce la parte **usata** del record, che non arriva
            // sempre in fondo all'ultimo settore: quello che manca non si inventa.
            if (target + 2 > bytes.size())
                break;
            bytes[target]     = bytes[usaOffset + sectorIndex * 2];
            bytes[target + 1] = bytes[usaOffset + sectorIndex * 2 + 1];
        }
    }

    size_t position = firstAttr;
    while (position + 8 <= limit)
    {
        const uint32_t code   = U32(bytes.data(), position);
        if (code == kAttributeEnd || code == 0)
            break;

        const uint32_t length = U32(bytes.data(), position + 4);
        // Sotto le 24 byte non esiste un attributo, e uscire dal record vuol dire
        // che la catena e' rotta: meta' elenco sembrerebbe un elenco completo.
        if (length < 0x18 || position + length > limit)
            return false;

        Attribute attribute;
        attribute.code = code;
        attribute.name = NameOf(code);

        const bool nonResident  = bytes[position + 8] != 0;
        const size_t nameLength = bytes[position + 9];
        const size_t nameOffset = U16(bytes.data(), position + 0x0A);
        attribute.resident      = !nonResident;

        if (nameLength > 0 && nameOffset + nameLength * 2 <= length)
            attribute.stream = FromUtf16(bytes.data() + position + nameOffset, nameLength);

        if (!nonResident)
        {
            if (length >= 0x18)
                attribute.size = U32(bytes.data(), position + 0x10);
        }
        else if (length >= 0x40)
        {
            // `RealSize` e' quanto vale il contenuto; `AllocatedSize` quanto il disco
            // ha riservato. La riga mostra il primo, che e' la domanda che si fa
            // guardando un file; il secondo e' quello che l'ispettore scrive gia'
            // come "Su disco".
            attribute.size = U64(bytes.data(), position + 0x30);
        }

        out->push_back(std::move(attribute));
        position += length;
    }

    // Un record senza attributi non e' un record: la testa c'e', il resto no.
    return !out->empty();
}

Result Read(const std::wstring& path)
{
    Result result;

    // Un percorso di rete non ha un volume raw da aprire: la domanda non si pone, e
    // rispondere "servono i privilegi" sarebbe falso.
    if (path.rfind(L"\\\\", 0) == 0)
        return result;

    wchar_t root[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), root, MAX_PATH))
        return result;

    wchar_t fileSystem[16]{};
    if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fileSystem, 16))
        return result;
    if (_wcsicmp(fileSystem, L"NTFS") != 0)
    {
        result.status = Status::NotNtfs;
        return result;
    }

    // L'identita' del file. `FileIdInfo` da' il riferimento a 64 bit del MFT: numero
    // di record nei 48 bit bassi, numero di sequenza nei 16 alti — che e' esattamente
    // la forma che il `FSCTL` vuole.
    uint64_t reference = 0;
    {
        const HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                                        nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return result;

        FILE_ID_INFO id{};
        if (GetFileInformationByHandleEx(file, FileIdInfo, &id, sizeof(id)))
        {
            // `FileId` e' un identificatore a 128 bit: su NTFS i primi 8 byte sono il
            // riferimento del MFT, gli altri sono zero. Si legge con `memcpy` e non
            // con un cast: `FILE_ID_128` e' un array di byte, e l'allineamento non e'
            // garantito.
            int64_t identity = 0;
            std::memcpy(&identity, id.FileId.Identifier, sizeof(identity));
            reference = static_cast<uint64_t>(identity);
        }
        else
        {
            BY_HANDLE_FILE_INFORMATION legacy{};
            if (GetFileInformationByHandle(file, &legacy))
                reference = (static_cast<uint64_t>(legacy.nFileIndexHigh) << 32) | legacy.nFileIndexLow;
        }
        CloseHandle(file);
    }
    if (reference == 0)
        return result;

    // Il volume raw. Il nome e' `\\.\C:`: la radice senza la barra finale.
    std::wstring device = std::wstring(L"\\\\.\\") + root;
    while (!device.empty() && device.back() == L'\\')
        device.pop_back();

    const HANDLE volume = CreateFileW(device.c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      0, nullptr);
    if (volume == INVALID_HANDLE_VALUE)
    {
        // La distinzione che conta per chi legge il pannello: "non hai i privilegi"
        // e' un'informazione utile, "non si apre" no.
        result.status = (GetLastError() == ERROR_ACCESS_DENIED) ? Status::NoPrivilege
                                                                : Status::Unavailable;
        return result;
    }

    NTFS_FILE_RECORD_INPUT_BUFFER input{};
    input.FileReferenceNumber.QuadPart = static_cast<LONGLONG>(reference);

    // Il record di un file sta in 1 KiB; il buffer si tiene largo perche' il record
    // puo' essere piu' grande (attributi non residenti, listati).
    std::vector<uint8_t> output(sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) + 8192, 0);
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(volume, FSCTL_GET_NTFS_FILE_RECORD, &input, sizeof(input),
                                    output.data(), static_cast<DWORD>(output.size()), &returned,
                                    nullptr);
    CloseHandle(volume);

    if (!ok)
    {
        result.status = (GetLastError() == ERROR_ACCESS_DENIED) ? Status::NoPrivilege
                                                                : Status::Unavailable;
        return result;
    }

    const NTFS_FILE_RECORD_OUTPUT_BUFFER* record =
        reinterpret_cast<const NTFS_FILE_RECORD_OUTPUT_BUFFER*>(output.data());
    const size_t header = offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer);
    if (returned <= header || record->FileRecordLength == 0 ||
        header + record->FileRecordLength > returned)
        return result;

    if (!ParseRecord(record->FileRecordBuffer, record->FileRecordLength, &result.attributes))
        return result;

    result.status = Status::Ok;
    return result;
}

} // namespace NtfsAttributes
