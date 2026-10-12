// ---------------------------------------------------------------------------
// Registro diagnostico.
//
// Spento di default, si accende con `BOLT_DIAG=1` e scrive `bolt-diag.log`
// accanto all'eseguibile. Serve a LEGGERE cosa risponde il sistema invece di
// dedurlo: quali voci offre la shell su questa macchina, quanto costa aprirle,
// quanti percorsi arrivano con un trascinamento. Sono cose che dipendono dal
// computer, e che una risposta scritta vale piu' di dieci minuti di ragionamento.
//
// Non ci finiscono ne' nomi di file dell'utente ne' elenchi di programmi
// installati: le voci si contano, non si trascrivono. Vedi `AGENTS.md` §0.
// ---------------------------------------------------------------------------
#pragma once

#include <string>

// Vero se il registro e' acceso.
bool DiagEnabled();

// Vero se si vogliono i contatori del motore nella barra di stato (`BOLT_DEV_HUD=1`):
// fotogrammi, quadrilateri, lavori. Servono a chi scrive il programma, non a chi lo
// usa, e di default non si vedono.
bool DevHudEnabled();

// Aggiunge una riga al registro. Le righe si accumulano e si riscrivono tutte
// insieme: aprendo in append a ogni riga il file restava incompleto.
void DiagLog(const std::string& line);

// Ora locale con i millisecondi: due giri nella stessa sessione si distinguono
// solo cosi', e la prima apertura non costa quanto la seconda.
std::string DiagClock();
