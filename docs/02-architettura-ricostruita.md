# Architettura ricostruita

Come è costruita File Pilot, dedotto dal binario. Ogni sezione distingue ciò che
è osservato da ciò che è dedotto.

---

## 1. Rendering — *Verificato*

Questa è la parte con l'evidenza più forte: i sorgenti GLSL sono nel binario.

### Il modello

Tutta la UI è disegnata da **una sola coppia di shader** `#version 330 core`.
Ogni elemento visibile — sfondo di un pannello, riga di elenco, icona, bordo,
ombra — è un quad con parametri propri:

```glsl
// vertex: i quattro angoli sono passati identici a tutti e quattro i vertici;
// gl_VertexID seleziona quale angolo è questo vertice
vec2 positions[4] = vec2[](bottomLeft, bottomRight, topLeft, topRight);
vec2 vertPos = positions[gl_VertexID];
```

```
layout(location = 0..3)  in vec2 p0, p1, p2, p3;          // angoli del quad
layout(location = 4..5)  in vec2 vertMinUV, vertMaxUV;    // sotto-rettangolo
layout(location = 6)     in vec4 vertColor;
layout(location = 7)     in vec4 vertGradient;            // sinistra, destra, basso, alto
layout(location = 8)     in vec4 vertCornerRadii;         // basso-sx, alto-sx, basso-dx, alto-dx
layout(location = 9)     in vec4 vertShapeThicknessMask;  // sinistra, destra, alto, basso
layout(location = 10)    in float vertDepth;              // indice del layer della texture array
layout(location = 11)    in float vertShapeThickness;
layout(location = 12)    in float vertShapeSoftness;
layout(location = 13)    in float vertDashLength;
layout(location = 14)    in float vertGapLength;
layout(location = 15)    in float vertDashCornerRad;
```

### Le conseguenze di progetto

1. **Un quad porta il proprio stile.** Non ci sono cambi di stato fra primitive:
   un buffer di vertici, una draw call. È il motivo per cui un'interfaccia con
   migliaia di elementi resta a costo costante.

2. **Le forme sono campi di distanza con segno, non geometria.** Il fragment
   shader contiene una funzione `RoundedBoxSDF`:

   ```glsl
   float RoundedBoxSDF(vec2 centerPos, vec2 halfDim, vec4 radii)
   {
       radii.xy = (centerPos.x > 0.0) ? radii.xy : radii.zw;
       radii.x  = (centerPos.y < 0.0) ? radii.x  : radii.y;
       vec2 q = abs(centerPos) - halfDim + radii.x;
       return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radii.x;
   }
   ```

   Da qui derivano, con lo stesso codice, angoli arrotondati, bordi di spessore
   arbitrario, ombre sfumate e antialiasing, senza suddividere la geometria e
   senza multisampling sul livello UI.

3. **`vertShapeSoftness` riusa lo stesso percorso per le ombre.** Un riempimento
   con sfumatura ampia *è* un'ombra. `Tooltip` e pannelli flottanti non hanno un
   codice di disegno separato.

4. **I bordi tratteggiati sono nel fragment shader**, non nella geometria: una
   funzione cammina il perimetro del rettangolo e fa `discard` sui frammenti fuori
   dai tratti. Serve per gli indicatori di drop target.

5. **Le icone vivono in una `sampler2DArray`.** `vertDepth` seleziona il layer,
   `vertMinUV`/`vertMaxUV` il sotto-rettangolo. Il flag `uniform bool textureIsMono`
   fa leggere il canale rosso come maschera alpha:

   ```glsl
   vec4 sampled = texture(textureSampler, vec3(fragUV, fragDepth));
   vec4 mono = vec4(sampled.r);
   vec4 pixel = fragColor * mix(sampled, mono, vec4(textureIsMono));
   ```

   Cioè: **un unico atlante di texture contiene sia le icone a colori sia le
   maschere monocromatiche**, e il colore viene applicato al volo. Da qui deriva
   il setting `IconTint` e la voce di tema `Color_IconTint`.

6. **`vertShapeThicknessMask` disegna bordi parziali.** Il fragment shader calcola
   quali lati sono "bordo" e sopprime quelli mascherati, con una gestione speciale
   degli angoli (`abs(skipLeftEdge - skipBottomEdge)`): un angolo sopravvive se
   almeno uno dei due lati adiacenti è attivo. È il meccanismo che permette a un
   singolo quad di disegnare, per esempio, solo una linea di separazione inferiore
   con gli angoli inferiori arrotondati.

7. **Il gradiente è interpolato bilineare dai quattro angoli** e moltiplicato come
   alpha. Serve per gli sfumati verticali di intestazioni e righe.

### Coordinate

Il fragment shader converte da `gl_FragCoord` (origine in basso a sinistra, +Y
verso l'alto) allo spazio UI (origine in alto a sinistra, +Y verso il basso) con
un uniform `renderHeight`. Il vertex shader applica una `uniform mat4 proj`.
Lo spazio UI è quindi in pixel, con origine in alto a sinistra — la scelta giusta
per una UI, che evita conversioni continue nel codice di layout.

### Cosa ho reimplementato

`src/shaders/ui.vert` e `src/shaders/ui.frag` implementano la stessa tecnica con
codice originale: nomi propri degli attributi, funzione di distanza scritta con
una selezione diversa del raggio, dash calcolato lungo il perimetro con un
percorso separato, e una scelta diversa sulla mascheratura dei bordi. In
particolare ho **rimosso la maschera per-lato dal fragment shader** e faccio
emettere alla CPU i segmenti di bordo parziali come riempimenti di un pixel: la
soppressione condizionale nel fragment shader con gestione dei casi d'angolo è
una fonte di bug, e per i separatori di riga — che sono il caso d'uso reale — un
quad sottile produce lo stesso risultato con logica banale. La differenza è
documentata qui perché è una scelta, non una svista.

### Catena di disegno

```
1. beginFrame: svuota il buffer dei vertici, imposta il viewport
2. per ogni elemento della UI: appendi 4 vertici da 34 float
3. endFrame:  carica i vertici, disegna con un indice condiviso
              glDrawElements(GL_TRIANGLES, quad * 6, GL_UNSIGNED_INT)
```

Nota di implementazione: con 4 vertici per quad e `GL_TRIANGLE_STRIP` le strisce
di quad adiacenti si cucirebbero fra loro producendo triangoli spuri. Uso quindi
un buffer di indici che espande ogni quad in due triangoli che condividono la
diagonale (`0,1,2` e `1,2,3`): stessa economia di vertici, nessun problema di
cucitura, nessun bisogno di primitive restart.

---

## 2. Integrazione con il sistema operativo — *Verificato*

Dal prefisso `Win_*` e dalle importazioni:

| funzione dedotta | evienza |
|---|---|
| `IShellFolder` / `IShellItemImageFactory` per icone e anteprime | `Win_GetIShellFolder`, `Win_GetUIObjectOfPaths`, `SHGetFileInfo`, `Win_GetIconFromImageList`, `Win_GetImagePreview` |
| cestino reale, non cancellazione definitiva | `Win_LoadRecycleBinProc`, `Win_GetUIObjectOfRecycleBin`, e i comandi `TrashDelete`, `TrashDeleteAll`, `TrashRestore`, `TrashRestoreAll` |
| notifica modifiche file system asincrona | `Win_DirWatcherProc`, `Win_DirWatcherStop`, e le azioni `FileAdded`, `FileRemoved`, `FileModified`, `FileRenamed` |
| drag&drop OLE con descrizione | `Win_DragDropRegisterTarget`, `Win_DragDropStart`, `Win_DragDropSetDescription`, `ole32.dll` |
| enumerazione share di rete e volumi | `Win_FetchVolumesAndNetworkShares`, `Win_NetworkSharesProc`, `NETAPI32.dll`, `MPR.dll` |
| creazione collegamenti e risoluzione | `Win_CreateShortcut`, `Win_GetShortcutPathWEx` |
| registro per l'integrazione con Explorer | 8 importazioni `Reg*` da `ADVAPI32.dll` |

Il punto importante: **il cestino è reale** (via shell COM), non una cartella
`$Recycle.Bin` manipolata a mano. E le modifiche del file system arrivano da un
watcher, non da polling.

---

## 3. File system — *Verificato + Inferito*

### Enumerazione — *Verificato*

`FindFirstFileExW`, la variante `Ex` e non `FindFirstFileW`. È la coppia
`FindExInfoBasic` + `FIND_FIRST_EX_LARGE_FETCH`: le informazioni restituite per
voce sono ridotte al minimo e il buffer di fetch è ampio, quindi il numero di
round-trip verso il kernel per directory cala di molto. È questa la meccanica
dietro l'apertura istantanea di cartelle con decine di migliaia di voci.

`FindFirstFileW` è importato anch'esso, probabilmente per i percorsi che non
ammettono la variante `Ex` (volumi di rete, per esempio).

### Statistiche di cartella — *Verificato nelle strutture, Inferito nell'algoritmo*

Questa è la parte in cui la mia prima lettura è stata **sbagliata**, e vale la
pena documentare come è stata corretta perché è il metodo che conta.

Ipotesi iniziale: le dimensioni di cartella erano lette e aggregate dal Master
File Table. La tabella `AT_*` sembrava provarlo. Due controlli l'hanno
falsificata:

1. la firma dei record MFT (`"FILE"`, `0x454C4946`) non compare **mai** nei
   2.191.872 byte di `.text`, e nemmeno `"BAAD"` o `"NTFS"`;
2. le stringhe `NTFS`, `$MFT` e `USN` non esistono nel binario.

Senza controllo di firma non esiste un parser MFT. Il modello corretto è:

```
job "SumDirStats", con budget di tempo, accodato alla work queue
    per ogni sottocartella, con concorrenza scelta per tipo di dispositivo:
        FindFirstFileExW(FindExInfoBasic | FIND_FIRST_EX_LARGE_FETCH)
        accumula: byte logici, byte allocati, conteggio file, conteggio cartelle
    pubblica il risultato parziale
    ReadDirectoryChangesW invalida la voce corrispondente
```

Le evidenze che sostengono questo modello:

| evidenza | lettura |
|---|---|
| job `SumDirStats`, stato `Outdated`, stato `SameData` | risultato memorizzato e invalidato, non ricalcolato |
| `Setting_CalcFolderSizes` + `_SSD` + `_HDD` + `_Network` | tre strategie per tipo di dispositivo |
| `Tooltip_CollectTime`, `Tooltip_CollectTimeDisabled` | budget di tempo configurabile e disattivabile |
| `HK_CalcDirsContents` | il calcolo si attiva su richiesta |
| `FindFirstFileExW` | enumerazione ottimizzata per volume |
| `ReadDirectoryChangesW` | notifica delle modifiche per invalidare |
| assenza della firma MFT | nessuna scansione del volume |

Le tre varianti per dispositivo seguono dal costo per seek: su un HDD conviene
poca concorrenza e lettura sequenziale, su un SSD molta concorrenza, su rete
concorrenza bassa e budget più alto. `Setting_CalcFolderSizes` è il valore
principale, i tre specifici lo specializzano.

Le colonne della lista che questo abilita sono, testualmente:
`FoldersCount`, `FilesCount`, `TotalItems`, `FolderUsage`, `VolumeUsage`,
`FreeSpace`, `PercentFull`, `Empty1..4`.

### La macchina di `SumDirStats`, ricostruita — *Verificato*

Il reverse ha individuato i pezzi. Dettaglio completo e pseudocodice in
`analysis/out/reverse/FINDINGS.md`.

| indirizzo | dimensione | ruolo |
|---|---|---|
| `FUN_140070660` | 1.151 B | crea il job: buffer di enumerazione **≥ 1 MB** via `VirtualAlloc`, header del work item di `0x1a8` (424) byte azzerato, registra il callback |
| `FUN_140070ae0` | 346 B | completion handler: attende `completati == totale`, rilascia il riferimento atomico, invalida se `entry.generation < DAT_140249268`, copia 12 campi di risultato nella entry stabile |
| `FUN_1401e81c0` | 1.369 B | chiama `FindFirstFileExW`+`FindNextFileW`+`FindClose`, **ricorsiva** — candidato worker |
| `FUN_1401fdfe0` | 6.007 B | stesse chiamate, **ricorsiva**, 18 callee — candidato worker |
| `FUN_1401ed300` | 5.185 B | gestore delle notifiche: referenzia `"REMOVE"`, `"MODIFY"`, `"RENAME"` |

Due conseguenze che correggono il modello precedente in modo sostanziale:

1. **L'invalidazione della cache non è per evento ma per generazione.** Il
   completion handler legge un contatore globale `DAT_140249268` e scarta il
   risultato se la entry è più vecchia. È l'origine dello stato `Outdated`.
   Il gestore delle notifiche marca le entry come da ricalcolare, non le cancella.
2. **Il buffer di enumerazione è di almeno 1 MB**, allocato esplicitamente dal
   programma. Questo è il contratto di `FIND_FIRST_EX_LARGE_FETCH`: il guadagno
   non viene dal numero di chiamate ridotto, ma dal fatto che il kernel riempie
   un buffer grande in una volta. Spiega la sensazione di apertura istantanea
   meglio di qualunque ipotesi su accessi privilegiati al file system.

**Metodo, perché conta.** Queste funzioni hanno **zero chiamanti statici**: sono
invocate solo via puntatore a funzione, e nessuna tabella le contiene come
indirizzo letterale. Ci si arriva **dal nome del job**: la stringa
`"Dir_SumDirStatsWorkUpdate"` è referenziata staticamente dal wrapper di logging,
e `rea xrefs` su quella stringa porta al callback. Regola generale ricavata:
**in questo binario si entra dal codice (stringhe referenziate, import) e non dai
dati.**

**Da verificare:** quale delle due funzioni ricorsive è `SumDirStats` e quale
`LoadDirectory`; i valori esatti di concorrenza e budget di tempo per i tre
profili; se la cache sopravvive fra sessioni. Vedi `docs/04-roadmap.md`.

### Ispezione degli attributi NTFS di un file — *Inferito*

La tabella `AT_*` (16 puntatori a nomi in `.data` a `0x140247e50`, vedi
`docs/01`) insieme agli import `CreateFileW` + `DeviceIoControl` e alla stringa
`"\\\\.\\"` indica un percorso separato e mirato: apertura del volume raw e un
`FSCTL` per ottenere il record NTFS di **un singolo file**, da mostrare
all'utente. Non è aggregazione su un volume, è una query puntuale.

È coerente con il fatto che la tabella non abbia riferimenti statici dal codice:
viene indicizzata da un enumeratore interno.

### Configurazione per cartella — *Inferito*

`FolderViewOptions_Inherited`, `_Modified`, `_ModifiedFrom`, `_Saved`,
`_SavedRecursive`, `_Revert`, `_SetDefault` indicano che la vista (colonne,
ordinamento, raggruppamento) si salva per percorso, con ereditarietà verso i
figli e la possibilità di applicare ricorsivamente o di tornare al valore
ereditato. È un modello di override con catena di precedenza, non un semplice
dizionario.

---

## 4. Configurazione e stato — *Verificato*

Tre file distinti, con versionamento e migrazione esplicita (i messaggi
`"Your FPilot-Config.json file (version %1) is from an older format."` e le
`WarningPopup_Corrupted*`):

| file | posizione | contenuto dedotto |
|---|---|---|
| `FPilot-Config.json` | `%APPDATA%\Voidstar\FilePilot` | impostazioni globali (`Setting_*`) |
| `FPilot-UserData.json` | `%APPDATA%\Voidstar\FilePilot` | dati utente: bookmark, scorciatoie, comandi rapidi, layout, temi personalizzati |
| `FPilot-Session.json` | `%LOCALAPPDATA%\Voidstar\FilePilot` | stato della sessione: finestre, tab, pannelli, ultimi percorsi |

I campi osservati direttamente nelle format string, con il significato:

```
"Version"                     int    versione dello schema, per la migrazione
"Path"                        string percorso della voce
"UncRoot"                     string radice di rete associata
"Score" / "Decay" / "Ordinal" int    ranking d'uso dei recenti
"WindowX/Y/Width/Height"      int    geometria della finestra
"WindowMaximized"             bool
"QuickAccessWidth"            int    larghezza della barra quick access
"QuickAccessClosed"           bool
"QuickAccessShowPaths"        bool
```

**Il programma si copia in `%LOCALAPPDATA%\Voidstar\FilePilot\FPilot.exe`** (la
stringa `"%S/FPilot.exe"` e il messaggio `"Failed to copy the executable file."`).
Serve per l'aggiornamento automatico: l'istanza residente si sostituisce.

---

## 5. Concorrenza — *Verificato*

`CreateThreadpool` e `SetThreadpoolThreadMinimum` più i nomi `Prog_CollectWorkUpdate`
e `Dir_SumDirStatsWorkUpdate` indicano un modello a work queue: le operazioni
lunghe (statistiche di cartella, thumbnail, ricerca) sono accodate e i risultati sono
raccolti con un aggiornamento periodico, senza bloccare il thread della UI. Il
nome `Collect` suggerisce che i risultati parziali siano aggregati in un
sottoprodotto della coda, coerentemente con l'enfasi del produttore sulla UI che
non si blocca mai.

La stringa `"%lu - Stop Prog_CollectWorkUpdate in '%S', file '%S' does not exist
in map anymore."` rivela che il lavoro in volo è indicizzato per file in una
mappa, e che la cancellazione deve tollerare file rimossi nel frattempo.

---

## 6. Cosa resta ignoto

- La formula esatta del punteggio dei recenti.
- I valori di concorrenza e budget di tempo del job `SumDirStats`, e come
differiscono fra SSD, HDD e rete.
- La gestione dei reparse point e dei collegamenti fisici multipli nel conteggio.
- Il throttling del calcolo parallelo — la stringa `Tooltip_CollectTime` e
  `Tooltip_CollectTimeDisabled` suggeriscono un budget di tempo configurabile per
  il calcolo delle dimensioni, ma il meccanismo non è osservabile staticamente.
- La struttura dati dell'indice di ricerca. Nessuna stringa `USN` o `MFT` è
  affiorata: la ricerca è quindi un indice in memoria costruito enumerando, non una
  query al journal di aggiornamento di NTFS.
- Il comportamento a runtime: timing, priorità dei thread, gestione degli errori
  di I/O parziali.

Questi punti sono il perimetro di lavoro del reverse mirato descritto in
[04-roadmap.md](04-roadmap.md).

---

## 7. Bug trovati e risolti

Documentati perché il metodo conta quanto il risultato: quasi tutti sono stati
trovati **guardando uno screenshot della propria applicazione**, non leggendo il
codice. Il ciclo usato è in `tools/capture-window.ps1`. **Nessuna voce è aperta**: le
settantasei del registro sono chiuse. Le ultime sedici: §7.61 — il ritaglio
per-quad, che non è un difetto visto a schermo ma un **debito** saldato, con accanto lo
strumento difettoso che lo nascondeva (un contatore che ripartiva a ogni lotto); §7.62,
il menu contestuale che offriva i comandi di File Pilot; §7.63, il percorso degli shader
della macchina di compilazione finito dentro il pacchetto — il primo difetto trovato
**impacchettando** invece che guardando; §7.64, l'Accesso rapido che apriva le cartelle
come posizioni codificate, con i comandi per percorso rifiutati; §7.65, le icone dei
`.lnk`, i nomi della Home e la casa che era una cartella; §7.66, il conto dei byte che
seguiva il tempo della shell invece dei byte; §7.67, la riga spenta che esisteva solo
nel pannello delle opzioni; §7.68, il recente che si chiamava col suo percorso; §7.69, il
pannello dell'operazione che era un muro e la coda che non c'era; §7.70, la selezione che
tornava alla prima riga; §7.71, il conto delle voci che non tornava; §7.72, la freccia dell'ordine mangiata dal
taglio; §7.73, le icone della Home che non erano quelle della barra laterale; e §7.74, la
Home senza date e senza numeri, con «Data modifica» sparita da tutte le liste; e §7.75, la
chiusura da venti secondi che fermava l'aggiornamento in app; e §7.76, nella griglia tutte
le cartelle con l'icona del cestino.

### 7.1 `gl_VertexID` in una draw call indicizzata — *il bug che nascondeva tutto*

Il vertex shader selezionava l'angolo del quad con
`(gl_VertexID == 0) ? aCornerBL : …`. Con `glDrawElements`, `gl_VertexID` **non è
un contatore 0–3**: è il valore letto dal buffer degli indici. Poiché i quad
usano gli indici `4N … 4N+3`, dal secondo quad in poi nessuna condizione era vera
e tutti e quattro i vertici ricadevano sullo stesso angolo: geometria di area
nulla.

Il sintomo era ingannevole: l'unica cosa visibile era il **primo** quad disegnato
— lo sfondo della finestra — che per caso era identico al colore di clear. La
finestra sembrava vuota per un colore di sfondo sbagliato, mentre in realtà
nessun elemento veniva rasterizzato.

Correzione: `int corner = gl_VertexID & 3;` — i bit bassi selezionano l'angolo
dentro il quad, perché ogni quad inizia a un multiplo di quattro.

Nota secondaria: la prima stesura della correzione usava `const int corner`, che
**non compila** in GLSL. `gl_VertexID` è un input built-in, non un'espressione
costante, e GLSL richiede che l'inizializzatore di un `const` sia costante. Il
errore si è manifestato come un MessageBox "Startup failed", non come output del
compilatore, perché quel percorso di errore esiste solo prima che la finestra ci
sia.

### 7.2 UV dell'atlas dei glifi normalizzate sulla dimensione sbagliata

`FontAtlas` costruisce una bitmap di glifi grande quanto serve alla griglia —
256×256 per 95 caratteri a 15 px — ma la carica in un layer della texture array
che è 1024×1024. Le UV erano normalizzate sui 256 del bitmap invece che sui 1024
del layer: ogni glifo campionava una zona diversa dell'atlas, cioè quasi sempre
vuota.

Il sintomo era caratteristico: icone perfette, pannelli perfetti, testo
**assente** (qualche sbavatura, cioè il campionamento del bordo della regione
caricata). La differenza fra icone e testo era esattamente la normalizzazione:
`ShellIconAtlas` riceveva già la dimensione del layer.

Correzione: `FontAtlas::Build` riceve `textureLayerSize` e normalizza su quello.
La lezione generale è che **la dimensione della bitmap sorgente e la dimensione
del layer di destinazione sono due grandezze diverse** e vanno tenute separate
nel codice, non condivise per comodità.

### 7.3 Colonne che si sovrappongono

La colonna `Type` conteneva nomi di tipo localizzati (`Cartella di file`, 108 px)
in una fascia larga 89 px, quindi il testo invadeva `Modified`. Il renderer non
ha una scissor rectangle, quindi il ritaglio deve avvenire nel codice di layout.

Correzione: una funzione `FitText` che tronca con ellissi alla larghezza
disponibile, applicata a nome e tipo; proporzioni delle colonne ricalcolate sulla
larghezza reale del testo più largo; `Size` allineata a destra perché i punti
decimali si incolonnino.

### 7.4 Un commento che mangiava la riga successiva

`return path;   // already a drive root such as C:\` — il backslash finale di un
commento `//` è il **carattere di continuazione di riga**, quindi la riga
successiva veniva commentata e una variabile spariva, con un errore del
compilatore che puntava alla riga sbagliata. Un classico del C che vale la pena
non dimenticare.

### 7.5 `QuadCount` che sovrastimava di trenta volte

`QuadCount()` divideva la dimensione del buffer dei vertici per quattro, ma un
quad non è quattro **float**: sono quattro vertici da trenta float ciascuno. Il
numero mostrato nella barra di stato era quindi gonfiato di trenta volte — la
cartella `Downloads` sembrava disegnare 41 100 quad al frame invece di 1 370. Il
difetto era nel valore diagnostico, non nel disegno, quindi non produceva
sintomi visibili: è il tipo di errore che sopravvive a lungo proprio perché non
fa male a nessuno. La correzione divide per `kFloatsPerVertex * kVerticesPerQuad`,
e la costante vive nel file di implementazione, quindi il metodo è dichiarato
nell'header e definito accanto a `End`.


### 7.6 Un'etichetta stampata sopra la miniatura

Nelle viste a griglia l'altezza della cella era un numero fisso scelto a occhio —
92 px per le icone, 158 per le tessere — mentre l'etichetta occupa due righe di
testo. Con un'icona da 64 px e due righe da 15, il contenuto chiede 94 px in una
cella da 92: l'etichetta finiva **sopra** la miniatura, e si vedeva il nome
stampato sulla foto.

La correzione non allunga i numeri: l'altezza **deriva** da icona più etichetta, e
nel disegno la dimensione dell'icona viene comunque ridotta se lo spazio sopra
l'etichetta non basta. Un valore derivato non può entrare in disaccordo con le sue
parti; un valore fissato a occhio sì, e ogni volta che una delle due parti cambia.

### 7.7 `Esc` che chiudeva l'applicazione

`Esc` azzerava il filtro e, se il filtro era già vuoto, chiudeva la finestra.
Nessuna applicazione Windows lo fa, ed è una trappola facile da innescare: due
pressioni di seguito e il lavoro sparisce. Ora `Esc` azzera il filtro e si ferma;
per chiudere ci sono la ✕ e `Alt`+`F4`.

È stato trovato per caso, mentre un mio script di prova mandava un `Esc` di troppo
per pulire lo stato: il difetto era reale, il modo in cui è emerso no.

### 7.8 Un alone di due pixel su ogni forma

Nella shader, il riempimento solido calcolava la copertura con
`smoothstep(0, 2, distance)`: la transizione partiva dal bordo e si esauriva **due
pixel fuori**. Ogni rettangolo e ogni quad di glifo portava quindi un alone
morbido, e l'interfaccia intera sembrava fuori fuoco — il difetto era uniforme e
per questo difficile da attribuire a una causa sola.

La copertura corretta attraversa 0,5 esattamente sul bordo: `clamp(0.5 - distance,
0, 1)` porta a 1 mezzo pixel dentro e a 0 mezzo pixel fuori. I bordi tracciati
(`vStrokeWidth`) erano già centrati e non sono stati toccati; le ombre usano il
proprio percorso di sfocatura, ora selezionato da `vSoftness` invece che dal
valore di feather.

### 7.9 L'atlante dei glifi senza gutter

Le celle dell'atlante erano affiancate senza spazio, e ogni quad era largo un
pixel in più dell'avanzamento del glifo. Con filtro `GL_LINEAR`, il pixel di
troppo campionava la prima colonna del glifo successivo: ghosting lungo i
tratti. Gli atlanti delle icone e delle anteprime avevano già un gutter di 2 px;
quello dei glifi era l'unico a non averlo. Ora le celle sono separate da 2 px e
le UV coprono solo l'inchiostro.

### 7.10 Texture ridotte senza catena di mipmap

La texture array era creata con `GL_TEXTURE_MAX_LEVEL 0` e filtro `GL_LINEAR`.
Le icone stanno in celle da 32 px e le anteprime in celle da 128, ma nella lista
si disegnano a 16: una riduzione fino a otto volte che campionava **quattro**
texel. Il risultato erano le anteprime a blocchi. Ora la catena di mipmap esiste
e viene rigenerata al massimo una volta per frame, prima del disegno, solo se
qualcosa è stato caricato.

### 7.11 Antialiasing in scala di grigi invece di ClearType

I glifi erano rasterizzati con `ANTIALIASED_QUALITY`. A queste dimensioni GDI
produce tratti più sottili e chiari di quelli di ogni altra finestra del
desktop, che usa ClearType con il suo ispessimento dei gambi. Accanto a Explorer
il nostro testo sembrava debole.

Ora la rasterizzazione è `CLEARTYPE_QUALITY` e la copertura si ottiene dalla
**media dei tre canali**: ClearType scrive tre coperture subpixel, la loro media è
la copertura in scala di grigi, e le frange di colore non sopravvivono alla
conversione. Le posizioni dei glifi vengono inoltre arrotondate al pixel intero:
l'avanzamento resta frazionario per non far derivare la spaziatura, ma un glifo
che cade su mezzo pixel viene ricampionato e perde nitidezza.

### 7.12 Sei dimensioni secondarie, tutte ricampionate

Il testo secondario — intestazioni di colonna, etichette dell'inspector, barra di
stato, voci dei menu — era disegnato con l'atlante del corpo a scala frazionaria:
sei valori diversi, da 0.80 a 0.95. Due difetti in uno.

Il primo è che **disegnare a 0.9 un glifo rasterizzato per 15 px lo ricampiona**:
l'hinting che GDI ha applicato vale per 15 px e non per 13.5, quindi ogni glifo
perde esattamente la nitidezza che il rasterizzatore aveva prodotto. Si vede
soprattutto sulle lettere con gambi verticali, dove il ricampionamento li
distribuisce su due colonne di pixel.

Il secondo è che sei dimensioni secondarie non sono una gerarchia: sono rumore.
Una barra di stato a 0.90 e un'etichetta a 0.92 non comunicano nulla di diverso,
e nessuno legge la differenza.

Ora c'è **un secondo corpo, rasterizzato**, su un quarto layer dell'array di
texture, disegnato a scala 1.0: una dimensione sola, e quella dimensione è
rasterizzata invece che ricampionata.

**Quanto più piccolo del corpo principale, e perché è derivato.** La differenza è
una costante sola (`AppState::kSmallFontDelta`), e vale 4: il dettaglio sta a 11 px
quando il nome sta a 15. È **derivata** e non scelta a parte perché la dimensione
del corpo principale è un'opzione — chi la cambia dalle opzioni vuole che
intestazioni, valori e barra di stato seguano, e due numeri indipendenti si
scollerebbero al primo ritocco. La richiesta dell'autore è stata «un font più
piccolo per i dettagli come dimensione, tipo file, i valori»: il secondario scrive
esattamente quelli, e sta più piccolo perché si legge **dopo** il nome, non invece
del nome.

*Limite dichiarato.* Il corpo secondario è **uno**: ispettore, intestazioni delle
colonne, barra di stato, palette e suggerimenti lo condividono. Se una superficie
sola dovesse avere una misura sua — l'ispettore più piccolo e la barra di stato no
— servirebbe un **terzo corpo**, cioè un settimo strato dell'array di texture, e
sono quattro megabyte di memoria video per una differenza di un pixel. Non vale il
prezzo, e si dice invece di pagarlo di nascosto.

Il costo è un layer di atlante in più su quattro: la dimensione delle texture non
cambia, cambia il numero di strati. Il corpo resta a 15 px, e resta deliberatamente
alla sua dimensione nativa, perché è quello dove l'hinting conta di più.

**Limite dichiarato:** lo zoom con `Ctrl`+`+`/`-` scala anche il corpo secondario,
quindi a zoom diverso da 1.0 il ricampionamento torna. Per eliminarlo servirebbe
una rasterizzazione a runtime per ogni dimensione, che è un altro mestiere.

### 7.13 Un conflitto di nome che congelava l'applicazione, di nascosto

Incollare un file in una cartella dove esisteva già un file con lo stesso nome
non faceva nulla: nessuna domanda, nessun errore, nessun rinomina. E non faceva
nulla **per sempre**: da quel momento l'applicazione non ridisegnava più.

Il difetto era in due opzioni che sembravano innocue. `FOF_NOCONFIRMATION` toglie
le domande, ed è giusto; ma senza `FOF_RENAMEONCOLLISION` un nome già occupato non
ha né una domanda da fare né una regola da seguire, quindi l'operazione viene
**saltata**. La shell però crea comunque la propria finestra di stato
(`OperationStatusWindow`), che resta lì e pompa messaggi: il programma non è
bloccato nel senso del task manager, è **dentro un ciclo di messaggi modale che
non finisce mai**.

Due cose rendono questo difetto cattivo:

**Sembra un difetto di rendering.** Le catture a due secondi di distanza erano
identiche al pixel — l'unico modo di distinguere "fermo" da "lento" era
confrontare due fotogrammi. Un'applicazione che risponde a `WM_NULL` sembra viva;
il fatto che risponda perché è dentro un ciclo modale non si vede da fuori.

**La finestra che blocca non si vede.** Stava dietro la finestra principale. La
diagnosi non poteva arrivare guardando lo schermo: è arrivata enumerando le
finestre del processo e leggendone i titoli. C'era scritto `2 azioni interrotte`.

La correzione aggiunge `FOF_RENAMEONCOLLISION`: un nome occupato diventa
`alfa - Copia.txt`. Prevedibile, immediato, nessun dialogo. Esplora risorse in
questo caso chiede; qui si sceglie, e si dichiara — un dialogo che non si vede è
peggio di una regola fissa.

### 7.14 Due barre del titolo, e il banco di prova che non poteva vederle

Per tutta la durata di M6 l'applicazione ha mostrato **due** barre del titolo: quella
di sistema, chiara, con `Bolt — C:\...\Downloads`, e quella disegnata da noi
sotto. Nessuna delle mie verifiche l'aveva mai vista.

**Perché non la vedevo.** Il mio strumento di cattura leggeva `GetClientRect`,
cioè l'area cliente della finestra. La barra di sistema vive *fuori* dall'area
cliente. Ogni singola cattura della sessione aveva ritagliato via esattamente la
regione di cui l'utente stava parlando. Il banco di prova non era rotto: era
**cieco in un punto preciso**, e quel punto era quello sbagliato.

L'ha trovata l'utente, non io. Un difetto visibile a chiunque usi il programma e
invisibile a chi lo verifica è la definizione di banco di prova sbagliato.

**La causa.** Il gestore c'era ed era scritto come lo scrivono tutti:

```cpp
case WM_NCCALCSIZE:
    if (wparam == TRUE) { ...; return 0; }
    break;
```

Il guasto è in quella condizione. Il messaggio arrivava con **`wParam` a `FALSE`**,
non a `TRUE`: la condizione non entrava mai, il caso cadeva su `DefWindowProc`, e
`DefWindowProc` ricalcola l'area non-cliente **dallo stile della finestra** — che
contiene `WS_CAPTION` — quindi rimetteva la didascalia. Il difetto non era nel
codice che c'era: era nel ramo che mancava.

L'ho scoperto scrivendo il valore nel log invece di continuare a dedurlo. Una
riga di registro ha risolto quello che dieci minuti di ragionamento avevano solo
aggirato.

**Il secondo difetto, nascosto dal primo.** Togliere l'area non-cliente toglie
anche ciò che DWM disegnava *attorno*: ombra e angoli arrotondati. Il bordo della
finestra diventava un salto netto di colore — misurato: nessuna sfumatura in otto
pixel. Su Windows 11 l'applicazione sembrava piatta accanto a tutte le altre.

Si rimedia con `DwmExtendFrameIntoClientArea` di un pixel per lato: DWM disegna il
riquadro **dentro** l'area cliente, quindi l'ombra e la curva tornano e la
didascalia **non** torna. Più `DWMWA_USE_IMMERSIVE_DARK_MODE`, perché il riquadro
di DWM segue il tema di sistema e un tema chiaro avrebbe disegnato una linea
bianca su un bordo scuro, e `DWMWA_WINDOW_CORNER_PREFERENCE`, perché gli angoli
arrotondati di Windows 11 valgono per le finestre con didascalia e questa non ne
ha.

Verificato misurando i pixel, non guardando: gradiente di quattordici pixel
attorno al bordo, linea di un pixel al posto giusto, e la curva dell'angolo
visibile nella mappa dei pixel.

### 7.15 La e commerciale degli acceleratori, a schermo

Le etichette che la shell restituisce sono etichette **da menu di Windows**, e in
un menu di Windows la e commerciale segnala il tasto di scelta rapida:
`Aggiungi ad &Accesso rapido`, `Apri con &Visual Studio`. Il nostro menu non ha
tasti di scelta rapida — si naviga con le frecce — quindi l'occhiello finiva a
schermo, in mezzo a una parola.

Trovato guardando la **prima** cattura del menu nuovo, non leggendo il codice:
dal codice `GetMenuItemInfoW` restituisce l'etichetta e basta, ed è vero. Che
quell'etichetta contenga una convenzione che non ci appartiene si vede solo
stampata.

La regola completa ha una seconda metà, che si scopre solo se la si scrive:
`&&` in un'etichetta di menu significa **una `&` vera**. Togliere ogni e
commerciale avrebbe rotto i nomi che ne contengono una — `Find && Replace`
sarebbe diventato `Find Replace`. Quindi si toglie la singola e si conserva la
coppia.

### 7.16 Un menu più alto della finestra

Il menu contestuale si dimensionava sul proprio contenuto e si ancorava al
puntatore, con il solo vincolo di non uscire dal bordo destro. Finché le voci
erano undici il vincolo non si incontrava mai; con i verbi della shell sono
diventate trentuno, e su una finestra di 760 px il menu **sporgeva sotto il bordo
inferiore**. Le ultime voci — `Incolla qui`, `Nuova cartella`, `Aggiorna` — non
erano né leggibili né cliccabili, e il sottotitolo con il conteggio della
selezione spariva del tutto.

Il difetto non era nel disegno: era nel fatto che **un menu che scorre non era
previsto**. La correzione non è un taglio grafico: le voci visibili sono un
intervallo intero, da `firstItem` in poi, e il pannello è alto esattamente quanto
le voci che entrano. Fuori da quell'intervallo una voce non si disegna, non si
sovrappassa e non si clicca — quindi non serve nessun taglio, perché quello che
non si disegna non sborda. Il taglio cade sempre **fra** due voci: una voce
mostrata a metà si sceglie per sbaglio.

Restava da dirlo. Un menu che scorre e non lo dichiara sembra un menu a cui
mancano delle voci, quindi c'è una barra sottile sul bordo destro, alta quanto la
parte visibile. E le frecce non bastano più: `PagSu`/`PagGiù` scorrono di cinque
voci, `Home`/`Fine` vanno agli estremi.

### 7.17 La rotella fermata dal sovrappasso

Con il menu aperto il puntatore sceglie la voce sotto di sé. La rotella scorreva
la finestra e poi la riportava indietro per **tenere visibile la voce scelta** —
regola giusta per le frecce, sbagliata per la rotella: con il puntatore fermo
sulla prima voce il menu si rifiutava di scorrere, come se la rotella non
funzionasse.

Le due regole sono due: le frecce **portano con sé** la scelta, la rotella
scorre e basta. E se scorrendo la voce scelta esce dal pannello, la scelta cade —
perché l'Invio esegue quello che si vede, e una voce invisibile non si vede.

Verificato su una finestra ridotta a 460 px di altezza, dove il menu straripa di
una decina di voci: da `Apri` a `Copia` sono **quattro** righe con quattro tacche
di rotella, anche con il puntatore fermo sulla prima voce.

La prima prova non era decidibile, e vale la pena dirlo: con il menu più alto
della finestra di **una sola** voce, la rotella toccava il tetto comunque e non si
distinguerà mai il caso rotto dal caso giusto. Il banco di prova ha dovuto
cambiare forma, non la misura.

### 7.18 Il menu della cartella sbagliata — plausibile e pericoloso

Col tasto destro sul fondo della lista il contesto giusto è la **vista**: la
cartella come contenitore, il cui menu è `Vista`, `Ordina per`, `Nuovo`,
`Incolla`, `Proprietà`. La shell lo espone con
`IShellFolder::CreateViewObject(IID_IContextMenu)`.

Il primo tentativo ha usato la strada già scritta per i file —
`GetUIObjectOf(IID_IContextMenu)` — passandole il percorso della cartella. La
chiamata **riesce**: la cartella è a sua volta un elemento dentro la propria
cartella padre, quindi la shell risponde con il menu della cartella *come
elemento*. Un menu che si apre, si legge e sembra giusto.

A leggerlo, però, c'erano `Taglia`, `Copia` ed `Elimina` sulla cartella aperta.
Un `Elimina` a destra di un fondo non è quello che si aspetta nessuno, e la voce
era a un clic di distanza in ogni tasto destro sbagliato.

Non c'era nessun errore da nessuna parte: nessun `HRESULT` fallito, nessuna
stringa vuota, nessun codice di uscita. La differenza fra i due contesti è una
scelta dell'API, e l'unico modo di accorgersene è **guardare le voci restituite**
— cioè il registro diagnostico, che le stampa tutte. Il difetto successivo, se
non ci fosse stato il registro, sarebbe stato un `Elimina` che cancella la
cartella sotto gli occhi dell'utente.

### 7.19 Il sottomenu che risponde `S_OK` e non fa niente

Il menu contestuale sa annidare, e i sottomenu della shell si aprono e si
leggono: `Nuovo` ne ha venti, `Invia a` undici. Chiamando `InvokeCommand` su una
loro voce si riceve `S_OK` — e non succede niente.

Sospetti provati, uno per uno: la maschera dei flag (`CMIC_MASK_FLAG_NO_UI`
spento, `CMIC_MASK_ASYNCOK` acceso — identico), l'identificativo (giusto: è
l'offset dall'inizio della banda riservata, lo stesso che esegue `Proprietà` e
`Copia come percorso`), e la via documentata per i sottomenu — `WM_COMMAND`
consegnato all'`IContextMenu3` — che su questo sistema non esiste
(`QueryInterface` rifiuta).

Poi il confronto che risolve: **il sottomenu di 7-Zip, che è di un'estensione,
esegue eccome** — apre il suo dialogo, verificato trovando la sua `#32770` fra le
finestre del sistema. Quindi la distinzione non è fra voci radice e voci
annidate: è fra i sottomenu che l'estensione si disegna e i sottomenu che la
shell **popola da sé**. I secondi la shell li consegna alla finestra del menu, e
noi una finestra di menu non l'abbiamo — l'HMENU lo leggiamo e basta, non lo
mostriamo mai.

La conclusione non è "ritentare": è che quelle voci **non si offrono**. Un elenco
che risponde `S_OK` e non fa niente è peggio di un elenco che manca, perché
l'utente conclude che il comando non serve a niente, non che non è partito. Il
filtro è per verbo canonico (`New`, `Windows.Share`, e le voci senza verbo, che
non si possono attribuire), e tiene i sottomenu delle estensioni.

**Il difetto vero era il silenzio, non il limite.** `InvokeCommand` che riesce e
non fa niente non è distinguibile da un comando che ha funzionato: l'unico modo di
accorgersene è guardare l'effetto sul disco, e per vederlo sono state necessarie
due prove — una che non produce niente (`documenti.zip` che non compare) e una
che produce qualcosa (la finestra di 7-Zip). Una sola delle due non avrebbe
distinto "non si può" da "non ho guardato".

### 7.20 Il menu in inglese accanto a tutto il resto in italiano

Le etichette del menu "Nuovo" sono stringhe **indirette**: il registro non
contiene "Cartella compressa", contiene

```
@C:\WINDOWS\system32\zipfldr.dll,-10194
```

cioè "il numero 10194 dentro la risorsa di quella DLL", tradotto nella lingua
dell'utente. La prima versione le risolveva con `RegLoadMUIString`, che è la
funzione di advapi32 nata per questo e non avrebbe aggiunto nessuna libreria. Il
risultato, guardato a schermo:

```
Archivio WinRAR          Archive WinRAR
Compressed (zipped) Folder
Rich Text Format
Shortcut
Library Folder
```

Metà menu in inglese, metà in italiano. La causa misurata: `RegLoadMUIString`
sulla stessa stringa restituisce **errore 87**, `ERROR_INVALID_PARAMETER`, e il
codice ripiegava sull'unico valore disponibile, cioè il nome registrato dal
vendor, che è in inglese.

`SHLoadIndirectString` (shlwapi) restituisce `Nuova cartella compressa`. È la
funzione che usa la shell, quindi i nomi adesso sono quelli che si vedono in
Esplora risorse. Il costo è una libreria in più nel link, e la ragione sta in una
riga: `RegLoadMUIString` non risolve queste stringhe.

Questo difetto non si vede né leggendo il codice — chiamare `RegLoadMUIString` è
corretto — né controllando che il menu si apra: si vede solo **leggendo le
etichette una per una**, in una lingua che si sa riconoscere.

### 7.21 Il file nuovo che nasceva vuoto, in silenzio

`Nuova cartella compressa` produce un `.zip`. La prima versione creava il file
con `IFileOperation::NewItem` e nient'altro: `S_OK`, nel posto giusto, col nome
giusto (`Nuova cartella compressa.zip`) — e **zero byte**. Un file che il
sistema dichiara creato, che compare nella lista, e che nessun programma riesce ad
aprire.

La causa: `NewItem` senza un modello crea un file vuoto, e i dati del modello
(`Data`, `FileName`, `NullFile`) li applica chi li legge. Per il `.zip` sono 22
byte scritti nel registro, per il `.docx` è una copia, per il `.rtf` è la stringa
`{\rtf1}`. Adesso li legge `src/shell_new.cpp` e li scrive `FillNewItemContent`.

Verificato contando i byte e aprendo il file: lo zip è di **22 byte** e `unzip -l`
lo legge come archivio valido vuoto; il `.xlsx` è di **6.111 byte**, cioè la copia
del modello di Office.

**Il secondo strato, trovato misurando i modelli.** I modelli di Office e di
LibreOffice *sono* gusci di zero byte su disco: `WORD.DOCX` nella cartella VFS di
Office è un file vuoto, e il contenuto vero lo produce il provider di Office, che
da fuori non si raggiunge. Quindi la copia "riesce" e il risultato è un `.docx` di
zero byte, che Word rifiuta di aprire.

La copia riuscita non è un errore, ma l'utente non deve scoprirlo aprendo il
file: adesso, se il file creato è di zero byte e un modello c'era, la barra di
stato lo dice — *"Il modello di questo tipo è vuoto: … è stato creato vuoto"*.
Un'azione che riesce producendo qualcosa di inutilizzabile va dichiarata come
un'azione che fallisce.

### 7.22 Il comando che risponde `TRUE` e non fa niente

`Invia a >` di Bolt legge la cartella SendTo e per ogni voce fa quello che
sembra ovvio: la avvia, con i percorsi scelti come argomenti.

```cpp
ShellExecuteExW(&info);   // lpFile = la voce, lpParameters = i percorsi scelti
```

Il risultato misurato, sul banco di prova, su una voce che in Esplora risorse
funziona tutti i giorni:

```
invio a voce 0: avviata          ← ShellExecuteEx ha restituito TRUE
nessun file creato               ← e non è successo niente
```

Nessun errore, nessun file, nessuna finestra. La peggiore delle risposte: una che
dichiara riuscito quello che non è avvenuto, e che quindi non si cerca.

La causa sta nel registro, e si legge in due valori:

| valore | dice |
|---|---|
| `.ZFSendToTarget` → il tipo della voce | la voce **non è un programma**: è un file che contiene un CLSID |
| `...\ShellEx\DropHandler` sul CLSID | quel CLSID si aspetta i file **lasciati cadere**, non una riga di comando |
| `NoOpen` sul CLSID | *"Drag Files onto this icon to compress them"* |

Cioè: avviare la voce è la domanda sbagliata. La voce è un **bersaglio di
caduta**, e i file glieli deve mettere qualcuno: `ShellExecuteEx` la "apre" e le
lascia le mani vuote.

**La correzione** è fare quello che farebbe il mouse: prendere il bersaglio di
caduta della voce (`IShellFolder::GetUIObjectOf` con `IID_IDropTarget`),
costruire l'oggetto dati della selezione (`SHCreateDataObject` dai PIDL dei
percorsi scelti) e lasciarlo cadere con `DragEnter` + `Drop` e
`DROPEFFECT_COPY`. La stessa voce poi produce `relazione.zip` di 218 byte dentro
la sandbox, con i file selezionati dentro.

**Il difetto non è nel codice, è nella premessa.** `ShellExecuteEx` su una voce
SendTo è il modo documentato, ed è corretto per le voci che sono collegamenti a
programmi — quelle funzionano davvero così. Ma metà della cartella SendTo non è
fatta di programmi, e per quelle il successo dichiarato è una bugia. Non si vede
leggendo il codice né guardando il menu: si vede **solo provando a eseguire una
voce** e contando i byte che ne escono. È la seconda volta che il tema si presenta
— la prima è il sottomenu della shell che risponde `S_OK` e non fa niente (§7.19) —
e la lezione è la stessa: per la shell, "ho eseguito il comando" e "è successo
qualcosa" sono due cose diverse, e l'unica delle due che si può verificare è la
seconda.

### 7.23 L'esito che non è quello che viene restituito

Trascinare un file da Bolt a Esplora risorse: il file **si sposta davvero**, e la
lista di Bolt continua a mostrarlo. Un file che non c'è più, in una lista che dice
che c'è: premuto subito dopo, il tasto Canc cancella l'originale.

La causa, letta nel registro diagnostico invece che ragionata:

```
trascinamento in uscita: 1 percorsi, effetto restituito 0, esito dichiarato 2
```

`0` è `DROPEFFECT_NONE`, `2` è `DROPEFFECT_MOVE`. La destinazione ha compiuto lo
spostamento, l'ha **dichiarato**, e ha restituito un effetto diverso da quello che
aveva fatto. Chi guarda solo il valore di ritorno di `DoDragDrop` conclude che non
è successo niente, e non ricarica la vista.

L'esito vero sta nell'oggetto dati, nel formato `CFSTR_PERFORMEDDROPEFFECT`: un
`DWORD` in memoria globale che la destinazione scrive per dire cosa ha fatto
davvero. Il valore di ritorno e quel formato sono due cose diverse, e **nessuna
delle due è superflua**: il primo dice cosa la destinazione ha accettato, il
secondo cosa ha concluso.

La correzione è leggerli tutti e due e unirli: se una delle due dice
"spostamento", i file non sono più dove stavano. Verificato: dopo la correzione
la lista si rifà da sola, senza premere F5.

**Il difetto non si vede scrivendo il codice**, perché il codice fa la cosa
documentata: `DoDragDrop` restituisce l'effetto, e l'effetto sembra la risposta.
Si vede solo **guardando la finestra dopo il gesto**, che è esattamente il genere
di verifica che un test automatico ben scritto salterebbe.

### 7.24 Il gesto vietato che lasciava un file in più

Trascinare un file sul fondo della cartella dove è già non deve fare niente: è il
caso in cui `DragOver` risponde `DROPEFFECT_NONE`, e il puntatore lo dice
smettendo di promettere qualcosa.

La prima versione di `Drop` però ricalcolava l'operazione dai tasti premuti invece
di guardare cosa era stato promesso. Con l'effetto vietato (`NONE`) il calcolo
diceva "copia" — e al rilascio il file veniva **duplicato**: un gesto che il
puntatore aveva dichiarato impossibile produceva `nota (2).txt`.

Adesso `Drop` guarda l'effetto negoziato e, se è `NONE`, non fa niente. Verificato:
trascinando un file sul fondo della propria cartella la cartella resta identica, e
il registro scrive `esito dichiarato 0`.

Questo non è stato trovato provando: è stato trovato **rileggendo il proprio
codice prima di provarlo**, ed è il motivo per cui il caso "rifiutato" è nella
lista delle prove invece di essere un caso che non si prova mai. Le prove servono
a trovare quello che non si sa; leggere serve a trovare quello che si è appena
scritto.

### 7.25 Il nome di una voce del cestino è un'altra voce

Per il cestino serviva una cosa sola: **Ripristina**. Si è costruita la navigazione
nello spazio dei nomi, si è aperto il cestino, si è fatto tasto destro su un file
riciclato — e il menu era quello di un file normale: *Modifica*, *Stampa*, *7-Zip*,
*Converti a Adobe PDF*. Nessun "Ripristina".

La causa si legge nel registro diagnostico, dove finiscono etichette e verbi:

```
voce: 'Modifica' verbo 'Edit'
voce: 'Stampa' verbo 'Print'
voce: '7-Zip' verbo 'SevenZip'
```

Il menu lo costruisce la shell, e la shell risponde su ciò che le si chiede. Le si
chiedeva il menu del **nome** della voce, e il nome assoluto di un file nel cestino
— `SHGetNameFromIDList(..., SIGDN_DESKTOPABSOLUTEPARSING)` — non è il file
riciclato: è il percorso del file *dentro* `$Recycle.Bin`, cioè il blob. Che è
un'altra voce: un file vero, in una cartella nascosta, con i suoi verbi. Tutti
plausibili, e tutti sbagliati per la cosa che si sta guardando.

**L'identità di una voce di spazio dei nomi è il suo PIDL**, non una stringa. Con
il PIDL — quello che si è già avuto in mano enumerando la cartella — `SHBindToParent`
dà la cartella giusta e `GetUIObjectOf` dà il menu giusto:

```
voce: 'Ripristina' verbo 'undelete'
```

**Il difetto non è nell'API: è nell'aver creduto che un nome basti.** Per il file
system basta, ed è per questo che tutto il resto dell'applicazione funzionava: un
percorso identifica un file. Nello spazio dei nomi no, e le stringhe che sembrano
funzionare sono la trappola peggiore — nessun errore, nessun avviso, solo il menu
di qualcos'altro. Si è visto **solo eseguendo un verbo e guardando cosa fa**.

Da qui una regola: quando una voce può venire da una cartella che non è un percorso,
si porta dietro il PIDL che la identifica. Serve anche a non trascinarla mai fuori
per sbaglio (vedi sotto).

### 7.26 "Questo PC" si chiamava come il computer

Aprendo "Questo PC" la briciola e il titolo della scheda dicevano il nome del
computer. Tutto il resto funzionava: due unità in elenco, tipi, dimensioni.

La causa: il nome lo si chiedeva all'oggetto —
`SHGetNameFromIDList(..., SIGDN_NORMALDISPLAY)` e, in una seconda versione,
`SHGetFileInfo(SHGFI_PIDL | SHGFI_DISPLAYNAME)`. Per "Questo PC" tutti e due
rispondono col **nome del computer**, perché è così che quella cartella si chiama
per la shell: se stessa. Explorer la chiama "Questo PC" perché il nome lo prende
altrove.

Il nome di una cartella di spazio dei nomi sta nel suo CLSID, nella stessa forma
delle etichette del menu "Nuovo":

```
HKCR\CLSID\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\LocalizedString
    = @C:\WINDOWS\system32\windows.storage.dll,-9216      → "Questo PC"
HKCR\CLSID\{645FF040-5081-101B-9F08-00AA002F954E}\LocalizedString
    = @C:\WINDOWS\system32\shell32.dll,-8964              → "Cestino"
```

Risolte con `SHLoadIndirectString`, le due cartelle si chiamano come le chiama
Windows. Il difetto aveva anche un secondo lato, che è la ragione per cui è
scritto qui: il nome del computer è **il nome di una macchina**, e non finisce in
una briciola per sbaglio.

### 7.27 Il 1601 in ogni riga di "Questo PC"

Aprendo "Questo PC", ogni unità mostrava come data `1601-01-01`. Non un errore:
un tempo di Windows che significa zero, formattato come se fosse una data. Le
unità non hanno una data di modifica, e la shell risponde con zero.

Un tempo a zero non è una data: è "non lo so", e si scrive lasciando la colonna
vuota. La stessa regola valeva già altrove nell'applicazione, ma qui la shell
risponde zero invece di non rispondere, ed è un caso diverso.

### 7.28 Il filtro che spariva al ricaricamento

Il difetto è stato trovato **sbagliando una prova**: dopo aver ripristinato un file
dal cestino ho fotografato la finestra per verificare che la riga fosse sparita, e
la fotografia mostrava il cestino **intero** — il filtro che avevo scritto per non
guardare i file di nessuno non c'era più.

Il ripristino passa da `ReloadKeeping`, che passa da `LoadDirectory`, che azzera il
filtro. La regola «il filtro non segue l'utente in un'altra cartella» è giusta;
applicarla anche al **ricaricamento della stessa cartella** era sbagliato, e faceva
sparire la casella del filtro a ogni F5 — cosa che l'applicazione faceva da
quando il filtro esiste, e che nessuno aveva notato perché nessuno ricarica una
cartella *con un filtro attivo* per guardare se il filtro resta.

Adesso il filtro si azzera solo quando la cartella cambia davvero. Verificato:
con "zz" nella casella, F5 lascia "zz" e lascia la lista filtrata.

Le due lezioni stanno insieme, e la seconda è più grande della prima: **una
cattura è una copia di quello che sta sotto gli occhi, e va guardata come tale**.
Per il cestino, da adesso, la verifica si fa contando — e le catture si fanno solo
dopo aver ristretto la lista a un file proprio.

### 7.29 I nomi con l'accento erano pieni di buchi

Nell'elenco comparivano `perch .txt` e `relazione pi recente.txt`. Non un carattere
sbagliato: un **buco** dove c'era la lettera accentata. I file sul disco si
chiamavano `perché.txt` e `relazione più recente.txt`.

L'atlante dei glifi rasterizzava `kFirstChar = 32 … kLastChar = 126`, cioè
l'ASCII stampabile, e la ricerca era `c - kFirstChar`: un array denso di 95 celle.
Un carattere accentato in UTF-8 sono **due byte**, e nessuno dei due stava in
quell'intervallo, quindi non si disegnava né l'uno né l'altro. In un file manager
italiano questo non è un caso di laboratorio: è il nome di metà dei documenti.

Tre cose sono cambiate insieme, perché una sola non bastava:

1. **l'atlante** rasterizza ASCII **più il blocco latino-1** (U+00A0–U+00FF: sono
   96 caratteri, una manciata di celle) più un elenco corto di simboli che
   l'interfaccia disegna — le frecce della barra strumenti, la stella dei
   segnalibri, i puntini di sospensione. La chiave del dizionario non è più il byte
   ma il **punto di codice**;
2. **il testo si legge in UTF-8**: `Measure` e `Draw` avanzano di carattere in
   carattere, non di byte in byte (`Utf8::Length`, `Utf8::Next`, `Utf8::At`);
3. **la troncatura taglia fra i caratteri**, mai dentro: `FitText` avanzava di un
   byte alla volta, e un taglio in mezzo a una sequenza lasciava in fondo al nome
   un carattere che non c'era.

**Perché non si era visto.** I nomi di prova erano tutti in ASCII — `nota.txt`,
`relazione.txt`, `dati.csv` — e le stringhe dell'interfaccia erano scritte per
evitare gli accenti: "Proprieta'" con l'apostrofo non è una scelta stilistica, era
una **conseguenza** dell'atlante. Adesso la voce del menu si chiama "Proprietà", e
`ShellLabelToAscii` — che ripiegava le lettere accentate della shell — ripiega solo
oltre il latino-1, che è il suo mestiere vero.

**Il difetto era invisibile a ogni prova che avessimo fatto**, perché nessuna
prova conteneva una lettera accentata. Si è trovato scrivendo un nome di file come
lo scriverebbe una persona.

### 7.30 La sezione che si apriva vuota, e il CLSID che non si apriva

"Accesso rapido" è una cartella dello spazio dei nomi, e il nome lo si è preso come
per il cestino: `SHGetKnownFolderIDList` + `SHGetNameFromIDList`. Il risultato:

```
--- accesso rapido: la posizione non si apre
```

Il registro dice perché: per il cestino e per "Questo PC" c'è una voce in
`FolderDescriptions`, e `SHGetKnownFolderIDList` pesca lì. Per "Accesso rapido" la
voce **non c'è** — c'è solo il CLSID in `HKCR\CLSID` — quindi la funzione
falliva e la sezione restava vuota.

La seconda versione scriveva il CLSID nel codice, ed era comunque sbagliata: con
`::{679f85cb-…}` la shell cerca la cartella **dentro lo spazio dei nomi del
desktop**, dove "Accesso rapido" non sta. La forma che risponde è `shell:::{…}`,
che la chiede per identificatore:

```
--- accesso rapido: 6 voci
```

Tre cose da portare via:

- **un nome di spazio dei nomi sbagliato non dà errore**: dà una cartella vuota. È
  la forma di guasto peggiore, perché sembra che non ci sia niente da mostrare
  invece di sembrare un guasto. Adesso `QuickAccessName` **verifica** che la shell
  riconosca quel nome prima di usarlo, e una sezione vuota non si disegna;
- **il registro diagnostico ha pagato il conto**: una riga con il motivo ha
  risolto quello che due tentativi a occhio non avevano risolto;
- il CLSID di "Accesso rapido" è **l'unico identificatore scritto nel codice** di
  tutto il progetto, e adesso c'è scritto perché.

### 7.31 Il disco da zero byte

L'inspector di un'unità diceva:

```
Type      Cartella di file
Size      0 B
Folders   0
Files     0
Modified
System    NTFS
Space     224.7 GB free (76% used)
```

Il disco ha 952,6 GB e il pannello dichiara zero byte, zero cartelle e zero file.
Non un errore di lettura: **una risposta data a una domanda che non aveva senso**.
Un volume non è una cartella, e le statistiche ricorsive non hanno niente da
camminare lì dentro; il servizio rispondeva comunque, con zeri, e l'inspector li
mostrava come se fossero un fatto.

Tre righe di conseguenza, tutte verificate a schermo:

- per un **volume** la dimensione è la **capacità del disco**
  (`GetDiskFreeSpaceExW`), non la somma di ciò che contiene — `Size 952,6 GB`;
- le statistiche si chiedono solo per una cartella **che sta su un percorso**, come
  già si faceva per le colonne della lista;
- la riga `Modified` di un volume **non esiste**: il file system non gli dà una
  data, e l'etichetta con niente accanto — che era il caso di `Ext` per una
  cartella, e di `Created`/`Accessed` per metà delle voci — non dice niente e
  occupa lo spazio di una riga che direbbe.

**Un numero falso detto con sicurezza è peggio di nessun numero**, ed è la stessa
lezione della sezione vuota di §7.30: quello che non si sa si tace.

### 7.32 E il trattino lungo, che era un buco

`Ext` per una cartella mostrava `—` quando non c'era estensione. A schermo non si
vedeva niente, perché U+2014 **non era nell'atlante**: fuori dai due intervalli
(ASCII e latino-1) e dall'elenco dei simboli, un carattere si disegna come un
buco. È lo stesso difetto di §7.29, in un posto dove nessuno lo cercava — non un
nome di file, ma un carattere scritto da noi in una stringa.

L'elenco dei simboli adesso comprende i trattini e le virgolette tipografiche
(`–  —  '  '  "  "`), che le etichette della shell usano davvero, e la riga non si
mostra più quando non ha niente da dire.

**La regola che ne esce**: un carattere non-ASCII si scrive nel codice **solo** se
sta nell'insieme rasterizzato. Altrimenti resta un buco, e un buco non fa rumore.

### 7.33 La destinazione non e' sempre una cartella

Il riquadro di navigazione e' diventato una destinazione di caduta, e la prima
versione del codice trattava ogni voce come una cartella dove mettere dentro: nome
del bersaglio, effetto copia o sposta, `IFileOperation`. Sulle cartelle funziona, e
sul **cestino** era sbagliato due volte:

- una copia nel cestino non vuol dire niente — il cestino non e' un posto dove si
  tiene una cosa, e' il gesto di disfarsene;
- con **Ctrl** premuto l'effetto sarebbe stato "copia", cioe' il file restava dov'e'
  e ne compariva un secondo dentro il cestino.

La correzione non e' un caso speciale dentro `Drop`: la destinazione ha un **verbo**.
`DragDrop::TargetAt` restituisce un `Target` con `DropVerb::DropInto` o
`DropVerb::Discard`, piu' il percorso; `Effect`, `Highlight` e `Drop` decidono in
base a quello, e per `Discard` i modificatori non si guardano nemmeno. Un'unita'
intera rifiuta di farsi buttare via, e lo dice subito: un effetto promesso e non
mantenuto e' peggio di un gesto rifiutato, che e' la lezione di §7.24.

Il bersaglio non si ricalcola a mano: le righe del riquadro sono gia' bersagli di
clic, quindi la geometria la sa `HitTest` — l'evidenziazione diventa un indice, non
un confronto di percorsi (due voci possono puntare alla stessa cartella, e quella
accesa e' quella **sotto il puntatore**).

**Difetto trovato mentre si verificava.** Un annullamento di `IFileOperation` usciva
in silenzio: `DragDrop::Drop` faceva `if (result == Cancelled) return;` e non
scriveva niente, da nessuna parte. Un gesto che finisce senza dire niente e'
indistinguibile da un difetto, e infatti la prima diagnosi e' stata sbagliata: il
messaggio di stato di un'altra prova, letto trenta secondi dopo, non dimostrava
niente, mentre il registro diagnostico — che non esisteva — avrebbe risposto in una
riga. Adesso l'annullamento si dice a schermo ("Operazione annullata"), e ogni
caduta scrive nel registro cosa ha fatto, quanti elementi e — quando serve — perche'
non e' riuscita.

**E il messaggio giusto lo copriva quello sbagliato.** A schermo, buttando via un
file, si leggeva `Spostati 1 elementi`: la caduta aveva detto `Buttati via 1 elementi
nel cestino`, e subito dopo la **fine del trascinamento** — che riferisce "Spostati N
elementi" senza sapere dove sono finiti — aveva sovrascritto il messaggio piu'
preciso con uno piu' generico. Adesso chi esegue la caduta alza una bandiera, e chi
chiude il trascinamento parla solo se nessuno l'ha gia' fatto: sa meno, quindi tace.

**Il metodo, in tre righe.** Questa parte e' costata piu' diagnosi che codice, e ogni
volta per la stessa ragione: un valore letto *dopo* non dimostra niente. Il
messaggio di stato dura quattro secondi e io l'ho fotografato trenta secondi dopo; il
registro diagnostico non c'era, e senza di lui l'unica ipotesi era "l'operazione e'
fallita in silenzio". Con una riga di registro, `esito della caduta: spostamento di 1
elementi, prodotti 1`, la stessa domanda ha avuto risposta in un minuto.

### 7.34 L'oggetto dati montato a mano viene accettato e non fa niente

Togliere il rifiuto al trascinamento fuori dalle cartelle di spazio dei nomi ha
richiesto un oggetto dati: le voci li' non hanno un percorso, quindi si portano come
**PIDL**. La prima versione lo costruiva a mano, con `SHCreateDataObject` e un
genitore ricavato dal PIDL togliendone l'ultimo elemento. Sembra ragionevole, e la
destinazione lo **accetta**: Esplora risorse risponde `DROPEFFECT_COPY`, il registro
scrive "effetto restituito 1", e non copia niente. L'esito dichiarato — quello che
la destinazione scrive nell'oggetto, `CFSTR_PERFORMEDDROPEFFECT` — dice `0`, cioe'
"non ho fatto niente".

La lezione e' la stessa del menu contestuale, in un altro posto: **l'oggetto dati lo
chiede la cartella che possiede le voci**, con `IShellFolder::GetUIObjectOf`, invece
di montarlo. Il cestino non e' una cartella come le altre, e chi lo sa e' la shell.

**Il residuo dichiarato, chiuso il 2026-10-17.** Con l'oggetto chiesto alla cartella
il trascinamento fuori funziona per le voci di una cartella **normale** (prova di
controllo) e **anche per una voce riciclata**: un file della sandbox messo nel cestino
dall'applicazione, cercato col filtro sul suo nome, trascinato dal cestino a una
cartella del progetto dentro una finestra di Esplora risorse, e' **arrivato** — 28
byte, contenuto identico, sparito dal cestino, con `esito dichiarato 2`
(`DROPEFFECT_MOVE`) nel registro. Non e' un "copia": trascinare fuori dal cestino
**ripristina**, ed e' quello che fa anche Esplora risorse.

**Perche' i due tentativi precedenti avevano concluso il contrario.** Non era OLE: era
il **banco**, in due modi diversi, e vale la pena scriverli perche' sono due trappole
di `AGENTS.md` §5 che si sono presentate insieme.

1. **Il primo piano.** `SetForegroundWindow` non vince il primo piano quando ce l'ha
   un'altra applicazione (Windows lo blocca, e la chiamata riesce lo stesso): la
   pressione del trascinamento andava a un'altra finestra, il gesto non cominciava, e
   la conclusione fu «il trascinamento sintetico verso Esplora risorse non e'
   riproducibile in questo banco». Adesso c'e' `tools/focus-window.ps1`
   (`AttachThreadInput`), e la stessa prova riesce.
2. **La misura della riga, sbagliata a occhio.** Il primo tentativo di oggi ha
   trascinato **un altro file**: la riga sotto il puntatore era stata stimata da una
   cattura piccola (passo 30 px invece di **24**), quindi la pressione e' caduta una
   riga piu' in basso. Il sospetto che ne e' nato — «il test dei clic non e' d'accordo
   col disegno, di una riga» — e' stato **falsificato misurando i pixel** della
   cattura invece di guardarla: il clic era giusto, la stima no. E' §5 alla lettera:
   una cattura si legge da un pixel, non dalla miniatura.

**Quante volte e' stato visto.** La prova di controllo (file normale) una volta, quella
della voce riciclata **una** volta: la ripetizione e' stata annullata perche' la
finestra di prova era stata aperta su un'altra scrivania virtuale e non si e' toccato
niente (`AGENTS.md` §0). Un secondo giro costa cinque minuti e va fatto appena la
macchina e' libera; fino ad allora il difetto e' **risolto con una prova**, non con
due.

Nota per chi ci riprova: la finestra di Bolt **rifiuta di proposito** le voci di
spazio dei nomi (`TargetAt` esce vuoto se `draggingNamespaceItems`), quindi la prova
va fatta **verso un'altra applicazione**. E il cestino si guarda **solo col filtro**
sul nome del file di prova: `AGENTS.md` §0.

### 7.35 Cambiando vista, il suggerimento restava appeso

Le viste si cambiano dalla palette, e cambiando vista le celle si spostano: la
griglia impagina per indice, e gli stessi indici cadono altrove. Il **sovrappasso**
pero' e' un indice, e nessuno lo azzerava — quindi il suggerimento continuava a
mostrare il nome della voce che stava sotto il puntatore *prima*, accanto a una
voce diversa. Un nome giusto accanto al posto sbagliato: la forma peggiore di errore
in una lista di file, e la stessa famiglia di "quello che vedi non e' quello che
credi" di §7.29.

Il sovrappasso adesso si azzera quando la vista cambia (`SetViewMode`), e lo
ricalcola il primo movimento del puntatore. Cambiare vista e' diventato un punto
solo, invece di quattro assegnazioni ripetute: era il posto dove la riga mancava.

Vale anche per l'ordinamento e per il filtro, che spostano le righe allo stesso
modo: li' l'indice sbagliato resta finche' il puntatore non si muove, ed e'
transitorio — dichiarato, non nascosto.

### 7.36 La lista disegnava fuori dal suo rettangolo

**Trovato dall'autore, guardando una cattura della griglia raggruppata.** In cima
alla finestra, sopra la barra delle schede e il percorso, c'erano quattro icone di
file: celle della griglia disegnate **fuori dalla lista**.

La lista saltava le righe interamente fuori dal riquadro — `row.y < list.y - row.h`
— e quella condizione e' vera finche' le righe sono alte uguale. Una cella di
griglia e' alta quanto una miniatura, quindi "una riga sopra il bordo" sono cento
sessanta pixel disegnati sopra il titolo. Il controllo non tagliava: **saltava**.

Adesso e' il ritaglio a tagliare, e dal 2026-10-20 **non costa piu' niente**: il
rettangolo viaggia nel vertice (vedi §7.61), quindi la lista si disegna dentro il suo
riquadro restando nello stesso disegno di tutto il resto. Una cella a meta' si vede a
meta', che e' quello che deve succedere quando si scorre. Il difetto c'era anche prima
delle bande di gruppo — bastava uno scorrimento non multiplo dell'altezza della cella,
e le frecce lo producevano — ma le bande lo hanno reso la norma invece dell'eccezione.

**Il prezzo, dichiarato allora e saldato dopo.** I quadrilateri si accumulano in un
batch solo, e con il `glScissor` il ritaglio obbligava a chiuderlo e riaprirlo: **tre
disegni per fotogramma invece di uno**, cinque con il visualizzatore di testo. Era
scritto qui, e adesso la riga dice l'altro numero: **uno**. Vedi §7.61.

**La lezione, e perche' le mie catture non l'avevano visto.** Un difetto di
*ritaglio* non si vede in una lista allineata: si vede scorrendo, e guardando il
bordo invece del centro. Io avevo fotografato la griglia **non scorsa** — dove le
celle stanno dentro — e le catture scorse le avevo **ritagliate sulla lista**, che
e' esattamente la regione del difetto. E' la stessa cosa di §7 (le due barre del
titolo), dove `GetClientRect` tagliava via la barra di sistema: il banco di prova
guardava dove il difetto non c'era. Qui e' bastato che l'autore guardasse la
finestra intera — ed e' il motivo per cui le catture di verifica si fanno sul
riquadro **intero** e solo la privacy autorizza a ritagliare.

### 7.37 Il limite scritto al contrario, e la fascia che usciva dal riquadro

**Trovato dall'autore, guardando la minimap.** Nel visualizzatore di testo, la
fascia che segna la parte visibile usciva dal suo riquadro e finiva **sopra** il
pannello, sempre piu' su; e l'area del testo sembrava vuota.

Una riga sola:

    app.textScroll = (std::min)((std::max)(0.0f, content - text.h), app.textScroll);

Il `max(0, ...)` era applicato al **limite massimo** invece che allo scorrimento.
Quel `min` limita solo in alto: scorrendo la rotella **verso l'alto** oltre l'inizio,
`textScroll` diventava negativo e li' restava. Con lo scorrimento negativo le righe
di testo venivano spinte verso il basso fino a sparire, e la fascia della minimap —
`scroll / content * altezza` — saliva sopra il riquadro. Adesso i limiti sono due,
nell'ordine giusto: `max(0, min(scroll, content - h))`.

**La stessa lezione di §7.36, applicata al testo.** Guardando il testo scorso si e'
visto che le righe si disegnavano **sopra la testata** `UTF-8 ... righe`: la prima
riga comincia apposta una riga prima di quella visibile (cosi' quella a meta' si
vede a meta'), e senza ritaglio finiva sopra. Vale anche li': un `continue` non e'
un ritaglio, e il ritaglio e' un rettangolo di ritaglio sull'area del testo — dal
2026-10-20 scritto nel vertice di ogni riga (§7.61), prima uno `glScissor` attorno a
un lotto.

**E il primo tentativo di ritaglio era stretto.** Ritagliando sul solo rettangolo
del testo i **numeri di riga** sparivano: stanno nella gutter, a sinistra. Un
ritaglio stretto e' un difetto quanto uno largo, e si vede solo guardando — la
lezione e' sempre la stessa e vale la pena ripeterla: dopo una correzione si
rimisura il bordo attorno alla correzione.

### 7.38 La shell interrogata due volte: un costo e una perdita

Il menu contestuale chiede alla shell i suoi verbi: i gestori registrati su questa
macchina rispondono in ~65 ms, e la prima apertura dopo l'avvio costa ~350 ms
perche' le loro DLL si caricano in-process. Nel registro dei lavori quel
`+22 handle di kernel per apertura` era annotato come un **costo**.

**Non era un costo: era una perdita.** Dieci menu contestuali di fila, contando
gli handle dopo ogni chiusura:

    avvio  666
    giro  1   menu aperto 1206 → chiuso 1135
    giro  2                1261 →       1179
    ...
    giro 10                1430 →       1338

Il contatore non tornava mai giu'. Non era nemmeno proporzionale ai gestori: la
stessa crescita si misura su un `.txt` della sandbox e su un `.cpp` con quindici
estensioni registrate, quindi non erano i gestori a essere tanti — era qualcosa che
restava vivo **per il fatto stesso** di aver aperto il menu.

**Dove, misurato e non dedotto.** Tre righe di registro con
`GetProcessHandleCount` attorno alle tre parti della costruzione del menu:
`Invia a` +0, `Nuovo file` +0, e tutto il resto sui verbi della shell. L'ipotesi
precedente — le chiavi `ShellNew` lette a ogni apertura — era sbagliata, e la
misura l'ha uccisa in un colpo: `+0`.

**La correzione e' quella che i documenti chiedevano da M7**: chiedere la shell
**una volta per selezione**, invece che a ogni tasto destro. Gli oggetti della
shell (`IContextMenu` e `HMENU`) vivono ora in una memoria in `AppState`, il menu
li prende in prestito e li restituisce, e a rilasciarli davvero e' la memoria
quando la chiave cambia.

**Perche' la memoria scade dopo 30 secondi.** Il registro diagnostico ha
raccontato da solo il motivo: la prima interrogazione della sessione ha risposto
`21 voci`, quella successiva — a estensioni ormai registrate — **`24 voci`**.
Tenendo la prima risposta per sempre, tre verbi non sarebbero mai comparsi
(`Apri nel Terminale`, `Apri con Code` sono fra questi: in Windows 11 i gestori
moderni si registrano al secondo giro). Con la scadenza, la risposta incompleta si
corregge da sola.

**Verificato.** Dieci menu sulla **stessa** selezione: dal secondo in poi il
registro dice `verbi della shell ripresi dalla memoria: 24 voci`, e il contatore
degli handle resta fra 1243 e 1245. Dieci menu su **righe diverse** — il caso in
cui una memoria per selezione non basterebbe — restano fra 1209 e 1238: ogni
risultato nuovo rilascia il precedente. Il menu, guardato, ha ancora tutti i verbi,
compresi quelli moderni.

Resta dichiarato un costo che non e' una perdita: **ogni selezione nuova costa una
interrogazione** (~65 ms). La shell e' l'unica autorita' sui propri verbi, e
chiederli e' l'unico modo di averli.

### 7.39 Ogni cosa su `C:` era "un volume"

**Trovato dall'autore, guardando l'inspector.** Per una cartella, le righe
`Size` e `Space` dicevano la capacita' del **disco**: 952,6 GB per
`analysis/tmp/sandbox/documenti`, che contiene 36 byte in due file. E per un file la
riga `Size` non c'era affatto.

La causa era una riga in `VolumeOf`:

    if (location.empty() || GetDriveTypeW(location.c_str()) == DRIVE_UNKNOWN)
        return info;   // non e' una radice di volume

`GetDriveTypeW` **risolve il percorso al volume che lo contiene**: su
`C:\...\sandbox\documenti` risponde `DRIVE_FIXED`, non `DRIVE_UNKNOWN`. Quindi la
condizione non era mai vera per niente che stesse su un disco, e ogni cartella e ogni
file diventava "un volume": le righe `System` e `Space` comparivano, e la dimensione
era quella del disco. Il commento accanto diceva la cosa giusta — "non e' una radice
di volume" — e il codice faceva un'altra domanda.

La domanda giusta e' **se il percorso *e'* la radice**, e il modo di porla e'
`GetVolumePathNameW`, che dice *quale* e' la radice del volume che contiene il
percorso: se non coincide col percorso, non e' un volume ma una cartella o un file
che ci sta sopra. Cosi' reggono anche i volumi montati dentro una cartella, che una
radice di unita' non li copre.

**Verificato contro il vero**, non contro l'impressione: `documenti` da' `Size 36 B`,
`Folders 0`, `Files 2` e `du -sb` dice 36 byte con due file; `dati.csv` da' `Size
28 B` e `stat` dice 28. Il caso volume — l'unico che la correzione *cambiava* — resta
quello di prima perche' il test nuovo e' **piu' stretto** del vecchio: una radice vera
lo passa, e la radice vera era gia' stata verificata in M8.

### 7.40 Il testo disegnato senza chiedersi quanto spazio avesse

**Trovato provando le opzioni, scegliendo `Font: Consolas`.** Due testi che con il
carattere di partenza convivevano hanno cominciato a **sovrapporsi**:

- i titoli delle colonne, a larghezze fisse: `Modified` correva dentro `Folders`, e
  in testata si leggeva `ModifedFoldersFilesSize`;
- nella lista a gruppi del GoTo, l'etichetta e il percorso allineato a destra, che si
  scrivevano uno sopra l'altro (`analysisters\Desktop\...`).

La causa e' la stessa nei due posti: **un testo veniva disegnato senza misurare lo
spazio che aveva**. Finche' il carattere e' `Segoe UI` a 15 px le larghezze fisse
bastano per fortuna; appena il carattere cambia — ed e' proprio una delle opzioni che
M9 aggiunge — la fortuna finisce.

Nei titoli di colonna l'etichetta si accorcia con `FitText` allo spazio prima della
colonna successiva, tolto il posto dell'imbuto del filtro, e le due colonne strette
(`Folders`, `Files`) hanno ricevuto lo spazio che mancava: col carattere di partenza
si legge il titolo intero accanto all'imbuto, e solo su una finestra stretta o con un
carattere molto largo il titolo si accorcia invece di finire sopra il vicino. Nelle
liste a gruppi l'etichetta e il dettaglio si dividono la riga: il dettaglio, quando
c'e', si taglia **davanti** con `FitTextTail` — di un percorso la parte che dice dove
si va e' la coda — e l'etichetta prende quello che avanza.

**Verificato a schermo**, non dedotto: con `Consolas` 16 i titoli restano separati e
il percorso lungo del GoTo diventa `...\analysis\tmp\sandbox` invece di finire sopra
il nome. Il caso con il carattere di partenza — l'unico che la correzione *cambia* —
e' quello che conviene guardare: con `Segoe UI` 15 e' identico a prima, perche' il
testo ci sta e `FitText` lo lascia intero.

### 7.41 La vista a elenco si presentava come "Icons"

**Trovato leggendo `DrawStatusBar` mentre si finiva la lingua**, non a schermo: il
nome della vista era scelto con

    const char* view = (app.viewMode == ViewMode::Details) ? "Details"
                     : (app.viewMode == ViewMode::Tiles)   ? "Tiles"
                                                           : "Icons";

Le viste sono **quattro** (`Details`, `List`, `Tiles`, `Icons`), e `List` cadeva nel
ramo di ripiego: la barra di stato diceva `Icons` mentre si guardava l'elenco. Un
nome che non c'e' non si indovina con l'`else`: si nomina.

Corretto con i quattro rami, e i nomi sono anche tradotti (`Elenco`/`List`,
`Dettagli`/`Details`, `Riquadri`/`Tiles`, `Icone`/`Icons`). **Verificato a schermo**:
con la vista a elenco la barra dice `Elenco` in italiano e `List` in inglese; prima
diceva `Icons` in tutte e due.

### 7.42 Il tasto "sali" e il comando "sali" erano due implementazioni

**Trovato facendo salire da una radice di volume.** La correzione stava nel comando
(`CmdGoUp`): da `C:\` il genitore e' se stesso, e sopra un disco c'e' "This PC". Ma
**Backspace non passava da li'**: aveva la sua riga,

    LoadDirectory(*app, ParentOf(app->directory));

e chiamava direttamente il calcolo del genitore. Finche' il comando e il tasto
facevano la stessa cosa la copia non si vedeva; appena il comando ha imparato a
salire da una radice, il tasto e' rimasto indietro. Due strade per la stessa azione
divergono sempre, prima o poi: e' la stessa regola per cui i pulsanti di navigazione
eseguono i comandi invece di reimplementarli.

Backspace adesso esegue `RunCommand(app, CmdGoUp)`, come i pulsanti e come
`Alt`+`↑` — la terza via, aggiunta perche' e' il gesto che il riferimento usa.

**Verificato a schermo** con `BOLT_DIAG=1` (una riga di registro diceva quale
cartella e quale genitore, invece di dedurlo): da `C:\`, `Backspace` e `Alt`+`↑`
portano entrambi a "This PC". L'etichetta e' **"This PC"** e non il "Questo PC" che
risponde la shell, perche' e' il nome del riferimento (`AGENTS.md` §3).

### 7.43 L'icona di una voce della barra laterale era sempre quella di cartella

**Trovato guardando la barra laterale**, non leggendo il codice: il disco `C:` si
disegnava come una cartella, e con lui ogni voce dei luoghi. Una riga sola, la
stessa per tutte:

    const ShellIconAtlas::IconUV* uv = app.icons.GetFor("", true);   // <dir>

Sopra quella riga c'era scritto — ed e' ancora li' — che «l'icona di un volume e'
quella che la shell da' all'unita': un disco, non una cartella». Il commento
descriveva l'intenzione, il codice chiamava `GetFor("", true)`, che per costruzione
risponde *cartella*: il disco, il cestino, `Questo PC`, la cartella personale e le
cartelle di sistema erano disegnati tutti uguali. Un commento che dice il falso non
fa rumore, e per questo e' la forma di difetto peggiore.

**La causa** e' che l'atlante sapeva rispondere solo a due domande — «che icona ha
questa estensione» e «che icona ha una cartella» — mentre una barra laterale non
elenca estensioni: elenca **oggetti**, ognuno con l'icona che Windows gli da'. Il
disco e' un disco perche' e' un disco, non perche' finisce in una certa maniera.

**La correzione** aggiunge la domanda che mancava: `GetForObject(location)` chiede
alla shell l'icona di **quell'oggetto**, per percorso (la radice di un'unita') o per
nome di spazio dei nomi (il CLSID di `Questo PC`, quello del cestino). Il PIDL e'
l'unico modo di farla rispondere per il cestino e per `Questo PC`, perche' un
percorso non ce l'hanno. Ogni voce della barra laterale porta con se' *quale oggetto
e'* (`Place::icon`) e smette di tingere l'icona: e' la sua, e si disegna com'e'.

Due cose che la correzione doveva rispettare. La prima: l'atlante **non ha
sfratto**, quindi l'icona per oggetto si usa dove l'insieme e' piccolo e chiuso —
le lettere di unita', le voci dei luoghi — e non per le righe di una lista, che
restano per estensione. La seconda: chiedere il tipo di un'unita' puo' far girare un
lettore ottico, quindi per una voce di spazio dei nomi la domanda si fa **quando si
apre la cartella**, una volta, e non a ogni fotogramma.

**Verificato a schermo**: `Windows (C:)` ha l'icona del disco sia nella barra
laterale sia nella lista di `Questo PC`, `Questo PC` ha il computer, il cestino ha
il cestino, e `Documenti`, `Immagini`, `Musica`, `Video`, `Download` hanno le loro.

**Il tentativo monocromatico, e perche' non c'e' piu'.** La voce aperta di M2
chiedeva la resa monocromatica, e il percorso `textureIsMono` esisteva nella shader
dal primo giorno senza che nessuno lo usasse: e' stato acceso — maschera di
copertura per chiave, barra laterale e striscia dei tab — e **visto a schermo**. Poi
l'autore ha chiesto le icone classiche di Esplora risorse, a colori. La scelta e'
sua, e va dove dice lui: la maschera e' stata **tolta**, non lasciata inerte —
`textureIsMono` resta quello che era, una capacita' della shader senza utilizzatore,
e questo e' scritto invece di essere nascosto. Vedi `docs/04`, M2.

### 7.44 L'icona di un file non era quella dell'applicazione che lo apre

**Trovato guardando la lista**: `dati.csv` usciva con il documento bianco generico,
mentre nella stessa riga la colonna Tipo diceva `Excel.CSV`. Se il tipo e' di Excel,
l'icona di Excel deve esserci.

**La causa, misurata e non dedotta.** `SHGetFileInfoW` con `SHGFI_USEFILEATTRIBUTES`
non tocca il disco — e' cio' che la rende economica, ed e' scritto in `shell_icons.h`
— e l'icona di Excel **non e' un file**: e' una **risorsa dentro** un file. La
catena, letta con una sonda usa e getta (`analysis/tmp/probe-assoc.ps1`):

| domanda | risposta della shell |
|---|---|
| icona di `.csv` | `shell32.dll,0` — il documento generico |
| icona di `Excel.CSV`, il **ProgID** | `xlicons.exe,9` — l'icona di Excel |
| icona di `.png` | `i_view64.exe,0` — IrfanView |
| icona di `.txt` | `@{Microsoft.WindowsNotepad_...?ms-resource://...}` |

Chiesta a un'**estensione**, la shell si ferma prima del ProgID, e per diverse
estensioni risponde con il documento generico. Chiesta a un **ProgID**, risponde
l'icona vera. Per questo la colonna Tipo era giusta e l'icona no: sono due domande
diverse, e prima se ne faceva una sola.

**La correzione**, in `IconForExtension`: si risolve il ProgID — la scelta
dell'utente (`FileExts`, `UserChoice`) e, se non c'e', quella di sistema — e l'icona
si chiede a quello. Poi si estrae da dove la shell dice (`SHDefExtractIconW`),
sciogliendo le virgolette e l'indice dopo la virgola. Una risorsa di
un'applicazione del pacchetto (`@...?ms-resource://...`) si fa risolvere con
`SHLoadIndirectString`; se non si risolve si **rinuncia** e si ripiega sul documento
generico, perche' un'icona sbagliata e' peggio di una anonima.

**Verificato a schermo**: `dati.csv` adesso ha l'icona verde di Excel, nella stessa
riga che dice `Excel.CSV`. Le altre voci della sandbox non cambiano, e va bene: la
loro associazione non porta a un'icona estraibile.

**Cosa questo non fa**, ed e' un limite dichiarato: l'icona resta quella
**dell'estensione**, non quella di quel singolo file. Il contenuto vero — l'anteprima
— e' un'altra cosa e c'e' gia' (`IShellItemImageFactory`, strato 2).

### 7.45 L'icona della vista a icone grandi si sgranava, e in grande era un'altra icona

**Trovato guardando la lista a icone grandi**, su richiesta dell'autore. Due sintomi
che sembravano uno:

- a 64 px e a 128 px le icone erano **morbide**, con i bordi a scalini;
- e il `.csv` che in elenco aveva l'icona di Excel, in grande mostrava il documento
  generico — **la stessa voce, due icone**.

**Le cause sono due, e vanno tenute separate.**

La prima: l'atlante delle icone rasterizza a `SM_CXICON`, **32 px**, e da li' non si
muove. Disegnare a 128 px vuol dire ingrandire di quattro volte, e un ingrandimento
di quattro volte si vede. Non e' un difetto dell'atlante: e' che per la vista a
icone grandi serviva un'icona **di quella misura**, e l'atlante non poteva darla.

La seconda: la misura grande la si chiedeva alla shell
(`IShellItemImageFactory` con `SIIGBF_ICONONLY`), e la shell risponde con l'icona
che da' **a un file**. Che per un `.csv` su questa macchina e' il documento generico
— e' la stessa cosa misurata in §7.44, da un'altra porta. Le icone piccole, nel
frattempo, erano gia' passate dall'associazione: da qui le due icone diverse.

**La correzione** fa in modo che la misura grande venga dalla **stessa fonte** di
quella piccola, e che non sia un ingrandimento:

- `ExtensionIconPixels(extension, size, …)` estrae l'icona dell'associazione **alla
  misura chiesta** (`SHDefExtractIconW` con `nIconSize`): a 128 px si vede un disegno
  a 128 px, non uno da 32 gonfiato. La sonda lo conferma: chiesta 128, la bitmap che
  torna e' 128x128;
- il worker delle anteprime ha ora **due domande distinte**, e la differenza conta:
  `SIIGBF_THUMBNAILONLY` per una miniatura — che deve fallire se il tipo non ha un
  provider, o sarebbe un'icona spacciata per anteprima — e `SIIGBF_ICONONLY` per
  un'icona. Prima ce n'era una sola, e chi voleva l'icona grande non aveva di meglio
  che chiedere una miniatura;
- si chiede **per tipo e non per file** (`icon:.<estensione>`, `icon:<dir>`), quindi
  duecento `.txt` sono una cella sola, e il prefisso tiene le icone separate dai
  percorsi delle miniature: due spazi di nomi in una cache sola si sfrattano a
  vicenda.

L'atlante ora dichiara la sua misura (`IconPixels()`), e chi disegna decide con
quella invece che con un 32 scritto a mano: e' la stessa regola dei numeri derivati.

**Verificato a schermo**, non a impressione: nella sandbox a 128 px le cartelle hanno
il bordo continuo e il `.csv` ha l'icona di Excel come in elenco. E misurato, perche'
"mi sembra piu' nitido" non e' una prova: lungo il bordo curvo di una cartella le
variazioni di colore misurano **1-3 pixel**, non multipli di 4 — un ingrandimento di
quattro volte le farebbe cadere tutte a multipli di 4.

**Limite dichiarato:** la cella grande e' di 128 px, e la vista a icone arriva a 256,
quindi **all'ingrandimento massimo l'icona e' ancora ingrandita di due volte**. Alzare
la cella vorrebbe dire 16 celle invece di 64 nell'atlante delle anteprime, cioe'
buttare via tre quarti delle miniature che ci stanno: non conviene, e si dice invece
di nasconderlo.

### 7.46 «Su disco» era la dimensione, e i due numeri venivano da due campi diversi

**Trovato verificando la funzione, prima di darla per fatta** — che e' il motivo per
cui la si verifica. Il conto ricorsivo dei byte allocati (M3) era scritto e sembrava
giusto; il numero che l'inspector mostrava per la cartella di prova combaciava con
il riferimento. Poi, per un **file solo**, il numero era quello logico:

| file | lunghezza | `FileStandardInfo.AllocationSize` | `GetCompressedFileSizeW` |
|---|---|---|---|
| `a.txt` | 10 240 | **12 288** | 10 240 |
| `e.txt` | 100 | **104** | 100 |
| `sparse.bin` | 67 108 864 | **131 072** | 131 072 |
| `zeros.bin` (compresso) | 20 971 520 | **0** | 0 |

Due cose, e valgono tutte e due.

La prima: per un file solo avevo usato `GetCompressedFileSizeW`, che e' la chiamata
che **si chiama** come la domanda. Su questa macchina risponde con la dimensione
logica — 10 240 per un file che ne occupa 12 288 — e un «su disco» uguale alla
dimensione e' una funzione che sembra non fare niente, che e' la forma di difetto
peggiore: non sbaglia rumorosamente, non fa niente. Il campo giusto e'
`FILE_STANDARD_INFO.AllocationSize`.

La seconda, piu' importante: il campo giusto e' **lo stesso** che l'enumerazione in
blocco legge per ogni voce della cartella (`FILE_ID_BOTH_DIR_INFO.AllocationSize`).
Due strade diverse per la stessa domanda possono rispondere cose diverse — e' scritto
in tutto questo registro — e la correzione e' averne una sola: la stessa struttura
per il file, la stessa struttura per le voci dell'albero.

**Come e' stato visto.** Con un albero di prova **dentro il progetto**
(`analysis/tmp/alloc-test`, contenuto finto: `AGENTS.md` §0), costruito apposta
perche' i due numeri **non** coincidessero: file piu' corti di un cluster, piu' lunghi
di un cluster ma non multipli, un file **sparso** da 64 MB con due soli cluster
scritti, e una cartella **compressa** con 20 MB di zeri. Un albero di file qualunque
non avrebbe distinto le due strade, perche' su questa macchina coincidono quasi
sempre.

E il confronto e' con un **riferimento indipendente**: una sonda che per ogni file
apre un handle e chiede `FILE_STANDARD_INFO` — una chiamata per file, l'altra strada
possibile — e somma. I due numeri:

    riferimento (una chiamata per file)   logico 97 527 908   allocato 9 580 648
    applicazione (a blocchi, per cartella)     93.0 MB             9.1 MB
    e per il file sparso:            riferimento 131 072   applicazione 128.0 KB

Due percorsi di codice diversi che dicono lo stesso numero: e' l'unico modo di
sapere che il numero e' quello e non un altro che gli somiglia.

**Nota su che cosa significa il numero.** `AllocationSize` e' quanto il disco ha
riservato: un file da 10 240 byte ne occupa 12 288 (tre cluster), un file da 100 byte
ne occupa 104 perche' sta **dentro** il record del file system, e un file compresso
o sparso ne occupa molto meno della sua lunghezza. E' la stessa cosa che Esplora
risorse chiama «Dimensione su disco», ed e' l'unico campo che la dice.

### 7.47 Il file creato non compariva: l'ultima azione vinceva sulla creazione

**Trovato verificando la sorveglianza delle cartelle** (M3), e trovato **solo** perche'
la prova e' stata fatta in modo da dire qualcosa: si e' creato un file da fuori e si e'
guardato se la riga compariva **senza toccare l'applicazione**. Non compariva.

Il registro diagnostico, acceso apposta, ha detto cosa era successo:

    22:33:48.577  sorveglianza: 1 notifiche (create 1, cancellate 0, modificate 0, rinominate 0)
    22:33:48.578  sorveglianza: 1 notifiche (create 0, cancellate 0, modificate 1, rinominate 0)
    22:33:48.588  cambiamento esterno: 1 voci, righe da ricaricare: no

**La causa.** Scrivere un file non produce una notifica, ne produce **due**: la
creazione e poi la modifica. Il sorvegliante teneva un cambiamento per percorso, e
della stessa voce ricordava **l'ultima azione** — che era `MODIFIED`. Il consumatore
poi decide se ricaricare l'elenco in base all'azione: `MODIFIED` non cambia le righe,
`CREATED` si'. La creazione era finita dietro la modifica, e la riga non compariva.
Il difetto non era nel sorvegliante, che faceva esattamente quello che diceva di
fare: era nella regola di fusione, che sceglieva l'azione sbagliata delle due.

**La correzione**: di un percorso toccato piu' volte si ricorda l'azione che **cambia
l'elenco**, non l'ultima arrivata. La regola sta in un posto solo
(`DirectoryWatcher::ChangesListing`), perche' la usano in due — chi raccoglie le
notifiche e chi le applica — e due copie della stessa regola divergono sempre.

**Verificato a schermo**, non dedotto: dopo la correzione il file creato da fuori
compare nella lista da solo (`analysis/tmp/w-07-voci.png`), e il registro dice
`righe da ricaricare: si` dove prima diceva `no`.

**Perche' la prova e' stata severa.** Fra il momento in cui si creava il file e
quello in cui si catturava, l'applicazione **non e' stata toccata**: niente clic,
niente `F5`. Se la riga compariva, era il sorvegliante a farla comparire. Una prova
che avesse premuto `F5` "per essere sicuri" avrebbe nascosto il difetto per sempre.

### 7.48 Il registro diagnostico lo scrivevano due thread

**Difetto introdotto da questa stessa modifica**, e corretto subito. `DiagLog`
accumula le righe in un `std::string` statico e riscrive il file ogni volta. Con il
sorvegliante delle cartelle le chiamate arrivano da **due thread** — quello della
finestra e quello che aspetta le notifiche — e due chiamate sovrapposte scrivevano e
leggevano lo stesso `std::string` insieme.

Il compilatore non lo dice: `/W4` non vede le corse, e nessun test lo avrebbe
mostrato, perche' si manifesta ogni tanto. Si e' visto **leggendo il diff** prima di
chiudere: una funzione documentata come `void DiagLog(const std::string&)` non dice
da quale thread si puo' chiamare, e nessuno se lo chiede finche' non e' tardi.

**Correzione**: un mutex dentro `DiagLog`, e la dichiarazione in `diag.h` che ora
dice che si puo' chiamare da qualunque thread — perche' e' vero, e perche' chi la
chiama deve poterlo sapere senza leggere il `.cpp`.

### 7.49 La linguetta di una sezione non cambiava mai verso, e non era un carattere

**Trovato dall'autore guardando**, con una frase che e' anche la diagnosi: «le
linguette vicino ad accesso rapido, recenti, preferiti non le voglio cosi'
statiche». Statiche: non cambiavano mai. Erano due difetti in uno, e il secondo
nascondeva il primo.

**Il primo: non erano elementi, erano caratteri.** Il verso si disegnava scrivendo
il testo `v` o `>` col font di interfaccia. Un carattere non e' un elemento: cambia
forma col carattere scelto nelle opzioni (`Font`, `Font size` in M9), non si
allinea con niente, e a nove pixel e' un grumo con l'antialiasing sbagliato. La
correzione e' una primitiva nuova nello shader — un **triangolo** — in tutti e due i
backend, perche' i due backend sono la stessa cosa detta in due lingue.

Due cose che la primitiva ha insegnato, e che valgono piu' della primitiva:

- **`const` in GLSL vuole un'espressione costante.** `const float width = halfSize.x;`
  dentro una funzione non e' una costante, e il compilatore lo rifiuta con un errore
  che non dice cosa fare. In HLSL sarebbe passato: i due shader vanno scritti uguali,
  o la differenza si scopre solo compilando l'altro backend.
- **In `point` space gli assi sono girati tutti e due.** Il frammento calcola
  `point = centro - frag`, quindi `+X` va a **sinistra** e `+Y` va in su'. Il
  rettangolo arrotondato non se ne accorge — e' simmetrico, e per anni ha funzionato
  — ma un triangolo si': il primo tentativo disegnava una linguetta che puntava
  dalla parte sbagliata. La riga che riporta il punto in uno spazio leggibile e'
  adesso commentata, perche' e' una di quelle che si cancellano per sbaglio.

**Il secondo difetto, che era quello vero.** Il verso lo decideva

    const bool collapsed = app.collapsedSections.count(item.title) != 0;

mentre lo stato di chiusura e' ricordato con **`item.key`** — la chiave inglese — ed
e' ricordato con quella apposta: «un percorso che dice `This PC` accanto a una barra
laterale che dice `Questo PC` e' due nomi per la stessa cosa», e la chiave non si
traduce perche' chiudere una sezione in italiano deve lasciarla chiusa anche in
inglese. Con il **nome** nel test, la chiave non si trovava mai: `collapsed` era
sempre falso, e la linguetta restava ferma. Le altre due strade — quali righe
disegnare, e cosa fare al clic — usavano gia' la chiave. **Due su tre**, e la terza
era quella che si vede.

Non e' un difetto del triangolo: c'era anche col carattere. E' un difetto che
**non faceva rumore**, perche' una freccia che non cambia mai sembra una scelta
grafica, e chi la guarda pensa che sia disegnata cosi'. E' la forma di guasto che
questo registro chiama peggiore: non sbaglia, non fa.

**Poi la forma, chiesta dall'autore**: «leggermente meno appuntite, meno allungate».
Erano equilatere — base e profondita' uguali, angolo in punta di 53 gradi — e
adesso sono **piu' larghe che profonde** (due terzi), con l'angolo in punta a 74
gradi, e gli **spigoli arrotondati** di poco. La punta resta una punta, ma smette di
essere un ago.

Due scelte che valgono piu' del ritocco. La prima: la forma si decide in **un posto
solo** (`DisclosureArrow`, in `app_ui.*`), non in ognuno dei quattro punti che la
disegnano — e il rettangolo lo costruisce quella funzione, perche' la linguetta
chiusa punta a destra mentre quella aperta punta in giu', e le due hanno bisogno di
proporzioni **opposte**: larga e bassa una, alta e stretta l'altra. Chi chiama non
deve saperlo. La seconda: l'arrotondamento si **deriva** dalla base, cosi' regge a
ogni ingrandimento senza un secondo numero da tenere allineato.

**Quattro posti, non uno.** Cercando il carattere nel codice ne sono usciti altri
tre, tutti con lo stesso trucco: le **bande dei gruppi** nella lista, le
intestazioni della **palette dei comandi** e quelle della finestra **GoTo**. Adesso
usano tutti la stessa primitiva, ed e' l'unico modo perche' restino uguali: quattro
disegni della stessa linguetta sono quattro occasioni di farne uno diverso.

**Verificato a schermo** su tutti e quattro: chiusa punta a destra, aperta in giu'
nella barra laterale, e in giu' nei gruppi della lista e nelle intestazioni della
palette. E su **entrambi i backend**, perche' i due shader sono due lingue per la
stessa forma e una delle due poteva restare indietro. Prima della correzione il
difetto e' stato **riprodotto apposta**, disegnando le due varianti affiancate in
rosso e in verde: «mi sembra diverso» non e' una prova, due triangoli affiancati in
un fotogramma si'.

### 7.50 Il piu' e la croce erano caratteri

**Stessa famiglia di §7.49**, e chiesto dall'autore subito dopo: il `+` che apre un
tab e la `x` che lo chiude erano **caratteri**, non elementi. Il `+` si disegnava col
corpo del testo, la `x` con quello secondario, e la chiusura della finestra con la
seconda. Tre posti, due primitve nuove: un **piu'** e una **croce**, nello shader,
per tutti e due i backend.

- Il **piu'** e' la **minima** di due rettangoli arrotondati: due barre dritte che si
  incrociano, con le punte tonde come ogni altro bordo del programma.
- La **croce** e' la minima distanza dalle **due diagonali**, ognuna un segmento con
  le punte tonde, accorciate quanto basta perche' le punte restino dentro il
  quadrato. Un incrocio di due strisce infinite sarebbe stato tagliato agli angoli
  dal rettangolo, e si sarebbe visto.
- Lo **spessore** della barra viaggia nel campo dei raggi, che per queste due forme
  non vuol dire niente: il formato del vertice e' chiuso a trenta float e non si
  allarga per una comodita'.

**Una trappola del GLSL, pagata subito.** Le due funzioni nuove usavano
`roundedBoxDistance`, che in `ui.frag` e' definita **piu' sotto**: in GLSL una
funzione va dichiarata prima di essere usata, e il risultato non e' un errore di
compilazione del C++ ma un `Startup failed` dell'applicazione — la finestra si apre e
dice che non e' partita. Il registro diceva `undefined variable
"roundedBoxDistance"`, che e' esattamente cosa fare. Vale la pena ricordarlo perche'
la tentazione e' aggiungere le funzioni vicino alle costanti, dove si leggono bene, e
il posto giusto e' **dopo** la funzione che usano. **Ed e' successo due volte**: la
seconda, un'ora dopo, arrotondando gli spigoli del triangolo, che ha cominciato a
usare `segmentDistance` — definita piu' sotto. Il GLSL e' stato sistemato al primo
colpo; l'**HLSL** no, perche' li' il blocco era in un altro ordine ancora. Due
linguaggi per la stessa forma vuol dire due volte lo stesso lavoro, e la seconda
volta non si ricorda la prima.

**Verificato a schermo**: il `+` e la `x` del tab sono due forme disegnate
(`analysis/tmp/p-04-big.png`), i tre pulsanti di finestra sono tutti e tre forme
(riduci a icona era gia' un rettangolo, ingrandisci due bordi, chiudi adesso una
croce), e i due clic funzionano: il `+` apre un secondo tab e la `x` lo chiude.

### 7.51 Il suggerimento che leggeva la riga 1 di 1

**Sintomo.** Con un file di testo scelto nel pannello di destra, entrando in una
cartella che contiene un'immagine l'applicazione **spariva**: nessun messaggio,
nessuna finestra, nessun `startup-error.log`, e nemmeno la voce di Windows Error
Reporting che di solito accompagna una caduta. Da bash si vedeva `Segmentation
fault`, e solo da li': fuori da un figlio della shell l'uscita sembrava pulita.

**Causa.** `app.hovered` e' un **indice** nell'elenco delle voci, e nessuno lo
azzerava quando l'elenco veniva rifatto. Il puntatore era fermo sulla riga 1
dell'elenco di prima (`immagini`, in una cartella di sei voci), l'ingresso nella
cartella portava l'elenco a **una** voce, e il suggerimento — che si disegna dopo
una pausa di 0,55 s, quindi in un fotogramma successivo all'ingresso — leggeva
`app.entries[1]` su un vettore di una voce sola. Cursore e selezione si ritrovano
per **nome** e non hanno questo problema; il sovrappasso no, perche' il puntatore
sta su un pixel, non su un file.

**Correzione.** `LoadDirectory` azzera `hovered` **e** `hoverStart` dove gia' azzera
cursore, selezione e scorrimento: l'indice appartiene all'elenco che si sta
lasciando. Le due letture (`DrawTooltip` e il blocco di `HoverToSelect` nel ciclo)
hanno anche il controllo di limite, che riporta lo stato a "niente sotto il
puntatore" invece di leggere fuori dal vettore.

**Perche' era intermittente.** Il suggerimento compare solo se il puntatore e'
fermo da oltre mezzo secondo: nei giri in cui uno script lo spostava dopo
l'ingresso, l'indice veniva ricalcolato e la caduta non si presentava. Tre tentativi
su tre con la stessa sequenza l'hanno reso riproducibile, ed e' li' che si e'
potuto **misurare** invece di discutere: `--- tooltip: riga 1 di 1 voci` nel
registro diagnostico, scritto dal fotogramma che poi moriva.

**Trovato per caso, mentre si faceva altro.** Il tema chiaro non c'entrava niente:
la caduta e' venuta fuori provando a entrare in una cartella con un'immagine per
guardare il tema. E' anche la ragione per cui la ricerca e' partita da
`git stash` — *succede anche senza le modifiche in corso?* — che e' la prima
domanda da farsi prima di toccare il codice.

### 7.52 Il backend Direct3D 11 che non poteva partire

**Risolto il 2026-10-18: era una parola riservata.** Il Direct3D 11 adesso parte,
disegna e resta il backend di partenza; il giro di verifica a schermo e' in fondo a
questa voce, e `docs/05` §3 lo racconta nell'ordine dei lavori.

**Sintomo.** Con `BOLT_BACKEND=d3d11` — e anche senza la variabile, che il
Direct3D lo prova per primo — il registro diceva sempre la stessa cosa:

```
--- backend Direct3D 11 non disponibile: HLSL main failed to compile (0x80004005):
ui.hlsl(32,30-34): error X3000: syntax error: unexpected token 'point'
```

e il programma ripiegava sull'OpenGL. Il ripiego funziona, quindi il difetto non si
vedeva: si vedeva **solo** in quella riga di registro, e chi non accendeva
`BOLT_DIAG=1` non poteva accorgersene. La milestone M10 si credeva chiusa e invece
il secondo backend **non era mai stato eseguito**.

**Causa.** Nelle funzioni di distanza dello shader incorporato in
`d3d11_renderer.cpp` il parametro si chiamava `point`:

```hlsl
float segmentDistance(float2 point, float2 a, float2 b, float radius)
```

In HLSL `point` **e' una parola riservata** (la primitiva del pipeline legacy),
quindi la compilazione si fermava li' e i due errori successivi («undeclared
identifier 'b'») erano la cascata del primo. Non c'entrava la macchina: la sorgente
e' una costante dentro il file, quindi quel backend **non poteva partire su nessun
computer**. Il rimedio e' rinominare il parametro in `p` — quindici occorrenze,
tutte nelle quattro funzioni di distanza — e la compilazione riesce: `--- backend:
Direct3D 11`.

**La correzione, applicata il 2026-10-18.** Il parametro si chiama `p` in tutte e
quattro le funzioni di distanza, con il commento accanto che dice perche' (una
parola riservata, non un gusto di scrittura). Prima di toccare l'applicazione la
compilazione e' stata provata **senza aprire finestre**: i due shader estratti dalla
sorgente e passati a `fxc` (lo stesso compilatore che `D3DCompile` usa a runtime),
che ha ripetuto l'errore identico su `point` e poi ha accettato `p`:

```
ps.hlsl(32,30-34): error X3000: syntax error: unexpected token 'point'
ps.hlsl(34,17-21): error X3000: syntax error: unexpected token 'point'
ps.hlsl(34,17):    error X3004: undeclared identifier 'b'
... poi, con `p`: `compilation object save succeeded` su vs_5_0 e ps_5_0
```

**Il giro di verifica**, con `BOLT_BACKEND=d3d11 BOLT_DIAG=1` sulla sandbox del
progetto (`analysis/tmp/sandbox`), una finestra sola sulla scrivania corrente
(`AGENTS.md` §0), chiusa quella e nient'altro aperto:

| superficie | cattura | cosa si e' visto |
|---|---|---|
| registro | `bolt-diag.log` | `--- backend: Direct3D 11`, e nessun errore HLSL |
| lista con le colonne, ispettore | `analysis/tmp/d3d-01-lista.png` | colonne, gruppi, icone di sistema, barra di stato |
| menu contestuale | `d3d-02-menu.png` | i verbi della shell, i separatori, la barra di scorrimento, i sottomenu |
| opzioni (`Ctrl+,`) | `d3d-03-opzioni.png`, `d3d-05-opzioni-fondo.png` | i controlli tipizzati, le due righe spente, il cursore della misura |
| mappa dei tasti | `d3d-06-keymap.png` | l'elenco dei comandi (83 di 83) col tasto accanto |
| tema chiaro | `d3d-07-chiaro.png` | fondo `(255,255,255)` contro `(35,36,40)` dello scuro, letto **da un pixel** |
| immagine grande e visualizzatore di testo | `d3d-09-anteprima.png`, `d3d-10-testo.png` | lo strato 5 e le righe del testo |
| viste a icone | `d3d-11-icone.png` | le miniature dallo strato 2, gli angoli arrotondati della cella scelta |

Le forme SDF (le linguette, il `+` e la croce della barra del titolo), il ritaglio
della lista e il testo si disegnano come nell'OpenGL: e' lo stesso shader, detto due
volte. `startup-error.log` non esiste, il processo si e' chiuso e non e' rimasta
nessuna finestra aperta.

**Quello che il giro non ha coperto**, e che va detto: le prove sono una per
superficie, non ripetute; il trascinamento (dentro, fuori, dal cestino) non e' stato
rifatto sotto Direct3D, ne' il dialogo di progressione, ne' le altre due viste a
icone. Restano da guardare quando si tocca quella parte, e adesso il backend che
gira e' quello: un difetto che comparisse solo li' si vedrebbe subito, non piu'
solo nel registro.

**Una caduta rara, incontrata mentre si misurava questo.** Provando il backend
corretto sono comparse cadute all'avvio, prima della riga
`accesso rapido: 7 voci` — cioe' dentro l'enumerazione delle cartelle della shell.
Misurate: `0 su 30` avvii con il commit precedente, `5 su 55` con le modifiche di
questa sessione (in OpenGL), `3 su 8` col Direct3D, e **`0 su 12` sotto
AddressSanitizer**, che quindi non la fa riprodurre e non ne da' una pila. Le
proporzioni non bastano a dire se l'abbia portata il tema o se fosse gia' li' (con
questi numeri la probabilita' che sia un caso e' intorno al 15%), quindi va cercata
con un debugger o con un campione molto piu' grande. **Resta dichiarata**, con la
sua probabilita' misurata: e' la prima cosa da riprendere. — *Chiusa il 2026-10-18
come §7.54: era §7.53 visto dall'altro lato, un pidl liberato e poi letto.*

### 7.53 Il pidl liberato e poi usato

**Sintomo.** Nessuno: è la forma peggiore. Il pidl di una voce di spazio dei nomi
veniva liberato e poi **letto di nuovo**, due righe più sotto.

**Causa.** In `ShellLocation::Read`, per ogni voce:

```cpp
if (absolute != nullptr)
{
    ... SHGetNameFromIDList(absolute, ...) ...
    CoTaskMemFree(absolute);          // <-- il pidl e' liberato qui
}

if (absolute != nullptr)              // ... e usato ancora qui
{
    const size_t size = ILGetSize(absolute);            // legge memoria restituita
    item.pidl.assign(...absolute + size);               // copia un blocco riusabile
    ... ILIsEqual(absolute, reparsed) ...               // e lo confronta
}
```

`ILGetSize` legge i primi byte del blocco, e `item.pidl` ne copia il contenuto: fra
la liberazione e la lettura l'allocatore può aver riusato quel blocco, quindi il
pidl della voce poteva essere **un altro** — o spazzatura. Le conseguenze sono
silenziose e stanno dove servono: i verbi della shell ricevono l'identità della
voce (il ripristino dal cestino, il trascinamento fuori), e `navigable` — che decide
se aprirla porta dove dice il nome — si calcolava da un confronto su memoria
restituita.

**Correzione.** `absolute` si libera **alla fine** del blocco che lo usa, dove
finiscono anche la copia del pidl e il confronto. Una riga spostata, con il
commento accanto che dice perché sta lì.

**Come è venuto fuori.** Non da una prova: dal rimuovere dei breadcrumb temporanei.
Il taglio meccanico ha tolto il `CoTaskMemFree` dalla prima `if` e l'ha rimesso in
fondo alla seconda — e il diff l'ha mostrato. E' la stessa lezione di §5 di
`AGENTS.md` al contrario: **rileggere il diff dopo un taglio meccanico serve anche a
trovare quello che c'era prima**, se il taglio ha spostato qualcosa invece di
toglierlo.

**Verificato**: build senza warning; un file della sandbox messo nel cestino
dall'applicazione, letto nel cestino col filtro sul suo nome (nome, tipo e
dimensione corretti), e **ripristinato** con la voce `Ripristina` del menu
contestuale (verbo `undelete` della shell, che riceve il pidl). La verifica è il
conteggio dei file nella sandbox, non una cattura del cestino — `AGENTS.md` §0.

### 7.54 La cartella che si elenca e qualche volta porta via l'applicazione

**Chiuso il 2026-10-18. Non era un difetto a sé: era §7.53 guardato dall'altro
lato** — lo stesso pidl liberato e poi letto, nella lettura dell'accesso rapido.

**Sintomo.** Cadute all'avvio circa una volta su dieci, **a raffiche**: `0 su 30`,
poi `5 su 55`, poi `3 su 8` col Direct3D, e piu' tardi `0 su 40`, `0 su 40`,
`0 su 60`. Coi breadcrumb, **4 cadute su 4** avevano l'ultima riga in
`BuildQuickAccess`, subito prima della lettura dell'accesso rapido.

**La pila che mancava era gia' su disco.** Nessuno l'aveva cercata: in
`%LOCALAPPDATA%\CrashDumps` c'erano **nove** dump di Bolt, scritti fra il 2026-10-13
e il 2026-10-15 — cioe' nei giorni in cui il difetto si misurava — perche' la
cartella dei dump era gia' configurata da qualcuno, prima che si decidesse che
serviva un debugger. Quattro sono di questa famiglia:

| dump | quando | dentro quale modulo | chiamato da |
|---|---|---|---|
| `740`, `8520`, `13868` | 2026-10-15, 22:31–22:35 | `windows.storage.dll`, **`ILGetSize+0x10`** | `Bolt.exe+0x98B05` |
| `76812` | 2026-10-14, 01:06 | `windows.storage.dll+0x159EF0` | `Bolt.exe+0x90A65` |

Le tre del 22:31–22:35 sono **lo stesso indirizzo** dentro la stessa versione della
DLL, e sono a quattro minuti di distanza: e' la raffica, fotografata. Lo stack non
l'ha camminato un debugger — su questa macchina `cdb.exe` non c'e' — ma un lettore
scritto per l'occasione (`dumpscan`, `StackWalk64` + `MiniDumpReadDumpStream`, in
`analysis/tmp/crashprobe/`): intestazione, exception stream e module list bastano a
dire *dove*, e la camminata dice *chi l'ha chiamato*.

**Causa.** `ILGetSize` che legge un pidl **gia' restituito a `CoTaskMemAlloc`**.
Fino al commit `93bfa66` (2026-10-14) `ShellLocation::Read` faceva cosi':

```cpp
CoTaskMemFree(absolute);
...
const size_t size = ILGetSize(absolute);      // <-- legge memoria restituita
item.pidl.assign(...absolute + size);         // ... e ne copia il contenuto
if (... ILIsEqual(absolute, reparsed) ...)    // ... e la confronta
```

E' **lo stesso difetto di §7.53**, che era stato trovato togliendo dei breadcrumb e
corretto spostando quel `CoTaskMemFree` in fondo al blocco: qui era la stessa riga
vista da fuori, come una caduta. Una cosa sola, due sintomi: uno silenzioso (il
pidl copiato era di un altro blocco) e uno rumoroso (la caduta).

**Perche' AddressSanitizer non la riproduceva.** Il pidl lo alloca **la shell**, con
il proprio allocatore: ASAN strumenta quello del CRT nostro, non quello di
`windows.storage.dll`. Liberare e rileggere un blocco di qualcun altro non e' un
errore *per ASAN*, quindi `0 su 12` non voleva dire «il difetto non c'e'»: voleva
dire «questo strumento non lo vede». E' la stessa lezione di §2: se il difetto e'
invisibile allo strumento, lo strumento e' sbagliato.

**Prova, e non correlazione.** La correzione era gia' nel codice; per sapere se e'
*quella* correzione, l'ordine di prima e' stato rimesso apposta e la build rifatta:

| build | avvii | cadute |
|---|---|---|
| ordine di prima (`free` prima delle due letture) | 120 | **6** (5%), `0xC0000005` |
| ordine corretto (`free` in fondo al blocco) | **515** | **0** |

Le sei cadute riprodotte hanno il registro diagnostico che si ferma **tutte e sei**
fra `--- backend: OpenGL` e `accesso rapido: N voci` — la stessa riga dei breadcrumb
del 2026-10-16, ricavata con lo stesso metodo. Il tasso ritrovato (5%) e' quello
delle raffiche storiche. Con l'ordine corretto, **0 su 515** (i 455 avvii della
misura di oggi piu' 60 di controllo): con zero su 515, il tetto al 95% e' ~0,6%,
contro il 10% misurato prima.

Il primo taglio dell'esperimento riusciva **troppo** bene: `150 su 150` di
`0xC0000374` (corruzione dell'heap). Era un difetto del taglio, non una conferma:
aveva lasciato **due** `CoTaskMemFree(absolute)` dove l'originale ne aveva uno, e
due liberazioni sono un altro difetto (deterministico) — non l'ordine da provare.
Vale la regola di §5 di `AGENTS.md` al contrario: quando una prova riesce meglio del
previsto, si rilegge il diff prima di scriverne il risultato.

**Perche' non arrivavano dump nuovi.** Mentre si riproduceva, `LocalDumps` (chiave
`HKCU`, `DumpType=1`) non scriveva **niente**, nemmeno per un programma di tre righe
che dereferenzia il nullo. La ragione sta in una riga sola che WER tiene in `HKCU`:
`LastRateLimitedDumpGenerationTime`. WER **limita** i dump locali, e in una raffica
di avvii il limite si mangia proprio quelli che servono. I dump che hanno chiuso la
questione sono quindi quelli **vecchi**, non quelli che si sarebbero potuti
scrivere: prima di armare un banco, si guarda cosa c'e' gia' nella cartella dei
dump. Vedi `docs/05` §5.

**Il banco, perche' la prossima caduta rara costa meno.** Un desktop Win32
**invisibile** (`CreateDesktop` + `STARTUPINFO.lpDesktop`): le finestre nascono e
muoiono la' dentro, quindi niente compare sullo schermo di chi lavora e nessuna
scrivania virtuale viene cambiata (`AGENTS.md` §0). Un avvio costa ~0,5 s, il
processo si ferma al primo fotogramma visibile, e le cartelle di prova sono quelle
della sandbox.

**Resta una seconda famiglia, dichiarata e non di questa voce.** Cinque dump cadono
**dentro il nostro eseguibile** (`+0x1AA6BB` due volte, `+0x19F9DB`, `+0x16EBC9`,
`+0x162999`; due con `0xC0000409` e parametro 7, cioe' un `terminate`), tutti fra il
2026-10-13 e il 2026-10-15. **Non si sono riprodotti**: 0 su 515 avvii oggi, e non
hanno la firma di questa voce (non passano da `ILGetSize`, e la pila sotto il primo
frame e' gia' spazzatura). Vanno cercati con lo stesso banco se ricompaiono; qui
restano scritti perche' e' successo.

### 7.55 Le superfici che parlano di una voce che la lista non mostra

**Chiusa il 2026-10-18.** La strada scelta è quella che il documento indicava come
più coerente: **il cursore si sposta** sulla prima riga visibile, come fa Esplora
risorse. Nel chiuderla sono uscite altre tre superfici con lo stesso difetto, e
adesso seguono tutte una regola sola.

**Sintomo, il primo.** Nel cestino, col filtro su un nome che corrisponde a **una**
voce, la lista ne disegnava una e l'ispettore ne mostrava **un'altra**: la voce su
cui stava il cursore prima di filtrare, che il filtro ha nascosto.

**Causa.** Il cursore è un nome e sopravvive al cambio di filtro — è voluto, come la
selezione: `SelectedPaths` infatti scarta le voci non visibili prima di proporre
un'operazione, e per questo nessun comando agisce su qualcosa che non si vede. Ma
l'ispettore legge il cursore e basta, mentre la lista disegna solo le righe che
passano il filtro: le due viste dicono cose diverse, e quella che resta scritta
(«Tipo, Dimensione, Percorso…») parla di un file che non c'è.

**Correzione.** `EnsureCursorOnVisibleRow` in `view_list.cpp`, chiamata in fondo a
`RebuildView`: se la voce del cursore non è più una riga della vista, il cursore va
sulla prima che c'è. Sta **dentro il ricostruire la vista** e non nel gestore del
filtro per una ragione sola: a nascondere righe non è solo il filtro. Ci arrivano
anche i **filtri di colonna** e un **gruppo chiuso**, che è l'altra strada per
lasciare il cursore su una riga che non si vede — e nessuno dei due passa dal
codice del filtro.

**La selezione non si tocca**, ed è la parte che la voce chiedeva di decidere. Le
voci nascoste restano selezionate: nessun comando ci agisce sopra (`SelectedPaths`
le scarta) e togliere la selezione mentre si filtra vorrebbe dire perdere il lavoro
fatto prima di filtrare. Quindi cursore e selezione possono divergere, ed è lo
stato giusto.

**Tre superfici sorelle, trovate mentre si verificava, e chiuse con la stessa
regola** — «una superficie dice solo quello che la lista mostra»:

1. **Il suggerimento** continuava a mostrare la voce di prima quando il filtro ne
   toglieva la riga da sotto il puntatore: il puntatore non si era mosso, quindi
   nessuno lo riazzzerava, e sopra un elenco vuoto restava scritto il nome di una
   cartella. Adesso `DrawTooltip` esce se la voce col puntatore sopra non è una riga
   della vista.
2. **Il suggerimento sopra il pannello delle proprietà**: era una fila di
   condizioni sciolte dentro `DrawTooltip` che non nominava i pannelli, e il nome
   del file finiva disegnato sopra le righe dei metadati. Adesso la regola è **una
   sola** — `TooltipsAllowed` — e comprende menu, menu contestuale, pannello delle
   proprietà, operazione in corso e trascinamento delle colonne.
3. **La barra di stato** contava `app.selection.size()`, quindi «3 selezionati»
   davanti a una riga sola, e «1 selezionato» con l'elenco vuoto. Adesso conta le
   scelte **visibili**, come il conto della posizione a destra conta le righe
   visibili. L'ispettore fa lo stesso per l'aggregazione di più voci: sommare anche
   quelle nascoste sarebbe un totale che sullo schermo non si trova.

**Verificato a schermo**, in una finestra sola sulla sandbox (`analysis/tmp/fix-*.png`):

- filtro `re` con il cursore su un'altra voce → la lista mostra `relazione.txt`,
  **il cursore si sposta lì** e l'ispettore la descrive (`fix-01`, `fix-02`);
- filtro `zzz` → nessuna riga, ispettore «Niente selezionato», nessun suggerimento
  rimasto, e **nessun «selezionato»** nella barra (`fix-14`);
- togliendo il filtro il cursore **torna** dov'era: è la ragione per cui, quando non
  c'è nessuna riga visibile, il cursore **non** viene azzerato ma lasciato dov'è
  (`fix-15`) — azzerarlo faceva perdere anche `Invio`, e questo è stato visto e
  corretto in questa stessa sessione;
- pannello delle proprietà aperto col puntatore sopra una riga → **nessun**
  suggerimento sopra il pannello (`fix-05`); chiuso il pannello, lo stesso
  suggerimento ricompare (`fix-06`), che è il controllo: era il pannello a farlo
  tacere, non un hover azzerato;
- gruppo `CZ` chiuso col cursore dentro → il cursore passa alla prima riga visibile, e
  `Invio` apre **quella** cartella (`fix-17`, `fix-18`).

**Un limite dichiarato.** Con la selezione tutta nascosta e il cursore su una riga
visibile, l'ispettore dice «Niente selezionato» invece di descrivere la riga del
cursore: il pannello si apre sulle voci **scelte visibili**, e in quello stato non
ce ne sono. È una scelta, non una dimenticanza: descrivere una voce che non è scelta
sarebbe l'altra faccia dello stesso difetto.

### 7.56 Le tre superfici che restavano in inglese

**Sintomo.** Con la lingua su **italiano**, la palette dei comandi si apriva con le
bande `View`, `Language`, `Appearance`, `Tabs` e una riga `Command palette` in mezzo
alle altre, e il pannello delle opzioni mostrava `Font`, `Font Size`, `Spacing`,
`Zoom` sopra descrizioni italiane. Lo ha visto l'autore, guardando la palette: «sbaglio
o molte cose sono in inglese?».

**Causa.** Due cose, e la seconda è quella che conta.

La prima è meccanica: quelle tre non sono stringhe scritte a mano — non si trovano
cercando un testo in inglese — sono **chiavi** disegnate così come sono. Le bande di
un menu disegnano il campo `group` dell'item (`View`, `Sort`, `GroupBy`), le righe
delle opzioni disegnano il campo `label` della tabella, e `CommandLabelIt` — la
tabella delle traduzioni dei comandi — non aveva la voce di `CmdPalette`, l'unica
mancante su ottanta.

La seconda è che **il documento diceva che andava bene così**. Il paragrafo «Cosa
resta in inglese, e perché» di `docs/04` sosteneva che i nomi del riferimento non si
traducono — «sono *i suoi nomi*, e cambiarli renderebbe il clone meno fedele» —
elencando proprio le colonne, le sezioni della barra laterale, le righe dell'inspector
e i gruppi della palette. Ma la decisione contraria è dell'autore, il **2026-10-14**
(`AGENTS.md` §3): i nomi del riferimento si traducono, e **la chiave resta l'inglese**
proprio perché è con la chiave che si ricorda cosa è chiuso. Il paragrafo non è mai
stato corretto: le superfici tradotte subito (colonne, barra laterale, inspector,
bande di raggruppamento) hanno continuato a funzionare, e quelle che il paragrafo
elencava sono rimaste in inglese **perché il paragrafo diceva che era giusto**.
`docs/05` nel frattempo dichiarava «lingua su tutte le superfici».

**Correzione.** Una tabella `MenuGroupLabelIt` in `view_menu.cpp` accanto a chi
disegna le bande (17 chiavi, una voce per i gruppi che non si chiamano come in
italiano: `File` resta `File`), usata **al disegno** e non sulla chiave; il campo
`labelIt` nella tabella `kRows` delle opzioni (27 nomi, con la parola del comando
dove la riga e l'ordine sono la stessa cosa — `Show Tooltips` è «Mostra i
suggerimenti» in tutt'e due i posti); la voce mancante di `CmdPalette`; e il paragrafo
di `docs/04` riscritto con quello che resta davvero in inglese (i verbi della shell,
che sono del sistema, la diagnostica di avvio, i nomi dei file). Il commento sopra
`CommandLabelIt`, che diceva «la lista è tenuta corta apposta», è stato corretto: con
ottanta voci su ottanta non è più una lista corta.

**Verificato a schermo**, in tutte e due le lingue — palette, GoTo, mappa dei tasti,
pannello delle opzioni — e con la prova che conta: **chiusa la banda `Vista` in
italiano, in inglese si chiama `View` ed è ancora chiusa**, e tornando indietro è di
nuovo `Vista` chiusa. La chiave non si è mossa. Catture in `analysis/tmp/keys/`.

**La lezione.** Un documento che descrive il codice di prima è la stessa specie di
difetto di un commento che lo fa: costa come un commento sbagliato e si paga a
schermo. E le superfici nate da una **chiave** invece che da una frase non si trovano
leggendo il codice — bisogna guardare la schermata e chiedersi cosa c'è scritto.

### 7.57 Il pannello che si apriva e non si vedeva (e diceva il falso)

**Sintomo.** `Fissa in...` scelto dalla palette non faceva **niente**: la palette si
chiudeva, nessun pannello compariva, e la barra di stato non diceva nulla. Due volte
di fila, con lo stesso esito, che è il modo in cui un guasto diventa «quel comando non
funziona».

**Causa: due, una sotto l'altra.** La prima era una **registrazione mancante**: un menu
nuovo va iscritto in **quattro** posti — `OpenMenu` (chi risponde all'input), il
disegno in `RenderFrame`, e le **due** sedi che eseguono una voce attivata, il clic e la
tastiera. Ne avevo fatti tre: il pannello si apriva, prendeva i tasti, e non si
disegnava. Il codice sembrava giusto perché lo era — per metà.

Tolta quella, il pannello **non si apriva ancora**. Qui la lezione è il metodo: invece
di rileggere il codice ho aggiunto **due righe di registro** (`DiagLog` con
`BOLT_DIAG=1`) — il comando che arriva e l'identificativo scelto dalla palette — e in
un colpo solo si è visto che il comando *arrivava* (`palette id 77`, `cmd 77`) e che
l'unica funzione che non compariva nel registro era `BuildPinMenu`. Il difetto era
dentro di lei: `applicable` era **invertita** (`!= removing` invece di `== removing`),
quindi il pannello usciva subito per «niente da fare» e scriveva in barra di stato
«questa cartella è già fissata in tutte le sezioni» — **la prima volta che si usava il
comando**, quando non era fissata da nessuna parte. Un messaggio falso, che è peggio
del silenzio: avrebbe fatto cercare il difetto nella lista dei fissaggi.

**Correzione.** La riga di disegno in `RenderFrame`; il verso della condizione (con il
commento accanto che dice cosa vuol dire «applicabile»: la destinazione **cambierebbe**
qualcosa) e la strumentazione tolta.

**Verificato a schermo.** Le catture di `docs/04` M6, e in particolare le due che
sarebbero state impossibili con la condizione invertita: il pannello con la riga
«Accesso rapido (è già fissata qui)» accanto a «Segnalibri», e `Togli da...` con la
riga «Segnalibri (non è fissata qui)».

**La lezione.** Un menu nuovo si registra in quattro posti e nessun compilatore li
tiene insieme: vale la pena scriverlo qui perché il prossimo menu lo pagherà uguale. E
quando la schermata dice «non succede niente», **una riga di registro** distingue in
trenta secondi «il comando non arriva» da «arriva e esce subito», che sono due difetti
in due file diversi.

### 7.58 I cinque pulsanti di navigazione erano caratteri

**Sintomo, e da dove viene.** L'autore ha guardato la barra del percorso e ha detto:
«non mi piacciono, non sono belli». Poi, più precisamente: «più *minimal*, non così
spesse, e devono stare sulla stessa riga dell'indirizzo».

Aveva ragione su tutte e tre le cose, e il perché è uno solo: quei cinque segni erano
i caratteri `←` `→` `↑` `↻` `★`, disegnati con `app.font`. Quattro famiglie di segni
diverse in una fila di cinque — frecce lineari sottili, un cerchio con la punta, una
stella piena — quindi quattro **spessori** e quattro **altezze ottiche** diverse, e il
disegno che cambia col carattere scelto (uno dei quali, fra l'altro, non ha tutte e
cinque le forme). È la stessa specie dei §7.49 e §7.50: un carattere non è un
elemento.

**Correzione.** Tre forme nuove nello shader SDF, in tutt'e due le lingue
(`src/shaders/ui.frag` e l'HLSL dentro `d3d11_renderer.cpp`):

- **freccia** — un'asta e una punta a 45°, tutte dello stesso spessore; le quattro
  direzioni si ottengono **ruotando il punto**, come già fa il triangolo, non
  scrivendo altre tre formule. Erano triangoli pieni nella prima versione, e a dodici
  pixel sembravano segnali stradali: la linea è più sobria e più leggibile;
- **aggiorna** — un anello aperto con la punta **piena** dentro il taglio. La punta a
  segmenti non si distingueva da un anello rotto, quindi lì il triangolo è quello
  giusto; è l'unica forma che ha una punta piena, e ha una ragione;
- **stella** — cinque punte, piene o di solo contorno. Lo stato «nei segnalibri» non è
  una seconda formula: è la stessa forma disegnata come **bordo**, che lo shader sa
  già fare per ogni forma.

Le bandiere nuove (64…1024) viaggiano negli stessi 34 float per vertice del batcher,
in tutt'e due i backend; lo spessore viaggia in `radius.x`, come per il `+` e la `x`.

**Le misure, che sono la metà del difetto.** L'icona sta in una scatola di **12 px**
dentro il pulsante da 26 (era 15, il 63% del pulsante: accanto a testo di 14 era la
cosa più forte della barra), con uno spessore di **1,2 px** (era 2). È la stessa
misura dei tre pulsanti della finestra, 12 su 46.

**L'allineamento, misurato e non guardato.** I pulsanti avevano un `+5` scritto a
mano e finivano **5 pixel più in alto** dell'indirizzo: misurato riga per riga su una
cattura (`analysis/tmp/nav-align.png`), icone 46–57 e testo 52–65. Adesso la riga la
decide il testo del percorso — `baseline + 2*scale + LineHeight*scale*0.5` — e i
pulsanti si centrano su quella: 52–61 contro 52–62.

**Tre trappole pagate qui, che valgono per ogni forma nuova.**

1. **Il verso dei vertici.** `quadDistance` prende le normali come `(dy, -dx)`, che
   sono quelle **uscenti** solo per un poligono in senso antiorario. L'aquilone della
   stella era scritto al contrario, quindi «dentro» era il **complemento**: la stella
   piena disegnava tutto tranne la stella, e il contorno mostrava una ragnatela. Il
   profilo della forma si controlla **fuori dall'applicazione**, con venti righe di
   Python che rifanno il campo e lo stampano a caratteri: dieci minuti contro dieci
   ricompilazioni.
2. **L'unione non è il minimo.** Per un'unione di forme, `min` è esatto **fuori** e
   sceglie il bordo più lontano **dentro**: il contorno della stella vuota mostrava
   righe che non esistono. Il segno lo dà l'unione (`dentro` se almeno un aquilone
   contiene il punto), la distanza la dà il minimo dei **dieci lati presi come
   segmenti**.
3. **Lo spazio di `point` ha gli assi girati.** `point = centro - frag`, quindi `+y`
   è **in su** e un angolo di −90° punta **in giù**: la stella usciva capovolta, con
   la punta in basso, e sembrava sbagliata invece che ruotata.

**Verificato a schermo**, sui due backend: `analysis/tmp/nav-11.png` e `nav-gl.png`
sono la stessa barra disegnata dal Direct3D 11 e dall'OpenGL; `nav-13.png` è la stella
piena nei segnalibri, con lo sfondo del pulsante tinto; `pin-sidebar-zoom.png` è la
stella **delle righe fissate nella barra laterale**, che era ancora il carattere `★`
ed è diventata la stessa forma (11 px, piena). Il contorno di attesa c'è: prima di
crederci, il profilo delle forme è stato stampato a caratteri.

**Cosa resta un carattere, e perché.** Il `>` fra le briciole del percorso e i
puntini di sospensione **sono testo**, non elementi: stanno dentro una riga di testo e
devono seguire il carattere scelto, come le virgolette. La distinzione non è
estetica: un elemento si allinea con gli altri elementi e ha uno stato, un segno di
punteggiatura no.

### 7.59 La caduta sul tasto Canc: la vista disegnata su un elenco che non c'era piu'

**Risolto il 2026-10-18** con AddressSanitizer. Trovato mentre si verificava lo
spezzamento dei comandi, e **non** portato da quello: cadeva anche l'eseguibile di
prima, 1 volta su 2, quindi il difetto c'era gia'.

**Sintomo.** Il processo moriva con un access violation al tasto `Canc`, in modo
intermittente. La sequenza che lo fa cadere, sulla sandbox del progetto:

1. doppio clic su una sottocartella;
2. clic su un file di **testo**, `F2`, rinominarlo, `Invio`;
3. `Ctrl+Shift+N` (cartella nuova), poi `Esc`;
4. clic su una riga, `Ctrl+C`, `Ctrl+V`;
5. `Canc`.

Il registro si fermava dopo l'incolla, senza una riga per l'eliminazione, che pure
**avveniva**: il file spariva dal disco.

**Causa, letta nella pila.** La build con ASan ha dato il referto esatto:

```
ERROR: AddressSanitizer: container-overflow ... READ of size 8
  #0 std::_String_val<...>::_Myptr
  #2 std::_Hash<std::_Uset_traits<std::wstring,...>>::count
  #3 Bolt::IsEntrySelected      src/view_list.cpp:956
  #4 Bolt::DrawList             src/view_list.cpp:1294
  #5 DrawFrame                  src/main.cpp:1712
  #6 RenderFrame                src/main.cpp:1787
  #7 wWinMain                   src/main.cpp:4316
  ... 0x123... is located 2320 bytes inside of 2952-byte region,
      allocated by Bolt::ReadDirectory   src/navigation.cpp:138
      ... from Bolt::ReloadKeeping ← FinishOperation ← wWinMain:4314
```

Il ciclo faceva, in quest'ordine: **ricostruiva la vista** (se `viewDirty`), poi
`FinishOperation`, poi `RenderFrame`. `FinishOperation` chiama `ReloadKeeping` →
`ReadDirectory`, che **sostituisce `entries`** e alza `viewDirty` — ma il disegno
avveniva lo stesso, con le `rows` costruite sull'elenco di **prima**: gli indici
dentro `entries` puntavano oltre la fine del vettore nuovo.

E' un `container-overflow`, non un use-after-free: la memoria era ancora allocata,
quindi in Release si leggeva spazzatura plausibile e la caduta usciva solo quando
quella spazzatura capitava su una pagina non mappata. Ecco perche' era intermittente —
e perche' il caso serviva un `Ctrl+C`/`Ctrl+V` prima: e' l'incolla che fa finire
un'operazione con l'elenco cambiato **mentre** la vista e' ancora quella di prima.

**Correzione.** La vista si ricostruisce in `RenderFrame`, subito prima di disegnare,
e non piu' nel ciclo dei messaggi: fra i due punti c'e' `FinishOperation`, che cambia
l'elenco. Resta nel ciclo solo il caso delle **statistiche**, che e' l'unico con un
tetto di tempo (un gruppo che dipende dai numeri si rifa' mentre i numeri arrivano, non
una volta per fotogramma). Sta in `RenderFrame` e non nel ciclo anche perche'
`RenderFrame` ha un secondo chiamante: l'evidenziazione della destinazione durante un
trascinamento OLE, che avrebbe lo stesso problema.

**I numeri.** Prima: **4 su 4** cadute con il banco che pilota la sequenza. Dopo: **4
su 4** vive. Con ASan: 3 su 3 referti `container-overflow` prima, 0 su 2 dopo (e le due
corse sono rimaste vive). L'eseguibile buono e' stato controllato con
`dumpbin /dependents`: non nomina `clang_rt`, e `build/asan` e' stato cancellato con
`CMakeLists.txt` rimesso com'era (`docs/05` §4).

**La lezione.** `viewDirty` e' una promessa che qualcuno deve **mantenere prima di
disegnare**, e il posto in cui la si manteneva era sbagliato di due chiamate. Un
`container-overflow` intermittente in Release e' quasi invisibile — niente dump, niente
voce nel registro di Windows nei primi tentativi, e il file che sparisce lo stesso — e
l'unico modo di vederlo e' lo strumento: ASan ha dato causa, riga e catena in una corsa
sola, dopo che tre tentativi di ragionamento sul codice non avevano prodotto niente.
Vale la regola di `AGENTS.md` §2: se il difetto e' visibile e i tuoi strumenti no, lo
strumento e' sbagliato.

---

### 7.60 L'argomento della riga di comando oltre i 260 caratteri: la cartella chiesta e quella aperta

**Risolto il 2026-10-18**, guardando l'opzione `LongPathsSupport` funzionare a metà.

**Sintomo.** `Bolt.exe "C:\...\una\cartella\molto\profonda"` con un percorso di **271**
caratteri non apriva quella cartella: l'applicazione partiva su **Questo PC**, senza dire
niente. Con `LongPathsSupport` accesa la cartella si apriva benissimo, ma solo navigandoci
dentro — l'argomento restava ignorato.

**Causa.** Due domande sullo stesso percorso, fatte in due momenti diversi e con due
regole diverse. L'argomento lo si validava **subito**, prima di `LoadSettings`, con
`GetFileAttributesW(path)` — che e' limitata a `MAX_PATH` e restituisce
`INVALID_FILE_ATTRIBUTES` oltre i 260 caratteri — e la validazione falliva prima che si
potesse sapere se l'opzione era accesa. Il prefisso `\?\` lo metteva solo l'enumerazione
(`ReadDirectory`), che arrivava dopo: due punti di I/O, una regola sola applicata a uno
dei due.

**Correzione.** L'argomento si **ricorda** e si valida dopo `LoadSettings`, con
`IOPath(path, app.longPaths)` — la stessa funzione che usa l'enumerazione. Il nome che si
apre resta quello **com'era scritto**: il prefisso serve a parlare con Win32, non a
cambiare il percorso che si mostra.

**Verificato a schermo.** Cartella di 271 caratteri creata nella sandbox
(`sandbox\cartella-lunga-01\…\cartella-lunga-11`, con `dentro.txt` in fondo).
Con l'opzione **spenta**: `Bolt.exe "<quel percorso>"` → Questo PC (`opt-long-off.png`).
Con l'opzione **accesa**: la cartella si apre, `dentro.txt` e' nella lista, e la briciola
del percorso si stringe da sola (`opt-long-on.png`). La cartella e' stata cancellata dopo
la prova.

**La lezione.** Un'opzione che si chiama «supporto ai percorsi lunghi» e che funziona su
un punto di I/O su tre e' peggio di un'opzione assente: sembra accesa e non lo e'. Quando
un comportamento si applica «a tutti i percorsi», i punti che leggono un percorso vanno
**elencati** — qui erano due, e ne era stato cambiato uno.

---

### 7.61 Il ritaglio era stato del disegno: cinque disegni per fotogramma, e un contatore che non li contava

**Risolto il 2026-10-20.** Non e' un difetto che si vede a schermo: e' un **debito**
che stava scritto in `AGENTS.md` §8 e in `docs/05` §2, voce 3 — «il posto giusto per un
ritaglio e' il quad, non la chiamata di disegno» — e che il 2026-10-20 e' stato saldato.

**Sintomo, misurato invece che dedotto.** Con i due ritagli attivi (lista e
visualizzatore di testo) il fotogramma faceva **cinque** disegni. Il numero non si
leggeva da nessuna parte: `BOLT_DEV_HUD=1` mostrava quadrilateri e non chiamate. Il
primo passo e' stato aggiungere il contatore (`IRenderer::DrawCalls`, che conta anche il
lotto in corso, perche' la barra di stato si disegna **dentro** l'ultimo lotto), e la
riga e' diventata `5 draws, 379 quads`.

**Causa.** Il ritaglio era uno **stato** — `glScissor` da una parte,
`D3D11_RASTERIZER_DESC::ScissorEnable` piu' `RSSetScissorRects` dall'altra — e uno stato
appartiene alla chiamata di disegno, non alla primitiva. Un batcher accumula tutto in un
lotto: per ritagliare una parte sola bisogna chiudere il lotto prima e riaprirlo dopo.
Due ritagli, quattro disegni in piu'.

**Correzione.** Il rettangolo e' un **campo del vertice**: 4 float in coda al record (34
per vertice invece di 30), `layout(location = 15) in vec4 aClip` nel GLSL e
`flat in vec4 vClip` nel frammento, che scarta i frammenti fuori — e la stessa cosa in
HLSL, dove il confronto si fa su `SV_Position`, gia' in pixel con l'origine in alto a
sinistra (e il rettangolo e' dichiarato `nointerpolation`, il `flat` dell'HLSL:
interpolare un valore identico nei quattro vertici aggiungerebbe un arrotondamento
proprio sul confronto che decide se un frammento esiste). `SetScissor`/`ClearScissor`
diventano `SetClip`/`ClearClip`: stesso significato per chi chiama, nessuno stato
grafico da spegnere. Spariscono il `glScissor`, lo `ScissorEnable` e i due
`RSSetScissorRects`. Il debito era dichiarato
come «renderebbe il ritaglio una proprieta' di chi disegna invece che dell'ordine in cui
si disegna», ed e' esattamente quello che e' successo: `main.cpp` e `view_inspector.cpp`
non chiamano piu' `End`/`Begin` attorno a un ritaglio.

**Il numero, dopo.** `1 draws, 1.565 quads` — la stessa schermata (visualizzatore di
testo aperto, filtro `lungo`, zoom 1,3), stesso backend Direct3D 11.

**Effetto collaterale onesto: il contatore dei quadrilateri mentiva.** `QuadCount`
riparte a ogni `Begin`, e con tre lotti per fotogramma mostrava **l'ultimo lotto**, non
il fotogramma: 379 su circa quattromila quadrilateri. Adesso che `Begin` e' uno solo il
numero e' vero (1.565). Era un difetto dello **strumento**, non del disegno, ed e' la
specie peggiore: il numero c'era, sembrava piccolo, e nessuno aveva motivo di
sospettarlo.

**Verificato a schermo, con un controllo che serviva davvero.** Due stati: la griglia di
`molte` scorsa a meta' cella, e il visualizzatore di testo scorso con lo zoom cambiato a
meta' (le due condizioni di §7.36 e §7.37).
- **Confronto col prima.** Ripetuta la stessa scena sul codice nuovo, l'area della lista
  e l'area del testo sono **identiche al pixel** alla cattura fatta col `glScissor`
  (`clip-before-icons.png` / `clip-after-icons.png`: 0 pixel diversi su 472.310;
  `clip-before-text.png` / `clip-after-text.png`: 0 su lista, testo e intestazione).
- **Controllo negativo.** Disattivato il taglio nel pixel shader, il difetto **torna**:
  `riga 005` disegnata sopra la testata `UTF-8 200 righe CRLF` e `riga 025` sotto il
  riquadro (`clip-controllo-senza-taglio.png`). E' la prova che a tagliare e' quella riga
  di shader, e non il caso.
- **Tutti e due i backend**: OpenGL (`BOLT_BACKEND=opengl`, `--- backend: OpenGL` nel
  registro) da' `1 draws, 1.565 quads` e le stesse righe tagliate; l'HLSL e' stato
  compilato prima con `fxc` (vs_5_0 e ps_5_0, senza aprire una finestra).

**La lezione, ed e' sul controllo.** Il primo controllo negativo era **parziale**: la
modifica che credevo di aver fatto spegneva solo il confronto orizzontale
(`false && (A) || B`) e lasciava in piedi quello verticale, quindi il difetto non
tornava e sembrava che il taglio fosse inerte. Un controllo che disattiva **meta'** della
condizione non e' un controllo: e' una conferma di comodo. Le parentesi, dopo una
sostituzione meccanica, si leggono — come si legge il diff, e come si legge il numero che
dice che il difetto **non** e' tornato.

---

### 7.62 Il menu contestuale offriva i comandi di File Pilot

**Trovato e corretto il 2026-10-20**, su richiesta dell'autore: «rimuovi dall'elenco che
esce dal tasto destro "*File pilot*" e i collegamenti che si riferiscono a file pilot».

**Sintomo.** Col tasto destro dentro Bolt comparivano, in mezzo ai verbi delle estensioni
installate, due voci che non sono di questa applicazione: **`Open in File Pilot`**
(`OpenInFilePilot`, su una selezione) e **`File Pilot here`** (`FilePilotHere`, sullo
sfondo della cartella). Sono i verbi dell'estensione di shell del prodotto originale —
quello di cui Bolt e' un clone.

**Perche' e' un difetto e non una funzione.** Le due voci non fanno niente che questo
programma non sappia fare: aprono la stessa cartella **in un altro programma**. In un
clone si leggono come una funzione nostra, e non lo sono: promettono un comportamento che
dipende da un'altra applicazione installata sulla macchina. Chi vuole File Pilot lo apre
da File Pilot, e il nostro menu resta il nostro. E' la stessa regola delle voci che
offriamo gia': non si offre quello che non si sa eseguire (`IsOwnShellVerb`).

**Correzione.** In `src/shell_menu.cpp`, dentro `ReadShellMenu` — cioe' nel punto in cui
il menu della shell diventa il nostro elenco, **un livello alla volta**, quindi vale anche
per i sottomenu che si aprono dopo. `IsOriginalAppVerb` confronta il **verbo canonico**
(`filepilot` dentro il nome), che e' il dato stabile: l'etichetta e' tradotta e cambia con
la lingua di Windows, mentre l'estensione porta il nome dell'applicazione in tutte le
varianti che registra. `LabelMentionsOriginalApp` guarda l'etichetta solo come **ripiego**,
per le voci ownerdraw che arrivano senza verbo. La voce saltata finisce nel registro
(`BOLT_DIAG=1`, `saltata (comando dell'applicazione originale)`) e si conta fra le
saltate, come le altre: un conto che non si vede e' un conto che non esiste.

**Verificato a schermo**, sui tre menu che il tasto destro apre:

| menu | prima | dopo |
|---|---|---|
| una cartella | `Open in File Pilot` fra `Open Git Bash here` e `Play with Ace Player` | assente; restano VLC, Ace Player, Visual Studio, Git, 7-Zip, Defender, Acrobat, Revo, `Copia come percorso`, `Crea collegamento` |
| lo sfondo della cartella | `File Pilot here` fra `Apri con Visual Studio` e `Open Git GUI here` | assente; restano `Apri nel Terminale`, `Apri con Code`, Git, `Proprietà`, e i nostri comandi |
| un file `.txt` | `Open in File Pilot` | assente |

Nel registro le due righe compaiono come `saltata (comando dell'applicazione originale)`.
Catture: `analysis/tmp/menu-shell.png` (prima), `menu-dopo.png` e `menu-sfondo-dopo.png`
(dopo).

**Cosa resta fuori, e perche'.** Due elenchi non si filtrano:

- **`Apri con >`** e' l'elenco dei **programmi installati** che sanno aprire quel file: se
  un giorno File Pilot ci compare, e' Windows che lo dice, e toglierlo sarebbe nascondere
  cosa c'e' sulla macchina. Il verbo e' `openas`, non un comando di File Pilot.
- **`Invia a >`** e' la cartella `SendTo` letta a ogni apertura (`src/shell_sendto.*`): un
  collegamento li' dentro e' un file di chi usa il programma, e un menu costruito su una
  cartella non nasconde il contenuto della cartella. Qui, misurato: **0** scorciatoie che
  nominino File Pilot in quella cartella, quindi oggi la domanda non si pone.

**La lezione.** «Non mostrare le voci di un altro programma» si fa guardando il **verbo**,
non l'etichetta: l'etichetta e' la cosa che si traduce, e la traduzione e' l'unica parte
che non si puo' prevedere. E si fa dove la shell diventa elenco — un punto solo — invece
che dove l'elenco si disegna, o i sottomenu se la scamperebbero.

---

### 7.63 L'eseguibile installato non trovava piu' gli shader: il percorso di compilazione dentro il pacchetto

**Trovato e corretto il 2026-10-20**, impacchettando la prima volta.

**Sintomo, che su questa macchina non c'e'.** L'eseguibile prodotto dal build contiene
una stringa assoluta — `C:/Users/.../Bolt/src/shaders` — e la usa per leggere `ui.vert`
e `ui.frag`. Qui funziona, perche' quella cartella esiste. Sul computer di chi installa
non esiste. Con Direct3D 11 non si nota niente, perche' gli shader HLSL sono scritti nel
codice; con `BOLT_BACKEND=opengl` il programma non parte e scrive `cannot read .../ui.vert`
in una finestra d'errore. Cioe': il difetto si sarebbe visto **solo** sul computer di chi
usa il programma, e solo sul backend di ripiego.

**Causa.** Due scelte giuste che insieme fanno un pacchetto sbagliato. `REA_SHADER_DIR` e'
una definizione di compilazione col percorso della **sorgente**: serve a modificare gli
shader a caldo mentre il programma e' aperto, ed e' una comodita' vera (`AGENTS.md` §4).
Ma il chiamante in `main.cpp` passava **quel** percorso a
`glRenderer.Initialize(REA_SHADER_DIR, &error)`, scavalcando la funzione dell'interfaccia
che poteva decidere altro. Il percorso utile a chi compila e' finito dentro l'artefatto
come se fosse un dato del prodotto.

**Correzione.** `ShaderDirectory()` in `ui_renderer.cpp` cerca **prima accanto
all'eseguibile** (la cartella dell'exe piu' `shaders`, e devono esserci tutti e due i
file) e solo se li' non ci sono ripiega su `REA_SHADER_DIR`. `main.cpp` chiama
`Initialize(&error)` e non nomina piu' nessun percorso. Il ripiego resta apposta: e' la
comodita' di chi lavora nel repository, e va tenuta — quello che non va tenuto e' il suo
**primato**. Una riga di registro dice quale cartella si sta leggendo
(`--- shaders: ...`), perche' un problema di shader su un'altra macchina deve sembrare un
problema di shader.

**Verificato con il registro, non a impressione.** Eseguibile copiato in una cartella con
i suoi `shaders/`: `--- shaders: ...\exe-esterno\shaders`. Eseguibile copiato **senza**
`shaders/`: `--- shaders: .../Bolt/src/shaders` (il ripiego). Pacchetto installato e
lanciato con l'OpenGL: `--- shaders: ...\install-prova\Bolt installato\shaders`, e la
finestra disegna (`analysis/tmp/setup-installato.png`).

**La lezione.** Un percorso che serve a chi compila non e' un percorso che serve a chi usa.
Quando un artefatto esce dal repository, i percorsi che porta dentro vanno guardati: `strings`
sull'eseguibile e' il comando piu' rapido, ed e' quello che ha trasformato "funziona" in
"funziona qui". Da qui in avanti il pacchetto si prova **da una cartella in cui la
sorgente non c'e'**, che e' l'unico modo di far parlare questo genere di difetto.

### 7.64 L'Accesso rapido apre le cartelle come posizioni codificate, e li' i comandi si rifiutano — *chiusa il 2026-10-21*

**Trovato il 2026-10-20**, verificando la Home: una riga della Home puo' essere una
cartella aperta prima, e aprendola si finisce in una posizione che il programma tratta
come spazio dei nomi.

**Sintomo, visto a schermo.** Una cartella aperta da `ACCESSO RAPIDO` (per esempio la
cartella di prova `sandbox`) si apre e mostra il contenuto giusto, ma:

- nell'ispettore la riga `Percorso` di una voce finisce in esadecimale
  (`...670069006e0069000000018000000`, cioe' la coda di un `shell:pidl:`), non in un
  percorso;
- il tasto destro sul fondo mostra `Incolla`, `Nuova cartella` e `Nuovo file`
  **spenti** (`analysis/tmp/home-39-qa-menu.png`), e `Rinomina`, `Elimina` e `Taglia`
  sarebbero spenti allo stesso modo su una voce scelta;
- le statistiche ricorsive non partono, e le colonne Cartelle/File/Dimensione restano
  `--`.

**Causa.** `ShellLocation::Read` decide se una voce e' apribile per nome con
`ILIsEqual(absolute, Parse(item.location))`. Per una voce **dentro** `ACCESSO RAPIDO`,
`absolute` e' un PIDL che ha per genitore il PIDL dell'Accesso rapido, mentre
`Parse(item.location)` e' un PIDL che parte dal desktop: sono due catene diverse per la
**stessa cartella**, e `ILIsEqual` risponde di no. Scatta allora il ripiego scritto per
il cestino — `EncodePidl`, perche' li' il nome assoluto punta davvero a un'altra voce —
e la posizione diventa `shell:pidl:...`. Da li' in poi `IsNamespace` e' vero, e
`RefuseInNamespace` rifiuta, giustamente, ogni comando per percorso.

Perche' non era stato corretto subito: tocca l'**identita'** di una voce dello spazio
dei nomi, e il cestino deve continuare a comportarsi come prima (li' il nome *non* e' la
voce). Servivano le sue prove, non un'aggiunta in coda alla Home.

**Correzione.** Fuori dal cestino c'e' una domanda migliore di `ILIsEqual`, che non
guarda le catene: `SHGetNameFromIDList(absolute, SIGDN_FILESYSPATH)` da' il percorso che
la shell associa a **quella voce**; se coincide col nome con cui la si e' trovata, aprire
quel nome apre questa voce. Dentro il cestino la domanda non vale — li' il nome e' il blob
dentro `$Recycle.Bin` e il PIDL e' l'identita' vera — quindi li' resta la regola di prima,
con `InsideRecycleBin` che chiede una volta sola il PIDL di `FOLDERID_RecycleBinFolder` e
lo confronta con `ILIsParent`. L'ispettore, di conseguenza, mostra le colonne della shell
solo dove la shell e' l'unica che sa (voci e cartelle riciclate): per una cartella normale
tornano le nostre righe — date nel nostro formato, dimensione allocata, attributi NTFS —
e la riga `Percorso` mostra un percorso invece di esadecimale. Le colonne della shell con
valore vuoto non si stampano piu'.

**Verificato il 2026-10-21** (`analysis/tmp/fix-*.png`):

- una cartella aperta da `ACCESSO RAPIDO`: titolo col percorso vero, ispettore con le
  **nostre** righe (`Tipo, Dimensione 36 B, Su disco 48 B, Cartelle 0, File 2, ...,
  Percorso ...analysis	mp\sandbox\documenti`), e il menu del fondo con `Incolla`,
  `Nuova cartella` e `Nuovo file` **accesi** (`fix-01`, `fix-02`);
- un'unita' aperta da "Questo PC" si apre col percorso (`C:\`), e `Questo PC` elenca le
  sue due voci come prima (`fix-10`);
- **cestino**, con un file della sandbox messo apposta (`zz-bolt-prova-cestino.txt`,
  filtro sul nome prima di guardare): l'ispettore mostra ancora le colonne della shell —
  `Percorso originale`, `Data di eliminazione`, `Attributi`, `Proprietario` — e il menu
  contestuale offre `Ripristina`; il ripristino ha rimesso il file nella sandbox
  (`fix-14`, `fix-15`). Il comportamento del cestino, insomma, non e' cambiato.

### 7.65 Le icone dei collegamenti, i nomi della Home e la casa che era una cartella

**Trovato e corretto il 2026-10-21**, guardando due catture mandate dall'autore: la Home
e un elenco di cartella con dentro dei `.lnk`.

**Tre sintomi, una sola famiglia di cause: chiedere alla shell la cosa sbagliata.**

1. **I `.lnk` avevano il documento generico**, non l'icona della destinazione con la
   freccetta. `IconFor` chiedeva l'icona per **estensione**: `GetFor(".lnk")` costruisce
   un nome finto (`x.lnk`) e lo passa con `SHGFI_USEFILEATTRIBUTES`, che non tocca il
   disco e quindi non puo' sapere ne' qual e' la destinazione ne' che e' un collegamento.
   La stessa icona sbagliata compariva anche nell'elenco `CONTENUTO` dell'ispettore.
2. **Nella Home le cartelle note si chiamavano in inglese** — `Documents`, `Music`,
   `Pictures`, `Videos` — mentre la barra laterale diceva `Documenti`, `Musica`... La
   Home costruiva il nome con `LeafName(path)`, cioe' il nome **sul disco**; la barra
   laterale usava il nome che da' la shell. Due nomi per la stessa cosa, e quello sbagliato
   era nella lista.
3. **La Home nella barra laterale e nel tab aveva l'icona di una cartella**, non la casa
   di Esplora risorse: la riga non dichiarava nessuna icona e il disegno ripiegava sulla
   cartella.

**Correzione.**

- `ShellIconAtlas::GetForPath` e' la domanda giusta: per un `.lnk` (o un `.url`) chiede
  l'icona del **file vero** con `SHGFI_LINKOVERLAY`, per tutto il resto resta l'icona
  dell'estensione, che non tocca il disco. La usano la lista, l'ispettore e — con la
  stessa risposta — il contenuto della cartella. La chiave della cache distingue le due
  domande (`lnk:` contro `obj:`), o la stessa cella si riuserebbe per due icone diverse.
- `FriendlyName(path)` in `app_ui.*` e' il nome con cui **Windows** chiama un percorso
  (`SHGetNameFromIDList`, `SIGDN_NORMALDISPLAY`), con una memoria per non interrogare la
  shell a ogni fotogramma. La usano la Home, le righe della barra laterale e il titolo
  del tab: `Documenti` dappertutto.
- `kHomeIcon` in `home.h` e' il CLSID della cartella **Home** di Windows 11
  (`{F874310E-B6B7-47DC-BC84-B9E6B38F5903}`), verificato chiedendo alla shell il nome di
  quella posizione (`Self.Name` = "Home"). La casa compare nella barra laterale e nel tab.

**Verificato a schermo** (`analysis/tmp/icone-*.png`): `zz-prova-link.lnk` nella sandbox
disegnato come **cartella con la freccetta blu** (ingrandito 10x: `icone-02-lnk-zoom.png`);
la Home con `Documenti`, `Immagini`, `Musica`, `Video` e `Gianmarco Tafuro` nella lista,
e la **casa** nel tab e nella barra laterale (`icone-03-home.png`); la stessa icona del
collegamento anche nell'elenco `CONTENUTO` dell'ispettore. Il collegamento di prova e'
stato cancellato dopo la verifica.

**Limite dichiarato.** L'atlante delle icone non ha sfratto, e un `.lnk` costa una cella
per collegamento (l'icona dipende dal file, non dall'estensione). Una cartella con
centinaia di collegamenti puo' riempirlo; oltre il pieno l'atlante restituisce l'ultima
icona risolta, che e' il ripiego gia' previsto. Con i pochi collegamenti delle prove non
si e' visto, e non si dichiara risolto.

### 7.66 Il conto dei byte seguiva il tempo della shell — *chiusa il 2026-10-21*

**Sintomo.** Il pannello dell'operazione dichiarava byte che non erano quelli scritti.
Con una cartella di un file da 4 MB e 400 file da 10 byte — il 99,9% dei byte sta nel
file grande — nel momento in cui il file grande era finito il conto diceva **23%**: la
barra era a un quinto mentre il disco aveva gia' scritto quasi tutto.

**Causa.** Un denominatore solo, moltiplicato per la frazione dei **punti** che la shell
riporta in `UpdateProgress`: `doneBytes = totalBytes × workSoFar / workTotal`. I punti
sono proporzionali al **tempo**, non ai byte — un file da 23 byte sono 2 punti, uno da
4 GB sono 131074 — quindi il numero usciva giusto per un file solo e sbagliato appena le
voci avevano dimensioni diverse. Era il debito dichiarato nella voce 4 di `docs/05` §2.

**Correzione.** Il conto ha due pezzi (`src/file_ops.cpp`, `JobState::doneBase` e
`itemBytes`):

- i byte delle voci **finite** sono misurati uno per uno, con `PKEY_Size` chiesto alla
  shell nel `Pre*` della voce — l'unico momento in cui la si ha in mano prima che venga
  toccata — e sommati al `Post*`;
- dentro la voce in corso si interpola con la frazione dei punti, ma **confinata** fra i
  byte gia' finiti e la fine della voce: fuori da quell'intervallo direbbe una cosa falsa;
- il conto non torna mai indietro, e una cartella (di cui `PKEY_Size` non dichiara la
  dimensione) non aggiunge byte inventati: li' la frazione dei punti resta l'unica cosa.

**Verificato**, e con lo strumento accanto perche' la differenza non si vede a occhio su
un fotogramma: il registro scrive **un decile di byte veri** con accanto quello che
diceva il conto vecchio (`BOLT_DIAG=1`). Nella sandbox, cartella mista: *90% dei byte
misurati, 23% secondo la frazione dei punti*. Controllo negativo: un file solo da 2 GB
— dove i due conti devono coincidere perche' non c'e' nessun'altra voce a falsare la
media — *100% dei byte misurati, 99% secondo la frazione dei punti*, e il pannello
catturato a meta' copia dice `1,9 GB / 2,0 GB (96%)`. Nei due giri la somma dei byte
misurati e' arrivata **esatta** al totale (4.004.000 e 2.147.483.648), cosa che la
frazione dei punti non garantiva.

**Una cosa provata e lasciata stare, con la ragione.** Il primo tentativo e' stato una
**coda** delle operazioni — la prima meta' della stessa voce di debito. E' stata
scritta e poi tolta: il pannello dell'operazione e' **modale** (`WM_LBUTTONDOWN`,
`WM_KEYDOWN` e i caratteri si fermano li'), quindi una seconda operazione sarebbe
arrivata solo nei primi 0,35 s, prima che il pannello compaia. Una coda che nessun gesto
puo' riempire e' peggio di un rifiuto: il rifiuto almeno e' vero. La coda chiede prima
una decisione sul pannello (modale come Esplora risorse, o finestra che lascia lavorare),
ed e' scritto in `docs/05` §3.

**Strumento che ne e' uscito.** Quando un passo delle operazioni fallisce, il registro
adesso **lo nomina**: `operazione, passo destinazione: 0x80070057`. Prima un HRESULT
senza nome ha fatto cercare il guasto nel posto sbagliato — era il percorso passato da
riga di comando con le barre in avanti, che `SHCreateItemFromParsingName` rifiuta.

### 7.67 La riga spenta esisteva solo nel pannello delle opzioni — *chiusa il 2026-10-21*

**Sintomo.** Il menu delle destinazioni del fissaggio scriveva la ragione **dentro il
nome** — `Segnalibri (non e' fissata qui)` — invece di mostrare la riga spenta. Le due
destinazioni si sceglievano lo stesso e l'unica cosa che succedeva era che il fissaggio
non cambiava niente: una voce che promette e non fa.

**Causa.** `GroupMenuItem::enabled` lo leggeva solo il ramo `richRows`, cioe' il pannello
delle opzioni. In un menu normale la riga spenta si disegnava come le altre, il cursore
ci si fermava, il clic la sceglieva e `Invio` la eseguiva. Era la voce 5 di `docs/05` §2,
con la nota che il secondo utilizzatore l'avrebbe pagata: il fissaggio ci era arrivato
vicino e aveva aggirato il problema nel modo peggiore, cioe' nelle etichette.

**Correzione.** Una regola sola nel componente (`src/view_menu.*`,
`GroupMenuItemEnabled` / `GroupMenuSelectedEnabled`), letta da tutte e tre le vie
d'ingresso: il cursore **non si posa** su una voce spenta (ne' con le frecce, ne' con
`Home`/`Fine`, ne' all'apertura, che cerca la prima accesa), il clic non la sceglie e
non sposta il cursore, `Invio` non la esegue, e il nome si spegne come nel pannello
delle opzioni (picco di luminosita' 234 → 106). `BuildPinMenu` non ha piu' bisogno della
parentesi: la ragione la dice il titolo del pannello, `FISSA IN` contro `TOGLI DA`.

**Verificato a schermo** (`analysis/tmp/pin-*.png`): le due righe accese prima di
fissare; dopo il fissaggio `Accesso rapido` spenta e il cursore posato su `Segnalibri`;
clic sulla riga spenta che lascia il menu aperto e **non sposta** il cursore (misurato
sullo sfondo della riga, che resta senza accento); `Giu'` che salta la riga spenta; il
verso opposto (`TOGLI DA`: 234 contro 106) e il toglimento che funziona davvero —
`Tolta da Accesso rapido` nella barra di stato e la riga sparita dalla barra laterale.
Controllo negativo sul pannello delle opzioni, che le righe spente le aveva gia': il
cursore da `Un clic apre` salta `Un clic: solo le cartelle` (spenta) e si posa su
`Doppio clic sul fondo`, con l'accento misurato sulla terza riga.

### 7.68 Nella barra laterale un recente si chiamava con il suo percorso — *chiusa il 2026-10-21*

**Sintomo**, segnalato dall'autore: nella sezione `RECENTI` una riga si leggeva
`C:/Users/Gianmarco/De…` — cioe' un **percorso** al posto di un nome, tagliato dalla
larghezza della riga. Le altre righe erano nomi.

**Causa**, ed erano due guasti in fila per lo stesso carattere. Un percorso fatto
arrivare **con le barre in avanti** (`C:/Users/…/sandbox/conto`, come si scrive in una
shell Unix o in un comando lanciato da li') veniva tenuto com'e':

1. `LeafName` cercava solo la barra **rovesciata**, quindi non trovava nessun nome e
   restituiva il percorso intero. E' la funzione che fa da ripiego quando la shell non
   sa dire un nome, quindi la riga della barra laterale mostrava il percorso;
2. `SHCreateItemFromParsingName` su quel percorso risponde `E_INVALIDARG`, quindi tutte
   le operazioni su file che ci lavoravano dentro fallivano con **«Parametro non
   corretto»** — un messaggio che non nomina il colpevole e manda a cercare il guasto
   nel `Job`. E' successo davvero, il 2026-10-21, ed era stato attribuito al banco di
   prova invece che all'applicazione (`docs/05` §5: «un valore incerto si logga»).

**Correzione.** `NormalizePath` in `src/app_ui.*`: la barra in avanti diventa barra
rovesciata **all'ingresso**, nei due punti in cui un percorso arriva da fuori — la riga
di comando (`wWinMain`) e `startupFolder` letto da `settings.ini`, che e' testo che si
puo' scrivere a mano. Le posizioni che non sono percorsi si lasciano stare: `shell:pidl:`
porta un PIDL codificato e li' la barra e' un carattere come un altro. Come ultima
difesa, `LeafName` e `ParentOf` accettano **tutti e due** i separatori: un nome non deve
dipendere da quale barra ha usato chi ha scritto il percorso.

**Verificato a schermo e contro disco** (`analysis/tmp/slash-*.png`):

- avvio con `Bolt.exe C:/…/sandbox/conto`: il percorso nella barra diventa
  `… > sandbox > conto`, il titolo del tab e' `conto`, e navigando dentro `dest` la
  sezione `RECENTI` mostra **`conto`**, non il percorso (`slash-02-recenti.png`);
- lo stesso avvio, **copia e incolla**: `uno.txt` copiato e incollato in `dest` riesce —
  `operazione: 6 byte misurati, 1 voci, 2 punti … 6 byte contati`, e il file c'e'. Prima
  della correzione la stessa sequenza si fermava su `passo destinazione: 0x80070057`
  (§7.66), cioe' sulla creazione dell'oggetto della shell per la cartella di
  destinazione;
- `startupFolder=C:/…/sandbox/conto` scritto a mano nel file delle preferenze: all'avvio
  la cartella e' quella giusta e il nome e' `conto`.

### 7.69 Il pannello dell'operazione era un muro, e la seconda operazione si perdeva — *chiusa il 2026-10-21*

**Sintomo.** Durante una copia lunga la finestra era **inutilizzabile**: un clic nella
lista non selezionava niente, una freccia non muoveva il cursore, una lettera non
arrivava nel filtro. E una seconda operazione — un incolla, una caduta — veniva
**rifiutata** con «C'e' gia' un'operazione in corso»: chi aveva lasciato andare il tasto
su una caduta leggeva dalla destinazione se era andata, e leggeva il nulla.

**Causa, ed erano due.** Il pannello era **modale** (`WM_LBUTTONDOWN`, `WM_KEYDOWN` e i
caratteri si fermavano li'), scelta di M7 ragionevole finche' una seconda operazione era
impossibile; e c'era **un solo posto** per l'operazione in corso (`app.operation`), quindi
la seconda non aveva dove stare. Le due cose si tenevano: senza una coda, lasciar passare
i gesti avrebbe solo cambiato il messaggio del rifiuto.

**La decisione**, presa qui perche' era l'unica cosa che mancava: **il pannello non e' piu'
modale**. Resta disegnato sopra tutto e prende solo quello che gli appartiene:

- un clic **dentro** il pannello non fa niente (`Annulla` annulla); un clic fuori si
  comporta come sempre — selezionare, navigare, copiare, trascinare;
- la rotella sopra il pannello non scorre l'elenco che gli sta dietro, fuori da lui si';
- `Esc` annulla; ogni altro tasto va dove andrebbe sempre;
- e le operazioni che arrivano mentre una gira vanno **in coda**.

**La coda.** `FileOps::Request` (azione, sorgenti, destinazione) e' una richiesta
**prima** di essere un `Job`: cosi' una richiesta che aspetta non avvia un thread che
misura byte per un'operazione che forse non partira'. `app.operationQueue` le tiene in
ordine di arrivo; `BeginOperation` accoda e restituisce **vero** (una caduta accettata
dichiara `DROPEFFECT_COPY`/`MOVE`, non piu' `NONE`); `StartNextOperation` fa partire la
successiva quando la precedente ha finito, ed e' chiamata da `FinishOperation` e non dai
suoi singoli esiti, cosi' non si puo' dimenticare in un ramo. Il pannello dice quante
aspettano (`— 1 elemento in coda`) e lo dice anche la barra di stato.

**Due cose che la finestra usabile ha reso necessarie.**

1. `app.operationOrigin`: quando l'operazione finisce, la vista si rifa' **solo se** la
   cartella sullo schermo e' ancora quella da cui si e' partiti. Navigare durante una
   copia e vedersi ricaricare l'elenco nuovo con la selezione di un'altra cartella era
   il primo effetto collaterale.
2. L'annullamento butta via anche la coda, e lo dice: `Esc` e il pulsante passano dalla
   stessa funzione (`CancelOperation`), e l'esito legge «Operazione annullata — 1
   elemento in coda annullato». Chi ferma una copia non si aspetta che ne parta
   un'altra appena quella e' morta.

**Verificato a schermo** (`analysis/tmp/q-*.png`), in sandbox con una cartella di 3000
file (una copia da 6 GB finiva in un secondo, e non lasciava il tempo di fare niente):

- copia in corso, poi clic su una riga: la selezione cambia e l'ispettore la segue
  (`q-95.png`), mentre il pannello segna `1858 di 3001 voci`;
- navigazione **dentro** un'altra cartella durante la copia: `dest`, 0 voci, pannello
  ancora a `21,5 KB / 43,9 KB (46%)` (`q-a0.png`), e alla fine la vista resta `dest`,
  senza rimbalzi (`q-a1.png`);
- seconda copia durante la prima: «1 elemento in coda — parte quando finisce quella in
  corso» nella barra di stato e `— 1 elemento in coda` nel pannello (`q-95.png`); alla
  fine della prima la seconda parte da sola e il file arriva;
- clic **dentro** il pannello: non succede niente, la selezione resta quella di prima;
- `Esc` e il pulsante `Annulla`: l'operazione si ferma e la richiesta in coda **non
  parte** (`secondo - Copia.txt` non c'e'), con il conto nell'esito;
- la selezione resta dov'era per tutta la copia — che ha richiesto la correzione di
  §7.70.

### 7.70 La selezione tornava alla prima riga a ogni cambiamento della cartella — *chiusa il 2026-10-21*

**Sintomo.** Con una cartella selezionata, un file creato da fuori (o dalla copia in
corso) faceva saltare cursore e selezione sulla **prima** riga. Si vedeva poco finche'
la finestra era inutilizzabile durante le operazioni; con il pannello non piu' modale
(§7.69) diventava il difetto principale: si lavora **durante** una copia, e quello che si
era scelto sparisce.

**Causa.** `ReloadKeeping(app, app.selection, app.cursor, scroll)` riceveva **per
riferimento** i due campi che `LoadDirectory` azzera (`selection.clear()`,
`cursor.clear()`, e poi il cursore si posa sulla prima voce). Dentro la funzione, `keep`
e `preferCursor` erano quindi il vuoto appena fatto: nessun nome sopravviveva, nessun
cursore da ritrovare, e la regola di ripiego sceglieva la prima riga. Silenzioso, perche'
il risultato e' sempre un elenco valido — solo non quello che si stava guardando.

**Correzione.** Le due cose si **copiano** all'inizio di `ReloadKeeping`, prima di
ricaricare (`keepNames`, `prefer`). Nessun chiamante cambia: `FinishOperation` passava
gia' un insieme costruito da lui, ed e' per questo che il difetto non si vedeva quando
l'operazione finiva.

**Verificato**: con `payload` selezionata, un file creato dalla riga di comando mentre
la finestra guarda la cartella — la selezione resta su `payload` (`q-50.png` prima,
`q-51.png` dopo, la riga col fondo d'accento e' la stessa); e durante la copia di §7.69
la selezione non si e' mossa per tutta la durata.

### 7.71 Il pannello diceva «3001 di 3000 voci» — *chiusa il 2026-10-21*

**Sintomo.** Copiando una cartella con 3000 file il pannello finiva su «3001 di 3000
voci», e per una cartella con un file dentro su «2 di 1». Due conti che non tornavano.

**Causa.** `Measure` contava **il contenuto** delle sorgenti e non le sorgenti stesse,
mentre la shell manda un `Post*` anche per la cartella che copia: una voce in piu' del
nostro denominatore, sempre.

**Correzione.** Le sorgenti che sono cartelle contano **una volta ciascuna** nella misura
(un file di partenza lo contava gia' il ciclo, e il suo `Post*` e' uno solo).

**Verificato** nel registro diagnostico della stessa copia: `3001 voci` misurate e
`3001 voci fatte` (prima: `3001` contro `3002`), e a schermo `1468 di 3001 voci` durante
e nessun numero impossibile alla fine.

### 7.72 La freccia dell'ordine spariva nelle colonne strette — *chiusa il 2026-10-21*

**Sintomo.** Cliccando su un'intestazione stretta — `Data c…`, `Cart…` — l'elenco si
ordinava ma la colonna non diceva **in che verso**: la freccia `^`/`v` non c'era.

**Causa.** L'etichetta si costruiva con il nome **e** la freccia, e poi `FitText` la
accorciava: la freccia stava in fondo, quindi era la prima cosa che il taglio mangiava.
Su una colonna larga non si vedeva, e con le colonne strette del layout a nove succedeva
su tre intestazioni.

**Correzione.** La freccia si aggiunge **dopo** aver accorciato il nome, e lo spazio che
le serve (`Measure`) si toglie prima dal tetto: il nome si accorcia di quel tanto in piu',
e il verso si legge sempre.

**Verificato a schermo**: con la colonna `Data creazione` ordinata e larga 130 px, il
titolo dice `Data c… ^` invece di `Data cre…` senza freccia (`col-07-head.png`).

### 7.73 Le righe della Home avevano l'icona di una cartella, la barra laterale quella giusta — *chiusa il 2026-10-21*

**Sintomo**, segnalato dall'autore con una cattura: nella Home, `Desktop`, `Documenti`,
`Download`, `Immagini`, `Musica`, `Video` e la cartella personale si disegnavano con la
**cartella gialla generica**, mentre la barra laterale mostrava per gli stessi posti le
loro icone (lo schermo blu di Desktop, il foglio di Documenti, la freccia verde di
Download). Due icone per lo stesso posto, a venti pixel di distanza.

**Causa.** Due domande diverse alla shell per la stessa cosa:

- la barra laterale chiedeva l'icona all'**oggetto** (`SHCreateItemFromParsingName` +
  `SHGFI_PIDL`, cioe' `GetForObject`), che e' l'unica che sa che
  `C:\Users\x\Documents` **e'** "Documenti" e non una cartella qualsiasi;
- la lista chiedeva l'icona al **percorso per attributi** (`GetForPath`), che non tocca
  il disco ed e' la scelta giusta per una lista di migliaia di file — ma una cartella
  nota, per lui, e' una cartella.

Nessuna delle due e' sbagliata: e' sbagliato usare la stessa per tutti. `IconFor` chiede
l'oggetto alle voci che portano un'**identita'** della shell (le unita', le posizioni di
spazio dei nomi), e per attributi a tutto il resto.

**Correzione.** `Entry::band` — che esiste gia' per dire a quale banda della Home
appartiene una riga, `-1` per tutto il resto — e' il segnale che serve: `IconFor` chiede
`GetForObject` per una riga della Home, e solo per quelle. Sono poche righe, e l'atlante
tiene la risposta: si paga la prima volta, non a ogni fotogramma.

**Una strada provata e scartata, con la ragione.** Il primo tentativo e' stato dare alla
voce una "chiave d'icona" trovata con `IKnownFolderManager::FindFolderFromIDList`, per
mappare un percorso alla sua cartella nota. Su questa macchina risponde "non trovata" per
tutte e sette — e le cartelle di Windows 11 sono reindirizzate dove il manager non
guarda. E' stato scritto, misurato con una riga di registro, e tolto: la strada buona era
un'altra domanda alla shell, non una mappa costruita a mano.

**Verificato a schermo** (`analysis/tmp/home-ic-list4.png` e `home-ic-list10.png`,
ingrandite sul pixel): sulla Home le righe di `Cartelle note` e `Recenti` hanno ora le
stesse icone della barra laterale — Desktop, Documenti, Download, Immagini, Musica,
Video, Cestino, Questo PC, le unita' — e una cartella fissata (`sandbox`) resta con
l'icona di una cartella, che e' quella giusta per lei.

### 7.74 La Home senza date e senza numeri, e «Data modifica» sparita da tutte le liste — *chiusa il 2026-10-21*

**Due difetti, un sintomo solo**, segnalato dall'autore: «la home è rotta, non vedo valori
di data modifica, totale, dimensioni ecc».

1. **«Data modifica» era vuota in tutte le liste**, non solo nella Home, e da una
   release sola: la 0.1.7, quella delle due colonne spente. Ogni cella prende la sua
   larghezza dal bordo della colonna **successiva** (`columnX[i + 1]`), e una colonna
   nascosta non viene disegnata — quindi la sua posizione **non si calcola** e resta
   zero. La larghezza di «Data modifica» diventava `0 - x - 12`, cioe' negativa, e
   `FitText` con una larghezza negativa restituisce la stringa vuota: la colonna c'era,
   l'intestazione c'era, e sotto non c'era niente. Il `--` delle colonne dei numeri si
   salvava perche' quelle usano il bordo destro dell'intestazione.
2. **La Home non chiedeva niente.** `MakeEntry` riempiva nome, tipo e banda e lasciava
   `modified`, `created`, `accessed` e `bytes` a zero; e la lista chiedeva le statistiche
   ricorsive solo alle voci con `location` **vuoto**, che per una riga della Home non e'
   mai vero (il percorso c'e', ed e' quello che serve a navigare). Risultato: date vuote
   e quattro colonne di numeri a `--`.

**Correzione.** `rightOf(column)` in `src/view_list.cpp`: il bordo destro di una cella e'
quello della prossima colonna **visibile** (`NextVisibleColumn`), o la fine
dell'intestazione. Una riga sola, letta da tutte le colonne; con tutte le colonne accese
da' esattamente i bordi di prima.

E in `src/home.cpp`: le date e la dimensione si chiedono al percorso con
`GetFileAttributesExW`, come le chiede una cartella qualunque, e la lista chiede le
statistiche anche alle righe della Home (`entry.band >= 0`) — tranne dove non c'e' un
albero: `Questo PC` e il cestino restano `--`, che li' e' la verita'.

**Il costo**, dichiarato: aprire la Home adesso avvia il conteggio delle cartelle che
mostra — la cartella personale sono decine di GB. Il servizio e' quello di sempre, con il
suo budget di tempo e il suo annullamento, e i numeri si riempiono progressivamente
(`HUD: 2 jobs`); ma e' lavoro che prima non si faceva, e ora si fa perche' le colonne lo
chiedono.

**Verificato a schermo**: la Home con `21/10/2026`, `08/10/2026`, `13/10/2026`… e i numeri
(`61.9K/369.6K/431.6K/22.6 GB` per Desktop, `55.2 GB` per la cartella personale),
`Cestino` e `Questo PC` a `--` (`home-fix-04.png`); accendendo `Data creazione` compare
anche quella (`29/09/2021`, `home-fix-05-head.png`); e una cartella normale
(`sandbox`) con date e numeri come prima (`home-fix-07.png`), che era il controllo che
la correzione non spostasse niente.

**La lezione.** La verifica della 0.1.7 aveva guardato **le intestazioni** — che colonne
ci sono, che la freccia si veda — e non i **valori sotto**. Le catture di quella release
erano ritagli della riga dei titoli: un difetto che svuota le celle non ci poteva
comparire. `AGENTS.md` §2 dice che si legge il pixel; qui mancava la riga da leggere.

### 7.75 Chiudere Bolt ci metteva venti secondi, e l'aggiornamento in app si fermava — *chiusa il 2026-10-21*

**Sintomo**, trovato provando a installare la 0.1.10 dall'app invece che a mano: il
comando `Controlla aggiornamenti` scaricava il setup, verificava l'impronta, lo avviava —
e non succedeva niente. La finestra si chiudeva, «App e funzionalita'» restava alla
versione vecchia, l'eseguibile non veniva sostituito. Nessun errore da nessuna parte.

**Causa, in due pezzi.** Il primo e' che `DirStatsService::Stop` **aspettava che la coda
si svuotasse**: `WorkerLoop` usciva solo con «stopping e niente in coda», e ogni lavoro
interrotto a meta' si rimetteva in fondo alla coda. Con la Home che adesso chiede le
statistiche (§7.74) — cioe' il conteggio della cartella personale, decine di GB —
chiudere l'applicazione voleva dire **finire quel conteggio**: misurati **20 secondi**.
Il secondo e' che il setup di Inno, in silenzioso, non aspetta un processo che sta
ancora usando il proprio eseguibile: rinuncia e esce senza dire niente, perche' i
messaggi sono soppressi. Le due cose insieme: l'aggiornamento «partiva» e non installava.

**Correzione.** Chiudendo, la coda si **abbandona**: i due `WorkerLoop` escono appena
vedono `stopping`, senza guardare cosa resta, e `Stop` svuota le code di ogni corsia
sotto il proprio lucchetto prima di svegliare i thread. E' quello che serve a un
programma che si chiude: il lavoro a meta' non interessa piu' a nessuno.

E, per non dipendere dalla velocita' dell'uscita nemmeno in futuro: il setup non si
avvia piu' mentre la finestra sta chiudendo, ma **due secondi dopo** — una riga di
`cmd.exe` che aspetta e poi lancia l'installer (`ping -n 3`). Cosi' anche un'uscita lenta
non puo' piu' far rinunciare l'installer.

**Verificato**: la stessa chiusura, con le statistiche in corso sulla Home, passa da
**20 s a 0,2 s** (misurata con `CloseMainWindow` e un'attesa sul processo). E
l'aggiornamento in app e' stato fatto **davvero**, dalla 0.1.11 alla 0.1.12: download,
impronta, setup, «App e funzionalita'» che dice `0.1.12`, eseguibile installato
**identico** a quello compilato (stesso SHA-256) e, rilanciato, «Bolt e' aggiornato
(0.1.12)». La stessa prova con la 0.1.10 — che la correzione non l'aveva — si fermava
come prima: e' cosi' che si e' saputo che il difetto era la lentezza dell'uscita e non il
setup, che da solo installava benissimo.

**La lezione.** Il difetto e' arrivato da una funzione aggiunta il giorno stesso (§7.74) e
si e' visto solo perche' una funzione di *un'altra* parte del programma — l'aggiornamento
— dipendeva dal tempo di chiusura. Un numero che nessuno aveva misurato: «chiudere
un'applicazione e' immediato» era un'ipotesi, non un fatto.

### 7.76 Nella griglia tutte le cartelle avevano l'icona del cestino — *chiusa il 2026-10-21*

**Sintomo.** Mettendo la Home a griglia (come chiedeva l'autore) le nove destinazioni
avevano **la stessa icona**: quella del cestino, che era la prima cella.

**Causa.** La chiave della cache delle icone **grandi** era il *tipo*, non la voce:
`icon:<dir>` per ogni cartella, `icon:.ext` per i file. Per i file e' la scelta giusta —
migliaia di file, poche estensioni — ma per una cartella il tipo non dice niente:
"Questo PC", il cestino e una cartella qualunque sono tutte `<dir>`, quindi la prima
icona risolta veniva restituita per tutte. Non si vedeva nella vista a dettagli perche'
li' le icone vengono dall'atlante, con la chiave del percorso.

**Correzione.** Per una cartella la chiave e' `icon:<dir>:<percorso>`. Le cartelle che
una vista mostra sono quelle sullo schermo, quindi la cache resta piccola — e l'icona
grande di una cartella adesso e' quella che la shell da' a **quel** percorso.

**Verificato a schermo** (`analysis/tmp/homegrid-02.png`): le nove destinazioni della Home
a griglia con le loro icone — cestino, schermo, foglio, freccia, cartella, fotografia,
nota, computer, pellicola — e la stessa griglia ritrovata tornando sulla Home dopo essere
passati da una cartella normale.
