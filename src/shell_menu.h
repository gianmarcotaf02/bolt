// ---------------------------------------------------------------------------
// I verbi della shell, visti da fuori.
//
// Quattro nomi e due costanti: leggere il menu che la shell sa fare dentro le nostre
// voci, innestarlo nel menu contestuale, staccarlo, e ridurre un'etichetta all'ASCII
// che l'atlante sa disegnare. Gli oggetti COM e la cache vivono in
// `AppState::ShellMenuCache`, perche' il menu e la cache devono morire insieme.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "app_state.h"

namespace Bolt
{
// La banda degli id della shell: dentro, un id e' l'indice di una voce per
// `IContextMenu`; fuori, e' un nostro `CommandId`. E' l'unica cosa che chi chiama
// deve sapere, ed e' per questo che sta qui.
constexpr int kShellCmdFirst = 0x6000;
constexpr int kShellCmdLast  = 0x6FFF;

// Un'etichetta della shell ridotta ad ASCII: l'atlante dei glifi copre il solo ASCII
// stampabile, e un accento mostrerebbe un buco.
std::string ShellLabelToAscii(const std::wstring& text);

// Stacca l'oggetto e il menu dal `ContextMenu` **senza** liberarli: restano nella
// cache, perche' la stessa selezione li riusera'.
void ShellMenuDetach(ContextMenu& menu);

// Legge un `HMENU` gia' popolato dalla shell dentro l'elenco delle voci, separatori
// compresi; |outSkipped| conta quelle lasciate fuori (id fuori banda, senza etichetta).
void ReadShellMenu(IContextMenu* shell, HMENU handle, bool folderBackground,
                   std::vector<ContextMenuItem>* out, int* outSkipped);

// Aggiunge al menu le voci della shell, se ce ne sono, passando dalla cache.
void AppendShellItems(AppState& app, ContextMenu& menu);
} // namespace Bolt
