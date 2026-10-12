#include "drag_drop.h"

#include "diag.h"

#include <shellapi.h>
#include <shlobj.h>

#include <atomic>

namespace DragDrop
{
namespace
{

// --- percorsi ---------------------------------------------------------------
//
// Poche righe scritte a mano invece di `PathIsPrefixW` e `PathRemoveFileSpecW`,
// che sui separatori e sulle radici hanno regole proprie: qui i casi che contano
// sono "C:\ab" NON e' dentro "C:\a", e "C:\a" -> "C:\" e non "C:".

std::wstring WithoutTrailingSeparators(std::wstring path)
{
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

std::wstring ParentOf(const std::wstring& path)
{
    const std::wstring trimmed = WithoutTrailingSeparators(path);
    const size_t slash = trimmed.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return std::wstring();
    if (slash + 1 == trimmed.size())
        return trimmed;                    // gia' una radice: "C:\a\" -> "C:\"
    return trimmed.substr(0, slash + 1);   // col separatore: "C:\a\b" -> "C:\a\"
}

bool SamePath(const std::wstring& a, const std::wstring& b)
{
    return _wcsicmp(WithoutTrailingSeparators(a).c_str(), WithoutTrailingSeparators(b).c_str()) == 0;
}

// |path| e' dentro la cartella |folder|? Un percorso che *comincia* con lo stesso
// testo non basta: "C:\documenti vecchi" comincia per "C:\documenti" e non c'e'
// dentro, e senza il separatore nel confronto si rifiuterebbero cadute legittime.
bool IsInside(const std::wstring& path, const std::wstring& folder)
{
    std::wstring prefix = WithoutTrailingSeparators(folder);
    prefix += L'\\';
    return _wcsnicmp(path.c_str(), prefix.c_str(), prefix.size()) == 0;
}

// Senza modificatori la copia o lo spostamento lo decide il volume, come in
// Esplora risorse: spostare fra due dischi costa una copia e una cancellazione, e
// nessuno se lo aspetta da un trascinamento.
bool SameVolume(const std::wstring& a, const std::wstring& b)
{
    wchar_t left[MAX_PATH]{};
    wchar_t right[MAX_PATH]{};
    if (GetVolumePathNameW(a.c_str(), left, MAX_PATH) == FALSE)
        return false;
    if (GetVolumePathNameW(b.c_str(), right, MAX_PATH) == FALSE)
        return false;
    return _wcsicmp(left, right) == 0;
}

// --- cosa arriva ------------------------------------------------------------

// I percorsi dentro l'oggetto dati. `CF_HDROP` e' quello che scrivono Esplora
// risorse e i file manager che si parlano: senza, la caduta non ci riguarda.
bool ReadPaths(IDataObject* data, std::vector<std::wstring>* paths)
{
    if (data == nullptr)
        return false;

    FORMATETC format{};
    format.cfFormat = CF_HDROP;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex   = -1;
    format.tymed    = TYMED_HGLOBAL;
    if (data->QueryGetData(&format) != S_OK)
        return false;

    STGMEDIUM medium{};
    if (data->GetData(&format, &medium) != S_OK)
        return false;

    // Per un CF_HDROP su memoria globale l'handle *e'* l'HDROP: non si blocca e non
    // si sblocca, lo legge DragQueryFile e lo libera ReleaseStgMedium.
    const HDROP drop = static_cast<HDROP>(medium.hGlobal);
    if (drop != nullptr)
    {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i)
        {
            const UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(length, L'\0');
            DragQueryFileW(drop, i, path.data(), length + 1);
            paths->push_back(path);
        }
    }
    ReleaseStgMedium(&medium);
    return !paths->empty();
}

// Cosa la destinazione ha DICHIARATO di aver fatto davvero.
//
// L'effetto che `DoDragDrop` restituisce non basta, ed e' un difetto che si vede
// solo guardando la lista dopo: una destinazione puo' compiere lo spostamento e
// scriverlo nel formato `CFSTR_PERFORMEDDROPEFFECT` dell'oggetto dati,
// restituendo poi un effetto diverso. E' quello che fa la shell quando sposta
// davvero i file: senza leggere questo, la lista continua a mostrare file che non
// ci sono piu'. Vedi `docs/02` §7.23.
DWORD PerformedEffect(IDataObject* data)
{
    FORMATETC format{};
    // Il formato si registra: e' una stringa, non un numero fisso come CF_HDROP.
    // I formati registrati stanno sotto 0xC000, e il cast e' quello che fa la
    // shell.
    format.cfFormat =
        static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT));
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex   = -1;
    format.tymed    = TYMED_HGLOBAL;
    if (format.cfFormat == 0 || data->QueryGetData(&format) != S_OK)
        return 0;

    STGMEDIUM medium{};
    if (data->GetData(&format, &medium) != S_OK)
        return 0;

    // Un DWORD in memoria globale, non un testo.
    DWORD effect = 0;
    if (medium.hGlobal != nullptr)
    {
        if (const DWORD* value = static_cast<const DWORD*>(GlobalLock(medium.hGlobal)))
        {
            effect = *value;
            GlobalUnlock(medium.hGlobal);
        }
    }
    ReleaseStgMedium(&medium);
    return effect;
}

// Vero se la caduta non si puo' fare: dentro cio' che si sta trascinando, o sulla
// cosa stessa. Una cartella non si copia dentro di se', e nessuno vuole scoprirlo
// dalla finestra di avanzamento.
// Buttare via un'unita' intera non si fa: ne' da qui ne' dal tasto Canc, e la shell
// stessa rifiuterebbe. Il controllo sta qui perche' un effetto promesso e non
// mantenuto e' peggio di un gesto rifiutato subito.
bool RefusesDiscard(const std::vector<std::wstring>& sources)
{
    for (const std::wstring& source : sources)
    {
        if (source.size() >= 2 && source.size() <= 3 && source[1] == L':')
            return true;
    }
    return false;
}

bool Refuses(const std::vector<std::wstring>& sources, const std::wstring& target)
{
    for (const std::wstring& source : sources)
    {
        if (SamePath(source, target) || IsInside(target, source))
            return true;
    }
    return false;
}

// --- le due interfacce OLE --------------------------------------------------

class DropTarget final : public IDropTarget
{
public:
    explicit DropTarget(HWND window) : window(window) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (out == nullptr)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropTarget)
        {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --references;
        if (left == 0)
            delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL point,
                                        DWORD* effect) override
    {
        // L'oggetto dati arriva **una volta sola**: `DragOver` e `DragLeave` non lo
        // ricevono, e le sorgenti non cambiano finche' il puntatore sta qui. Si
        // legge adesso, invece di rileggerlo a ogni movimento del mouse.
        sources.clear();
        const bool readable = ReadPaths(data, &sources);
        DiagLog("--- " + DiagClock() + " trascinamento in entrata: " +
                (readable ? std::to_string(sources.size()) + " percorsi"
                          : std::string("nessun elenco di file nell'oggetto dati")));
        return Effect(keys, point, effect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD* effect) override
    {
        return Effect(keys, point, effect);
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        sources.clear();
        Highlight(window, Target{});
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD /*keys*/, POINTL point,
                                   DWORD* effect) override
    {
        if (effect == nullptr)
            return E_POINTER;

        // Quello che avevamo promesso in DragOver: chi molla usa questo per
        // copiare o spostare, e non si ricalcola.
        const DWORD negotiated = *effect;
        *effect = DROPEFFECT_NONE;

        // Se non avevamo promesso niente — dentro se stesso, o dove i file sono
        // gia' — non si fa niente. Senza questo controllo l'effetto "vietato"
        // diventava una **copia**, e un gesto annullato lasciava un file in piu'.
        if ((negotiated & (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK)) == 0)
        {
            sources.clear();
            Highlight(window, Target{});
            return S_OK;
        }

        std::vector<std::wstring> paths = sources;
        if (paths.empty())
            ReadPaths(data, &paths);

        POINT client{point.x, point.y};
        ScreenToClient(window, &client);

        const Target target = TargetAt(window, client);
        Highlight(window, Target{});
        sources.clear();
        if (paths.empty() || target.Empty())
            return S_OK;
        if (target.verb == DropVerb::Discard)
        {
            if (RefusesDiscard(paths))
                return S_OK;
        }
        else if (Refuses(paths, target.path))
        {
            return S_OK;
        }

        const bool move = (negotiated & DROPEFFECT_MOVE) != 0;
        // Nel registro va la forma della destinazione, non il percorso: "una riga",
        // "il riquadro" o "il cestino" bastano a leggere cosa e' successo.
        DiagLog("  caduta: " + std::to_string(paths.size()) + " percorsi su " +
                (target.verb == DropVerb::Discard
                     ? std::string("il cestino")
                     : std::string(target.sidebarRow >= 0 ? "il riquadro"
                                                          : (target.ontoRow ? "una riga"
                                                                            : "la cartella aperta"))) +
                ", " + (target.verb == DropVerb::Discard
                            ? std::string("eliminazione")
                            : std::string(move ? "spostamento" : "copia")));
        ::DragDrop::Drop(window, paths, target, move);
        *effect = move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        return S_OK;
    }

private:
    ~DropTarget() = default;

    // Cosa succede se si molla adesso, sul punto |point| (coordinate schermo).
    HRESULT Effect(DWORD keys, POINTL point, DWORD* effect)
    {
        if (effect == nullptr)
            return E_POINTER;
        *effect = DROPEFFECT_NONE;

        if (sources.empty())
        {
            Highlight(window, Target{});
            return S_OK;
        }

        // POINTL e' in coordinate SCHERMO: e' la posizione del puntatore, non
        // quella dentro la finestra.
        POINT client{point.x, point.y};
        ScreenToClient(window, &client);

        const Target target = TargetAt(window, client);
        Highlight(window, target);
        if (target.Empty())
            return S_OK;

        // Buttare via: sempre uno spostamento, e i modificatori non si guardano.
        // Ctrl non puo' trasformarlo in una copia — non c'e' niente da copiare — e
        // Shift non ha un'alternativa da offrire.
        if (target.verb == DropVerb::Discard)
        {
            if (!RefusesDiscard(sources))
                *effect = DROPEFFECT_MOVE;
            return S_OK;
        }

        if (Refuses(sources, target.path))
            return S_OK;

        const bool control = (keys & MK_CONTROL) != 0;
        const bool shift   = (keys & MK_SHIFT) != 0;
        const bool move    = shift || (!control && SameVolume(sources.front(), target.path));

        // Spostare dove sono gia': non c'e' niente da fare. L'effetto "niente" lo
        // dice al puntatore, che smette di promettere qualcosa.
        if (move && sources.size() == 1 && SamePath(ParentOf(sources.front()), target.path))
            return S_OK;

        *effect = move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        return S_OK;
    }

    std::atomic<ULONG> references{1};
    HWND               window;
    std::vector<std::wstring> sources;   // lette in DragEnter, valgono per tutto il gesto
};

class DropSource final : public IDropSource
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (out == nullptr)
            return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropSource)
        {
            *out = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --references;
        if (left == 0)
            delete this;
        return left;
    }

    // Il puntatore del sistema: freccia, piu', eccetera. Disegnare un cursore
    // nostro vorrebbe dire indovinare cosa fa la destinazione, e lo dice lei.
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
    {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override
    {
        // Esc annulla, il tasto lasciato conclude: sono le due sole cose che
        // decidono la fine di un trascinamento.
        if (escape)
            return DRAGDROP_S_CANCEL;
        if ((keys & (MK_LBUTTON | MK_RBUTTON)) == 0)
            return DRAGDROP_S_DROP;
        return S_OK;
    }

private:
    ~DropSource() = default;

    std::atomic<ULONG> references{1};
};

} // namespace

IDataObject* DataObjectFor(const std::vector<std::wstring>& paths)
{
    std::vector<PIDLIST_ABSOLUTE> absolute;
    for (const std::wstring& path : paths)
    {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl != nullptr)
            absolute.push_back(pidl);
    }
    if (absolute.empty())
        return nullptr;

    // I figli si passano relativi alla loro cartella: si prende la parte finale di
    // ogni percorso assoluto, e la cartella dal primo.
    PIDLIST_ABSOLUTE folder = ILClone(absolute.front());
    std::vector<PCUITEMID_CHILD> children;
    if (folder != nullptr)
    {
        ILRemoveLastID(folder);
        for (PIDLIST_ABSOLUTE pidl : absolute)
            children.push_back(ILFindLastID(pidl));
    }

    IDataObject* data = nullptr;
    if (!children.empty())
        SHCreateDataObject(folder, static_cast<UINT>(children.size()), children.data(), nullptr,
                           IID_IDataObject, reinterpret_cast<void**>(&data));

    for (PIDLIST_ABSOLUTE pidl : absolute)
        CoTaskMemFree(pidl);
    if (folder != nullptr)
        CoTaskMemFree(folder);
    return data;
}

IDataObject* DataObjectForPidls(HWND owner, const std::vector<const BYTE*>& pidls)
{
    if (pidls.empty())
        return nullptr;

    // L'oggetto dati lo chiede **la cartella che possiede le voci**, non lo
    // costruisce questo modulo: e' la stessa scelta del menu contestuale, e per la
    // stessa ragione. Un oggetto dati montato a mano — `SHCreateDataObject` con un
    // genitore ricavato dal PIDL — viene accettato dalla destinazione, che dichiara
    // perfino "copia", e poi non copia niente: il cestino non e' una cartella come
    // le altre, e chi lo sa e' la shell.
    IShellFolder* parent = nullptr;
    if (FAILED(SHBindToParent(reinterpret_cast<PCUIDLIST_ABSOLUTE>(pidls.front()),
                              IID_IShellFolder, reinterpret_cast<void**>(&parent), nullptr)) ||
        parent == nullptr)
        return nullptr;

    std::vector<PCUITEMID_CHILD> children;
    children.reserve(pidls.size());
    for (const BYTE* pidl : pidls)
        children.push_back(ILFindLastID(reinterpret_cast<PCUIDLIST_ABSOLUTE>(pidl)));

    IDataObject* data = nullptr;
    parent->GetUIObjectOf(owner, static_cast<UINT>(children.size()), children.data(),
                          IID_IDataObject, nullptr, reinterpret_cast<void**>(&data));
    parent->Release();
    return data;
}

void DragOutData(HWND window, IDataObject* data, size_t count, DWORD* effect)
{
    if (effect == nullptr)
        return;
    *effect = DROPEFFECT_NONE;
    if (data == nullptr)
        return;

    // Ammessi tutti e tre: la destinazione sceglie. `LINK` c'e' perche' e' quello
    // che serve a creare un collegamento (Ctrl+Maiusc), e chi non lo sa fare
    // risponde con quello che sa.
    DropSource* source = new DropSource();
    DWORD chosen = DROPEFFECT_NONE;
    if (SUCCEEDED(DoDragDrop(data, source,
                             DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &chosen)))
        *effect = chosen;

    // L'esito vero: quello restituito, e quello che la destinazione ha scritto
    // nell'oggetto dati. Se una delle due dice "spostato", i file non sono piu'
    // dove stavano, e chi ha chiamato deve rifare la vista.
    const DWORD performed = PerformedEffect(data);
    *effect |= performed;

    DiagLog("--- " + DiagClock() + " trascinamento in uscita: " + std::to_string(count) +
            " voci, effetto restituito " + std::to_string(chosen) + ", esito dichiarato " +
            std::to_string(performed));

    source->Release();
    (void)window;   // la finestra serve a OLE, che la conosce dalla registrazione
}

void DragOut(HWND window, const std::vector<std::wstring>& paths, DWORD* effect)
{
    if (effect == nullptr)
        return;
    *effect = DROPEFFECT_NONE;
    if (paths.empty())
        return;

    IDataObject* data = DataObjectFor(paths);
    if (data == nullptr)
        return;

    DragOutData(window, data, paths.size(), effect);
    data->Release();
}

bool Register(HWND window)
{
    DropTarget* target = new DropTarget(window);
    const HRESULT result = RegisterDragDrop(window, target);
    target->Release();   // la referenza che conta adesso e' quella che ha preso OLE
    return SUCCEEDED(result);
}

void Unregister(HWND window)
{
    RevokeDragDrop(window);
}

} // namespace DragDrop
