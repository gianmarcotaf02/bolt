# Bolt — reimplementazione guidata da analisi statica

**Bolt** è un clone didattico di **File Pilot** (file manager nativo per Windows di
Voidstar d.o.o.), ricostruito da zero partendo dall'analisi statica del binario
distribuito.

Progetto personale, uso personale, non commerciale. **Non salva nulla su disco:**
nessuna impostazione, nessun elenco di recenti, nessuna traccia. Vedi
[docs/06-privacy.md](docs/06-privacy.md). Nessuna parte del codice
dell'applicazione originale è copiata in questo repository: vedi
[la metodologia](docs/00-metodologia.md) per cosa è stato studiato e come è stato
reimplementato in modo indipendente.

## Stato

| Componente | Stato |
|---|---|
| Analisi statica del binario target | ✅ completata — artefatti in `analysis/out/` |
| Reverse di `SumDirStats` | ✅ ricostruito — `analysis/out/reverse/FINDINGS.md` |
| Renderer UI (quad batcher + SDF) | ✅ funzionante |
| Contesto OpenGL 3.3 core | ✅ funzionante |
| Atlas di glifi e rendering testo | ✅ funzionante (via GDI) |
| **Icone di sistema reali** | ✅ funzionante (atlas da `SHGetFileInfoW`) |
| Enumerazione directory | ✅ funzionante (`FindFirstFileExW` + large fetch) |
| **Navigazione**: doppio click, Backspace, Enter, F5 | ✅ funzionante |
| **Breadcrumb cliccabile** | ✅ funzionante |
| **Colonne ordinabili + troncamento** | ✅ funzionante |
| **Inspector con metadati reali** | ✅ funzionante (testo, anteprima immagine con griglia di sfondo `InspectorHideGrid`, proprietà shell) |
| Quick access con cartelle note di sistema | ✅ funzionante, con la sezione **`RECENTS`** di sessione |
| **Anteprime (thumbnail)** | ✅ funzionante (`IShellItemImageFactory`, atlas dedicato, cache LRU) |
| **Statistiche di cartella** (job `SumDirStats`) | ✅ funzionante (worker per profilo di volume, budget di tempo, risultati progressivi) |
| **Filtro e raggruppamento** | ✅ funzionante (filtro incrementale e per tipo, filtro del nome dalla linguetta della colonna, 5 dimensioni di raggruppamento, bande collassabili) |
| **Selezione multipla e rettangolare** | ✅ funzionante (Ctrl/Shift+click, banda, Ctrl+A) |
| **Modalità di visualizzazione** | ✅ Details, List, Tiles, Icons, con i **selettori nella barra in fondo** e la misura delle anteprime (0/50/100%) |
| **Sovrappasso del mouse** | ✅ evidenzia riga o cella, si spegne uscendo dalla finestra |
| **Due corpi di testo** | ✅ 15 px per il contenuto, 13 px per il secondario |
| **Tab multipli** con barra del titolo propria | ✅ funzionante, con drag&drop (dentro un tab si diventa figli) e albero dei tab (chiudi ramo, chiudi albero, distacca) |
| **Command palette** a gruppi collassabili | ✅ funzionante (72 comandi + le cartelle note) |
| **Finestra GoTo** con recenti e segnalibri | ✅ funzionante (di sessione) |
| Cronologia di navigazione per tab | ✅ funzionante (`Alt`+`←` / `Alt`+`→`) |
| Quick access pinnato, drag&drop dei tab | 🟡 albero dei tab fatto e verificato; resta il quick access pinnato |
| Operazioni su file (copia, sposta, rinomina, cestino) | ✅ funzionanti, clipboard interoperabile con Esplora risorse |
| Drag&drop, menu contestuale della shell | ✅ funzionanti (OLE dentro e fuori, menu a più livelli con **Proprietà in-app** e **Proprietà di Windows**) |
| Persistenza configurazione, temi, hotkey | 🟡 pannello delle opzioni e `settings.ini`; mancano temi e mappa tasti rimappabile |
| Backend Direct3D 11 | ✅ secondo backend dietro `IRenderer`, scelto all'avvio (`BOLT_BACKEND=d3d11|opengl`, con fallback) |

Vedi [docs/04-roadmap.md](docs/04-roadmap.md) per la sequenza completa,
[docs/05-stato-e-prossimi-passi.md](docs/05-stato-e-prossimi-passi.md) per dove si
è arrivati e cosa resta, e
[docs/02-architettura-ricostruita.md](docs/02-architettura-ricostruita.md#7-bug-trovati-e-risolti)
per i bug di rendering trovati e come.

## Build

Richiede Visual Studio 2022 (o Build Tools) con Windows SDK e CMake ≥ 3.20.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
.\build\RelWithDebInfo\Bolt.exe
```

Controlli a runtime:

| azione | effetto |
|---|---|
| doppio click su una cartella | entra |
| doppio click su un file | apre con l'applicazione associata |
| click su un componente del percorso | naviga lì |
| click sull'intestazione di colonna | ordina; un secondo click inverte |
| colonne `Folders` / `Files` / `Size` | conteggi e dimensione ricorsivi; sottolineati mentre crescono |
| click sulla sidebar | va alla cartella nota |
| click sull'hamburger ≡ | mostra o nasconde la sidebar |
| rotella | scorre |
| `Invio` | apre l'elemento selezionato |
| `F5` | ricarica, e ricalcola le statistiche di cartella |
| `Ctrl+P` / `F4` | finestra GoTo (percorso, cartelle note, segnalibri, recenti) |
| `Ctrl+Shift+P` | command palette |
| `Ctrl+,` | pannello delle opzioni (lista cercabile, con i controlli tipizzati) |
| `Ctrl+T` / `+` | nuovo tab sulla cartella corrente |
| `Ctrl+W` / `✕` sul tab | chiude il tab (l'ultimo non è chiudibile) |
| `Ctrl+Tab` / click sul tab | cambia tab |
| frecce, `Home`, `PagSu`/`PagGiù` | sposta il cursore (salta le bande di gruppo) |
| click / `Ctrl`+click / `Shift`+click | seleziona / aggiunge / intervallo |
| trascinamento su area vuota | selezione rettangolare (`Ctrl` per sommarla) |
| `Ctrl+A` | seleziona tutto ciò che il filtro lascia a schermo |
| digita un carattere | avvia il filtro incrementale |
| `.pdf` o `*.png` nel filtro | filtra per tipo |
| `Backspace` | cancella un carattere del filtro; a filtro vuoto risale (da una radice di volume va a "This PC") |
| `Alt`+`↑` | sale di un livello; da `C:\` porta a "This PC" |
| `Esc` | azzera il filtro (non chiude: per chiudere la ✕ o `Alt+F4`) |
| `Ctrl+Shift+G` o click su `Group:` | cambia la dimensione di raggruppamento |
| `Alt`+`←` / `Alt`+`→` | indietro / avanti nella cronologia del tab |
| `Ctrl+1` / `2` / `3` / `4` | Details / List / Tiles / Icons |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | copia / taglia / incolla (clipboard di sistema) |
| `F2` | rinomina sul posto |
| `Ctrl+Shift+N` | nuova cartella, col nome gia' in scrittura |
| clic destro | menu contestuale: apri, copia, taglia, rinomina, elimina, proprieta' (in-app) e proprieta' di Windows |
| `Canc` / `Shift+Canc` | cestino / eliminazione definitiva |
| frecce in griglia | `Su`/`Giù` di una riga, `Sinistra`/`Destra` di una cella |
| click su una banda di gruppo | collassa o espande |
| `Ctrl` + `+` / `-` | zoom della UI |

Si può partire da una cartella specifica come con `explorer`:

```powershell
.\build\RelWithDebInfo\Bolt.exe "D:\qualcosa"
```

## Struttura

```
analysis/
  targets/     binari analizzati (non ridistribuiti, solo riferimento locale)
  tools/       extract.mjs, callers.mjs, refs-to.mjs, summarize-dossiers.mjs
  out/         report PE, stringhe, identificatori, spec, shader
  out/reverse/ ricostruzione di SumDirStats con pseudocodice
  rea-snapshot database di analisi Ghidra, riusato fra le sessioni
docs/          metodologia, analisi, architettura ricostruita, spec, roadmap,
               privacy
brand/         identità visiva: brief del logo, palette, prompt pronti
src/
  main.cpp          finestra Win32, contesto GL, navigazione, layout, input
  ui_renderer.*     backend OpenGL: quad batcher, una draw call per frame, mipmap
  d3d11_renderer.*  backend Direct3D 11: stessi quadrilateri e shader in HLSL
                    per icone e anteprime (ridotte fino a otto volte in lista)
  font_atlas.*      atlas di glifi GDI -> texture array, layer 0 (15 px) e 3 (13 px); ClearType
                    ridotto a copertura in scala di grigi, con gutter fra le celle
  shell_icons.*     icone di sistema -> texture array, layer 1
  shell_preview.*   anteprime via IShellItemImageFactory -> layer 2, worker thread
  dir_stats.*       statistiche di cartella ricorsive: job, profili di volume,
                    budget di tempo, risultati progressivi
  gl_loader.*       caricamento runtime delle entry point OpenGL 3.3
  file_ops.*        copia, sposta, rinomina, cestino via IFileOperation;
                    clipboard CF_HDROP con effetto preferito
  shaders/          ui.vert, ui.frag — scritti da zero
tools/
  capture-window.ps1           screenshot di una finestra, per il ciclo visivo
  setup-reverse-toolchain.ps1  installazione verificata di Ghidra + JDK + REA
```

## Licenza e uso

Codice di questo repository: uso personale e didattico.
File Pilot è un prodotto commerciale closed-source di Voidstar d.o.o.; questo
progetto non è affiliato, non lo ridistribuisce e non ne include il codice.
