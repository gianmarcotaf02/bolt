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
// sceglie il nome definitivo, e poi il contenuto lo scrive chi legge il modello
// — vedi `Template::Source`. Questo modulo legge nomi e contenuti.
//
// I modelli col valore `Command` sono **esclusi**: quel valore avvia
// un'applicazione invece di creare un file ("msaccess.exe /NEWDB 1", "Wab.exe
// /CreateContact \"%1\""), e imitarlo significherebbe eseguire righe di registro
// arbitrarie per creare un file. Vedi `docs/04-roadmap.md`.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace ShellNew
{
struct Template
{
    // Da dove viene il contenuto della voce nuova. Il vuoto lo lascia la shell;
    // gli altri due li scriviamo noi, perche' `IFileOperation::NewItem` senza un
    // modello crea un file di zero byte — e **uno zip di zero byte non e' uno
    // zip**. Vedi `docs/04-roadmap.md`, sezione M7.
    enum class Source
    {
        Empty,         // "NullFile": un file vuoto
        TemplateFile,  // "FileName": copia di un file modello
        Data,          // "Data": i byte scritti nella chiave
    };

    std::wstring label;         // "Documento di testo"
    std::wstring extension;     // ".txt"
    std::wstring suggestedName; // "Nuovo documento di testo.txt"

    Source            source = Source::Empty;
    std::wstring      templatePath;  // per TemplateFile: il file da copiare
    std::vector<BYTE> data;          // per Data: i byte
};

// L'elenco, in ordine di etichetta. Si legge una volta sola — il registro non
// cambia mentre il programma gira — ed e' vuoto se il registro non ha modelli.
const std::vector<Template>& Templates();
} // namespace ShellNew
