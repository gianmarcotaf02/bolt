// ---------------------------------------------------------------------------
// L'ispettore: i metadati di una voce, la sua anteprima, e cosa c'e' dentro una
// cartella.
//
// Era in `main.cpp`, dentro il namespace anonimo, ed era il pezzo di vista piu'
// grosso rimasto li' dopo la lista. Qui ci sta quello che si vede nel pannello di
// destra: le righe dei metadati, l'immagine grande decodificata, il visualizzatore
// di testo, e il contenuto della cartella scelta.
//
// Due funzioni escono da qui, e sono due perche' due sono i chiamanti:
//
//   * `DrawInspector` — il pannello, chiamato dal fotogramma;
//   * `BuildInspectorRows` — le righe, chiamate anche dal pannello delle proprieta',
//     che e' la **stessa** informazione in un'altra finestra: due calcoli separati
//     mostrerebbero prima o poi due numeri diversi.
//
// La direzione delle dipendenze e' quella degli altri moduli di vista: questo file
// include `app_state.h`, `app_ui.h` e `view_list.h` — per il cursore, il percorso e
// l'icona di una voce, che sono domande della lista — e non conosce ne' i menu ne' i
// comandi.
// ---------------------------------------------------------------------------
#include "view_inspector.h"

#include "app_ui.h"
#include "image_preview.h"
#include "language.h"
#include "shell_location.h"
#include "text_view.h"
#include "view_list.h"

// Dopo windows.h, come vuole la SDK: le PROPERTYKEY si appoggiano ai tipi di
// sistema. Servono per le proprieta' che il file system non dice.
#include <windows.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace Bolt
{
namespace
{
// Le proprieta' che il file system non dice — le dimensioni di un'immagine — lette
// da `IPropertyStore` e tenute per percorso: si chiedono una volta sola, e la stessa
// voce resta scelta finche' il cursore non si sposta.
const FileProperties& PropertiesOf(AppState& app, const std::wstring& path)
{
    const auto cached = app.fileProperties.find(path);
    if (cached != app.fileProperties.end())
        return cached->second;

    FileProperties properties;
    IPropertyStore* store = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT,
                                                    IID_IPropertyStore,
                                                    reinterpret_cast<void**>(&store))) &&
        store != nullptr)
    {
        PROPVARIANT value;
        ULONG number = 0;

        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_Image_HorizontalSize, &value)) &&
            SUCCEEDED(PropVariantToUInt32(value, &number)))
            properties.imageWidth = number;
        PropVariantClear(&value);

        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_Image_VerticalSize, &value)) &&
            SUCCEEDED(PropVariantToUInt32(value, &number)))
            properties.imageHeight = number;
        PropVariantClear(&value);

        store->Release();
    }

    return app.fileProperties.emplace(path, properties).first->second;
}
// Gli attributi NTFS di un file, una volta per percorso. La lettura apre il volume
// raw, e senza privilegi di amministratore non riesce: anche il **motivo** si tiene,
// altrimenti a ogni fotogramma si riproverebbe un'apertura destinata a fallire.
const NtfsAttributes::Result& NtfsOf(AppState& app, const std::wstring& path)
{
    const auto cached = app.ntfsAttributes.find(path);
    if (cached != app.ntfsAttributes.end())
        return cached->second;
    return app.ntfsAttributes.emplace(path, NtfsAttributes::Read(path)).first->second;
}

// Quanto spazio il disco ha riservato per **un** file. Una chiamata, e solo dove
// serve per una voce sola: per un albero sarebbe una chiamata per file, ed e' per
// questo che `dir_stats` legge le cartelle in blocco.
//
// Il campo e' `FILE_STANDARD_INFO.AllocationSize` — **lo stesso** che l'enumerazione
// in blocco legge per ogni voce, quindi le due strade non possono raccontare cose
// diverse. Non si usa `GetCompressedFileSizeW`, che sarebbe la chiamata ovvia: su
// questa macchina risponde con la dimensione **logica** (10240 per un file da 10240,
// che occupa 12288), e un "su disco" uguale alla dimensione e' una funzione che
// sembra non fare niente.
bool AllocatedSizeOf(const std::wstring& path, uint64_t* bytes)
{
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    FILE_STANDARD_INFO info{};
    const bool read = GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(info)) != FALSE;
    CloseHandle(handle);

    if (!read)
        return false;
    *bytes = static_cast<uint64_t>(info.AllocationSize.QuadPart);
    return true;
}

// L'immagine grande dell'inspector: si decodifica quando cambia la voce scelta, e si
// carica nello strato una volta sola. La miniatura della shell arriva a 128 px e va
// bene per un'icona; qui il file si decodifica davvero, e ingrandirlo e' poi solo
// geometria di un quad.
//
// Si prova a decodificare solo per le estensioni che sono immagini: tentare su ogni
// file vorrebbe dire far aprire a WIC anche quello che non e' un'immagine, e su un
// JPEG da dieci megapixel la decodifica si sente.
bool LooksLikeImage(const std::string& extension)
{
    static const char* const kImageExtensions[] = {
        "jpg", "jpeg", "jpe", "png", "bmp", "gif", "tif", "tiff", "webp", "avif", "heic", "heif",
        "jfif", "ico",
    };
    for (const char* candidate : kImageExtensions)
    {
        if (extension == candidate)
            return true;
    }
    return false;
}

void EnsureBigImage(AppState& app)
{
    const int cursorIndex = CursorIndex(app);
    std::wstring path;
    if (cursorIndex >= 0)
    {
        const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
        if (!entry.isDirectory && entry.location.empty() && LooksLikeImage(entry.extension))
            path = JoinPath(app.directory, entry.wideName);
    }

    if (path == app.bigImagePath)
        return;

    app.bigImagePath     = path;
    app.bigImage         = path.empty() ? ImagePreview::Image{} : ImagePreview::Load(path, kBigImageSide);
    app.bigImageUploaded = false;
    app.imageZoom        = 1.0f;
}

// Disegna l'anteprima nell'area data, adattata e con l'ingrandimento scelto. Restituisce
// falso se non c'e' niente da disegnare, cosi' chi chiama sa che lo spazio resta al
// visualizzatore di testo o alle righe.
bool DrawBigImage(AppState& app, const Layout& layout, const Rect& area, float* usedHeight)
{
    // `0%` spegne le anteprime: anche l'immagine grande lascia il posto all'icona,
    // perche' e' la stessa scelta detta una volta sola.
    if (!app.bigImage.Valid() || app.PreviewZoom() <= 0.0f ||
        area.w < 40.0f || area.h < 40.0f)
        return false;

    if (!app.bigImageUploaded)
    {
        app.renderer->UploadLayerRegion(kBigImageLayer, app.bigImage.rgba.data(), app.bigImage.width,
                                       app.bigImage.height, 0, 0);
        app.bigImageUploaded = true;
    }

    // Adattata allo spazio, poi ingrandita: il fattore non scende sotto quello che fa
    // entrare l'immagine, cosi' "zoom indietro" non la fa uscire dal pannello.
    const float fit = (std::min)(area.w / static_cast<float>(app.bigImage.width),
                                 area.h / static_cast<float>(app.bigImage.height));
    const float scale = (std::max)(fit, fit * app.imageZoom);
    const float width  = static_cast<float>(app.bigImage.width) * scale;
    const float height = static_cast<float>(app.bigImage.height) * scale;

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const Rect frame{area.x, area.y, area.w, (std::min)(area.h, height + 8.0f * layout.scale)};
    r.Draw(UIDraw::Fill(frame, t.previewFrame, 6.0f * layout.scale));

    // La griglia dietro l'immagine. Serve a distinguere lo sfondo dell'immagine —
    // spesso bianco, o trasparente — da quello del pannello, ed e' la "griglia
    // dell'inspector" della chiave `InspectorHideGrid` (`analysis/out`). Si disegna
    // **sotto** l'immagine: un PNG trasparente la lascia intravedere, una foto la
    // copre. Un quad per linea, e solo dove serve: e' la stessa griglia di sempre.
    if (!app.inspectorHideGrid)
    {
        const float step = 16.0f * layout.scale;
        const Color line = t.panelBorder.WithAlpha(0.6f);
        const float inset = 5.0f * layout.scale;
        for (float x = frame.x + step; x < frame.right() - inset; x += step)
            r.FillRect(Rect{x, frame.y + inset, 1.0f, frame.h - inset * 2.0f}, line);
        for (float y = frame.y + step; y < frame.bottom() - inset; y += step)
            r.FillRect(Rect{frame.x + inset, y, frame.w - inset * 2.0f, 1.0f}, line);
    }

    // Il quad si disegna solo per la parte che entra: con l'ingrandimento acceso
    // l'immagine puo' essere piu' larga del pannello, e lasciarla uscire la farebbe
    // disegnare sopra il resto della finestra.
    const float visibleWidth  = (std::min)(width, area.w);
    const float visibleHeight = (std::min)(height, frame.h - 8.0f * layout.scale);
    const Rect picture{area.x + (area.w - visibleWidth) * 0.5f,
                       frame.y + (frame.h - visibleHeight) * 0.5f, visibleWidth, visibleHeight};
    const float u = (app.bigImage.width > 0)
                        ? (visibleWidth / width) * static_cast<float>(app.bigImage.width) /
                              static_cast<float>(kBigImageSide)
                        : 0.0f;
    const float v = (app.bigImage.height > 0)
                        ? (visibleHeight / height) * static_cast<float>(app.bigImage.height) /
                              static_cast<float>(kBigImageSide)
                        : 0.0f;
    r.Draw(UIDraw::Image(picture, Color{1.0f, 1.0f, 1.0f, 1.0f}, static_cast<float>(kBigImageLayer),
                         0.0f, 0.0f, u, v));
    r.Draw(UIDraw::Border(frame, t.panelBorder, 1.0f, 6.0f * layout.scale));

    if (usedHeight != nullptr)
        *usedHeight = frame.h + 10.0f * layout.scale;
    return true;
}

// --- visualizzatore di testo (M8) -------------------------------------------
//
// Tre passi, e ognuno ha una ragione: il **documento** si legge quando la voce
// scelta cambia (e' l'unica cosa in memoria che riguarda il contenuto di un file, e
// vive quanto la selezione: `docs/06`); le **righe come si vedono** si calcolano una
// volta per misura, perche' con l'andare a capo acceso una riga logica puo' essere
// piu' righe di schermo e rifarlo a ogni fotogramma sarebbe lavoro sprecato; il
// **disegno** legge solo quello che sta dentro la finestra.

void EnsureTextDocument(AppState& app)
{
    const int cursorIndex = CursorIndex(app);
    std::wstring path;
    if (cursorIndex >= 0)
    {
        const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];
        // Solo per i file che stanno su un percorso: un nome di spazio dei nomi non
        // si apre come testo.
        if (!entry.isDirectory && entry.location.empty())
            path = JoinPath(app.directory, entry.wideName);
    }

    if (path == app.textDocumentPath)
        return;   // gia' letto, o gia' saputo che non c'e' niente da leggere

    app.textDocumentPath = path;
    app.textDocument     = path.empty() ? TextView::Document{} : TextView::Load(path);
    app.textScroll       = 0.0f;
    app.textRowsDirty    = true;
}

void LayoutTextRows(AppState& app, const FontAtlas& font, float width, float textScale)
{
    if (!app.textRowsDirty && app.textRowsWidth == width && app.textRowsZoom == textScale)
        return;

    app.textRows.clear();
    app.textRowsWidth = width;
    app.textRowsZoom  = textScale;
    app.textRowsDirty = false;
    app.textLongest   = 0;

    const std::vector<std::string>& lines = app.textDocument.lines;
    for (size_t index = 0; index < lines.size(); ++index)
    {
        const std::string& line = lines[index];
        app.textLongest = (std::max)(app.textLongest, line.size());

        if (!app.textWrap || line.empty())
        {
            app.textRows.push_back(TextRow{index, 0, line.size()});
            continue;
        }

        // Andare a capo: si cammina la riga e si taglia dove non ci sta piu'. Il
        // taglio cade fra due caratteri, come tutti i tagli di testo di questo
        // programma, e va avanti anche quando una parola da sola non ci sta.
        size_t start = 0;
        float used = 0.0f;
        for (size_t i = 0; i < line.size(); i = Utf8::Next(line, i))
        {
            const float advance = font.MeasureOne(Utf8::At(line, i, nullptr), textScale);
            if (used + advance > width && i > start)
            {
                app.textRows.push_back(TextRow{index, start, i - start});
                start = i;
                used = 0.0f;
            }
            used += advance;
        }
        app.textRows.push_back(TextRow{index, start, line.size() - start});
    }
}

// Un carattere come stringa UTF-8: `FontAtlas::Draw` vuole una stringa, e costruirla
// con `substr` per ogni lettera di ogni riga visibile sarebbe un'allocazione per
// lettera. Le stringhe corte non toccano l'heap, quindi questa non costa niente.
std::string OneChar(uint32_t codepoint)
{
    std::string text;
    if (codepoint < 0x80)
    {
        text.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint < 0x800)
    {
        text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else if (codepoint < 0x10000)
    {
        text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    else
    {
        text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return text;
}

// Una riga di testo, con gli spazi visibili se accesi: un punto dove c'e' uno spazio,
// una freccia dove c'e' una tabulazione. E' l'unico modo di vedere *cosa* c'e' dove
// sembra non esserci niente, ed e' quello per cui esiste `InspectorShowWhitespace`.
void DrawTextRow(IRenderer& r, const FontAtlas& font, const std::string& line, const TextRow& row,
                 float x, float y, float scale, bool whitespace, Color color, Color whitespaceInk,
                 float maxWidth)
{
    const float space = font.MeasureOne(' ', scale);
    const float limit = x + maxWidth;

    float penX = x;
    int column = 0;
    for (size_t i = row.offset; i < row.offset + row.length; i = Utf8::Next(line, i))
    {
        const uint32_t codepoint = Utf8::At(line, i, nullptr);

        if (codepoint == '\t')
        {
            // La tabulazione porta alla colonna successiva multipla di quattro: si
            // calcola quanto manca, invece di disegnarne sempre quattro.
            const int stop = (column / 4 + 1) * 4;
            if (whitespace && penX + space <= limit)
                font.Draw(r, penX, y, "\u2192", whitespaceInk, scale);
            penX = x + space * static_cast<float>(stop);
            column = stop;
            if (penX > limit)
                break;
            continue;
        }

        const float advance = font.MeasureOne(codepoint, scale);
        if (penX + advance > limit)
            break;   // fuori dal pannello: si smette, invece di disegnare sopra altro

        if (codepoint == ' ' && whitespace)
            font.Draw(r, penX, y, "\u00B7", whitespaceInk, scale);
        else
            font.Draw(r, penX, y, OneChar(codepoint), color, scale);

        penX += advance;
        ++column;
    }
}

void DrawTextViewer(AppState& app, const Layout& layout, const Rect& area)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;
    const FontAtlas& font = (app.monoFontOk ? app.monoFont : app.smallFont);
    const TextView::Document& document = app.textDocument;

    // La riga di stato: la codifica, quante righe, con che terminatori. Sono le tre
    // cose che si vogliono sapere di un file di testo prima di leggerlo, e la codifica
    // e' quella che l'originale chiama `PanelInspector_TextEncodingUTF8`.
    char header[160];
    std::snprintf(header, sizeof(header),
                  document.lines.size() == 1 ? Tr("%s   %zu riga%s%s", "%s   %zu line%s%s")
                                              : Tr("%s   %zu righe%s%s", "%s   %zu lines%s%s"),
                  TextView::EncodingName(document.encoding), document.lines.size(),
                  document.endings.empty() ? "" : "   ", document.endings.c_str());
    app.smallFont.Draw(r, area.x, area.y, header, t.textDim, scale);
    if (document.truncated)
        app.smallFont.Draw(r, area.x + app.smallFont.Measure(header, scale) + 12.0f * scale, area.y,
                           Tr("(primi 1 MB)", "(first 1 MB)"), t.textDim, scale);

    const float top = area.y + 18.0f * scale;
    const Rect body{area.x, top, area.w, area.bottom() - top};
    if (body.h < 40.0f * scale || document.lines.empty())
        return;

    const float gutter = app.textLineNumbers ? 40.0f * scale : 0.0f;
    const float minimapWidth = app.textMinimap ? (std::min)(58.0f * scale, body.w * 0.25f) : 0.0f;
    const Rect text{body.x + gutter, body.y, body.w - gutter - minimapWidth, body.h};

    const float textScale = scale * app.textZoom;
    LayoutTextRows(app, font, text.w, textScale);

    const float lineHeight = font.LineHeight() * textScale;
    const float content = static_cast<float>(app.textRows.size()) * lineHeight;
    // Il limite e' **due**: sotto zero non si scorre, e oltre `content - text.h`
    // nemmeno. Prima ce n'era uno solo scritto al contrario — `min(max(0, content -
    // text.h), scroll)` limita solo in alto, e il `max(0, ...)` era applicato al
    // limite invece che allo scorrimento. Scorrendo la rotella verso l'alto oltre
    // l'inizio, `textScroll` diventava negativo e li' restava: il testo veniva
    // spinto verso il basso fino a sparire, e la fascia della minimap usciva dal
    // suo riquadro e finiva sopra il pannello. Trovato dall'autore, guardando.
    app.textScroll = (std::max)(0.0f, (std::min)(app.textScroll, content - text.h));

    const int first = (std::max)(0, static_cast<int>(app.textScroll / lineHeight) - 1);
    const int last = (std::min)(static_cast<int>(app.textRows.size()),
                                first + static_cast<int>(text.h / lineHeight) + 3);

    // Le righe si ritagliano sull'area del testo, come la lista sul suo rettangolo
    // (§7.36). Il `first` qui sopra comincia **una riga prima** di quella visibile,
    // cosi' la riga a meta' si vede a meta' invece di comparire di colpo — ed e'
    // proprio quella riga che, senza ritaglio, finisce sopra la testata
    // `UTF-8 ... righe`. Si vedeva scorrendo.
    // Il ritaglio parte dal bordo del pannello, non da quello del testo: i numeri di
    // riga stanno nella gutter, a sinistra, e ritagliando sul solo testo sparivano —
    // un ritaglio stretto e' un difetto quanto uno largo.
    app.renderer->End();
    app.renderer->SetScissor(Rect{body.x, text.y, text.right() - body.x, text.h});
    app.renderer->Begin(app.viewportWidth, app.viewportHeight);

    for (int row = first; row < last; ++row)
    {
        const TextRow& item = app.textRows[static_cast<size_t>(row)];
        const float y = text.y + static_cast<float>(row) * lineHeight - app.textScroll;

        // Il numero si scrive solo dove comincia una riga *logica*: quando una riga va
        // a capo, le sue continuazioni appartengono alla stessa riga.
        if (app.textLineNumbers && item.offset == 0)
        {
            const std::string number = std::to_string(item.line + 1);
            app.smallFont.Draw(r, body.x + gutter - app.smallFont.Measure(number, scale) - 10.0f * scale,
                               y, number, t.textDim, scale);
        }

        DrawTextRow(r, font, document.lines[item.line], item, text.x, y, textScale,
                    app.textShowWhitespace, t.text, t.whitespaceInk, text.w);
    }

    app.renderer->End();
    app.renderer->ClearScissor();
    app.renderer->Begin(app.viewportWidth, app.viewportHeight);

    // La minimap: una barra per riga di schermo, lunga quanto la riga piu' lunga che
    // ci sta dentro, e la parte che si sta guardando evidenziata. Serve a orientarsi in
    // un file lungo, che e' l'unica cosa che chiede di essere guardata cosi'.
    if (app.textMinimap && minimapWidth > 0.0f && app.textLongest > 0)
    {
        const Rect map{text.right() + 10.0f * scale, body.y, minimapWidth - 10.0f * scale, body.h};
        const int bands = (std::max)(1, static_cast<int>(map.h));
        const double rows = static_cast<double>(app.textRows.size());
        for (int band = 0; band < bands; ++band)
        {
            const size_t from = static_cast<size_t>(static_cast<double>(band) / bands * rows);
            const size_t to = (std::max)(from + 1, static_cast<size_t>(
                                                       static_cast<double>(band + 1) / bands * rows));
            size_t longest = 0;
            for (size_t i = from; i < to && i < app.textRows.size(); ++i)
                longest = (std::max)(longest, app.textRows[i].length);
            if (longest == 0)
                continue;
            const float fraction =
                (std::min)(1.0f, static_cast<float>(longest) / static_cast<float>(app.textLongest));
            r.FillRect(Rect{map.x, map.y + static_cast<float>(band), map.w * fraction, 1.0f},
                       t.textDim.WithAlpha(0.45f));
        }

        if (content > text.h && content > 0.0f)
        {
            const float visibleTop = app.textScroll / content * map.h;
            const float visibleHeight = (std::min)(map.h, text.h / content * map.h);
            r.FillRect(Rect{map.x, map.y + visibleTop, map.w, visibleHeight},
                       t.accent.WithAlpha(0.18f));
        }
    }
}

// Riempie le righe che descrivono |entry|: le stesse per l'inspector e per il
// pannello delle proprieta'. Non disegna e non sa dove finiranno, perche' quello
// che sa e' chi le mostra. `app` non e' costante: chiedere le statistiche di una
// cartella e' una richiesta, e la richiesta parte da qui.

//
// Scegliendo una cartella, l'ispettore dice cosa c'e' dentro **senza aprirla**: le
// prime voci, con la loro anteprima. E' la domanda «cosa c'e' qui?» fatta con gli
// occhi invece che aprendo e tornando indietro.
//
// Il costo ha un tetto in tutti e due i posti in cui tocca qualcosa: la lettura e' di
// `kPeekItems` voci — una cartella con centomila file si ferma alla tredicesima — e le
// anteprime sono le stesse che la lista chiede gia' alla shell, quindi non aprono una
// strada nuova e non allungano la coda dei lavori.

// Quante voci si mostrano. Dodici stanno in tre colonne per quattro righe, e sono
// dodici anteprime da chiedere invece di un albero.
constexpr int kPeekItems = 12;

// Le prime voci di una cartella, con il solo necessario a disegnarne una cella.
//
// Si ferma al tetto **e guarda se ce n'e' un'altra**: cosi' una cartella enorme non
// costa una lettura intera per scrivere «e altre 90.000 voci», che e' un numero che
// non serve a nessuno. `more` dice che ce n'e' almeno un'altra, che e' quello che
// basta a dirlo.
//
// Restituisce falso se la cartella non si e' potuta **aprire**: «vuota» e «non
// leggibile» sono due cose diverse, e dirne una per l'altra sarebbe la solita
// risposta inventata.
bool ReadFolderPeek(const std::wstring& folder, std::vector<PeekEntry>* out, bool* more)
{
    out->clear();
    *more = false;

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = folder + L"\\*";
    HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                     FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE)
    {
        // Stesso ripiego dell'enumerazione della lista: alcuni provider di rete
        // rifiutano il suggerimento di lettura in blocco.
        search = FindFirstFileW(pattern.c_str(), &data);
    }
    if (search == INVALID_HANDLE_VALUE)
        return false;

    do
    {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
            continue;

        if (static_cast<int>(out->size()) >= kPeekItems)
        {
            *more = true;
            break;
        }

        PeekEntry peek;
        peek.path        = folder + L"\\" + name;
        peek.name        = ToUtf8(name);
        peek.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (!peek.isDirectory)
        {
            const size_t dot = peek.name.rfind('.');
            if (dot != std::string::npos && dot + 1 < peek.name.size())
            {
                peek.extension = peek.name.substr(dot + 1);
                std::transform(peek.extension.begin(), peek.extension.end(),
                               peek.extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            }
        }

        out->push_back(std::move(peek));
    }
    while (FindNextFileW(search, &data) != FALSE);

    FindClose(search);
    return true;
}

// La cartella di cui mostrare il contenuto, se ce n'e' una: quella scelta, e solo se
// e' **una sola** e si puo' elencare. Un nome di spazio dei nomi non ha figli da dare
// a `FindFirstFile` — le voci del cestino non sono percorsi — e su piu' scelte non
// c'e' niente da mostrare. La radice di un volume invece si': elencare `C:\` e'
// esattamente la domanda che si sta facendo.
void EnsureFolderPeek(AppState& app)
{
    std::wstring folder;
    if (app.selection.size() <= 1)
    {
        const int index = CursorIndex(app);
        if (index >= 0)
        {
            const Entry& entry = app.entries[static_cast<size_t>(index)];
            const std::wstring candidate = EntryLocation(app, entry);
            if (entry.isDirectory && entry.navigable && !candidate.empty())
                folder = candidate;
        }
    }

    if (folder == app.peekFolder)
        return;   // gia' letta, o gia' saputo che non c'e' niente da leggere

    app.peekFolder   = folder;
    app.peekReadable = false;
    app.peekEntries.clear();
    app.peekMore = false;
    if (!folder.empty())
        app.peekReadable = ReadFolderPeek(folder, &app.peekEntries, &app.peekMore);
}

// Le righe: icona e nome, una voce per riga, alte quanto le righe dell'ispettore
// (20 px) cosi' l'elenco continua a leggersi come il resto del pannello. La misura
// dell'icona e' quella della riga dei **dettagli** della lista, e la ricetta per
// prenderla e' la stessa: miniatura vera se ce n'e' una, altrimenti quella del tipo.
// A 16 px non si chiede alla shell l'icona grande — sarebbe una rasterizzazione da
// 128 px per disegnarne 16 — e l'atlante, che rasterizza a 32, e' la misura giusta.
void DrawFolderPeek(AppState& app, const Layout& layout, const Rect& area)
{
    // «Non si riesce a leggere» non si dice qui: i metadati sopra lo dicono gia', e
    // un titolo senza niente sotto non e' un'informazione.
    if (!app.peekReadable || area.h < 70.0f * layout.scale)
        return;

    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    app.smallFont.Draw(r, area.x, area.y, Tr("CONTENUTO", "CONTENTS"), t.textDim, scale);

    // "e altre voci" sulla stessa riga dell'intestazione, a destra: come riga a se'
    // sarebbe una riga rubata all'elenco.
    if (app.peekMore)
    {
        const std::string more = Tr("e altre voci", "and more");
        app.smallFont.Draw(r, area.right() - app.smallFont.Measure(more, scale), area.y, more,
                           t.textDim, scale);
    }

    // Una cartella **vuota** lo dice. E' la cosa piu' importante che si puo' sapere di
    // una cartella prima di aprirla, e non mostrare niente al suo posto la lascerebbe
    // indistinguibile da un elenco che non e' stato disegnato.
    if (app.peekEntries.empty())
    {
        app.smallFont.Draw(r, area.x, area.y + 22.0f * scale, Tr("(vuota)", "(empty)"),
                           t.textDim, scale);
        return;
    }

    const float rowHeight = 20.0f * scale;
    const float iconSide  = 16.0f * scale;
    const float top       = area.y + 22.0f * scale;

    const int rows  = (std::max)(1, static_cast<int>((area.bottom() - top) / rowHeight));
    const int total = static_cast<int>(app.peekEntries.size());

    // L'opzione che spegne le anteprime le spegne anche qui: e' la stessa domanda
    // («cosa c'e' dentro»), e due risposte diverse nella stessa finestra sono due
    // bugie di cui una sola e' voluta.
    const bool wantPreview = app.PreviewZoom() > 0.0f;

    for (int i = 0; i < rows && i < total; ++i)
    {
        const PeekEntry& item = app.peekEntries[static_cast<size_t>(i)];
        const float y = top + rowHeight * static_cast<float>(i);

        const Rect icon{area.x, y + (rowHeight - iconSide) * 0.5f, iconSide, iconSide};
        ShellPreviewCache::IconUV preview;
        if (wantPreview && app.previews.Query(item.path, item.extension, &preview) ==
                               ShellPreviewCache::State::Ready)
        {
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer, preview.u0,
                                 preview.v0, preview.u1, preview.v1));
        }
        else if (const ShellIconAtlas::IconUV* uv =
                     app.icons.GetFor(item.extension, item.isDirectory))
        {
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer, uv->u0,
                                 uv->v0, uv->u1, uv->v1));
        }

        const float textX   = area.x + iconSide + 6.0f * scale;
        const float textMax = area.right() - textX;
        app.smallFont.Draw(r, textX, y + 4.0f * scale,
                           FitText(app.smallFont, item.name, scale, textMax), t.text, scale);
    }
}
} // namespace

void BuildInspectorRows(AppState& app, const Entry& entry, std::vector<InspectorRow>* out)
{
    std::vector<InspectorRow>& rows = *out;
    rows.clear();

    // Un **volume** non e' una cartella: le sue dimensioni sono la capacita' del
    // disco, non la somma di quello che c'e' dentro, e le statistiche ricorsive li'
    // non hanno niente da camminare. Prima si chiedeva lo stesso il conto, e per un
    // disco l'inspector rispondeva "0 B, 0 cartelle, 0 file": un numero falso detto
    // con sicurezza, che e' peggio di nessun numero.
    const std::wstring full = EntryLocation(app, entry);
    const ShellLocation::Volume volume = ShellLocation::VolumeOf(full);
    const bool fromNamespace = !entry.location.empty();

    // Folder numbers, if the panel is counting them. Asking here covers the
    // selected row even when it has scrolled out of view.
    DirStats stats;
    bool hasStats = false;
    if (entry.isDirectory && !volume.isVolume && !fromNamespace)
    {
        app.stats.Request(full);
        hasStats = app.stats.TryGet(full, &stats);
    }

    // Una voce di una cartella di spazio dei nomi — un file riciclato — la descrive
    // **la shell**, con le sue colonne: tipo, dimensione, date, percorso originale,
    // data di eliminazione, attributi, proprietario. Sono le stesse informazioni che
    // darebbero le righe qui sotto, piu' quelle che solo il cestino sa. Mostrarle
    // tutte e due riempirebbe il pannello di righe ripetute, quindi per queste voci
    // si usa la vista della shell e basta.
    const bool shellColumns = !entry.pidl.empty();
    if (shellColumns)
    {
        for (const ShellLocation::Column& column : ShellLocation::Columns(entry.pidl))
        {
            if (rows.size() >= 11)
                break;   // oltre, le righe uscirebbero dal pannello
            rows.push_back({ToUtf8(column.title), ToUtf8(column.value), false});
        }
    }

    if (!shellColumns)
        rows.push_back({Tr("Tipo", "Type"), entry.typeText, false});
    if (volume.isVolume)
    {
        if (!volume.totalText.empty())
            rows.push_back({Tr("Dimensione", "Size"), volume.totalText, false});
    }
    else if (!shellColumns && entry.isDirectory)
    {
        const bool growing = hasStats && stats.partial;
        rows.push_back({Tr("Dimensione", "Size"), hasStats ? HumanBytes(stats.logicalBytes)
                                                            : Tr("conteggio...", "counting..."),
                        growing});
        // I byte che il disco ha davvero riservato si mostrano **solo se misurati**:
        // su un volume che non sa rispondere, un minimo spacciato per totale e'
        // peggio di un numero che manca.
        if (hasStats && stats.allocatedKnown)
            rows.push_back({Tr("Su disco", "Size on disk"), HumanBytes(stats.allocatedBytes),
                            growing});
        rows.push_back({Tr("Cartelle", "Folders"), hasStats ? HumanCount(stats.folders) : "--",
                        growing});
        rows.push_back({Tr("File", "Files"), hasStats ? HumanCount(stats.files) : "--", growing});
    }
    else if (!shellColumns)
    {
        rows.push_back({Tr("Dimensione", "Size"), entry.sizeText, false});

        // Quanto spazio il disco ha **riservato**: e' un'altra domanda, e la
        // differenza con la dimensione e' fatta di cluster, di file residenti e di
        // compressione. Per un file solo costa una chiamata, e la si fa qui perche'
        // il pannello si apre su richiesta — per un albero sarebbe una chiamata per
        // file, ed e' per questo che `dir_stats` legge le cartelle in blocco.
        uint64_t allocated = 0;
        if (AllocatedSizeOf(full, &allocated))
            rows.push_back({Tr("Su disco", "Size on disk"), HumanBytes(allocated), false});
    }
    // Le date che il file system da' e che non si vedevano: la creazione e l'ultimo
    // accesso. Un tempo a zero non e' il 1601, e' "non lo so": la riga non si mette,
    // e vale anche per la modifica — un volume non ne ha una, e una riga con
    // l'etichetta e niente accanto non dice niente.
    const auto dated = [](const FILETIME& time) {
        return time.dwLowDateTime != 0 || time.dwHighDateTime != 0;
    };
    if (!shellColumns)
    {
        if (dated(entry.created))
            rows.push_back({Tr("Creato", "Created"), FormatFileTime(entry.created, /*withTime=*/true),
                            false});
        if (dated(entry.modified))
            rows.push_back({Tr("Modificato", "Modified"), entry.modifiedFull, false});
        if (dated(entry.accessed))
            rows.push_back({Tr("Accesso", "Accessed"), FormatFileTime(entry.accessed, /*withTime=*/true),
                            false});
    }

    // Gli attributi NTFS del record: quello che il file system non espone — Standard
    // Information, File Name, Data, Index Root... Non e' una scansione del volume, e'
    // una richiesta sola per il record di **questo** file.
    //
    // Il nome dell'attributo va nella colonna del **valore**, non in quella
    // dell'etichetta: li' c'e' posto per 74 punti, e "Standard Information" si
    // troncherebbe a meta' parola. Le righe con l'etichetta vuota sono la lista sotto
    // la sua intestazione, e la larghezza e' quella che serve.
    if (!shellColumns && !volume.isVolume && !full.empty())
    {
        const NtfsAttributes::Result& ntfs = NtfsOf(app, full);
        switch (ntfs.status)
        {
        case NtfsAttributes::Status::Ok:
            rows.push_back({Tr("NTFS", "NTFS"), std::string(), false});
            for (const NtfsAttributes::Attribute& attribute : ntfs.attributes)
            {
                if (rows.size() >= 24)
                    break;   // un record con cento attributi non e' una lista da leggere
                std::string text = attribute.name;
                if (attribute.size > 0)
                    text += ", " + HumanBytes(attribute.size);
                if (!attribute.stream.empty())
                    text += " (" + attribute.stream + ")";
                rows.push_back({std::string(), text, false});
            }
            break;

        // I due modi di non avere la lista si dicono, e sono diversi: uno si puo'
        // risolvere (avviare come amministratore), l'altro no. Una lista vuota al
        // posto di questa riga direbbe "questo file non ha attributi", che e' falso.
        case NtfsAttributes::Status::NoPrivilege:
            rows.push_back({Tr("NTFS", "NTFS"),
                            Tr("serve l'amministratore", "needs administrator"), false});
            break;
        case NtfsAttributes::Status::NotNtfs:
            rows.push_back({Tr("NTFS", "NTFS"),
                            Tr("volume non NTFS", "not an NTFS volume"), false});
            break;

        // Una posizione di rete o di spazio dei nomi: la domanda non si pone, e una
        // riga che dice "non disponibile" per ogni file remoto sarebbe rumore.
        case NtfsAttributes::Status::Unavailable:
            break;
        }
    }

    // Le proprieta' che il file system non dice: le dimensioni di un'immagine le sa
    // il gestore di quel tipo, e le risponde la shell.
    const FileProperties& properties = PropertiesOf(app, full);
    if (properties.imageWidth > 0 && properties.imageHeight > 0)
    {
        rows.push_back({Tr("Immagine", "Image"), std::to_string(properties.imageWidth) + " x " +
                                                       std::to_string(properties.imageHeight) + " px",
                        false});
    }

    // Un volume: il file system e lo spazio. Sono proprieta' del volume, non del
    // file che ci sta sopra, e si chiedono al sistema.
    if (volume.isVolume)
    {
        if (!volume.fileSystem.empty())
            rows.push_back({Tr("Sistema", "System"), volume.fileSystem, false});
        if (!volume.freeText.empty())
        {
            char free[96];
            std::snprintf(free, sizeof(free), Tr("%s liberi (%d%% usato)", "%s free (%d%% used)"),
                          volume.freeText.c_str(),
                          static_cast<int>(volume.usedFraction * 100.0f));
            rows.push_back({Tr("Spazio", "Space"), free, false});
        }
    }

    // Il percorso: e' la riga che si copia, e l'unica che dice *dove* si sta
    // guardando. Si taglia davanti.
    rows.push_back({Tr("Percorso", "Path"), ToUtf8(full), false, /*tail=*/true});

    // "Ext" solo per quello che un'estensione ce l'ha: una riga con l'etichetta e
    // niente accanto occupa lo spazio di una che direbbe qualcosa. E il trattino
    // lungo che c'era prima non era nemmeno un carattere che l'atlante conosceva:
    // a schermo si vedeva un buco, non un trattino.
    if (!entry.extension.empty())
        rows.push_back({Tr("Estensione", "Ext"), entry.extension, false});

}

void DrawInspector(AppState& app, const Layout& layout)
{
    IRenderer& r = *app.renderer;
    const Theme& t = app.theme;
    const float scale = layout.scale;

    r.Draw(UIDraw::Shadow(Rect{layout.inspector.x + 2.0f, layout.inspector.y + 3.0f,
                               layout.inspector.w, layout.inspector.h},
                          t.shadow, 10.0f * scale, 8.0f * scale));
    r.Draw(UIDraw::Fill(layout.inspector, t.panelBg, 8.0f * scale));
    r.Draw(UIDraw::Border(layout.inspector, t.panelBorder, 1.0f, 8.0f * scale));

    float y = layout.inspector.y + 13.0f * scale;
    app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y,
                       Tr("ISPETTORE", "INSPECTOR"), t.textDim, scale);
    y += 24.0f * scale;

    // Il documento del visualizzatore, se ce n'e' uno da leggere: la lettura avviene
    // qui e non a ogni fotogramma, perche' cambia solo quando cambia la voce scelta.
    EnsureTextDocument(app);

    const int selectedCount = static_cast<int>(app.selection.size());
    const int cursorIndex = CursorIndex(app);

    if (selectedCount == 0 || cursorIndex < 0)
    {
        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y, Tr("Niente selezionato", "Nothing selected"), t.textDim, scale);
        return;
    }

    if (selectedCount > 1)
    {
        // The reference inspector has labels for "file singolo o conteggio" and
        // "dimensione o somma": with several entries selected it reports the
        // aggregate. Folder sizes come from the cache only, because asking for a
        // folder that is not on screen would start work the user cannot see.
        uint64_t totalBytes = 0;
        int files = 0;
        int folders = 0;
        for (const std::wstring& name : app.selection)
        {
            const int index = FindEntryByName(app, name);
            if (index < 0)
                continue;
            const Entry& item = app.entries[static_cast<size_t>(index)];
            if (item.isDirectory)
            {
                ++folders;
                DirStats stats;
                if (app.stats.TryGet(JoinPath(app.directory, item.wideName), &stats))
                    totalBytes += stats.logicalBytes;
            }
            else
            {
                ++files;
                totalBytes += item.bytes;
            }
        }

        const std::string title =
            std::to_string(selectedCount) +
            (selectedCount == 1 ? Tr(" elemento selezionato", " item selected")
                                : Tr(" elementi selezionati", " items selected"));
        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y, title, t.text, scale);
        y += 26.0f * scale;

        const std::string breakdown =
            std::to_string(files) + Tr(" file, ", files == 1 ? " file, " : " files, ") +
            std::to_string(folders) +
            (folders == 1 ? Tr(" cartella", " folder") : Tr(" cartelle", " folders"));
        // Che tipi sono: "3 Documento di testo, 2 Cartella di file". Il conteggio
        // per tipo e' quello che dice cosa si sta per spostare o cancellare, e
        // l'inspector e' l'unico posto dove c'e' spazio per dirlo.
        std::unordered_map<std::string, int> byType;
        for (const std::wstring& name : app.selection)
        {
            const int index = FindEntryByName(app, name);
            if (index >= 0)
                ++byType[app.entries[static_cast<size_t>(index)].typeText];
        }
        std::vector<std::pair<std::string, int>> types(byType.begin(), byType.end());
        std::sort(types.begin(), types.end(),
                  [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                      return a.second > b.second;
                  });
        const std::pair<const char*, std::string> summary[] = {
            {Tr("Elementi", "Items"), breakdown},
            {Tr("Dimensione", "Size"), HumanBytes(totalBytes)},
        };
        for (const auto& item : summary)
        {
            app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y, item.first, t.textDim, scale);
            app.smallFont.Draw(r, layout.inspector.x + 96.0f * scale, y, item.second, t.text, scale);
            y += 20.0f * scale;
        }

        // Un tipo per riga invece di tutti su una: "3 Documento di testo, 2 Cartella
        // di file" non ci sta in una riga sola, e troncato dice la meta' di quello che
        // serve. L'etichetta sta sulla prima, le altre si allineano sotto.
        for (size_t i = 0; i < types.size() && i < 4; ++i)
        {
            app.smallFont.Draw(r, layout.inspector.x + 14.0f * scale, y,
                               (i == 0) ? Tr("Tipi", "Types") : "", t.textDim, scale);
            const float valueX = layout.inspector.x + 96.0f * scale;
            const float valueMax = layout.inspector.right() - valueX - 14.0f * scale;
            const std::string line = std::to_string(types[i].second) + " " + types[i].first;
            app.smallFont.Draw(r, valueX, y, FitText(app.smallFont, line, scale, valueMax), t.text,
                               scale);
            y += 20.0f * scale;
        }
        if (types.size() > 4)
        {
            app.smallFont.Draw(r, layout.inspector.x + 96.0f * scale, y,
                               Tr("e altri ", "and ") + std::to_string(types.size() - 4) +
                                   Tr("", " more"),
                               t.textDim, scale);
        }
        return;
    }

    const Entry& entry = app.entries[static_cast<size_t>(cursorIndex)];

    ShellPreviewCache::IconUV preview;
    const ShellPreviewCache::State previewState =
        app.previews.Query(EntryLocation(app, entry), entry.extension, &preview);

    // L'anteprima grande: il file decodificato davvero, adattato al pannello e
    // ingrandibile. Prende il posto della miniatura da 128 px, che per una fotografia
    // e' un'icona piu' grande, non un'anteprima.
    EnsureBigImage(app);
    const Rect previewArea{layout.inspector.x + 14.0f * scale, y,
                           layout.inspector.w - 28.0f * scale,
                           (std::min)(260.0f * scale, layout.inspector.h * 0.42f)};
    float imageHeight = 0.0f;
    const bool hasBigImage = DrawBigImage(app, layout, previewArea, &imageHeight);
    if (hasBigImage)
        y += imageHeight;

    // Per un file di testo la miniatura della shell e' un'icona di documento: lo
    // spazio serve al testo, che e' l'anteprima vera.
    if (!hasBigImage && previewState == ShellPreviewCache::State::Ready && !app.textDocument.isText)
    {
        // A ready thumbnail gets the width of the panel and pushes the name
        // underneath it: this is the one place large enough to be worth looking at.
        const float side = 128.0f * scale;
        const Rect frame{layout.inspector.x + 14.0f * scale, y, side, side};
        r.Draw(UIDraw::Fill(frame, t.previewFrame, 6.0f * scale));
        r.Draw(UIDraw::Image(frame.Inset(1.0f, 1.0f), Color{1.0f, 1.0f, 1.0f, 1.0f}, kPreviewLayer,
                             preview.u0, preview.v0, preview.u1, preview.v1));
        r.Draw(UIDraw::Border(frame, t.panelBorder, 1.0f, 6.0f * scale));
        y += side + 12.0f * scale;

        app.font.Draw(r, layout.inspector.x + 14.0f * scale, y,
                      FitText(app.font, entry.name, scale, layout.inspector.w - 28.0f * scale),
                      t.text, scale);
        y += 26.0f * scale;
    }
    else
    {
        if (const ShellIconAtlas::IconUV* uv = IconFor(app, entry))
        {
            const Rect icon{layout.inspector.x + 14.0f * scale, y, 32.0f * scale, 32.0f * scale};
            r.Draw(UIDraw::Image(icon, Color{1.0f, 1.0f, 1.0f, 1.0f}, kIconLayer,
                                 uv->u0, uv->v0, uv->u1, uv->v1));
        }

        // Name too long for the inspector column is truncated the same way.
        const float valueMax = layout.inspector.w - 110.0f * scale;
        app.font.Draw(r, layout.inspector.x + 56.0f * scale, y + 8.0f * scale,
                      FitText(app.font, entry.name, scale, valueMax), t.text, scale);
        y += 46.0f * scale;
    }

    std::vector<InspectorRow> rows;
    BuildInspectorRows(app, entry, &rows);

    for (const InspectorRow& row : rows)

    {
        // L'etichetta si tronca alla colonna che ha: una colonna della shell puo'
        // chiamarsi "Posizione originale" e finire sopra il valore.
        const float labelX = layout.inspector.x + 14.0f * scale;
        app.smallFont.Draw(r, labelX, y,
                           FitText(app.smallFont, row.label, scale, 74.0f * scale), t.textDim,
                           scale);
        const float valueX = layout.inspector.x + 96.0f * scale;
        const float valueMax = layout.inspector.right() - valueX - 14.0f * scale;
        // Un percorso si taglia davanti, tutto il resto dietro: le due cose che si
        // vogliono sapere sono "dove finisce" e "cosa c'e' scritto".
        const std::string shown = row.tail
                                      ? FitTextTail(app.smallFont, row.value, scale, valueMax)
                                      : FitText(app.smallFont, row.value, scale, valueMax);
        app.smallFont.Draw(r, valueX, y, shown, t.text, scale);
        if (row.growing)
        {
            const float width = app.smallFont.Measure(row.value, scale);
            r.FillRect(Rect{valueX, y + 15.0f * scale, width, 1.0f}, t.textDim);
        }
        y += 20.0f * scale;
    }

    // Il testo del file, sotto i metadati: i metadati restano la prima cosa da
    // leggere, e il testo prende lo spazio che avanza. Se non ne avanza abbastanza non
    // si mostra niente, invece di mostrare tre righe in un buco.
    app.textViewerRect = Rect{};
    if (app.textDocument.isText)
    {
        const Rect remaining{layout.inspector.x + 14.0f * scale, y,
                             layout.inspector.w - 28.0f * scale,
                             layout.inspector.bottom() - y - 12.0f * scale};
        if (remaining.h > 70.0f * scale)
        {
            app.textViewerRect = remaining;
            DrawTextViewer(app, layout, remaining);
        }
    }

    // Cosa c'e' dentro la cartella scelta, in fondo al pannello. Solo per una
    // cartella: di un file dicono gia' l'anteprima grande o il testo, e ripetere la
    // stessa domanda due volte nel pannello sarebbe rumore.
    EnsureFolderPeek(app);
    if (entry.isDirectory && !app.peekFolder.empty())
    {
        const Rect peekArea{layout.inspector.x + 14.0f * scale, y + 12.0f * scale,
                            layout.inspector.w - 28.0f * scale,
                            layout.inspector.bottom() - y - 20.0f * scale};
        DrawFolderPeek(app, layout, peekArea);
    }

    // Qui c'era un riquadro tratteggiato "Drop files here (not wired up yet)",
    // lasciato da M1 quando il tratteggio serviva solo a dare qualcosa da fare al
    // tracciato dello shader. Adesso il tratteggio ha il suo uso vero —
    // l'evidenziazione della destinazione di una caduta, in `DrawList` — e questo
    // segnaposto prometteva qualcosa che nessuna milestone ha mai previsto.
}
} // namespace Bolt
