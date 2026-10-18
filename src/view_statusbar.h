// ---------------------------------------------------------------------------
// La barra di stato, e la riga che sta in fondo alla finestra.
//
// Era una funzione di disegno in `main.cpp` in mezzo a un file che fa altro. Qui ci
// sono due cose che sembrano una sola e non lo sono: l'esito di quello che si e'
// appena fatto, che prende il posto di tutto il resto per quattro secondi, e i
// numeri — quante voci, quante scelte, dove si e' nella lista — con i selettori
// della barra in fondo.
//
// I selettori registrano i loro rettangoli come bersagli di clic e non rispondono da
// soli: chi li preme e' l'input, che conosce i comandi. E' la stessa divisione della
// lista: il disegno sa dove sono le cose, l'input sa cosa fanno.
// ---------------------------------------------------------------------------
#pragma once

#include "app_state.h"

namespace Bolt
{
void DrawStatusBar(AppState& app, const Layout& layout);

// L'esito di quello che si e' appena fatto. Un'operazione che fallisce in silenzio e'
// peggio di una che non parte: senza un messaggio, "Sposta" su un file in uso
// sembrerebbe non aver fatto nulla.
void ReportStatus(AppState& app, const std::wstring& message);
} // namespace Bolt
