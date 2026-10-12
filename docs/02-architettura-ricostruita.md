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
2. per ogni elemento della UI: appendi 4 vertici da 30 float
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

Documentati perché il metodo conta quanto il risultato: tutti e tre sono stati
trovati **guardando uno screenshot della propria applicazione**, non leggendo il
codice. Il ciclo usato è in `tools/capture-window.ps1`.

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

Ora c'è **un secondo corpo, rasterizzato a 13 px**, su un quarto layer
dell'array di texture, disegnato a scala 1.0. Il testo secondario ha una
dimensione sola e quella dimensione è rasterizzata invece che ricampionata.

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
