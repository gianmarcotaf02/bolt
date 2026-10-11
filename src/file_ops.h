// ---------------------------------------------------------------------------
// Operazioni su file.
//
// Tutto passa da IFileOperation, che e' il motore che usa Esplora risorse: la
// finestra di avanzamento, il cestino, la gestione dei conflitti, la
// integrazione con Annulla e i permessi sono quelli del sistema. Reimplementarli
// a mano significherebbe rifare peggio qualcosa che Windows fa gia' bene, e
// rompersi su tutti i casi che il sistema copre da vent'anni.
//
// La clipboard usa CF_HDROP con l'effetto preferito, che e' il formato che
// Esplora risorse legge e scrive: si puo' copiare da Bolt e incollare in
// Esplora risorse, e viceversa.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace FileOps
{
enum class Result
{
    Ok,
    Cancelled,   // l'utente ha annullato dalla finestra di avanzamento
    Failed,      // |error| contiene il motivo
};

// Copia |sources| dentro la cartella |destination|.
Result Copy(HWND owner, const std::vector<std::wstring>& sources,
            const std::wstring& destination, std::wstring* error);

// Sposta |sources| dentro la cartella |destination|.
Result Move(HWND owner, const std::vector<std::wstring>& sources,
            const std::wstring& destination, std::wstring* error);

// Elimina |paths|. Con |permanently| falso va nel cestino, che e' cio' che deve
// fare il tasto Canc: l'eliminazione definitiva e' Shift+Canc e deve restare
// scomoda.
Result Delete(HWND owner, const std::vector<std::wstring>& paths, bool permanently,
              std::wstring* error);

// Rinomina |path| in |newName|. |newName| e' il solo nome, non un percorso.
Result Rename(HWND owner, const std::wstring& path, const std::wstring& newName,
              std::wstring* error);

// Crea una cartella e ne restituisce il percorso in |created|.
Result CreateFolder(HWND owner, const std::wstring& parent, const std::wstring& name,
                    std::wstring* created, std::wstring* error);

// --- clipboard -------------------------------------------------------------

// Mette |paths| nella clipboard. Con |cut| l'effetto preferito e' lo spostamento,
// ed e' quello che fa disegnare le icone in grigio a Esplora risorse.
bool SetClipboardFiles(HWND owner, const std::vector<std::wstring>& paths, bool cut);

// Vero se la clipboard contiene un elenco di file.
bool ClipboardHasFiles();

// Legge l'elenco di file dalla clipboard. |cut| riceve l'effetto preferito.
bool ReadClipboardFiles(std::vector<std::wstring>* paths, bool* cut);

// Svuota la clipboard, dopo che un incolla l'ha consumata.
void ClearClipboard(HWND owner);
} // namespace FileOps
