// ---------------------------------------------------------------------------
// I modelli del menu "Nuovo" di Windows, letti dal registro.
//
// La shell non espone nessuna API per enumerarli: il menu "Nuovo" lo costruisce
// internamente, e le chiavi `ShellNew` stanno in tre posti diversi —
//
//   1. sotto la chiave dell'estensione:            ".zip\ShellNew"
//   2. sotto un ProgID annidato NELLA chiave:      ".docx\Word.Document.12\ShellNew"
//   3. sotto il ProgID di default dell'estensione: "MQL5.File\ShellNew"
//
// La forma 2 e' quella piu' usata su Windows 11 ed e' quella che rende il menu
// della shell molto piu' lungo di quello che si otterrebbe guardando solo la
// prima. Le etichette sono stringhe **indirette** ("@…dll,-id"), risolte con
// `RegLoadMUIString`: e' il meccanismo di Windows, quindi i nomi dei tipi
// arrivano tradotti senza avere le traduzioni dentro il nostro eseguibile.
//
// La creazione non passa da qui: la fa `FileOps::NewItem`, cioe' la shell, che
// applica da sola i valori della chiave — file modello, byte, comando. Questo
// modulo legge soltanto i nomi.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace ShellNew
{
struct Template
{
    std::wstring label;         // "Documento di testo"
    std::wstring extension;     // ".txt"
    std::wstring suggestedName; // "Nuovo documento di testo.txt"
};

// L'elenco, in ordine di etichetta. Si legge una volta sola — il registro non
// cambia mentre il programma gira — ed e' vuoto se il registro non ha modelli.
const std::vector<Template>& Templates();
} // namespace ShellNew
