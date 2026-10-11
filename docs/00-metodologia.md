# Metodologia

## Cos'è questo progetto

Un clone didattico di File Pilot per Windows, scritto da zero. L'obiettivo non è
distribuire un prodotto: è capire come è costruita un'applicazione nativa
performante e reimplementarne l'architettura in modo indipendente.

L'aspetto che rende il lavoro un progetto di ingegneria e non un esercizio di
copia è **il metodo**: il comportamento del target viene ricostruito da evidenze
osservabili, non da codice sorgente, e ogni conclusione è tracciata fino alla sua
fonte.

## Vincoli che mi sono imposto

1. **Nessun codice dell'originale in `src/`.** Gli shader recuperati dal binario
   stanno in `analysis/out/shaders/` come materiale di studio, con un'intestazione
   che lo dichiara. `src/shaders/` contiene una riscrittura originale che
   implementa la stessa tecnica con struttura, nomi e commenti propri.
2. **Nessuna stringa, icona o risorsa copiata.** I nomi dei setting e dei comandi
   sono usati come *specifica* (cosa esiste), non come asset.
3. **Nessuna esecuzione del target a fini di estrazione.** L'analisi è statica e
   read-only. Il binario non viene mai lanciato per l'analisi, mai modificato.
4. **Tracciabilità.** Ogni affermazione in `docs/01` e `docs/02` è etichettata
   come *Verificato* (osservato direttamente nel binario), *Inferito* (dedotto da
   più evidenze) o *Da verificare*.

## Il metodo in quattro passi

### 1. Caratterizzazione del target

Prima di leggere una singola istruzione: cos'è il file? Il report PE
(`analysis/out/pe-report.md`) stabilisce architettura, sezioni, sottosistema,
entropia (per escludere packing), funzioni con unwind info, librerie importate e
presenza di un riferimento PDB.

Questa fase decide la fattibilità. Un binario packato o obfuscato avrebbe richiesto
un approccio diverso; qui l'entropia è 6.28 e non c'è packer riconosciuto, quindi
l'analisi statica diretta è percorribile.

### 2. Estrazione di evidenze a costo zero

Molte informazioni di progetto sono nel `.rdata` come dati, non come codice:

- **identificatori interni** usati come chiavi runtime (tabelle di setting, label
  di UI, nomi di comando) → mappa semantica del prodotto, 1124 voci;
- **sorgenti GLSL** memorizzati come stringhe multi-riga → tecnica di rendering;
- **format string** di `printf`/`swprintf` → schema dei file di configurazione;
- **codici di attributo NTFS** → quali strutture su disco il programma interpreta;
- **nome dei file sorgente** negli assert → struttura del progetto.

Tutto questo è ottenuto da `analysis/tools/extract.mjs`, che legge il file e non
lo esegue. È il passo con il miglior rapporto valore/costo dell'intero progetto.

### 3. Attribuzione di responsabilità

Le librerie importate dicono dove finisce il codice dell'autore e dove inizia il
sistema operativo. In questo target: `NtQueryDirectoryFile` per l'enumerazione,
`FindFirstVolumeW` per i volumi, `SymFunctionTableAccess64` per il walking dello
stack nel crash handler, `SHGetFileInfo`/`IShellFolder` per l'integrazione shell,
assenza totale di import CRT (runtime statico). Da qui si deduce quali sottosistemi
devono essere reimplementati e quali vanno solo collegati.

### 4. Reverse mirato, guidato da domanda

Solo a questo punto si scende nel codice, e mai con l'obiettivo "capire
l'applicazione". Il ciclo è:

```
nome di setting o comando (stringa)
      -> riferimento incrociato
      -> funzione che la usa
      -> pseudocodice
      -> comportamento riprodotto in C++ mio
```

Le domande ad alto rendimento individuate finora sono elencate in
`docs/04-roadmap.md`. Le tre più significative:

- come aggrega le statistiche di cartella leggendo il Master File Table;
- come assegna i punteggi ai percorsi recenti (i campi `Score`, `Decay` e
  `Ordinal` compaiono nelle format string del file di sessione);
- perché la dimensione delle cartelle è calcolata in modo diverso su SSD, HDD e
  rete (tre setting distinti).

## Strumenti

| Strumento | Ruolo |
|---|---|
| `analysis/tools/extract.mjs` | estrazione artefatti da PE, nessuna dipendenza |
| Ghidra (headless) | disassemblaggio e decompilazione per il reverse mirato |
| REA | orchestratore MCP che espone Ghidra al mio agente di sviluppo |

REA non è indispensabile per i passi 1–3: quelli sono ricostruzione di dati e li
fa `extract.mjs`. REA serve dal passo 4 in poi, quando la domanda è "cosa fa
questa funzione".

## Perché il metodo è la parte valutabile

La difficoltà tecnica del progetto non è scrivere un file manager: è ricostruire
un'architettura di cui non si ha il sorgente, con evidenze verificabili e senza
copiare. Il risultato è ispezionabile: chiunque può rilanciare `extract.mjs` sul
binario e ottenere gli stessi artefatti, poi verificare che il codice in `src/`
non sia una copia ma una reimplementazione.

## Limiti dichiarati

- Senza simboli né PDB, la mappa fra le 1124 chiavi e le 3075 funzioni non è 1:1.
  L'abbinamento richiede il passo 4 e resta parziale.
- Il comportamento a runtime (timing, thread, gestione errori) non è osservabile
  staticamente. Un'osservazione runtime comparativa è prevista in milestone 6.
- La reimplementazione fedele dell'aspetto visivo è guidata da screenshot pubblici
  e dall'uso dell'applicazione, non da estrazione di risorse.
