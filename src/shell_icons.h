// ---------------------------------------------------------------------------
// Shell icon atlas.
//
// File and folder icons come from the shell, not from the application: that is
// what makes a file manager look like Windows rather than like a rectangle
// simulator. SHGetFileInfoW resolves an icon for an extension or an attribute
// set, and the result is rasterised into one layer of the renderer's texture
// array, laid out as a grid of cells.
//
// Two decisions worth stating:
//
//  * Icons are keyed by extension (or by "directory"), not per file. The shell
//    returns the same icon for every .txt file, so caching per extension keeps
//    the atlas small and the lookup free. SHGFI_USEFILEATTRIBUTES means the call
//    never touches the disk.
//
//  * The atlas is rasterised at 32x32 and drawn at roughly half that, so it
//    still looks correct when the UI is zoomed in.
//
//  * There are two ways of asking, and they are not interchangeable. `GetFor`
//    names an **extension**, the shell answers without touching the disk, and
//    the cache stays small however long the list is. `GetForObject` names a
//    **shell object** — a drive, a known folder, the recycle bin — and the shell
//    looks at it: it is the only way to get the icon a drive really has, and it
//    is meant for the places where the set is small and closed, because this
//    atlas has no eviction and one icon per file would fill it.
// ---------------------------------------------------------------------------
#pragma once

#include "ui_renderer.h"

#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

class ShellIconAtlas
{
public:
    struct IconUV
    {
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    };

    ShellIconAtlas() = default;
    ~ShellIconAtlas();

    ShellIconAtlas(const ShellIconAtlas&) = delete;
    ShellIconAtlas& operator=(const ShellIconAtlas&) = delete;

    // Allocates the atlas layer and warms the cache with the icons for folders
    // and for entries without an extension.
    bool Initialize(IRenderer& renderer, int layer, int atlasSize, std::string* error);

    // Returns the atlas rectangle for one entry. |extension| is matched case
    // insensitively and may be empty. Never returns null: unknown extensions fall
    // back to the generic file icon.
    const IconUV* GetFor(const std::string& extension, bool isDirectory);

    // L'icona di one shell object, looked at rather than guessed from a name:
    // a drive root ("D:\\"), a known folder, "This PC", the recycle bin — either a
    // real path or a namespace name (`::{...}`). Null when the shell has no icon
    // for it, so the caller keeps its own fallback.
    //
    // |linkOverlay| chiede la freccetta del collegamento: serve per i `.lnk`, dove
    // l'icona e' quella della destinazione **piu'** il segno che e' un collegamento.
    const IconUV* GetForObject(const std::wstring& location, bool linkOverlay = false);

    // L'icona giusta per una voce che ha un percorso: per quasi tutto e' quella
    // dell'estensione, senza toccare il disco; per un `.lnk` (o un `.url`) e' quella
    // del file vero, con la freccetta. E' la stessa domanda che fanno la lista e
    // l'anteprima del contenuto, e la risposta deve essere una sola.
    const IconUV* GetForPath(const std::wstring& path, const std::string& extension,
                             bool isDirectory);

    // L'icona di una voce di menu contestuale. Due forme perche' sono due fonti
    // diverse, e nessuna delle due e' un'ipotesi:
    //
    //   * `GetForMenuBitmap` — la bitmap che la shell (o l'estensione) ha messo nel
    //     `hbmpItem` della voce. E' di proprieta' dell'HMENU e vale finche' il menu
    //     vive: si legge subito, si carica in una celletta e non si tocca piu'. `key`
    //     e' la chiave della cache, e deve essere stabile fra un'apertura e l'altra
    //     (noi usiamo il verbo).
    //   * `GetForIconSpec` — `"C:\...\app.exe"` o `"C:\...\app.exe",-105`, cioe' il
    //     valore `Icon` che un'applicazione scrive sotto la propria chiave di verbo:
    //     e' l'unico posto in cui dichiara quale icona vuole nel menu.
    //
    // Null quando l'icona non c'e': la voce si disegna senza, che e' diverso dal
    // disegnarla con un'icona sbagliata.
    const IconUV* GetForMenuBitmap(const std::string& key, HBITMAP bitmap);
    const IconUV* GetForIconSpec(const std::wstring& spec);

    int IconCount() const { return static_cast<int>(cells_.size()); }

    // Quanti pixel misura il disegno dentro una celletta. E' la misura oltre la
    // quale un'icona va chiesta **grande** invece di essere ingrandita: ingrandire
    // questo valore si vede, ed e' il punto in cui la vista a icone grandi si
    // sgranava. Derivato dalla cella, non scelto a occhio.
    int IconPixels() const { return iconPixels_; }

private:
    // How the icon is asked for, which decides the flags: a synthetic name plus
    // attributes (no disk access), a real path, or a PIDL.
    enum class Source { SyntheticName, RealPath, Pidl };

    // Rasterises one icon and uploads it into the next free cell. |name| is a
    // path, or a synthetic name, or a PIDL — never a string that was built from a
    // PIDL, because a PIDL has NULs inside it. Returns null when the atlas is
    // full or the shell has no icon for the key.
    const IconUV* Rasterize(const std::string& key, LPCWSTR name, DWORD attributes, Source source,
                            bool linkOverlay = false);

    // L'icona gia' in mano, ovunque sia stata presa: la converte in RGBA e la
    // carica nella prima celletta libera. Non distrugge |icon|.
    const IconUV* UploadIcon(const std::string& key, HICON icon);

    // I pixel gia' in mano, BGRA a canale diritto: e' il punto in cui convergono
    // tutte le strade (icona, bitmap di menu, specifica del registro) e l'unico
    // che sa come si riempie una celletta. Un'immagine piu' piccola della cella si
    // centra, cosi' una bitmap da 16 px sta nella cella da 32 senza deformarsi.
    const IconUV* UploadPixels(const std::string& key, int width, int height,
                               const std::vector<uint32_t>& pixels);

    IRenderer* renderer_ = nullptr;
    int layer_ = 0;
    int atlasSize_ = 0;
    int cellSize_ = 0;
    int iconPixels_ = 32;   // il disegno dentro la celletta, senza il margine
    int columns_ = 0;

    std::unordered_map<std::string, IconUV> cache_;
    std::vector<std::string>                cells_;   // key per occupied cell
};

// L'icona dell'applicazione che apre |extension|, alla misura chiesta, in pixel BGRA
// a canale **diritto**: la stessa icona che l'atlante mostra piccolo, un'altra
// dimensione. Serve a chi disegna grande — le viste a icone — perche' ingrandire la
// rasterizzazione da 32 px si vede, e perche' due misure della stessa icona non
// devono essere due icone diverse. Falso se l'associazione non porta a un'icona
// estraibile: chi chiama ha il suo ripiego.
bool ExtensionIconPixels(const std::string& extension, int size, int* width, int* height,
                         std::vector<uint32_t>* pixels);
