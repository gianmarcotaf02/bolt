#include "drag_drop.h"

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

// Vero se la caduta non si puo' fare: dentro cio' che si sta trascinando, o sulla
// cosa stessa. Una cartella non si copia dentro di se', e nessuno vuole scoprirlo
// dalla finestra di avanzamento.
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
        ReadPaths(data, &sources);
        return Effect(keys, point, effect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD* effect) override
    {
        return Effect(keys, point, effect);
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        sources.clear();
        Highlight(window, L"", false);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL point,
                                   DWORD* effect) override
    {
        if (effect == nullptr)
            return E_POINTER;

        // Quello che avevamo promesso in DragOver: chi molla usa questo per
        // copiare o spostare, e non si ricalcola.
        const DWORD negotiated = *effect;
        *effect = DROPEFFECT_NONE;

        std::vector<std::wstring> paths = sources;
        if (paths.empty())
            ReadPaths(data, &paths);

        POINT client{point.x, point.y};
        ScreenToClient(window, &client);

        bool ontoRow = false;
        const std::wstring target = TargetAt(window, client, &ontoRow);
        Highlight(window, L"", false);
        sources.clear();
        if (paths.empty() || target.empty() || Refuses(paths, target))
            return S_OK;

        const bool move = (negotiated & DROPEFFECT_MOVE) != 0;
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
            Highlight(window, L"", false);
            return S_OK;
        }

        // POINTL e' in coordinate SCHERMO: e' la posizione del puntatore, non
        // quella dentro la finestra.
        POINT client{point.x, point.y};
        ScreenToClient(window, &client);

        bool ontoRow = false;
        const std::wstring target = TargetAt(window, client, &ontoRow);
        Highlight(window, target, ontoRow);
        if (target.empty() || Refuses(sources, target))
            return S_OK;

        const bool control = (keys & MK_CONTROL) != 0;
        const bool shift   = (keys & MK_SHIFT) != 0;
        const bool move    = shift || (!control && SameVolume(sources.front(), target));

        // Spostare dove sono gia': non c'e' niente da fare. L'effetto "niente" lo
        // dice al puntatore, che smette di promettere qualcosa.
        if (move && sources.size() == 1 && SamePath(ParentOf(sources.front()), target))
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

    // Ammessi tutti e tre: la destinazione sceglie. `LINK` c'e' perche' e' quello
    // che serve a creare un collegamento (Ctrl+Maiusc), e chi non lo sa fare
    // risponde con quello che sa.
    DropSource* source = new DropSource();
    DWORD chosen = DROPEFFECT_NONE;
    if (SUCCEEDED(DoDragDrop(data, source,
                             DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &chosen)))
        *effect = chosen;

    source->Release();
    data->Release();
    (void)window;   // la finestra serve a OLE, che la conosce dalla registrazione
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
