# Analisi del binario

Artefatti grezzi in `analysis/out/`. Questo documento interpreta i numeri.

Target: `analysis/targets/FPilot-2026-08-31.exe`
Build 2026-08-31, beta 0.8.5 secondo il sito del produttore.
Seconda build conservata per confronto comportamentale: `FPilot-2026-02-10.exe`.

## Cosa è il file — *Verificato*

```
sha256          ab5e7bbf…63b2676
formato         PE32+ — x86-64, sottosistema windows-gui
dimensione      2.585.680 byte
entry point     0x1e0120
image base      0x140000000
entropia        6.281  (non compresso, non packato)
firma           Authenticode, GlobalSign EV Code Signing
```

Nessun packer riconosciuto, nessuna sezione a entropia anomala. L'analisi statica
diretta è percorribile.

## Dimensioni del codice — *Verificato*

Dalla sezione `.pdata`, che contiene le `RUNTIME_FUNCTION` di ogni funzione con
unwind information:

| | build 2026-02-10 | build 2026-08-31 |
|---|---|---|
| `.text` (virtual size) | 1.824.436 B | 2.191.808 B |
| funzioni con unwind info | 2.838 | **3.075** |
| byte coperti | 1.798.882 | 2.163.287 |
| mediana funzione | 126 B | **129 B** |
| p90 | 881 B | 949 B |
| massimo | 139.914 B | 333.273 B |
| < 64 B | 932 | 980 |
| 64–256 B | 1.005 | 1.104 |
| 256 B – 1 KB | 648 | 702 |
| > 1 KB | 253 | 289 |

Due letture importanti:

1. **Il conteggio è un limite inferiore.** Le leaf function senza unwind info non
   compaiono in `.pdata`. Il numero reale di funzioni è ≥ 3075.
2. **Il 68% delle funzioni è sotto i 256 byte.** Questo è il singolo fattore che
   rende il reverse praticabile: funzioni piccole vengono decompilate in C
   leggibile e verificabile. Non è un blob monolitico.

Delta fra le due build in sei mesi: **+237 funzioni, +364 KB di codice**. Il
tasso di crescita (circa 60 KB/mese) è utile per stimare lo sforzo di un clone:
un'implementazione fedele deve coprire l'equivalente di ~2,2 MB di codice compilato.

## Superficie verso il sistema operativo — *Verificato*

| DLL | funzioni | cosa implica |
|---|---|---|
| `KERNEL32.dll` | 99 | I/O, thread, sincronizzazione |
| `USER32.dll` | 74 | finestre, messaggi, input |
| `GDI32.dll` | 25 | DC, bitmap, font per il rasterizzatore |
| `SHELL32.dll` | 19 | icone, menu contestuali, `IShellFolder`, cestino |
| `OPENGL32.dll` | 18 | backend grafico primario |
| `ADVAPI32.dll` | 11 | registro, token, sicurezza |
| `ole32.dll` | 9 | COM, drag&drop |
| `PROPSYS.dll`, `NETAPI32.dll`, `dwmapi.dll`, `DWrite.dll`, `COMCTL32.dll`, `MPR.dll` | 2/2/1/1/1/1 | proprietà file, share di rete, effetti finestra, DirectWrite, common controls, mount point |
| `d3d11.dll`, `D3DCOMPILER_47.dll` | 1 + 1 | backend Direct3D, con compilazione shader a runtime |

**Non c'è nessun import CRT**: né `msvcrt.dll` né `ucrtbase.dll`. Il runtime C è
linkato staticamente. Conseguenza pratica: non si può usare la libreria standard
come ancora per riconoscere il codice dell'autore, ma in compenso tutto il codice
presente è codice dell'applicazione.

## Cosa rivela il codice macchina — *Verificato*

Stringhe di nome funzione importate che identificano scelte di progetto precise:

| simbolo | cosa significa |
|---|---|
| `NtQueryDirectoryFile` | enumerazione directory tramite API NT nativa, non `FindFirstFileW`. Permette di leggere più record per chiamata e controllare il buffering. |
| `FindFirstVolumeW`, `GetVolumePathNamesForVolumeNameW` | enumerazione volumi per GUID, non per lettera di unità |
| `SymFunctionTableAccess64`, `SymGetModuleBase64` | walking dello stack, per il crash handler (`SetUnhandledExceptionFilter` è presente) |
| `SetProcessDpiAwarenessContext` | consapevolezza DPI per-monitor |
| `CreateThreadpool`, `SetThreadpoolThreadMinimum` | work queue per le operazioni in background |
| `GetDpiForWindow` | layout che segue il DPI della finestra |
| `GetFileSizeEx`, `ReadFile`, `WriteFile`, `FlushFileBuffers` | I/O diretto, nessuna astrazione sopra Win32 |

## La tabella `AT_*`: cosa dice e cosa NON dice — *Verificato*

Il binario contiene 16 stringhe `AT_*` che sono i nomi dei tipi di attributo dei
record NTFS (Master File Table):

```
AT_StandardInformation  AT_FileName        AT_Data           AT_Bitmap
AT_AttributeList        AT_ObjectID        AT_IndexRoot      AT_IndexAllocation
AT_SecurityDescriptor   AT_VolumeName      AT_ReparsePoint   AT_EA
AT_VolumeInformation    AT_PropertySet     AT_LoggedUtilityStream
```

### Struttura esatta

Le stringhe sono contigue in `.rdata`, da `0x14021a4e0` a `0x14021a600`. I loro
indirizzi sono raccolti in un **array di 16 puntatori da 8 byte in `.data`**, a
`0x140247e50`, indicizzato per tipo di attributo: `AT_IndexRoot` è l'elemento 8,
al `0x140247e90`. Subito dopo la tabella, a `0x140247ed0`, inizia una **seconda
tabella di coppie {puntatore, lunghezza}** con i nomi dei job asincroni
dell'applicazione (vedi sotto).

### Verifica negativa: nessun parser MFT

Questa era la mia ipotesi iniziale — che le dimensioni di cartella fossero
calcolate leggendo il Master File Table — e **è stata falsificata da due
controlli indipendenti**:

1. Un parser di record MFT deve confrontare la firma del record, i quattro byte
   `"FILE"` (`0x454C4946`). In tutti i 2.191.872 byte di `.text` quell'immediato
   compare **zero volte**. Stesso esito per `"BAAD"` (`0x4F4F4241`) e per i
   marcatori di boot `"NTFS"`.
2. Le stringhe `NTFS`, `$MFT` e `USN` **non esistono nel binario**, in nessuna
   codifica.

Un parser MFT senza controllo di firma non esiste: salterebbe il primo record
corrotto letto male. La conclusione è che il binario non effettua una scansione
del volume.

### Allora a cosa serve la tabella

Gli import la collocano con precisione:

| import | uso |
|---|---|
| `DeviceIoControl` | un `FSCTL` su un handle di volume |
| `CreateFileW` + la stringa `"\\\\.\\"` | apertura di un volume raw, es. `\\.\C:` |
| `GetFileInformationByHandleEx`, `GetFinalPathNameByHandleW` | identificazione di un file |

Modello dedotto: il programma apre il volume raw per **una singola richiesta
mirata** — verosimilmente `FSCTL_GET_NTFS_FILE_RECORD` — per ottenere il record
NTFS di un file specifico e **mostrarne gli attributi all'utente**. La tabella
`AT_*` serve a tradurre il codice numerico dell'attributo in un nome leggibile.
È una funzione di ispezione, non di aggregazione: spiega perché la tabella non
ha alcun riferimento statico dal codice (è indicizzata da un enumeratore interno)
e perché il volume non viene mai scansionato.

Vedi `analysis/out/mft-attributes.md`.

## La tabella dei job asincroni — *Verificato*

A `0x140247ed0`, immediatamente dopo la tabella `AT_*`, c'è un array di 21 coppie
`{char* nome, int32 lunghezza}`:

```
PathExistence    CollectDirStats   CheckGotos        SortFiles
FilterFiles      SortFilterFiles   FilterFileTypes   CollectInspectStats
FilterText       LoadDirectory     CreateFileMap     LoadFileTypes
SortFileTypes    SumDirStats       FileMeta          NoFileError
ReadError        ParseError        SameData          Outdated
Success
```

Lettura: sono i **nomi dei job in background** (le prime quindici voci) seguiti
dai **possibili esiti** di un job (`FileMeta`, `NoFileError`, `ReadError`,
`ParseError`, `SameData`, `Outdated`, `Success`).

Questo conferma e precisa il modello di concorrenza: il programma ha una work
queue con **job tipizzati e nominati**, i cui risultati sono raccolti con un
aggiornamento periodico. `SumDirStats` è il job che somma le statistiche di
directory e corrisponde al nome `Dir_SumDirStatsWorkUpdate` che affiora in un
messaggio diagnostico; `CollectDirStats` e `CollectInspectStats` ne sono le
varianti per la lista e per l'inspector.

**Nota di metodo.** Questa tabella non ha riferimenti statici dal codice più di
quanto ne abbia la tabella `AT_*`: `rea xrefs`, `rea trace` e uno scan esaustivo
dei displacement RIP-relative su tutto `.text` non trovano alcun sito reale. Le
uniche corrispondenze sono falsi positivi verificati (uno cade dentro la funzione
che disegna i marcatori di drag&drop dei tab). Le due tabelle sono quindi lette
attraverso una base calcolata, oppure sono metadati usati solo per diagnostica.

## Come si ottengono davvero le dimensioni di cartella — *Inferito*

Le dimensioni non vengono lette dal MFT: vengono **calcolate**, e il programma
tratta il calcolo come un'operazione costosa e configurabile.

| evidenza | lettura |
|---|---|
| `FindFirstFileExW` importato (variante `Ex`, non `FindFirstFileW`) | enumerazione con `FindExInfoBasic` e `FIND_FIRST_EX_LARGE_FETCH`: meno round-trip per voce e buffer di fetch ampio |
| job `SumDirStats` con stato `Outdated` e `SameData` | il risultato è memorizzato e invalidato, non ricalcolato |
| `Setting_CalcFolderSizes` più `CalcFolderSizesSSD`, `_HDD`, `_Network` | tre strategie distinte per tipo di dispositivo |
| `Tooltip_CollectTime` e `Tooltip_CollectTimeDisabled` | esiste un budget di tempo configurabile per il calcolo |
| hotkey `HK_CalcDirsContents` | il calcolo si attiva su richiesta |
| `ReadDirectoryChangesW` | le modifiche invalidano la cache |

Il modello coerente con tutte le evidenze è: enumerazione parallela con
`FindFirstFileExW` in modalità large-fetch, eseguita da un job con budget di
tempo, con parallelismo e budget diversi fra SSD, HDD e rete, e risultati
memorizzati con invalidazione. Il vantaggio di File Pilot non sarebbe quindi un
accesso privilegiato al file system, ma un'enumerazione molto efficiente e
non bloccante.

Le tre varianti per dispositivo si spiegano con il costo per seek: su un HDD
conviene leggere sequenzialmente con poca concorrenza, su un SSD molta
concorrenza, su rete concorrenza bassa e budget più alto.

**Da verificare** (vedi `docs/04-roadmap.md`): i valori esatti di concorrenza e
budget, e se `DeviceIoControl` venga usato anche per qualcosa di diverso
dall'ispezione dei singoli file.

## Cosa rivela il `.rdata` — *Verificato*

### Shader GLSL completi

Il binario contiene i sorgenti GLSL **in chiaro, multi-riga, con i commenti
dell'autore**. Recuperati integralmente in
`analysis/out/shaders/recovered-glsl-snippets.txt`: un vertex shader e un
fragment shader `#version 330 core`, 8,4 KB totali.

Il contenuto è analizzato in [02-architettura-ricostruita.md](02-architettura-ricostruita.md).
Nota di metodo: questo è codice dell'autore originale ed è conservato come
materiale di studio. Non è stato copiato in `src/`.

### Identificatori interni come chiavi runtime

1124 identificatori usati come chiavi, non come simboli — ma con una convenzione
di prefissi che li rende leggibili:

| prefisso | conteggio | significato |
|---|---|---|
| `HK_*` | 556 | comandi della mappa tasti, ciascuno con il suo tooltip |
| `Tooltip_*` | 184 | testi di aiuto dei controlli |
| `GroupByLabel_*` | 54 | etichette dei raggruppamenti (fasce di conteggio, fasce temporali) |
| `Installer_*` | 36 | flusso di installazione |
| `Color_*` | 35 | token del sistema di temi |
| `Panel_*` | 27 | struttura del pannello lista |
| `Win_*` | 23 | integrazione shell (cestino, scorciatoie, drag&drop, notifica modifiche, share di rete) |
| `ConfirmPopup_*`, `WarningPopup_*` | 21 + 12 | inventario delle finestre di dialogo |
| `CmdGroup_*` | 19 | categorie della command palette |
| `AT_*` | 16 | attributi NTFS |
| `PanelInspector_*` | 20 | etichette dell'inspector |
| `FolderViewOptions_*` | 11 | persistenza delle opzioni di vista per cartella |
| `SearchTabs_*` | 10 | formattazione "quanto tempo fa" nella ricerca |
| altri (`P_*`, `Dir_*`, `Prog_*`, `Rename_*`) | ~10 | operazioni, statistiche, work queue |

Nomi di funzione interni affiorano anche nei messaggi diagnostici:
`Prog_CollectWorkUpdate`, `Dir_SumDirStatsWorkUpdate`, `P_PerformFilesOperation`,
`P_GetContextMenu`, `P_OpenPathInNewInstance`, `Win_ContextMenuBuild`,
`Rename_Start`. Confermano che le statistiche di cartella sono calcolate da un
work update asincrono raccolto da `Prog_`.

Vedi `analysis/out/identifiers.md` e `identifiers.json`.

### Schema dei file di configurazione

I file di stato sono scritti con `printf`, quindi le format string ne rivelano i
campi:

```
"Version": %d,          "Path": "%S",           "UncRoot": "%S",
"Score": %d,            "Decay": %d,            "Ordinal": %d,
"WindowX": %d,          "WindowY": %d,          "WindowWidth": %d,
"WindowHeight": %d,     "WindowMaximized": %s,  "QuickAccessWidth": %d,
"QuickAccessClosed": %s, "QuickAccessShowPaths": %s,
"InstallerPresentedVersion": "%S"
```

Due cose emergono:

- i percorsi nell'elenco recenti hanno **un punteggio, un decadimento e un
  ordinale**: c'è un algoritmo di ranking dell'uso. La milestone 4 lo ricostruisce.
- c'è supporto esplicito per `UncRoot`, cioè il programma distingue i percorsi di
  rete e li tratta separatamente dalla mappa dei volumi locali.

### Path di build e sorgente

```
C:\Users\Vjekoslav\Projects\Voidstar\File Pilot\Bin\FPilot.pdb
C:\Users\Vjekoslav\Projects\Voidstar\File Pilot\Source\Program\Program.c
C:\Users\Vjekoslav\Projects\Voidstar\File Pilot\Source\Program\Voidstar.h
C:\Users\Vjekoslav\Projects\Voidstar\File Pilot\Source\Win\WinMain.c
C:\Users\Vjekoslav\Projects\Voidstar\File Pilot\Source\Win\WinFontRasterizer.cpp
```

Il PDB **non è distribuito**, quindi non ci sono simboli recuperabili. I path
confermano però la struttura del repository originale: un `Program.c`
probabilmente monolitico, un livello `Win/`, un rasterizzatore di font scritto a
mano, e un modulo `Voidstar` condiviso.

## Posizione in memoria e comportamento — *Da verificare*

`RELOCS_STRIPPED` è impostato insieme a `EXECUTABLE`. Il flag indica che la
tabella di rilocazione è stata rimossa, il che normalmente implica un'immagine
non rilocabile a runtime. Il target è comunque `LARGE_ADDRESS_AWARE` su una image
base fissa. Il motivo di questa combinazione non è determinabile staticamente:
richiede un'osservazione a runtime che è fuori dal perimetro di questa fase.

## Riepilogo dello stato delle conclusioni

| Conclusione | Stato |
|---|---|
| Non packato, analizzabile staticamente | Verificato |
| ~3075 funzioni, 68% sotto 256 B | Verificato |
| Renderer = quad batcher con SDF, GLSL 3.30 | Verificato (sorgenti recuperati) |
| Statistiche cartella da record MFT | **Falsificata** — manca la firma `FILE` in `.text`; l'aggregazione avviene per enumerazione con `FindFirstFileExW` |
| `AT_*` = nomi di attributi NTFS per ispezione di un singolo file | Verificato (tabella + `DeviceIoControl`) / Inferito (`FSCTL_GET_NTFS_FILE_RECORD`) |
| Job asincroni nominati con esiti tipizzati | Verificato (21 coppie nome/lunghezza a `0x140247ed0`) |
| Tabelle `AT_*` e dei job senza riferimenti statici dal codice | Verificato (`rea xrefs`, `rea trace`, scan dei displacement) |
| Ranking dei recenti con punteggio e decadimento | Verificato (format string) / Da verificare (formula) |
| Configurazione per cartella ereditabile | Inferito (`FolderViewOptions_Inherited`, `_Recursive`) |
| Backend grafico selezionabile a runtime | Verificato (`Setting_GraphicsBackend` con valori Direct3D e OpenGL) |
| Nessun simbolo recuperabile | Verificato (PDB non distribuito) |
