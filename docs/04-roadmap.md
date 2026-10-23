# Roadmap

Sequenza di lavoro, ordinata in modo che ogni milestone produca qualcosa di
funzionante e verificabile. Le stime sono per lavoro part-time.

Stato corrente: **M1, M2, M2b, M3, M3b, M4, M5, M5b, M5c, M5d, M8, M9 e M10
completate**; M6 e M7 parziali; M2 con un limite dichiarato. Questo riassunto invecchia a ogni
milestone: la tabella viva è in `docs/05-stato-e-prossimi-passi.md` §1, che si
aggiorna a ogni pausa lunga.

---

## M1 — Shell e architettura di rendering ✅

Obiettivo: dimostrare end-to-end che l'architettura ricostruita funziona.

- [x] finestra Win32, DPI per-monitor
- [x] contesto OpenGL 3.3 core con fallback
- [x] loader runtime delle entry point (nessuna dipendenza esterna)
- [x] quad batcher: 4 vertici da 34 float per quad, indice condiviso, una draw call
- [x] shader SDF originali: riempimenti arrotondati, bordi, ombre, dash, gradienti
- [x] texture array per le icone, con maschera monocromatica
- [x] atlas di glifi via GDI
- [x] enumerazione directory reale
- [x] shell a tre regioni con scorrimento, selezione, zoom

**Verifica:** l'eseguibile si avvia, le shader compilano, la UI disegna ed è
navigabile. Il numero di quad al frame è visibile nella barra di stato.

---

## M2 — Icone e anteprime reali 🟡

L'atlas delle icone di sistema e quello delle anteprime sono fatti; resta solo il
percorso monocromatico.

- [x] `SHGetFileInfoW` con `SHGFI_USEFILEATTRIBUTES`: icone per estensione e per
      attributi, senza toccare il disco
- [x] cache per chiave (`<dir>`, `<file>`, `.estensione`) in una griglia
      dell'atlas
- [x] conversione da HICON a RGBA, con alpha premoltiplicato rimosso e fallback
      alla maschera AND per le icone senza canale alpha
- [x] `IShellItemImageFactory` per anteprime su misura: `SIIGBF_THUMBNAILONLY`,
      così un tipo senza provider fallisce e ricade sull'icona invece di
      restituire l'icona stessa spacciata per anteprima
- [x] dimensione dell'atlas limitata da `AssetCacheSize` ed eventuale sfratto:
      atlas di 64 celle da 128 px e politica least-recently-used con riuso della
      cella
- [x] **ogni oggetto con la sua icona**: `GetForObject` chiede alla shell l'icona di
      una voce — il disco per un'unita', il computer per `Questo PC`, il cestino per
      il cestino, le cartelle di sistema per `Documenti`/`Immagini`/`Musica`/…
- [x] **l'icona del file è quella dell'applicazione che lo apre**: si risolve il
      ProgID e si estrae da dove la shell dice, così un `.csv` ha l'icona di Excel e
      non il documento generico
- [ ] resa monocromatica delle icone in tema scuro (percorso `textureIsMono`)

*La resa monocromatica è stata provata, vista a schermo e poi **tolta**: l'autore
ha chiesto le icone classiche di Esplora risorse, a colori, e la scelta è sua. La
voce resta qui aperta e non risolta, con la ragione accanto — un elenco che dice
"fatto" di una cosa disfatta è peggio di un elenco che la lascia aperta. La maschera
di copertura è stata rimossa invece di essere lasciata inerte: `textureIsMono`
resta, come prima, una capacità della shader senza utilizzatore. Vedi §7.43 e §7.44
di `docs/02`.

*Il nucleo è fatto: icone reali per estensione, anteprime reali per immagini e
video, e la memoria dell'atlas resta limitata mentre si scorre una cartella
grande.*

**Design delle anteprime.** La decodifica gira su un worker thread con
apartment MTA, perché un JPEG freddo su una condivisione di rete costa decine di
millisecondi e bloccherebbe il frame loop. Il worker produce RGBA ad alpha
non premoltiplicato in una cella; l'upload su texture avviene sul thread che
possiede il contesto GL, in `Pump()`, una volta per frame. Le richieste sono
deduplicate mentre sono in volo e azzerate al cambio di cartella, così uscire da
una cartella da 1500 file non lascia il worker a decodificare anteprime che non
sono più a schermo. Vedi `src/shell_preview.h`.

---

## M2b — Navigazione e usabilità ✅

Nato come contorno della milestone 2, ma è ciò che rende il programma usabile.

- [x] doppio click per entrare in una cartella, o aprire il file con l'app
      associata
- [x] `Backspace` risale al genitore, `F5` ricarica, `Invio` apre
- [x] breadcrumb cliccabile componente per componente
- [x] intestazioni di colonna cliccabili, con inversione al secondo click e
      indicatore di ordinamento
- [x] troncamento con ellissi del testo che eccede la colonna
- [x] `Size` allineata a destra
- [x] quick access dalle cartelle note reali (`SHGetKnownFolderPath`)
- [x] cartella corrente evidenziata nella sidebar
- [x] percorso di partenza da riga di comando
- [x] DPI per-monitor come scala iniziale della UI
- [x] log degli errori di avvio su file, non solo MessageBox

---

## M3 — Statistiche di cartella ✅

> **Correzione.** Questa milestone era intitolata "Statistiche da MFT" e
> prevedeva la lettura e l'aggregazione del Master File Table. Quella ipotesi è
> stata **falsificata**: la firma dei record MFT non compare nel codice e le
> stringhe `NTFS`/`$MFT`/`USN` non esistono nel binario (vedi `docs/01`). Il
> lavoro è quindi di natura diversa e non richiede privilegi elevati.

Modello corretto: enumerazione parallela ottimizzata, dentro un job con budget di
tempo, con risultati memorizzati e invalidati.

- [x] job `SumDirStats` nella work queue, con avanzamento parziale pubblicato
- [x] enumerazione con `FindFirstFileExW` in modalità `FindExInfoBasic` +
      `FIND_FIRST_EX_LARGE_FETCH`, e fallback a `FindFirstFileW` dove non ammessa
- [x] accumulo di byte logici e conteggi di file e cartelle
- [x] accumulo dei byte **allocati**, chiesti **alla cartella in blocco**
      (`GetFileInformationByHandleEx`, `FileIdBothDirectoryInfo`): un syscall per
      cartella invece di uno per file, e l'allocazione arriva insieme alla lunghezza.
      Dove quell'interfaccia non risponde — volumi non NTFS, certe condivisioni — si
      ripiega su `FindFirstFileExW` e i byte allocati restano **non misurati**: la
      riga «Su disco» non si mostra, invece di mostrare un minimo come se fosse un
      totale. Vedi §7.46 di `docs/02`
- [x] tre profili di concorrenza e budget per SSD, HDD e rete
- [x] budget di tempo per ciclo, interrompibile e riprendibile
- [x] cache dei risultati con distinzione fra parziale e completo
- [x] invalidazione da `ReadDirectoryChangesW`: un sorvegliante segue la cartella
      aperta e i numeri si aggiornano da soli (`src/dir_watch.*`, `docs/02` §7.47).
      *Limite misurato:* la sorveglianza di Windows non e' ricorsiva. Si vedono i
      cambiamenti **nella cartella aperta** e, per via della data di modifica che si
      propaga, quelli **dentro le sue sottocartelle immediate**. Due livelli sotto
      non arriva niente: misurato — `documenti` con un file in `documenti\fondo\`
      resta a 3 file invece di 4. Li' resta `F5`, ed e' scritto invece di essere
      nascosto (`docs/02` §7.47)
- [x] attivazione su richiesta: solo le righe visibili e la selezione
- [x] reparse point non seguiti; hard link contati per nome (vedi nota)
- [x] degradazione su volumi non NTFS e su rete

### Come è implementato — *Ricostruito*

`src/dir_stats.{h,cpp}`. Il modello segue la macchina ricostruita: un job per
cartella, budget di tempo per fetta, risultati parziali pubblicati mentre il job
gira, e un token per cartella che rende **stale** ogni job in volo quando la
cartella viene invalidata — la stessa idea del confronto di generazione
`DAT_140249268` del binario di riferimento.

La differenza sostanziale rispetto all'originale è dove.

**Profili di volume.** La concorrenza e la fetta di tempo dipendono dal
dispositivo: 3 worker e 8 ms su SSD, 1 worker e 25 ms su disco meccanico, 2
worker e 40 ms su rete. Il tipo si ricava da `GetDriveTypeW` e, per i dischi
fissi, dalla proprietà `StorageDeviceSeekPenaltyProperty` interrogata con
`DeviceIoControl` — che è anche l'unico import di `DeviceIoControl` presente nel
binario di riferimento, quindi l'ipotesi più semplice che lo giustifica.

> **Attenzione:** questa è una **ricostruzione plausibile**, non un reperto. La
domanda n. 3 del registro («quale parametro differenzia SSD, HDD e rete») resta
aperta finché non si localizza chi referenzia `Setting_CalcFolderSizesSSD`.

**Come si legge una cartella.** I byte **allocati** — quanto spazio il disco ha
riservato, che è un'altra cosa dalla lunghezza del file — non li espone
`FindFirstFileExW`, che è la lettura più veloce e per questo resta quella di ripiego.
La lettura normale è `GetFileInformationByHandleEx(FileIdBothDirectoryInfo)`, che
chiede alla cartella **molte voci per volta** e le porta tutte e due le dimensioni: un
syscall per cartella invece di uno per file (`GetCompressedFileSizeW`, che per giunta
su questa macchina risponde la dimensione logica — vedi `docs/02` §7.46). Dove quella
interfaccia non è servita, il ricorso è la `FindFirstFileExW` di prima, e allora
l'allocazione di quell'albero resta **non misurata**: è diverso da zero, ed è scritto.

**Concorrenza e ripresa.** Le fette scadono a granularità di *cartella*: non si
può sospendere `FindNextFileW` a metà di una directory e riprenderla, quindi il
budget si controlla fra una directory e la successiva, e ogni 4096 voci si
pubblica comunque un parziale, per non lasciare muta una directory enorme.

**Reparse point e hard link.** I reparse point non vengono seguiti: sono link,
seguirli conterebbe due volte la destinazione e un ciclo non terminerebbe. Gli
hard link restano contati una volta per nome, perché distinguerli richiede un
identificatore di file per voce e l'enumerazione base non lo fornisce. È un
limite dichiarato, non un errore nascosto.

**Domande per REA, in ordine di priorità:**

1. Nella funzione che referenzia `Dir_SumDirStatsWorkUpdate`, quale granularità ha
   l'aggiornamento parziale e dove viene controllato il budget di tempo?
2. Nella funzione che referenzia `Setting_CalcFolderSizesSSD`, quale parametro
   cambia rispetto a HDD e rete: concorrenza, dimensione della fetch, o soglia di
   interruzione? I tre valori hanno percorsi di codice distinti o differiscono in
   una tabella di costanti?
3. Come vengono evitati i doppi conteggi su reparse point e hard link?
4. La cache dei risultati sopravvive fra sessioni o è solo in memoria?

**Come trovare le funzioni.** La tabella dei nomi dei job a `0x140247ed0` non ha
riferimenti statici, quindi la ricerca va fatta **dal lato codice**: cercare le
funzioni che chiamano `FindFirstFileExW` sono poche decine in tutto il binario, e
fra queste va cercata quella che accumula conteggi e byte. Questo è il primo
compito della sessione di reverse successiva.

*Stima: 2–4 settimane.* Meno della stima precedente, perché non servono
privilegi elevati né la correttezza di un formato su disco non documentato; resta
il lavoro sui casi limite (reparse point, hard link, errori di I/O parziali).

---

## M3b — Attributi NTFS di un file ✅

Feature separata e più piccola, dedotta dalla tabella `AT_*` e dagli import
`CreateFileW` + `DeviceIoControl`. **Scritta e verificata il 2026-10-18** in
`src/ntfs_attributes.*`, e collegata all'ispettore.

- [x] apertura del volume raw `\\.\X:`
- [x] `FSCTL_GET_NTFS_FILE_RECORD` per il record di un file identificato
      (`FileIdInfo` → riferimento MFT a 64 bit: numero di record nei 48 bit bassi,
      sequenza nei 16 alti)
- [x] mappatura dei codici di attributo ai nomi della tabella `AT_*` — indice
      `(codice >> 4) - 1`, che è la forma misurata sul binario: `AT_IndexRoot` è
      l'elemento **8**, cioè `0x90`
- [x] presentazione nell'inspector: una riga `NTFS` e la lista sotto, col nome
      dell'attributo nella colonna del **valore** (la colonna dell'etichetta è larga
      74 punti e "Standard Information" ci si troncherebbe a metà parola)
- [x] degradazione pulita senza privilegi elevati: `Status::NoPrivilege` →
      la riga dice «serve l'amministratore», e **non** una lista vuota

**Il record si corregge prima di leggerlo.** La *update sequence array* in testa al
record protegge gli ultimi due byte di ogni settore da una scrittura interrotta:
senza rimetterli, un record sano si legge come corrotto proprio sui confini di
settore. `ParseRecord` la applica, ed è la ragione per cui è una funzione separata
da `Read`: non fa I/O, quindi si prova anche dove i privilegi non ci sono.

**Verificato il 2026-10-18, senza amministratore** (`analysis/tmp/ntfstest/`,
sei controlli positivi e tre negativi): un record costruito a mano con sei
attributi — residente, non residente, uno stream nominato `:stessa`, e il sesto
attributo messo apposta **a cavallo** della fine del primo settore — si legge con
i nomi e le dimensioni giusti; **senza** la correzione dei settori lo stesso record
non si legge (è il controllo che dice che la correzione serve); firma sbagliata e
record troncato dicono **no** invece di restituire metà elenco; e su un file vero,
senza privilegi, la risposta è `NoPrivilege`.

**Verificato a schermo e contro disco, elevato** (la macchina era libera, e chi
lavora ha autorizzato il prompt):

| file | record letto | torna? |
|---|---|---|
| `nota.txt` | Standard Information 72, File Name 82, Data 23 | il file è di 23 byte e il nome di 8 caratteri: `0x48 = 72`, `0x42 + 2×8 = 82` |
| `appunti.txt` | Standard Information 72, File Name 88, Data 17 | 17 byte; nome di 11 caratteri: `0x42 + 22 = 88` |
| `documenti\` (cartella) | Standard Information 72, **due** File Name (82 e 84), Index Root 376 B **($I30)** | il nome lungo e quello DOS 8.3; l'indice ha il nome standard `$I30` |

Nell'ispettore la lista compare come riga `NTFS` con gli attributi sotto
(`analysis/tmp/m3b-inspettore-ntfs.png`), e **senza** privilegi la stessa riga dice
«serve l'amministratore»: le due facce sono state guardate tutte e due, quella
degradata prima e quella vera dopo.

**Due trappole pagate in questa prova**, ora in `docs/05` §5: un'applicazione
**elevata** non riceve i clic sintetici di un banco non elevato (UIPI), e non si
chiude con `taskkill`. La prima si aggira aprendo Bolt **direttamente sulla cartella
di prova**, così non c'è niente da cliccare (all'epoca il cursore cadeva sulla prima riga;
dal 2026-10-21 non c'è nessuna preselezione, `docs/04` M5); la
seconda vuole un `UAC` in più, e va messa in conto prima di avviare.

**Limiti dichiarati.** La lista è tenuta per percorso, come le proprietà del tipo
di file: se il record cambia (uno stream nuovo, un collegamento fisico in più) la
voce resta quella letta la prima volta finché non cambia la selezione. E il codice
`0xD0` (EA Information) è l'unico dei sedici che il riferimento **non** nomina: si
scrive in esadecimale invece di indovinarne il nome.

*Stima: 3–5 giorni.* È un percorso isolato e ben delimitato.

---

## M4 — Ranking dei recenti ✅

- [x] **la formula** di `Score`, `Decay` e `Ordinal`, ricostruita da REA
      (`FUN_140189bb0`): punteggio +3 per visita che satura a 10, `Decay` azzerato
      dalla visita e cresciuto di uno per ogni cartella **sorella** aperta, un punto
      di punteggio per ogni passaggio oltre il decimo, `Ordinal` = il piu' grande + 1.
      Il codice e' `src/recents.*`; le prove indirizzo per indirizzo sono sotto
- [x] la sezione **`RECENTS`** nella barra laterale: le cartelle visitate, la piu'
      recente per prima, col nome della cartella, ordinate per `Score` decrescente e
      poi per `Ordinal` — e il nome si spegne secondo il punteggio (`ShowRecents`)
- [x] **i tre modi** di `StoreRecents`: `LocalDisk`, `Persistent`, `Session`. Il modo
      dice **cosa** si registra (tutte le cartelle, o solo quelle dei volumi locali
      fissi), non dove finisce: nessuno dei tre scrive su disco (`docs/06`)
- [x] i **file** aperti, non solo le cartelle, con peso 1; e `SortRecents`, che porta
      i recenti in cima alla lista
- [x] integrare con la finestra GoTo e i gruppi di accesso rapido: `Accesso rapido`
      nel GoTo (con «Cancella i recenti» in fondo) e il fissaggio dalle righe della
      barra laterale
- [x] ~~**Domanda per REA**~~ — **risposta il 2026-10-18**: la formula e' quella
      ricostruita qui sotto, e verificata a schermo in sandbox (`docs/05` §3, voce 5)

*Stima: 1 settimana, rispettata.* Il blocco che aveva fermato due sessioni era del
**client** di REA, non del server: la prima chiamata che tocca Ghidra paga ~210 s di
import e auto-analisi, e il timeout era scritto nella chiave sbagliata (`AGENTS.md`
§5). La via empirica — leggere i numeri del riferimento — non serve piu'.

### Quello che l'analisi ha gia' stabilito — *2026-10-18*

REA ha aperto `analysis/targets/FPilot-2026-08-31.exe` (sha256 `ab5e7bbf…`) e Ghidra
ha finito l'auto-analisi (3075 procedure da `.pdata`), ma in questa sessione **ogni
interrogazione vera e' andata in timeout**: solo le chiamate che non toccano il
database rispondono. La formula quindi **non c'e' ancora**. Quello che si e' ricavato
senza decompilatore, leggendo il binario con capstone, e' butta la base per la
prossima sessione:

- il **lettore** della sessione sta a `0x1401a747f` (confronta le chiavi `Score`,
  `Decay`, `Ordinal`), lo **scrittore** a `0x1401a9c7d` (le tre `printf` con
  `"Score": %d,` ecc.);
- **le larghezze dei campi**, lette nelle istruzioni dello scrittore — ed e' il dato
  che conta:

  | campo | offset | tipo | istruzione |
  |---|---|---|---|
  | `Score` | `entry+0x20` | **16 bit** | `movzx eax, word ptr [rsi+0x20]` |
  | `Decay` | `entry+0x22` | **16 bit** | `movzx eax, word ptr [rsi+0x22]` |
  | `Ordinal` | `entry+0x24` | **32 bit** | `mov eax, dword ptr [rsi+0x24]` |

  Un punteggio a **16 bit** non e' un caso: satura a 65535, quindi l'algoritmo
  **limita** il punteggio invece di lasciarlo crescere — e `Decay` e' della stessa
  misura, cioe' un **numero piccolo di bucket** (giorni o passi di decadimento), non
  un timestamp a 64 bit. `Ordinal` a 32 bit e' un contatore d'ordine.
- l'operazione interna si chiama **`RecentsUpdate`** (stringa a `0x14022bce8`),
  accanto a `ReevalPaths`, `GotoCorrectPaths` e `GotoClearDirties`: e' il nome da
  cercare nella prossima passata, perche' una delle quattro fa il lavoro;
- il binario porta anche `SortRecents` e `StoreRecents` come opzioni, e la modalita'
  `persistent` come stringa accanto a `"Recents": [`.

**Il blocco era del client, non di REA (risolto il 2026-10-18).** Il server risponde:
`initialize` 2,0 s, `tools/list` 0,2 s con 139 tool, `open_binary` 0,1 s. Il costo e'
tutto nella **prima** interrogazione che tocca il database di Ghidra —
`list_procedures` **215 s** di import e auto-analisi — e dopo quella `xrefs` e
`procedure_pseudo_code` rispondono in decimi di secondo. Il client di pi chiudeva a
60 s (default dell'SDK MCP) perche' nella registrazione la chiave era `timeout`
(secondi, formato di Pi) mentre `mcp-adapter.json` vuole **`requestTimeoutMs`**
(millisecondi): la conversione avviene solo importando un `mcp.json` in formato Pi, e
li' il valore veniva perso in silenzio. Corretto in `~/.pi/agent/mcp-adapter.json`
(`requestTimeoutMs: 900000`, backup accanto al file). Il referto completo e il modo
di misurare stanno in `AGENTS.md` §5; il banco che parla col server senza pi e'
`analysis/tmp/rea-probe.mjs`, che salva anche il pseudocodice
(`rea-writer.json`, `rea-recentsupdate-logger.json`).

**Il prossimo passo, concreto.** Con REA ora utilizzabile: `xrefs` su `RecentsUpdate`
e `procedure_pseudo_code` sulle funzioni che lo circondano, per arrivare a chi
**aggiorna** i tre campi. Lo scrittore (`0x1401a9c7d`) e' gia' decompilato: formatta
`Score`/`Decay`/`Ordinal` e le stringhe della sessione, non calcola niente.
Se REA resta muta, la via empirica e' far girare **il riferimento** e leggere come
cambiano i numeri — ma quella tocca lo stato dell'applicazione in
`%APPDATA%\Voidstar\FilePilot`, cioe' dati di chi lavora, e va chiesta (`AGENTS.md`
§0).

---

## M5 — Ricerca, filtro, ordinamento, raggruppamento ✅

- [x] **nessuna preselezione entrando in una cartella**, il 2026-10-21 su richiesta
      dell'autore: entrando in una sezione o in una cartella non c'e' **niente di scelto**,
      mentre prima la prima riga si selezionava da sola — e un `Canc` appena arrivati
      cancellava un file che nessuno aveva scelto. Il cursore si posa al primo tasto di
      movimento (`MoveSelection` parte dalla prima riga quando non ce l'ha), il primo clic
      sceglie, `Ctrl+A` prende tutto. Visto a schermo: `sandbox` con `7 voci` e
      «Niente selezionato»; `Giu'` che sceglie la prima riga; una cartella aperta e la Home
      senza niente di scelto; `F5` che non riporta la selezione dove non c'era.
- [x] filtro incrementale sulla lista (`Panel_FilterHint`, `FilterText`, `FilterSticky`)
- [x] filtro per tipo di file (`FilterFileTypes`, `FilterTypes`)
- [x] ordinamento per ogni colonna (`SortedBy`, `SortIndex`, `FolderSort`)
- [x] **tre colonne in piu'** il 2026-10-18 — `Data creazione`, `Data accesso` e
      `Totale` — nove in tutto. Le due date le da' gia' `FindFirstFile` insieme alla
      modifica, e si formattano **quando si disegnano** (`ColumnValueOf`), non una
      volta per voce letta; `Totale` e' un derivato — cartelle + file + la voce stessa
      — cosi' le due colonne accanto lo rendono verificabile. Ordinabili e filtrabili
      come le altre, con i comandi `Ordina per data di creazione`/`di accesso`/`per
      numero di elementi`. Con nove colonne i **titoli** si accorciano (`Data mo…`):
      quello che deve entrare e' il valore — dieci caratteri per una data, pochi per
      un numero — e le colonne si possono sempre allargare col mouse. Visto a schermo:
      `documenti` con 0 cartelle, 2 file e **Totale 3**; `vuota` con 0, 0 e **1**
      (`analysis/tmp/cols-9.png`)
- [x] **le colonne si adattano al contenuto**, il 2026-10-21 su richiesta dell'autore:
      «adatta» e' il valore di partenza, non una voce da cercare in un menu. Aprendo una
      cartella ogni colonna si misura sul suo contenuto — l'intestazione e il valore piu'
      largo di un **campione** di duecento voci, che e' il compromesso che fa anche Esplora
      risorse, perche' una cartella da diecimila righe non si misura tutta per disegnarne
      trenta — e il **nome prende quello che resta**, con un minimo leggibile; se le altre
      non lasciano spazio si stringono loro, perche' un elenco di cui non si legge il nome
      non serve. I conteggi e la dimensione si misurano sulla forma che avranno
      (`999.9K`, `999.9 GB`), non sul `--` di adesso, o la colonna si allargherebbe da sola
      un istante dopo. Trascinando un bordo si passa alla larghezza scelta a mano e resta
      per tutte le cartelle; il **doppio clic** su un bordo torna ad adattare (prima
      riportava la colonna a una frazione scritta a mano nella tabella, che era lo stesso
      gesto con un numero indovinato). Visto a schermo: `sandbox` con `Documento di testo`
      e le date intere, `vuota` con le sole intestazioni, il trascinamento che resta su
      un'altra cartella, e il doppio clic che riadatta sulla stessa.
- [x] **le due date accessorie si possono spegnere** — il 2026-10-21, su richiesta
      dell'autore: `Data creazione` e `Data accesso` sono **spente di partenza**, e si
      accendono da due righe delle opzioni (`Mostra la data di creazione`, `Mostra la
      data di ultimo accesso`, preferenze `showCreatedDate`/`showAccessedDate`). In un
      elenco di file la data che si guarda e' quella di **modifica**; tre date
      affiancate si prendono lo spazio dei nomi per dire quasi sempre la stessa cosa
      (creazione = modifica, accesso = adesso). La colonna nascosta **non occupa
      spazio**: il suo bordo si porta su quello della prima visibile che la segue, e lo
      spazio va alla colonna prima (`ColumnEdges` in `src/view_list.*`); le frazioni
      scelte col mouse restano, quindi riaccenderla la rimette dov'era. Spegnerla non
      lascia niente di invisibile acceso: **il filtro per valori di quella colonna si
      toglie**, e se era lei a ordinare si torna al nome. Visto a schermo
      (`analysis/tmp/col-*.png`): sette colonne di partenza, `Data creazione` che
      compare accendendola e resta dopo un riavvio, le nove con tutte e due, l'ordine che
      torna su `Nome` spegnendo la colonna che ordinava, e il bordo trascinato fino a
      riprendersi lo spazio della colonna nascosta.
- [x] **le date si scrivono europee** — `gg/mm/aaaa`, e `gg/mm/aaaa hh:mm` dove c'e' l'ora
      (lista, suggerimenti, ispettore). Prima erano `aaaa-mm-gg`: ordina bene e si legge
      male (`2026-10-20` va guardato due volte). Non e' una stringa tradotta, quindi vale
      uguale in italiano e in inglese, e la lunghezza e' la stessa di prima — dieci
      caratteri, sedici con l'ora — quindi le colonne non si stringono. Le produce una
      funzione sola, `FormatFileTime` in `src/app_ui.cpp`; le bande del raggruppamento
      per data **non** sono date ma nomi relativi («Oggi», «Ieri»), e restano quelli.
      Visto a schermo: `20/10/2026` nelle tre colonne e `Creato 20/10/2026 16:03`
      nell'ispettore (`analysis/tmp/menu-icone.png`)
- [x] raggruppamento sulle cinque dimensioni di `GroupByLabel_*`
- [x] righe di intestazione di gruppo collassabili
- [x] selezione multipla e rettangolare (`Color_RectSelection`)

### Il modello di vista — *Ricostruito*

Con filtro e raggruppamento la lista **non è più la directory**: una riga non
corrisponde più a una voce. `AppState::rows` è una lista di righe sopra l'array
delle voci, con `Row::header` che distingue una banda di gruppo da una voce, e
viene ricostruita solo quando cambia qualcosa che la riguarda: filtro,
raggruppamento, ordinamento, cartella, oppure un risultato di statistica nuovo e
solo ogni 250 ms, perché altrimenti un publish per fetta diventerebbe un rebuild
per frame.

La selezione resta un indice nella directory, non nella vista: l'inspector e
l'apertura non devono sapere che esiste un filtro. Dove invece la vista conta è
navigazione e disegno, quindi `MoveSelection` salta le bande,
`EnsureSelectionVisible` riporta in vista la riga corrispondente e lo scorrimento
conta le righe.

**Filtro.** Termini separati da spazi, in AND. Un termine che inizia con un punto
o contiene un jolly è una richiesta sul tipo (`.pdf`, `*.png`) invece che sul
nome: è il percorso `FilterFileTypes`/`FilterTypes` espresso nella stessa casella
del filtro testuale. `FilterSticky` esiste nella tabella delle chiavi; qui il
filtro **non** è sticky, perché un filtro ereditato da un'altra cartella sembra
una cartella vuota.

**Raggruppamento.** Cinque dimensioni, con le etichette prese dalla tabella
`GroupByLabel_*`. Le **soglie** non lo sono: `docs/03` §8 le registra come ignote,
quindi in `GroupKeyFor` sono valori scelti e isolati in un unico punto, facili da
correggere quando l'osservazione del target li rivelerà. La dimensione
sull'occupazione del volume usa `GetDiskFreeSpaceExW`, come l'originale.

**Conteggio delle corrispondenze.** La barra del filtro mostra
`corrispondenze / totale`. Conta le voci che superano il filtro, non le righe
disegnate: collassare un gruppo non deve sembrare un cambiamento del filtro.

### Selezione — *Ricostruito*

**Per nome, non per indice.** Ordinare, filtrare e ricaricare rinumera l'array
delle voci: una selezione tenuta per indice scivola su un file diverso appena
l'utente ordina per dimensione — un difetto peggiore di nessuna selezione. Quindi
la selezione è un insieme di **nomi**, e l'indice si ritrova quando serve.

Le tre modalità dello standard Windows, senza inventarne di nuove:

| gesto | effetto |
|---|---|
| click | seleziona solo quella voce, e la rende cursore e ancora |
| `Ctrl`+click | aggiunge o toglie la voce |
| `Shift`+click, `Shift`+frecce | intervallo dall'ancora al cursore, ricalcolato a ogni passo (così tornando indietro si restringe) |
| trascinamento su area vuota | banda rettangolare |
| `Ctrl` o `Shift` + trascinamento | la banda si somma alla selezione |
| `Ctrl+A` | tutto ciò che è a schermo: il filtro fa parte della domanda, quindi fa parte della risposta |

**Un difetto trovato verificando.** La banda teneva conto solo di `Ctrl`, non di
`Shift`, quindi `Shift`+trascinamento **cancellava** la selezione invece di
sommarsi. È emerso perché un click di prova cadeva a un pixel dal bordo di una
riga: il click mancava la riga, finiva nel ramo "area vuota" e azzerava tutto.
Il difetto era reale, il test era sbagliato per un pixel — ed è servito lo stesso.

**Riempimento e contorno sono separati**, come i token `ContentSelection` e
`OutlineSelection` dell'originale: con più righe selezionate bisogna comunque
vedere su quale sta lavorando la tastiera.

La banda si disegna sopra le righe che sta per selezionare e solo dentro la
regione della lista, così trascinare oltre il bordo non sembra selezionare la
sidebar. L'hit test alla fine del trascinamento usa i rettangoli delle righe
registrati dal frame precedente: al più un frame di ritardo, che conta solo se
l'utente rilascia nell'istante in cui la lista scorre.

**Limite dichiarato.** La somma delle dimensioni nell'inspector di una selezione
multipla usa le statistiche **già in cache** per le cartelle: chiedere il conteggio
di una cartella fuori schermo avvierebbe lavoro che l'utente non vede.

---

## M5c — Modalità di visualizzazione ✅

Due impaginazioni (righe di testo, griglia di celle) e due dimensioni di cella
coprono le otto voci di visualizzazione di Explorer, perché la maggior parte di
quelle sono la stessa impaginazione a una scala diversa.

- [x] `Details` — righe con tutte le colonne, intestazione visibile
- [x] `List` — righe compatte, icona più piccola, stesse colonne
- [x] `Icons` — griglia, cella da 64 px di miniatura
- [x] `Tiles` — griglia, cella da 128 px di miniatura
- [x] nome su due righe con ellissi nell'ultima, centrato sotto la miniatura
- [x] intestazione nascosta in griglia, spazio restituito alla lista
- [x] frecce: `Su`/`Giù` di una riga di griglia, `Sinistra`/`Destra` di una cella
- [x] `Ctrl`+`1`/`2`/`3`/`4`, più i quattro comandi nella palette
- [x] secondo corpo di glifi **rasterizzato** per il testo secondario, 4 px sotto il
      corpo principale e da quello **derivato** (vedi §7.12 di
      `docs/02`), su un quarto layer dell'array di texture
- [x] **i valori delle colonne** — tipo, data, cartelle, file, dimensione — si
      leggono nel carattere dei **dettagli**, come le intestazioni sopra di loro; il
      **nome resta nel corpo**, perché e' quello che si legge per primo. Con il corpo
      `Cartella di file` non ci stava e diventava `Cartella di fi...` (chiesto
      dall'autore). Perché il carattere più piccolo serva davvero, il bordo destro
      della colonna `Tipo` sta a **0,45** e non a 0,42: lo spazio lo prende dalla
      colonna della data, che ha un titolo lungo e dieci cifre di contenuto, e dai
      conteggi, che sono numeri. A schermo si leggono interi `Cartella di file` e
      `Documento di testo`, e tutte e sei le intestazioni restano intere
- [x] **raggruppamento in griglia**: le bande di gruppo stanno anche fra le celle,
      a piena larghezza, e la riga di celle sotto di loro ricomincia da sinistra
- [x] **ridimensionamento continuo della cella**: `Ctrl`+rotella sopra la griglia, da
      32 a 256 px, e la griglia si reimpagina mentre si scorre
- [x] **icona grande chiesta grande**: oltre la misura dell'atlante (32 px) l'icona la
      da' la shell **alla misura della cella**, dalla stessa associazione che nutre le
      icone piccole, cosi' la stessa voce non ha due icone diverse (`docs/02` §7.45).
      *Limite dichiarato:* la cella grande e' 128 px e la vista arriva a 256, quindi
      all'ingrandimento massimo c'e' ancora un ingrandimento di due volte

### Perché le celle sono alte quanto dicono di essere — *Ricostruito*

L'altezza della cella era un numero fisso scelto a occhio, e l'etichetta a due
righe finiva **sopra** la miniatura: si vedeva il nome stampato sulla foto. Ora
l'altezza si **deriva** da icona più etichetta, e nel disegno la dimensione
dell'icona viene comunque ridotta se lo spazio sopra l'etichetta non basta. Un
numero derivato non può entrare in disaccordo con le sue parti.

### Perché la geometria si calcola una volta — *Ricostruito*

La griglia impaginava le celle **per indice**: `indice % colonne` e `indice /
colonne`. È una formula che vale finché ogni riga ha la stessa altezza, e una banda
di gruppo a piena larghezza la rompe: la banda prende una riga intera, e sotto di
lei il conto delle colonne riparte da zero.

La formula è stata sostituita da una **geometria calcolata una volta**
(`GridRects`): un rettangolo per ogni riga della vista, senza lo scorrimento
dentro, rifatto solo quando cambia qualcosa da cui dipende (le righe, la larghezza
dell'elenco, la vista, il raggruppamento, l'ingrandimento). La usano tutti:
chi disegna, chi colpisce col mouse — che riceve il rettangolo *disegnato*, quindi
segue per costruzione — chi scorre per sapere quanto è alto il contenuto, e chi
tiene il cursore in vista con le frecce.

Tre conti separati che devono coincidere prima o poi non coincidono; uno solo non
può sbagliare. Ed è anche il motivo per cui il lavoro è stato fatto in un giro
suo: sotto le dita c'erano il disegno, il clic, la rotella e la tastiera.

### `Icons` e `Tiles` erano due punti, non due viste — *Ricostruito*

`Icons` e `Tiles` differivano **solo** per la misura: icona 64 px contro 128, cella
104 contro 176. Ed erano due numeri fissi in una `switch`, con due rapporti diversi
fra cella e icona (104/64 di qua, 176/128 di là) per la stessa cosa.

Ora c'è **una misura sola** (`gridIconSize`) e tutto il resto si deriva: la cella è
l'icona più lo spazio per l'etichetta, l'altezza è l'icona più due righe di testo. I
due comandi restano, come **scorciatoie** ai due punti noti (64 e 128); `Ctrl`+rotella
porta la misura dove si vuole, fra 32 e 256, e la griglia si reimpagina mentre si
scorre.

**Il nome della vista segue la misura**: sotto i 96 px si chiama `Icons`, sopra
`Tiles`. È una conseguenza del principio di `AGENTS.md` §6 — un controllo che dichiara
qualcosa che la vista non sta facendo è peggio di un controllo assente — perché
altrimenti il piè di pagina avrebbe continuato a dire `Tiles` mentre si guardavano
icone da 40 px. L'ingrandimento dell'interfaccia è un'altra cosa e resta il suo:
`Ctrl`+rotella non lo tocca.

**Verificato**: quattro celle per riga a 128 px, tre a 176, cinque a 80 — e a 80 il
piè di pagina dice `Icons` mentre l'ingrandimento resta `100%`.

Il secondo corpo di glifi è fatto.

---

## M5d — Sovrappasso e comportamento del clic ✅

Non era nei piani. È emerso da una domanda dell'utente — «quando passo sopra dei
file si preseleziona qualcosa?» — e la risposta era che **no, e non era previsto**:
la roadmap non ne parlava, ma lo spec funzionale dell'originale sì. Lacuna mia, non
una scelta.

Il campo `app.hovered` era già calcolato a ogni `WM_MOUSEMOVE` e **non veniva mai
letto**: l'impianto c'era, il disegno no. Il token di tema `Hover` / `ContentHover`
esisteva già fra i 35.

- [x] riquadro di sovrappasso su righe e celle di griglia
- [x] si spegne uscendo dalla finestra (`TrackMouseEvent` + `WM_MOUSELEAVE`) —
      senza, resterebbe appeso sull'ultima riga toccata, perché uscendo non arriva
      più nessun `WM_MOUSEMOVE`
- [x] la selezione vince sul sovrappasso: la riga selezionata **non** cambia colore
      sotto il puntatore
- [x] `HoverToSelect` — il passaggio **seleziona**, non solo evidenzia: dopo una
      pausa di 0,35 s, e non mentre si trascina o con un menu aperto
- [x] `UnderlineOnHover` — sottolineatura al passaggio, sotto il nome **visibile**
      (sotto quello intero, con un nome troncato, sarebbe una riga lunga quanto una
      parola che non c'è)
- [x] **il rettangolo di selezione seleziona mentre si trascina**, il 2026-10-21 su
      richiesta dell'autore: le righe coperte si accendono **subito**, durante il gesto,
      invece di comparire tutte al rilascio. La selezione si rifa' a ogni movimento da una
      **base** (`rectBase`: quello che c'era prima del gesto quando si tiene `Ctrl`) piu'
      le righe coperte, quindi stringendo il rettangolo si deseleziona e un rettangolo
      senza `Ctrl` non eredita niente dal precedente. Visto a schermo fotografando **a
      meta' gesto** (`analysis/tmp/rect-01.png`, `rect-03.png`): righe blu col tasto
      ancora giu'; con `Ctrl` la riga scelta prima resta e le altre si sommano; un
      rettangolo piu' piccolo dopo ne lascia due sole (`rect-04.png`). Per provarlo il
      banco ha guadagnato `-Ctrl` in `tools/drag-mouse.ps1`.
- [x] `ShowTooltips` — nome per esteso, tipo, data e dimensione; per una cartella
      anche i conteggi ricorsivi quando ci sono. Compare dopo 0,55 s **di puntatore
      fermo**: il conto riparte a ogni movimento, quindi scorrendo l'elenco non
      passa niente davanti
- [x] `SingleClick` e `SingleClickFoldersOnly` — un clic apre, e con la seconda
      chiave solo le cartelle: sui file serve ancora il doppio, che è quello che
      vuole chi accende solo la prima
- [x] `ActOnPress` — l'attivazione avviene **premendo** invece che mollando.
      Verificato con una cattura scattata a tasto ancora giù: la cartella era già
      aperta. **L'interpretazione è nostra**: il significato esatto della chiave
      nello spec non è ricostruito, e questa è la lettura che le dà un senso — senza
      di essa sarebbe un doppione di `SingleClick`
- [x] `DoubleClickToParent` — doppio clic sul **fondo** della lista: su di un
      livello. Su una riga il doppio clic apre, che è un'altra cosa
- [x] `ShowCheckboxes` — una casella che **riflette la selezione** (una casella con
      una verità propria sarebbe una seconda verità da tenere allineata, e prima o
      poi le due divergono). Si preme per scegliere: il clic non apre, non arma un
      trascinamento e non sposta il cursore. Nella griglia sta nell'angolo della
      cella, che è centrata; nell'elenco sposta icona e nome di 22 px invece di
      coprirli. Il segno di spunta è un quadrato pieno perché nell'atlante dei glifi
      non c'è un carattere di spunta, e scriverne uno fuori dall'insieme rasterizzato
      darebbe un buco (§7.32)

**Tutte e cinque spente di partenza.** Sono *impostazioni* nell'originale, e il
doppio clic è il gesto che si aspetta chiunque: cambiarlo senza che nessuno l'abbia
chiesto vorrebbe dire cambiare il programma sotto le mani di chi lo usa. Si accendono
dalla palette, gruppo `Interaction`, che è il nome della riga nella tabella delle
chiavi.

### Cosa fa l'originale, e cosa non abbiamo ancora — *Ricostruito*

Lo spec funzionale elenca otto chiavi di interazione (`docs/03`, sezione
Interazione), e il tema ha **quattro** token di sovrappasso: `Hover`,
`ContentHover`, `MatchHover`, `OutlineHover`. La famiglia `Match*` copre quattro
stati di una riga che corrisponde al filtro — normale, sovrappasso, selezionata,
selezionata ma disabilitata — quindi nell'originale il sovrappasso **convive con
la selezione** invece di essere assorbito da essa.

Nota di merito: `HoverToSelect` è una **chiave di configurazione**, quindi
nell'originale non è il comportamento predefinito. Evidenziare e selezionare sono
due cose distinte, e la seconda si accende. Anche `UnderlineOnHover` è una chiave,
non un dato di fatto.

**I valori di partenza dell'originale non sono ricostruiti**, e questi si
dichiarano: `ShowTooltips` acceso, `HoverToSelect` e `UnderlineOnHover` spenti. Si
accendono e si spengono dalla palette, che è il posto dove stanno le altre chiavi.

**Un difetto trovato guardando la griglia.** Cambiando vista le celle si spostano e
l'indice della riga sotto il puntatore non vuol più dire niente: il suggerimento
restava appeso a una voce che non era più quella sotto il mouse — cioè un nome
sbagliato accanto al puntatore, che è la forma peggiore di errore in una lista di
file. Il sovrappasso adesso si azzera cambiando vista e lo ricalcola il prossimo
movimento.

---

## M6 — Tab, quick access, palette, GoTo ✅

- [x] tab multipli: creazione, chiusura, attivazione, stato indipendente
- [x] barra del titolo propria al posto di quella nativa, con pulsanti di finestra
- [x] **il menu dell'applicazione dietro il pulsante ☰**, il 2026-10-21 su richiesta
      dell'autore, che ha mandato la cattura del riferimento: `Vai a...` (`Ctrl+P` `F4`),
      `Comandi` (`Ctrl+Maiusc+P`), i due layout (`Applica il layout` / `Salva il layout`),
      `Opzioni` (`Ctrl+,`), `Controlla aggiornamenti` — dove il riferimento ha il suo
      setup, noi abbiamo l'aggiornamento — e in fondo la **versione** (`BOLT v0.1.16`),
      come li' `FILE PILOT v0.8.5`.
      E' un **menu contestuale** a un livello solo, quindi usa quel componente (separatori,
      voci spente, sottotitolo) invece di un secondo modo di disegnare un menu; le
      scorciatoie a destra vengono dalla mappa dei tasti (`KeyMap::LabelsFor`), quindi si
      aggiornano da sole se si rimappa. `Applica il layout` si vede **spento** quando la
      cartella non ne ha uno salvato — e per questo e' nato `CmdLoadFolderLayout`, che
      prima non c'era (il layout si applicava solo entrando). Finche' il menu non c'era, il
      ☰ accendeva la barra laterale: adesso quella ha il **suo pulsante** accanto, col
      riquadro pieno quando si vede. Visto a schermo: il menu con le scorciatoie e la
      versione (`analysis/tmp/menu-01.png`), `Applica il layout` spento a 160 di luminosita'
      e acceso a 234 dopo aver salvato, `Vai a...` che apre il GoTo, `Opzioni` che apre il
      suo pannello, `Controlla aggiornamenti` che dice che Bolt e' aggiornato, e il
      pulsante della barra laterale che la toglie e la rimette.
- [x] command palette con gruppi collassabili
- [x] finestra GoTo con recenti e segnalibri
- [x] componente menu a gruppi, condiviso da palette e GoTo
- [x] cronologia di navigazione per tab (`Alt`+`←` / `Alt`+`→`)
- [x] drag&drop dei tab (`TabDropMarker_*`): dentro un altro tab si diventa figli,
      sui bordi fratelli prima o dopo; un tab non entra in un proprio discendente
- [x] albero dei tab (`CloseInTree`, `CloseAllInTree`, `DetachTab`) — **verificato**
- [x] **barra laterale a sezioni** — QUICK ACCESS (i fissaggi della shell),
      BOOKMARKS, STORAGE (i volumi con occupazione e spazio libero), PLACES — tutte
      richiudibili, e la stella nella barra strumenti che aggiunge un segnalibro
- [x] **pulsanti di navigazione** nella barra strumenti: indietro, avanti, su,
      aggiorna, segnalibro — gli stessi comandi della tastiera e della palette.
      I cinque segni sono **forme disegnate**, non caratteri (`←` `→` `↑` `↻` `★`),
      in una scatola di 12 px con spessore 1,2 px come i pulsanti della finestra, e
      centrati **sulla riga dell'indirizzo** misurata dal testo e non a occhio:
      `docs/02` §7.58, che ha anche le tre trappole (verso dei vertici, unione delle
      forme, assi girati di `point`)
- [x] il terzo contesto del componente a gruppi: il menu contestuale (fatto in M7)
- [x] **il fissaggio in qualunque sezione** — deciso dall'autore il 2026-10-16, e fatto:
      `Fissa in...` e `Togli da...` (palette e menu contestuale) mettono una cartella in
      **accesso rapido** o nei **segnalibri**, e nell'accesso rapido le nostre righe
      stanno in cima a quelle della shell con una **stella** che le distingue. Anche
      una cartella standard si fissa come qualsiasi altra. Vedi sotto

### Il fissaggio, e cosa si può fissare — *Scelta dell'autore*

Deciso il 2026-10-16: **qualsiasi categoria si può pinnare** — accesso rapido,
segnalibri, o anche una cartella standard come Desktop. Non e' "aggiungi ai
segnalibri": e' **fissa questa cartella dove dico io**, e le sezioni della barra
laterale sono le destinazioni possibili.

Due cose vanno decise **prima** di scrivere, e sono tutte e due di sostanza:

1. **La lista della shell non si scrive.** L'accesso rapido di Windows non ha
   un'API documentata per aggiungerci una voce: i suoi fissaggi stanno in un
   *jump list* (`%APPDATA%\Microsoft\Windows\Recent\AutomaticDestinations`), e
   l'unico modo di toccarlo e' `ICustomDestinationList`, che pero' scrive un file
   **della shell**. Due strade, e la seconda va provata prima di prometterla:
   **(a)** i fissaggi sono **nostri** e la sezione li mostra accanto a quelli che la
   shell ha gia' (una riga con la nostra stella, le loro com'erano); **(b)** si
   scrive il jump list e si accetta che Windows possa riscriverlo. La (a) e' il
   default; la (b) e' una prova da fare e da dichiarare se fallisce.
2. **Dove vivono i fissaggi.** Un elenco di percorsi fissati e' un elenco di
   **cartelle di qualcuno**, e `docs/06` dice che `settings.ini` non contiene ne'
   nomi di file ne' cartelle. Quindi, come i segnalibri di oggi: **in memoria, per la
   sessione**. Ricordarli fra le sessioni vuol dire cambiare quella regola, e quella
   e' una decisione dell'autore, non un dettaglio di implementazione.

Quello che c'era da fare — un comando (`Fissa in...`) nel menu contestuale e nella
palette, le sezioni che accettano le righe fissate, e il toglimento — **adesso c'e'**:

- **`Fissa in...`** apre l'elenco delle destinazioni — **Accesso rapido** e
  **Segnalibri**, le due sezioni che accettano una riga — e **`Togli da...`** apre lo
  stesso elenco per togliere. Due comandi e non uno con un verso: chi cerca "togli"
  non deve leggere "fissa" e capire da solo che fa il contrario. La riga di una
  destinazione che non cambierebbe niente si **disegna spenta** e non si sceglie
  (`docs/02` §7.67: prima la ragione era scritta dentro il nome, perche' il
  componente dei menu non sapeva spegnere una riga). Se non c'e' niente da fare, il
  pannello non si apre e l'esito si legge nella barra di stato.
- **Nell'accesso rapido le nostre righe stanno in cima**, con la stella dei
  segnalibri in fondo alla riga e in colore d'accento; quelle della shell restano
  sotto, come erano, senza stella. La stella sta in fondo e non davanti al nome
  perche' davanti avrebbe spostato le etichette di una riga sola, e l'elenco non
  sarebbe piu' allineato.
- **Una cartella standard si fissa come qualsiasi altra**: `PLACES` e' di sola
  lettura e resta di sola lettura — e' anche il posto giusto per le cartelle standard,
  che ci sono gia' — ma "fissa Desktop" vuol dire metterlo qui, fra i nostri.

**Le due decisioni, applicate.** La (a): i fissaggi sono **nostri** e la sezione li
mostra accanto a quelli che la shell ha gia'. La (b) — scrivere il jump list di
Windows — **non si e' provata, e non serve**: guardando il menu contestuale si vede
che il verbo della shell per farlo c'e' gia' (`Aggiungi a Preferiti`, quello di
Esplora risorse), e lo scrive **il sistema**. Farlo noi vorrebbe dire lasciare una
traccia nella barra laterale di chi lavora per un guadagno che nessuno vede. I
fissaggi vivono quindi in memoria, per la sessione, come i segnalibri e i recenti:
`settings.ini` non contiene percorsi (`docs/06`).

**Limiti dichiarati.**

- **I due comandi agiscono sulla cartella aperta, non sulla selezione.** Per questo
  nel menu contestuale compaiono **solo sul fondo**: li' sotto non c'e' una selezione
  con cui confonderli. Con una cartella scelta, "fissa in..." vorrebbe dire "fissa la
  voce scelta", che non e' quello che questo comando fa — e offrirlo lo stesso
  sarebbe promettere qualcosa che non mantiene.
- ~~**Il toglimento sta nella palette o nel menu contestuale**, non su un gesto della
  riga~~ — **fatto il 2026-10-18**: il tasto destro su una voce della barra laterale
  apre il menu di **quella riga** (`Fissa in...`, `Togli da...`, `Rimuovi dai
  recenti`), e i tre comandi agiscono sul suo percorso invece che sulla cartella
  aperta. Vedi `docs/04` M4 e `view_context_menu.cpp` (`BuildSidebarContextMenu`).
- **I fissaggi non sopravvivono alla sessione**, come i segnalibri di oggi: e' la
  decisione (a), non una dimenticanza.

**Verificato a schermo** (catture in `analysis/tmp/keys/pin-*.png`): quattro cartelle
fissate in accesso rapido, ognuna con la stella, sopra le righe della shell che la
stella non ce l'hanno; la stella della barra strumenti che si accende fissando nei
segnalibri; `Togli da...` che toglie — la riga sparisce dalla barra laterale e la
stella si spegne — con l'esito nella barra di stato (`Fissata in Accesso rapido`,
`Tolta da Accesso rapido`); la riga fissata che **porta** alla cartella cliccandola;
le due voci della palette e le due del menu contestuale, presenti sul fondo e **non**
con una selezione; e le stesse schermate in inglese (`PIN TO`, `Where?`, `Quick
access`, `Bookmarks`).

**Una trappola pagata qui, e vale per il prossimo menu:** un menu nuovo si registra in
**quattro** posti — `OpenMenu`, il disegno in `RenderFrame`, e le due sedi che
eseguono una voce attivata (clic e tastiera). Me n'ero dimenticato uno, il disegno: il
pannello si apriva, prendeva i tasti, e non si vedeva. E' la **schermata** che l'ha
detto, non il codice, che sembrava giusto; e sotto c'era un secondo guasto, una
condizione invertita che faceva uscire il pannello subito con un messaggio **falso**
(«gia' fissata in tutte le sezioni», la prima volta che si usava il comando). Trovato
con una riga di registro, non ragionando. Vedi `docs/02` §7.57.

### L'albero dei tab, verificato — *Visto*

Il modello c'era dal primo giorno di M6 — un tab ha **un'identita'** e un `parentId`,
non una posizione — e le tre operazioni erano scritte ma mai guardate. Adesso sono
state provate a schermo, su tre tab:

- **Creazione:** si trascina un tab sul **terzo di mezzo** di un altro e diventa
  suo figlio. Il figlio si disegna **rientrato** e con la riga che lo lega al
genitore; il trascinamento **rifiuta** un tab dentro un proprio discendente, perche'
un albero con un anello non e' piu' un albero.
- **`CloseInTree`:** chiude il tab scelto **e i suoi figli**. Con il genitore attivo
  sono spariti due tab in un colpo solo, e l'altro ramo e' rimasto.
- **`CloseAllInTree`:** chiude **tutto l'albero** a cui appartiene il tab, radice
  compresa. Col **figlio** attivo sono spariti lui *e* il genitore, e l'albero
  estraneo e' rimasto: e' cio' che lo distingue dal primo.
- **`DetachTab`:** il figlio torna una radice; il rientro e la riga spariscono.

L'ultimo tab non si chiude mai: chiudendo un albero intero se ne risparmia uno, e
lo si dice invece di lasciare una finestra vuota.

**Metodo.** Un trascinamento e' ambiguo da leggere in una cattura — due tab con lo
stesso titolo si assomigliano — quindi la prova si e' appoggiata a una riga di
`BOLT_DIAG`: `tab drop: 2 figlio di 0` dice *cosa* e' successo, e la cattura dice
*come si vede*. Le due cose insieme, non una delle due.

### La barra laterale a sezioni, e il confronto col riferimento — *Ricostruito*

Quattro sezioni, perché le voci sono di natura diversa e chi non usa una non deve
guardarla: **QUICK ACCESS** (i fissaggi della shell), **BOOKMARKS** (nostri, in
memoria sola: `docs/06`), **STORAGE** (i volumi con la barra di occupazione e lo
spazio libero), **PLACES** (le cartelle standard, e il cestino in fondo). Le
intestazioni sono richiudibili e lo stato è ricordato **per nome**: le righe si
spostano quando i segnalibri cambiano, e una sezione chiusa non deve riaprirsi da
sola perché ne è arrivata un'altra sopra.

Il confronto col riferimento (`file pilot.png`, guardato su richiesta dell'autore)
ha dato il resto della lista, e vale la pena scrivere cosa si è preso e cosa no:

| cosa si vede nel riferimento | qui |
|---|---|
| sezioni nella barra laterale, richiudibili | fatte |
| STORAGE col volume e lo spazio libero | fatto |
| pulsanti indietro/avanti/su/aggiorna a sinistra del percorso | fatti, con la stella dei segnalibri in più |
| il bersaglio di un collegamento accanto al nome, in tono spento | fatto |
| piè di pagina con posizione, modalità di vista e ingrandimento | fatto; i contatori del motore sono passati dietro `BOLT_DEV_HUD=1` |
| icone di filtro nelle intestazioni delle colonne | fatte: un imbuto di tre barre, acceso quando la colonna filtra |
| colonne ridimensionabili col mouse | fatte: si trascina il bordo, il doppio clic lo riporta com'era |

Le due voci sono state chiuse insieme, e le decisioni che non si vedono sono queste:

- **il filtro per colonna è un elenco di valori *tolti*, non di valori tenuti.** Una
  colonna appena aperta non filtra niente, e questo si legge dalla sua lista vuota
  senza doverla confrontare con quella dei valori possibili.
- **i valori si contano all'apertura del pannello e non si riordinano mai.**
  Togliendone uno le caselle non devono saltare sotto il puntatore. Per la stessa
  ragione l'ordine è per frequenza decrescente, che non cambia mentre si spunta.
- **il pannello non si chiude quando si spunta.** Una lista di caselle si spunta a
  più riprese: è tutto il motivo per cui è una lista e non un interruttore.
- **la dimensione filtra per fasce** (`< 1 KB`, `< 1 MB`, …): un valore per file non
  filtra niente. È l'unica colonna che non usa il valore mostrato, ed è dichiarato.
- **il nome non ha il filtro**: ogni valore è distinto, e un elenco di cento nomi
  non è un filtro. Il testo libero resta nella casella di sotto, che è un'altra cosa.
- **larghi e filtri sono della vista, non della scheda** — come le colonne stesse. Il
  filtro di testo invece è della scheda, e resta dov'era. È una scelta, non una
  dimenticanza.
- **i valori sono al massimo 40**: oltre, l'elenco non si legge più. Dichiarato nel
  codice, dove si taglia.
- righe e valori arrivano dal componente della palette, quindi il pannello scorre e
  si filtra da tastiera senza codice nuovo: è il **secondo utilizzatore** che lo ha
  fatto estrarre, e adesso ne ha un terzo.

**La quarta voce della tabella era un difetto vero, trovato guardando.** Un
collegamento mostrava `collegamento-prova.lnk` e basta: cosa apre lo sa solo la
shell, e chiederlo costa una `IShellLink` + `IPersistFile::Load` per file — una
volta per percorso, perché una riga si disegna sessanta volte al secondo.

E il piè di pagina diceva `60 fps | 477 quads | 0 stats jobs`: numeri di chi scrive
il programma, in un posto dove chi lo usa cerca "dove sono" e "quanti ne ho
scelti". Adesso dice `5 items  1 selected` a sinistra e `1 / 5 | Details | 100%` a
destra — ed è la forma del riferimento.

### Il componente a gruppi, e quando è stato estratto — *Ricostruito*

`GroupMenu` è una lista modale, filtrabile, raggruppata e collassabile che **non
sa cosa significhi una voce**: `id` è affare di chi la chiama. È ciò che le
permette di servire sia la palette dei comandi sia la finestra GoTo.

**È stata estratta quando è arrivato il secondo utilizzatore, non prima.** Al
momento della palette l'astrazione sarebbe stata indovinata: non si sapeva quali
parti sarebbero state comuni — filtro, gruppi, navigazione, disegno della banda —
e quali specifiche del contesto. Con GoTo la cucitura è diventata evidente, e
l'estrazione è stata uno spostamento. Il terzo contesto previsto dalla roadmap è
il menu contestuale della shell, che arriva con le operazioni su file.

**Cronologia, recenti e segnalibri.** La cronologia è **per tab** e si salva e
ricarica insieme al resto dello stato del tab; camminare verso una destinazione
nuova scarta il ramo in avanti, come in ogni browser. I recenti e i segnalibri
sono **globali e di sessione**: `StoreRecents` ha tre modalità nella tabella
delle chiavi (`LocalDisk`, `Persistent`, `Session`) e questa è la terza, quindi
nulla viene ancora scritto su disco — la persistenza è M9.

**`Alt`+`←`/`→` arrivano come `WM_SYSKEYDOWN`, non `WM_KEYDOWN`,** perché la
finestra ha un menu di sistema: vanno intercettati lì o non arrivano affatto. Il
raggruppamento si è spostato su `Ctrl`+`Shift`+`G` per lasciare a GoTo la
combinazione ovvia.

### Command palette — *Ricostruito*

Un comando è **dato, non callback**: la tabella `kCommandTable` è l'unico punto in
cui un comando viene dichiarato, `RunCommand` l'unico in cui viene eseguito.
Dichiarare ed eseguire nello stesso posto è comodo finché i comandi sono trenta e
diventa un problema quando sono trecento; la separazione è ciò che rende
sostituibile la tabella con una generata.

Le voci sono raggruppate e i gruppi si collassano, come nell'originale. Solo i
gruppi che la shell sa davvero eseguire sono presenti: `Tabs`, `Folder`, `View`,
`Sort`, `GroupBy`, `Text`, `Scroll`, più `Places`, che è generato dai percorsi
noti invece di avere un comando per ciascuno. Niente voci morte.

**Perché il componente riusabile non c'è ancora.** La roadmap lo prevede condiviso
fra palette, menu contestuale e GoTo. Estrarlo adesso significherebbe indovinare
la cucitura con un solo utilizzatore: si estrae quando il secondo rivela quali
parti sono davvero comuni — quali fra filtro, gruppi collassabili, navigazione e
disegno della banda — e quali sono specifiche del contesto. La palette è quindi
scritta in modo che l'estrazione sia un spostamento, non una riscrittura.

**Ereditarietà della palette.** Non eredita il filtro della lista: apre con la
propria casella vuota. Il filtro della lista risponde a "quali file", quello
della palette a "quale comando": sono due domande diverse e condividerne una
sarebbe un errore.

### Barra del titolo propria — *Ricostruito*

Il titolo nativo viene rimosso rispondendo a `WM_NCCALCSIZE` con zero: il client
copre l'intera finestra e il disegno della barra passa all'applicazione, che è il
presupposto dei tab. `WM_NCHITTEST` restituisce `HTCLIENT` su pulsanti e tab (che
devono restare cliccabili), `HTCAPTION` sul resto della striscia — il che regala
trascinamento, doppio click per massimizzare e snap senza scriverli — e i codici
di bordo per il ridimensionamento, che restano attivi solo quando la finestra non
è massimizzata.

**Il caso massimizzato è quello che si sbaglia.** Una finestra massimizzata è
gonfiata dal bordo di ridimensionamento: senza correzione il contenuto finisce
sotto il bordo dello schermo. `WM_NCCALCSIZE` rientra quindi il client della
dimensione del frame quando `IsZoomed` è vero.

**Stato per tab.** Il tab conserva cartella, voci, selezione, cursore,
scorrimento, filtro, ordinamento e raggruppamento. Il passaggio di tab salva lo
stato vivo nel tab da cui si esce e carica quello del tab in arrivo: il resto del
programma continua a lavorare su un solo insieme di campi e non ha mai bisogno di
sapere che i tab esistono. Le cache di anteprime e statistiche sono globali e
indicizzate per percorso, quindi sopravvivono al cambio senza lavoro.

*Nota:* il titolo del tab attivo viene dallo stato vivo, non dallo snapshot — lo
snapshot è vuoto mentre il tab è attivo, perché le sue voci sono state spostate
fuori. È un dettaglio che si vede subito: il tab dell'appena avviato programma
mostrava "This PC" al posto della cartella.

### Salire da una radice di volume — *Visto*

`Backspace` e `Alt`+`↑` salgono di un livello; da `C:\` il genitore di un percorso
non esiste, e sopra un disco c'e' **"This PC"**, la cartella di spazio dei nomi dove
si scelgono le unita'. Ci si arriva con
`ShellLocation::ParsingNameOf(FOLDERID_ComputerFolder)`, che non ha un CLSID scritto a
mano nel nostro codice.

L'etichetta e' **"This PC"** e non il "Questo PC" che la shell risponde in italiano:
e' il nome del riferimento (`AGENTS.md` §3), ed e' anche la voce che apre la sezione
`PLACES` della barra laterale, prima di `Home`.

Le tre vie — pulsante, `Backspace`, `Alt`+`↑` — eseguono lo stesso comando. Prima
`Backspace` ne aveva una sua, ed e' la ragione per cui §7.42 di `docs/02` esiste.

*Stima: 3–5 settimane.* È qui che va speso il tempo per unificare il componente
a gruppi, invece di scriverne tre.

---

## M7 — Operazioni su file 🟡

- [x] copia, sposta, rinomina, crea cartella (`IFileOperation`)
- [x] eliminazione verso il cestino reale, e definitiva con `Shift`+`Canc`
- [x] clipboard con formato shell (`CF_HDROP` + effetto preferito), quindi
      interoperabile con Esplora risorse nei due sensi
- [x] rinomina sul posto, con il campo disegnato sopra la riga
- [x] esito delle operazioni nella barra di stato
- [x] scorciatoie e comandi nella palette
- [x] dopo un incolla con conflitto, la selezione cade sull'elemento che la shell
      ha davvero creato, non su quello di partenza
- [x] le statistiche della cartella vengono invalidate dopo un'operazione
- [x] menu contestuale del tasto destro: si ancora al puntatore, alto quanto il
      contenuto, con separatori e navigazione da tastiera; la riga sotto il
      puntatore viene preselezionata
- [x] **Proprietà**, in due voci: `Proprietà` apre il **pannello in-app** (le stesse
      righe dell'inspector, in una finestra nostra), `Proprietà di Windows` apre
      ancora la finestra vera di Windows via `ShellExecuteEx`, per ciò che solo la
      shell sa (permessi, versioni precedenti, pagine per tipo di file)
- [x] le voci di **primo livello** della shell dentro il menu contestuale (Apri con,
      le voci delle estensioni installate), dopo i nostri comandi e dopo un
      separatore. La strada è quella del binario originale —
      `IShellFolder::GetUIObjectOf` → `IContextMenu` → `QueryContextMenu` →
      `GetCommandString(GCS_VERBW)` → `InvokeCommand`: vedi sotto
- [x] **ripristino dal cestino** e **drag&drop in ingresso e in uscita**: erano qui
      come "da fare" e sono stati fatti — le voci qui sotto lo dicono una per una,
      con il metodo con cui sono state verificate. Erano due righe rimaste indietro,
      e un elenco che dice "da fare" di una cosa fatta manda a lavorare su niente
- [x] il menu contestuale sa **annidare**: pannelli a destra del pannello padre,
      che si aprono col sovrappasso o con la freccia destra, scorrono, si
      richiudono con `Esc` e con la freccia sinistra, e portano il `>` sulle voci
      che ne hanno uno. I sottomenu delle **estensioni** (`7-Zip`, `WinRAR`) si
      aprono e si eseguono: verificato vedendo comparire il dialogo di 7-Zip
- [x] `Nuovo file >`, fatto da noi: i tipi si leggono dalle chiavi **`ShellNew`**
      del registro e la creazione passa da `IFileOperation::NewItem`, che sceglie il
      nome definitivo (e il `(2)` dei conflitti). Il **contenuto** però lo
      scriviamo noi: `NewItem` senza un modello crea un file di zero byte, e uno
      zip di zero byte non è uno zip. Vedi sotto
- [x] `Invia a >`, fatto da noi: la cartella SendTo è una cartella vera, quindi si
      legge (`%APPDATA%\Microsoft\Windows\SendTo`, 11 voci su questa macchina in
      0,3 ms) e si esegue **lasciandoci cadere sopra la selezione**, che è quello che
      fa la shell. Non con `ShellExecuteEx`: vedi §7.22 di `docs/02`
- [x] **drag&drop dentro e fuori**, con OLE: file trascinati da Esplora risorse
      dentro la lista (su una riga di cartella o sul fondo), file trascinati da
      Bolt in un'altra finestra, Ctrl copia / Shift sposta / stesso volume sposta,
      destinazione tratteggiata durante il gesto, e i casi vietati che non fanno
      niente invece di fare qualcosa di sbagliato (§7.23 e §7.24 di `docs/02`)
- [x] **trascinare sul riquadro di navigazione**: le sue righe sono bersagli come
      le altre, quindi `TargetAt` chiede la geometria a `HitTest` invece di
      rifarla. E la destinazione non è sempre una cartella: sul **cestino** il verbo
      è *buttare via*, non *mettere dentro* — non si copia nel cestino, e Ctrl non
      trasforma il gesto in una copia. Vedi §7.33 di `docs/02`
- [x] **trascinare *fuori* da una cartella di spazio dei nomi**: le voci portano
      l'oggetto dati della shell invece di un nome, e l'oggetto lo chiede **la
      cartella che le possiede** — montarlo a mano lo faceva accettare e non fare
      niente (§7.34 di `docs/02`). Verificato per una cartella normale **e per una
      voce riciclata**: trascinata fuori dal cestino in una cartella si **ripristina**
      lì (28 byte, contenuto identico, sparita dal cestino), che è quello che fa anche
      Esplora risorse. Visto il 2026-10-17, dopo due giri in cui il banco diceva il
      contrario: vedi §7.34
- [x] **navigazione nello spazio dei nomi** e **ripristino dal cestino**: il
      cestino è un collegamento nella barra laterale, si apre come una cartella,
      e "Ripristina" è il verbo della shell sulla voce — che esiste solo se alla
      shell si passa il **PIDL** e non il nome (§7.25 di `docs/02`). Le voci che
      vogliono un percorso sono spente, e si vede
- [ ] i modelli col valore **`Command`** restano fuori dal menu `Nuovo file`:
      quel valore avvia un'applicazione invece di creare un file
      (`msaccess.exe /NEWDB 1`, `Wab.exe /CreateContact "%1"`), e imitarlo
      significherebbe eseguire righe di registro arbitrarie
- [x] **la rinomina seleziona il nome senza estensione**, come Esplora risorse:
      `F2` apre il campo con la parte da riscrivere **evidenziata** (tutto il nome
      per una cartella), e il primo carattere la sostituisce invece di accodarsi.
      Perche' serviva un cursore e non bastava togliere l'estensione dal campo: il
      campo scriveva solo **in coda**, quindi `nota` + `promemoria` faceva
      `notapromemoria` — e con l'estensione levata e rimessa alla conferma il
      risultato era `.txtpromemoria`, il nome nuovo **dopo** l'estensione. Adesso
      `renameCaret` dice dove si inserisce, `renameSelectEnd` quanta parte e'
      selezionata, e il `Backspace` cammina indietro di **un carattere** (non di un
      byte: un accento ne occupa due). Verificato: `nota.txt` + `F2` + `promemoria` +
      `Invio` → il file sul disco si chiama `promemoria.txt`
      (`analysis/tmp/rename-select.png`, `rename-typed2.png`)
- [x] il menu della **cartella**, quando non c'è selezione, cioè il tasto destro
      sul fondo della lista: è il menu della *vista*, chiesto con
      `IShellFolder::CreateViewObject(IID_IContextMenu)`, **non** quello della
      cartella come elemento. Il clic destro sul fondo azzera la selezione, come
      in Esplora risorse, e da lì arrivano `Proprietà`, `Apri nel Terminale`,
      `Apri con …` e le voci delle estensioni
- [x] **dialogo di progressione con annullamento proprio**: copia, spostamento e
      cestino girano su un **thread di lavoro** con un conto che si muove e un
      pulsante che ferma davvero (`FileOps::Job`, sotto)

### Il dialogo di progressione, e cosa la shell non fa — *Ricostruito*

Una copia da 5 GB non è un'attesa: è qualcosa che si guarda e che si può fermare.
Prima non si poteva né l'una né l'altra cosa — l'operazione girava **sincrona sul
thread della finestra**, che restava ferma — e le tre cose che mancavano erano un
conto, un numero che si muove e un pulsante.

Adesso `FileOps::Job` esegue copia, spostamento e cestino su un **thread di lavoro**
(COM `MTA`: su quel thread nessuno pompa messaggi, e un appartamento ad attesa
singola che aspetta una chiamata che nessuno consegna è un blocco), e il pannello
disegnato da noi prende il posto della finestra della shell, spenta con
`FOF_SILENT`. Le tre operazioni non hanno più un finale ciascuna: l'esito si legge in
un posto solo (`FinishOperation`) quando `Running()` diventa falso, e il registro
(`BOLT_DIAG=1`) porta una riga per ogni operazione.

Due cose la shell non le dà, ed entrambe si sono dovute misurare:

- **Non c'è un `Abort`.** `IFileOperation` non ha un metodo per fermarsi: chi annulla
  è il **sink**, che risponde con un codice d'errore. Il documento dice che un errore
  da `Pre*Item` annulla l'operazione **e tutte quelle in coda**; `UpdateProgress`
  passa di lì anche a metà di un file solo, ed è l'unico punto da cui fermare una
  copia da 5 GB prima della sua fine. Verificato: annullata a metà di un file da
  4 GB, la shell **cancella da sola il file parziale** che aveva creato.
- **I "punti" di `UpdateProgress` non sono byte.** Misurati con `BOLT_DIAG=1`: un
  file da 4 GB sono **131074 punti** (32 KB l'uno), un file da 23 byte sono **2
  punti**. La frazione è una frazione, e moltiplicata per i byte che `Job` misura
  prima di partire dà un conto **esatto per un file solo** e interpolato su tante
  voci piccole (i punti sono "proporzionali al tempo", non ai byte). Il conto vero
  si legge nella stessa riga del registro, così la differenza si vede invece di
  supporla.

Il pannello compare solo se l'operazione sta ancora girando dopo 0,35 s: una copia di
due file piccoli finisce prima, e un pannello che compare per tre fotogrammi è
peggio di nessun pannello. `Esc` e il pulsante `Annulla` chiedono la stessa cosa, e
finché la shell non ha davvero smesso il pannello lo dice.

Una cosa che l'asincrono ha reso possibile e che va **rifiutata**: **sostituire**
l'operazione in corso con la seconda. Sostituirla vuol dire annullarla senza dirlo, e per
una caduta è peggio — chi ha mollato il tasto legge dalla destinazione se l'operazione è
riuscita, e su `DROPEFFECT_MOVE` cancella gli originali. La seconda non sostituisce: va
**in coda** (`docs/02` §7.69), e la caduta dichiara l'effetto vero (`COPY`/`MOVE`), non
`NONE`: una richiesta accettata è una richiesta che sarà fatta. **Resta dichiarato**
l'annullamento che dipende dal fatto che la shell legga il codice d'errore del sink, che è
documentato per i `Pre*Item` e osservato per `UpdateProgress`. **Il conto dei byte non è
più interpolato** (§7.66): le voci finite valgono i loro byte misurati, i punti
interpolano solo dentro la voce in corso — e il numero delle voci conta anche la cartella
di partenza, come fa la shell (§7.71).

Verificato a schermo e contro disco, nella sandbox: il conto che si muove dentro un
file solo (938 MB → 1,8 GB sulla stessa copia), l'annullamento col tasto **e** col
pulsante (nessun file parziale rimasto), un incolla normale (`nota - Copia.txt`
creato e selezionato), un'eliminazione nel cestino, e una caduta su una riga. I
riscontri sono in `analysis/tmp/op*.png`.

### Le proprietà, in-app e di Windows — *Ricostruito*

`Proprietà` non apre più la finestra di Windows: apre un **pannello nostro**, modale,
con le stesse righe che mostra l'inspector. E' la scelta di chi usa il programma —
il dialogo di sistema è modale, bianco e fuori dal tema — e **non** quella di
rinunciare a quello di sistema, che resta nella voce accanto.

Due voci, perché sono due cose diverse:

- **`Proprietà`** — il pannello in-app. Le righe le costruisce `BuildInspectorRows`,
  **la stessa funzione dell'inspector**: due viste della stessa cosa con un solo
  calcolo, o prima o poi mostrerebbero numeri diversi. Per una selezione multipla
  mostra l'aggregato (conteggio, somma, tipi), come l'inspector.
- **`Proprietà di Windows`** — la finestra di sistema via `ShellExecuteEx`. E'
  l'unico posto per **Sicurezza/permessi, Versioni precedenti, Compatibilità**, le
  pagine registrate dai gestori dei tipi di file, e la **scrittura**. Il pannello
  in-app è in sola lettura: rifare quelle pagine sarebbe un altro mestiere, e
  peggiore.

Il pannello è modale come un menu: `Su`/`Giù`/`PagSu`/`PagGiù`/`Home`/`Fine`
scorrono, la rotella scorre, `Esc` chiude, un clic fuori chiude. In fondo ha il
pulsante **`Proprietà di Windows...`**, perché la voce che apre la shell non sparisca.

### Il menu contestuale non è un `GroupMenu` — *Scelta*

Condividono l'idea di "una lista di righe etichettate", ma differiscono in tutto
il resto: il menu contestuale si ancora al puntatore invece di stare al centro,
è alto quanto il contenuto invece di 420 px fissi, ha **separatori** e non bande,
e non ha né filtro né piede. Forzarlo dentro `GroupMenu` avrebbe voluto dire un
campo "modalità" e due impaginazioni dentro lo stesso componente.

Il menu **non aggiunge capacità**: riusa i comandi che già esistono e li rende
raggiungibili dove servono.

### I verbi della shell nel menu contestuale — *Ricostruito*

La shell sa fare cose che non sono nostre: `Apri con`, `Invia a`, i verbi delle
estensioni installate (`Converti a Adobe PDF`, `Modifica nel Blocco note`,
`Ripristina versioni precedenti`, `Copia come percorso`). Non si reimplementa
niente. Il binario originale fa la stessa cosa — `Win_GetIShellFolder`,
`Win_GetUIObjectOfPaths`, `P_GetContextMenu`, `Win_ContextMenuBuild`,
`P_ContextMenuInvoke`, e la stringa `GCS_VERBW` — quindi la strada è quella:

```
SHParseDisplayName → SHBindToParent(IID_IShellFolder) → GetUIObjectOf(IID_IContextMenu)
   → QueryContextMenu in un HMENU che non mostriamo mai → GetMenuItemInfoW / GetCommandString
   → InvokeCommand(CMINVOKECOMMANDINFOEX con lpVerbW = MAKEINTRESOURCE(id - kShellCmdFirst))
```

**L'HMENU non si mostra.** Chi disegna resta il nostro renderer, quindi il menu
resta coerente con il resto della finestra; l'HMENU serve solo a leggere le voci,
un po' come il PDB serviva solo a leggere i nomi. Vive quanto l'`IContextMenu` e i
due si rilasciano insieme alla chiusura: verificato contando gli **oggetti USER**
del processo, che non crescono (41 → 40 su cinque aperture), mentre se l'HMENU
restasse appeso crescerebbe di uno per tasto destro.

**Gli id della shell vivono in una banda riservata** (`0x6000`–`0x6FFF`): dentro
quella banda un id è un indice per `IContextMenu`, fuori è un nostro `CommandId`.
Così una voce sola può essere eseguita da chi la sa eseguire, senza un campo in
più che dica di chi è.

**Le voci che offriamo già si tolgono per verbo canonico, non per etichetta**:
`open`, `cut`, `copy`, `paste`, `delete`, `rename`, `properties`. L'etichetta è
tradotta e cambia con la lingua di Windows, il verbo no. Senza questo filtro il
menu avrebbe due `Taglia` e tre `Proprietà`. `openas` **non** è nell'elenco: è
proprio uno dei motivi per cui la shell viene interrogata.

**Le voci che la shell offre spente si mostrano spente.** Una selezione mista non
è una ragione per nascondere che quel verbo esiste: le frecce le saltano e l'Invio
non le esegue, ma la voce resta leggibile.

### Le icone delle voci della shell — *fatte il 2026-10-20*

Le voci che vengono dalla shell portano l'icona del programma che le offre, come in
Esplora risorse. Due fonti, e nessuna delle due e' un'ipotesi:

1. **La bitmap della voce.** La shell (o l'estensione) puo' mettere un `hbmpItem` nel
   `MENUITEMINFO`: si legge con `MIIM_BITMAP`, si converte in RGBA (`ReadBitmap`, che
   c'era gia' per le icone) e si carica in una celletta dell'atlante. E' di proprieta'
   dell'HMENU, quindi si legge subito, alla prima apertura, e poi non serve piu'.
2. **La specifica del registro.** Un'applicazione che registra un verbo dichiara la
   propria icona in un valore `Icon` sotto la chiave del verbo — la forma e'
   `percorso,indice` dell'icona dentro il file, e l'indice puo' essere negativo.
   `VerbIconSpec` guarda `*\shell`, `AllFilesystemObjects`, `Directory`,
   `Directory\Background`, `Folder` e `Drive`, prima in `HKCU` e poi in `HKLM`, e si
   ferma alla prima che risponde.

Se nessuna delle due c'e', la voce resta **senza icona**: non si ripiega sul nome
dell'applicazione, perche' un'icona sbagliata e' peggio di nessuna icona. La colonna
compare **per livello** — se almeno una voce del livello ne ha una, tutte le etichette
si spostano di 20 px — perche' un menu con l'icona su otto righe e il testo a due
rientri si legge peggio di un menu senza icone. L'icona e' 16 px, centrata sulla riga.
L'atlante delle icone non ha sfratto — un'icona per file lo riempirebbe — ma qui
l'insieme e' **chiuso**: i verbi di una macchina sono qualche decina, e le icone di
menu si caricano una volta per verbo, non una per apertura.

*Limite dichiarato: le voci del **menu moderno di Windows 11** non consegnano la
bitmap a chi legge il menu classico, e non dichiarano un `Icon` nel registro — la loro
icona sta nel manifest del pacchetto, che `IContextMenu` non espone.* Misurato sulle
voci di questa macchina: `7-Zip` resta senza icona (verbo `SevenZip`, nessun
`hbmpItem`, nessuna chiave `Icon` in nessuna delle sei radici, nessun verbo per
estensione), mentre `WinRAR` e `Modifica nel Blocco note` **hanno** la bitmap e quindi
l'icona. Le voci senza verbo (`Analizza con Microsoft Defender…`, `Combina file in
Acrobat…`) arrivano come ownerdraw e restano senza: si potrebbe chiedere al gestore di
disegnarle con `IContextMenu3::HandleMenuMsg2`, ma vuol dire lasciar disegnare il menu
a lui, e questo menu lo disegna il nostro renderer.

Visto a schermo in Direct3D 11 e in OpenGL (`BOLT_BACKEND=opengl`): icone su
`Aggiungi alla scaletta del lettore multimediale VLC`, `Apri con Visual Studio`,
`Open Git GUI here`, `Open Git Bash here`, `Riproduci con VLC`, `WinRAR`, `Modifica nel
Blocco note`, `Apri con Code`, `Edit with PyCharm`; niente su `7-Zip`, `Apri con`,
`Copia come percorso`, `Crea collegamento`. Catture: `analysis/tmp/menu-icone.png`,
`menu-icone-file.png`, `menu-icone-opengl.png`.

**I comandi dell'applicazione originale non si offrono.** Su una macchina dove File
Pilot e' installato, la sua estensione di shell aggiunge `Open in File Pilot`
(`OpenInFilePilot`, sulla selezione) e `File Pilot here` (`FilePilotHere`, sullo sfondo
della cartella). Non sono funzioni di Bolt: aprono la stessa cartella **in un altro
programma**, e dentro un clone si leggono come nostre. Si filtrano in
`ReadShellMenu` — cioe' dove la shell diventa elenco, un livello alla volta, quindi
anche nei sottomenu — confrontando il **verbo canonico**, che e' il nome stabile;
l'etichetta si guarda solo come ripiego per le voci ownerdraw senza verbo. Non si
filtrano invece `Apri con` (elenco dei programmi installati: e' Windows che parla) e
`Invia a` (una cartella letta a ogni apertura: un collegamento li' e' un file di chi
usa il programma, e un menu costruito su una cartella non ne nasconde il contenuto).
Fatto il 2026-10-20, su richiesta dell'autore: `docs/02` §7.62

**Il fondo della cartella è un altro contesto, e chiederlo male non dà errore.**
Con niente selezionato il menu della vista si chiede con
`CreateViewObject(IID_IContextMenu)`; chiedere invece la cartella *come elemento*
(`GetUIObjectOf`) restituisce un menu plausibile e pericoloso: `Taglia`, `Copia`,
`Elimina` **sulla cartella aperta**. Il registro l'ha mostrato — sei voci con
`cut`, `copy`, `delete` che nessuno si aspetta a destra di un fondo. Vedi §7.18 di
`docs/02`.

**I sottomenu hanno due proprietari, e solo uno si può servire.** Un sottomenu di
un'**estensione** è disegnato da lei e i suoi comandi si eseguono con
`InvokeCommand`, come le voci radice: verificato col dialogo di 7-Zip. Un
sottomenu che la shell **popola da sé** (`Nuovo`, `Invia a`, `Dare accesso a`) si
legge benissimo — `Nuovo` ne ha venti — ma l'esecuzione risponde `S_OK` e non fa
niente, perché la shell consegna quei comandi alla finestra del menu.

Quindi: il componente annida, i sottomenu delle estensioni si offrono, quelli
della shell si filtrano per verbo canonico. Vedi `docs/02` §7.19. Un elenco che
risponde `S_OK` e non fa niente è peggio di un elenco che manca.

### Il menu "Nuovo file", fatto da noi — *Ricostruito*

La shell il proprio "Nuovo" lo popola in un sottomenu i cui comandi non si
possono eseguire da fuori (vedi sopra), quindi il menu se lo costruisce Bolt. Le
fonti sono le stesse che usa la shell, e sono **tre**, non una:

| dove sta `ShellNew` | esempio |
|---|---|
| sotto la chiave dell'estensione | `.zip\ShellNew` (WinRAR) |
| sotto un ProgID **annidato** nella chiave | `.docx\Word.Document.12\ShellNew` |
| sotto il ProgID di default dell'estensione | `MQL5.File\ShellNew` |

La seconda è la più usata su Windows 11 e senza di essa il menu avrebbe un quinto
delle voci: sul banco di prova, 18 modelli invece di 4. Sull'elenco del registro,
la **shell non offre nessuna API**: il menu lo costruisce internamente, e le
chiavi vanno lette a mano (`src/shell_new.cpp`).

**Le etichette sono stringhe indirette** (`@C:\WINDOWS\system32\zipfldr.dll,-10194`)
e si risolvono con `SHLoadIndirectString` — non con `RegLoadMUIString`, che sulla
stessa stringa restituisce un errore e lascia il menu in **inglese** mentre tutto
il resto è in italiano. Vedi §7.20 di `docs/02`.

**Il contenuto lo scriviamo noi.** `IFileOperation::NewItem` sceglie il nome
definitivo — è l'unico modo di avere il `(2)` dei conflitti senza indovinarlo —
ma senza un modello crea un file di zero byte. I valori del registro sono tre:
`NullFile` (vuoto), `FileName` (copia di un file modello) e `Data` (i byte).
Verificati sul banco di prova: `Nuova cartella compressa.zip` esce di **22 byte** e
si apre come zip valido, `Nuovo foglio di lavoro di Microsoft Excel.xlsx` di
**6.111 byte**, cioè la copia vera del modello di Office.

Due cose che la verifica ha scoperto e che nessuna lettura avrebbe dato: i modelli
di Office e di LibreOffice sono **gusci di zero byte** su disco (il contenuto lo
produce il loro provider, che da fuori non si raggiunge), e il valore `Data` di
`.rtf` è una **stringa** `{\rtf1}` e non un blocco binario. Il primo caso adesso
si dichiara all'utente invece di consegnargli un `.docx` che Word rifiuta di
aprire (§7.21 di `docs/02`); il secondo si accetta in tutte e due le forme.

**I modelli col valore `Command` restano fuori.** Quel valore non crea un file:
avvia un programma. La lettura del registro, quando serve, costa **meno di un
millisecondo** (misurata: 0,7 ms per 20 modelli) e avviene alla prima apertura del
sottomenu, non a ogni tasto destro.

### Il menu "Invia a", fatto da noi — *Ricostruito*

Qui la fonte non è nascosta come le chiavi `ShellNew`: **"Invia a" è una cartella
vera**, `%APPDATA%\Microsoft\Windows\SendTo`, e un file lì dentro è una
 destinazione. Si legge a ogni apertura, non una volta sola come il registro,
perché chi ci mette dentro un collegamento nuovo si aspetta di vederlo; il costo è
una `FindFirstFile` su una manciata di file — misurato: **0,25–0,32 ms** per 11
voci. L'etichetta è il nome del file senza estensione, come in Esplora risorse,
quindi si possono vedere due lingue nella stessa lista: i file sono stati creati
in momenti e installazioni diverse, e la shell mostra gli stessi nomi.

**Eseguirla, invece, non è avviarla.** La voce "cartella compressa" della cartella
SendTo non è un programma: è un file `.ZFSendToTarget` che contiene un CLSID, e il
suo handler è registrato come `DropHandler` — il registro lo dice con le parole
*"Drag Files onto this icon to compress them"*. `ShellExecuteEx` su quella voce
restituisce `TRUE` e non fa niente: nessun errore, nessun file, che è la peggiore
delle risposte possibili (§7.22 di `docs/02`).

Si fa quindi quello che farebbe il mouse: si prende il **bersaglio di caduta**
della voce (`IShellFolder::GetUIObjectOf` con `IID_IDropTarget`) e gli si lascia
cadere sopra la selezione, come `IDataObject` costruito con `SHCreateDataObject`
dai PIDL dei percorsi scelti. `DROPEFFECT_COPY`, perché "invia a" manda una copia e
non porta via l'originale. Vale per tutte le voci della cartella, compresi i
collegamenti ai programmi, che si aprono con i file ricevuti. Verificato da capo a
fondo: la voce che comprime ha prodotto **`relazione.zip` dentro la sandbox** (218
byte, con `relazione.txt` dentro), e la barra di stato ha scritto *"Avviato:
…"*.

"Avviato" e non "fatto", perché la destinazione comprime, copia o apre una
finestra sua e il suo esito non è nostro da dichiarare; se invece la voce non
accetta i file, lo dice.

### Il trascinamento, che è di OLE — *Ricostruito*

Trascinare fra due programmi non è un accordo fra due programmi: è un **oggetto
dati** che il sistema consegna alla finestra sotto il puntatore. Da qui due metà,
e servono tutte e due:

| verso | interfaccia | chi la fornisce |
|---|---|---|
| in entrata | `IDropTarget` registrato con `RegisterDragDrop` | noi, sulla nostra finestra |
| in uscita | `IDropSource` + `DoDragDrop` | noi, quando il mouse si muove con il tasto giù |

L'oggetto dati è quello della shell (`SHCreateDataObject`, lo stesso che scrive
Esplora risorse), quindi una cartella o un altro file manager lo accettano senza
sapere chi gliel'ha mandato. Il modulo che se ne occupa è `src/drag_drop.cpp`; la
destinazione sotto il puntatore la decide `main.cpp`, perché solo lì si sa cosa
c'è sotto — una riga di cartella, o il fondo della lista (che vale la cartella
aperta).

**La regola copia/sposta è quella di Esplora risorse**, e sono tre casi: Ctrl
copia, Shift sposta, e senza tasti si guarda il volume — stesso volume sposta,
volume diverso copia. Un file trascinato nella cartella dove è già non si sposta:
lì non c'è niente da fare, e il puntatore lo dice.

**L'evidenziazione della destinazione si disegna da un callback di OLE.** Durante
un trascinamento che nasce da qui il ciclo principale è fermo dentro `DoDragDrop`,
quindi la riga non si accenderebbe mai: il ciclo modale di OLE consegna però i
messaggi, e un fotogramma disegnato quando la destinazione **cambia** basta. È
`RenderFrame`, l'unico posto in cui si disegna fuori dal ciclo.

**Quattro cose misurate**, tutte con `BOLT_DIAG=1` → `bolt-diag.log`:

- un trascinamento in entrata arriva con i percorsi già pronti (`1 percorsi`);
- un trascinamento **in uscita** verso Esplora risorse: `effetto restituito 0,
  esito dichiarato 2` — la destinazione sposta e lo dichiara in un formato
  dell'oggetto dati, non nel valore di ritorno (§7.23 di `docs/02`);
- un trascinamento rifiutato: `esito dichiarato 0`, e la cartella resta identica
  (§7.24);
- la vista si rifà da sola dopo uno spostamento, senza premere F5.

Prove fatte, con `tools/drag-mouse.ps1` (preme, muove a passi, **fotografa a metà
gesto**, rilascia) e `tools/explorer-side.ps1`:

| prova | esito |
|---|---|
| `nota.txt` da Bolt sulla riga della cartella `documenti` | spostato, riga tratteggiata a metà gesto |
| `relazione.txt` da Esplora risorse al fondo della lista di Bolt | spostato, tratteggio sul bordo della lista |
| `contratto.txt` da Bolt a Esplora risorse | spostato dalla shell, vista rifatta da sola |
| un file sul fondo della **propria** cartella | rifiutato: nessun `(2)`, registro `esito dichiarato 0` |

### Le cartelle che non sono percorsi — *Ricostruito*

Il cestino e "Questo PC" non hanno un percorso: si raggiungono con un nome che
comincia per `::`, e si aprono con `SHParseDisplayName`. Il CLSID non è scritto a
mano nel codice — si chiede con `SHGetKnownFolderIDList` e
`SHGetNameFromIDList` a chi lo sa.

```cpp
SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, ...)  → PIDL
SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING) → "::{645FF040-…}"
```

Le voci si enumerano con `IShellFolder::EnumObjects` e diventano `Entry` normali,
con le stesse funzioni di formattazione delle altre: **così lista, ordinamento,
filtro e gruppi non hanno dovuto sapere niente** di nuovo. Da qui in poi una voce
del cestino è una voce come le altre.

Due cose non si potevano indovinare:

- **il nome** di queste cartelle non lo sa l'oggetto. Per "Questo PC" risponde col
  nome del computer; il nome vero sta in `HKCR\CLSID\{…}\LocalizedString`, la
  stessa stringa indiretta delle etichette del menu "Nuovo" (§7.26 di `docs/02`);
- **l'identità** di una voce è il suo PIDL, non il suo nome. Il nome di un file nel
  cestino è il percorso del file *dentro* `$Recycle.Bin`: chiedendo il menu di quel
  nome la shell risponde col menu di quel file, senza alcun errore (§7.25).

**Cosa non si fa, e si vede.** Dentro una cartella di spazio dei nomi le voci che
vogliono un percorso — Taglia, Copia, Rinomina, Elimina, Incolla, Nuova cartella,
Nuovo file, Invia a, terminale, Proprietà — sono **spente** nel menu contestuale:
li' le voci si gestiscono coi verbi della shell, che quegli oggetti li conoscono.
Non si trascina fuori da li' (l'oggetto dati si costruisce dai nomi, e sarebbero i
nomi sbagliati). **In una cartella riciclata invece si entra** dal 2026-10-18: la sua
posizione e' il PIDL codificato (`shell:pidl:…`, `ShellLocation::EncodePidl`), perche'
il nome assoluto di una cartella riciclata punta al file riciclato. Vedi la casella
in fondo a questa sezione.

**Verificato** (con un file della sandbox messo nel cestino e poi filtrato):

| prova | esito |
|---|---|
| "Questo PC" aperto dalla riga di comando | due unità, "Questo PC" in briciola e nel titolo della scheda |
| entrare in un'unità da "Questo PC" | briciola `C: >` |
| cestino dalla barra laterale | si apre, si chiama "Cestino", colonne Tipo e Data compilate |
| menu contestuale su un file riciclato | **`Ripristina` verbo `undelete`**, e le nostre voci spente |
| "Ripristina" eseguito | il file torna nella sandbox col contenuto intatto |
| filtro attivo, poi F5 | il filtro resta e la lista resta filtrata (§7.28) |
| **entrare in una cartella riciclata** | si apre e mostra il suo contenuto; `Alt+Su` torna al cestino |

**Quattro costi misurati** (registro: `BOLT_DIAG=1` → `bolt-diag.log`):

Due note sull'elenco, perché sono differenze misurate rispetto al menu della
shell e non difetti noti:

- *"Documento di testo" la shell lo conosce da sé.* Nessuna delle 37 chiavi
  `ShellNew` di questa macchina nomina `.txt`, eppure Esplora risorse lo offre. La
  voce si aggiunge a mano, con l'etichetta chiesta a `SHGetFileInfo` (la stessa
  fonte della colonna "Tipo", quindi tradotta). Un `.txt` vuoto è esattamente
  quello che si vuole.
- *"Immagine bitmap" invece non si aggiunge.* Quella la shell la crea con un
  contenuto suo che il registro non dichiara: la nostra sarebbe un file di zero
  byte con l'estensione `.bmp`, cioè un'immagine che nessun programma apre.
  Meglio nessuna voce di una voce che non funziona.

**Quattro costi misurati** (registro: `BOLT_DIAG=1` → `bolt-diag.log`):

| misura | valore | lettura |
|---|---|---|
| prima apertura dopo l'avvio | ~350 ms | le DLL delle estensioni si caricano in-process |
| regime | ~65 ms | il costo di interrogare i gestori registrati, a ogni tasto destro |
| fondo della cartella | ~220 ms la prima volta | `CreateViewObject` carica un altro giro di gestori |
| handle di kernel | +22 per apertura — **una perdita, non un costo** | non tornavano mai giu': chiusa in §7.38 di `docs/02` con una memoria per selezione |
| estensioni moderne Win11 | dalla **seconda** apertura | le voci dei gestori moderni (`Apri nel Terminale`, `Apri con Code`) si registrano solo al secondo giro |

L'ultimo è un limite dichiarato, non risolto: non dipende da noi.

**La scelta è stata fatta** (§7.38 di `docs/02`): la shell si chiede **una volta per
selezione** e il risultato si tiene in memoria per 30 secondi. Il tempo di una
selezione nuova resta (~65 ms), ma aprirlo due volte non lo paga due volte — e
soprattutto la perdita di handle è sparita: dieci menu di fila adesso lasciano il
contatore fermo, sia sulla stessa selezione sia su dieci righe diverse. La scadenza
di 30 secondi non è un numero a caso: la prima interrogazione della sessione
risponde un elenco **incompleto** (21 voci contro 24) perché in Windows 11 i gestori
moderni si registrano al secondo giro.

**Difetto trovato contando le frecce.** Con otto pressioni la selezione si
fermava su "Incolla qui" invece che su "Proprietà". `ContextMenuMove` partiva da
0 quando niente era selezionato e poi incrementava dentro il ciclo, quindi **la
prima freccia sceglieva la seconda voce**. Guardando il menu il difetto non si
vede: si è visto solo contando quante pressioni servivano, perché il numero non
tornava di uno.

### Perché il ripristino dal cestino non è una casella da spuntare

Non è pigrizia: è che **il cestino non ha un percorso**. La nostra navigazione è
basata sui percorsi — `ReadDirectory` + `FindFirstFile` — e il cestino è una
cartella dello *spazio dei nomi della shell*, fatta di PIDL, non di file.

`C:\$Recycle.Bin` esiste come percorso, ma contiene le coppie `$I…`/`$R…`: i nomi
originali non ci sono, e mostrarli così sarebbe peggio che non mostrarli. Per
ripristinare bisogna (a) saper navigare lo spazio dei nomi, che è una **seconda
fonte di elenco**, e (b) invocare il verbo "Ripristina" del menu contestuale, il
cui nome è **localizzato** — su Windows italiano è "Ripristina", e sbagliare
verbo sul cestino significa cancellare davvero.

Quindi: la voce resta aperta, e la sua dipendenza vera è **il browsing dello
spazio dei nomi**, cioè un pezzo di M7 che oggi non c'è. Farlo prima di quello
sarebbe un imballaggio sopra una funzione che manca.

### Difetti trovati provando — *Ricostruito*

**I conflitti di nome bloccavano l'applicazione in silenzio.** Con
`FOF_NOCONFIRMATION` e senza `FOF_RENAMEONCOLLISION`, incollare un file dove ne
esisteva gia' uno non produceva ne' una domanda ne' un rinomina: l'operazione
veniva saltata, e la shell lasciava una finestra di stato modale
(`OperationStatusWindow`) che non si vedeva, perche' stava dietro la finestra
principale, ma che teneva il programma dentro il proprio ciclo di messaggi.
L'applicazione **sembrava congelata, e il difetto sembrava di rendering**: due
catture a due secondi di distanza erano identiche al pixel.

La diagnosi e' arrivata enumerando le finestre del processo, non guardando lo
schermo: la finestra che bloccava non era visibile. E' lo stesso errore di metodo
del §5 di `docs/05` — guardare il sintomo invece della causa.

**La selezione cadeva sul file sbagliato dopo un incolla con conflitto.**
Risolto con un `IFileOperationProgressSink`: i metodi `Post*` ricevono l'elemento
appena creato, ed è l'unico punto in cui il nome scelto dalla shell è
osservabile. Verificato: dopo l'incolla il cursore sta su `alfa - Copia.txt` e
non su `alfa.txt`. Prima, un `Canc` subito dopo avrebbe eliminato l'originale.

Lo stesso sink ha permesso di togliere un'euristica: `CreateFolder` indovinava il
nome finale leggendo il disco e provando i suffissi `(2)`, `(3)`, `(4)` in un
ciclo. Adesso il nome lo riporta la shell. Un valore derivato al posto di uno
indovinato, di nuovo.

**Le statistiche delle sottocartelle restavano vecchie.** Le colonne
`Folders`/`Files`/`Size` di una cartella si vedono dal suo genitore, quindi è lì
che si nota il numero sbagliato. Dopo ogni operazione ora si invalida la cartella
in cui si è lavorato. Verificato: `una-cartella` mostrava `0 | 0 | 0 B`, dopo aver
incollato un file al suo interno e essere risaliti mostra `0 | 1 | 5 B`.

*Stima originale: 4–6 settimane.* Non usare `SHFileOperation`: è deprecata.

---

## M8 — Inspector ✅

- [x] metadati di base: tipo, dimensione, **creazione, modifica, accesso**, percorso
- [x] proprietà che il file system non dice, lette da `IPropertyStore`:
      **dimensioni dell'immagine** (`64 x 64 px`), e per i **volumi** il file system
      e lo spazio libero
- [x] selezione multipla: conteggio per tipo ("3 Documento di testo, 2 Cartella di
      file, 1 Excel.CSV"), somma delle dimensioni, cartelle e file
- [x] anteprima del file selezionato nella sua cella grande
- [x] **visualizzatore di testo**: righe con numeri, **minimap** con la parte
      visibile, **spazi visibili** (un punto per uno spazio, una freccia per una
      tabulazione), andare a capo, ingrandimento, e la rotella che scorre il testo
      (con Ctrl lo ingrandisce). `InspectorShowWhitespace`,
      `InspectorHideLineNumbers`, `InspectorHideMinimap`, `InspectorWordWrap`,
      `InspectorZoomLevel`
- [x] rilevamento della codifica (UTF-8, UTF-8 con BOM, UTF-16 LE e BE, ANSI) e
      conteggio delle righe, con i terminatori (`CRLF`, `LF`, `CR`, `misto`)
- [x] **anteprima immagine con zoom e adattamento**: il file si decodifica davvero
      (WIC, quindi JPEG, PNG, BMP, GIF, TIFF e i codec installati) e si carica in un
      **sesto strato** dell'array, da 1024 px. Niente secondo campionatore e niente
      ramo nello shader: ingrandire è geometria di un quad
- [x] le 20 etichette `PanelInspector_Label*`: percorso originale e data di
      eliminazione arrivano dalle **colonne della shell**, con i nomi che usa lei —
      per una voce di spazio dei nomi l'inspector mostra *le sue* colonne, che sono
      la sua vista di quell'oggetto, invece di ripetere metà delle nostre
- [x] **`InspectorHideGrid`** — la "griglia dell'inspector" e' quella **dietro
      l'anteprima immagine**: serve a distinguere lo sfondo dell'immagine (spesso
      bianco, o trasparente) da quello del pannello. Ricostruita dalle stringhe:
      la chiave `InspectorHideGrid` sta nella tabella delle impostazioni accanto a
      `InspectorHideLineNumbers` e `InspectorHideMinimap`, e il suo comando e'
      `HK_InspectorToggleImageGrid`, con la descrizione «Toggle display of image
      grid to better differentiate background from the image contents»
      (`analysis/out/strings-all.txt`). Si disegna **sotto** l'immagine: un PNG
      trasparente la lascia vedere, una foto la copre. Comando in palette
      (`Image: toggle background grid`), preferenza ricordata in `settings.ini`
      come `inspectorHideGrid`
- [x] **il contenuto di una cartella scelta, in fondo al pannello**, in **elenco**:
      le prime voci con la loro icona o miniatura, senza aprirla — la domanda «cosa
      c'è qui dentro?» fatta con gli occhi. **Richiesto dall'autore**, non
      ricostruito dall'originale

*Stima: 3 settimane.* Il rasterizzatore di font proprio dell'originale
(`WinFontRasterizer.cpp`) resta fuori perimetro: GDI è sufficiente.

### Il contenuto di una cartella, senza aprirla — *Scelta*

Richiesto dall'autore: scegliendo una cartella, l'ispettore ne mostra il contenuto
sotto i metadati, in **elenco** — icona e nome, una voce per riga. Serve a riconoscere
una cartella **prima** di aprirla, e a non perdere il posto nell'elenco per andare a
guardarci dentro. La prima stesura era una **griglia** di miniature; l'autore l'ha
chiesta in elenco, che e' anche la forma in cui i nomi si leggono tutti.

Tre decisioni, e vale la pena scriverle:

- **La lettura è limitata a dodici voci.** Una cartella con centomila file si ferma
  alla tredicesima: la domanda è «cosa c'è qui», non «elenca tutto», e questa lettura
  gira sul thread dell'interfaccia quando il cursore si sposta. Che ce ne siano altre
  si sa da **una** voce in più invece che contandole tutte, e lo si dice sulla riga
  dell'intestazione (`e altre voci`, a destra): come riga a sé costava una riga di
  celle, e la scelta diventava fra mostrarne nove col conteggio o dodici senza.
- **Le anteprime sono quelle della lista**, nello stesso ordine: miniatura vera se
  ce n'è una, altrimenti quella del tipo. Una strada sola per la stessa domanda, e
  nessuna coda di lavori in più.
- **Le righe sono alte quanto quelle dell'ispettore** (20 px) e l'icona è quella
  della riga dei **dettagli** della lista: miniatura vera se ce n'è una, altrimenti
  quella del tipo. A 16 px non si chiede alla shell l'icona grande — sarebbe una
  rasterizzazione da 128 px per disegnarne 16. E l'opzione che spegne le anteprime le
  spegne anche qui: è la stessa domanda, e due risposte diverse nella stessa finestra
  sono due bugie di cui una sola è voluta.

**Non è interattiva**, ed è dichiarato: le righe si guardano soltanto. Cliccarle
vorrebbe dire dare all'ispettore dei bersagli, e l'ispettore oggi non ne ha.

Limiti dichiarati: il contenuto si legge quando la cartella **scelta cambia**, quindi
un file arrivato da fuori mentre quella cartella è scelta non compare finché non si
sposta il cursore (la sorveglianza delle cartelle non è ricorsiva); e una cartella di
spazio dei nomi non si elenca con `FindFirstFile`, quindi lì l'elenco non c'è — per
il cestino la domanda non ha comunque senso.

### La griglia dell'immagine — *Ricostruito*

Una chiave di M8 era rimasta senza risposta: `InspectorHideGrid`. Le stringhe del
binario la sciolgono, e non era una cosa da indovinare. Nella tabella delle
impostazioni compare accanto a `InspectorHideLineNumbers` e `InspectorHideMinimap`,
ma il comando che le corrisponde e' `HK_InspectorToggleImageGrid`, e la sua
descrizione e' esplicita: «Toggle display of image grid to better differentiate
background from the image contents». Cioe': la griglia non e' del testo, e' **dietro
l'anteprima dell'immagine**, ed esiste perche' un'immagine con lo sfondo bianco (o
trasparente) si confonde col pannello.

Il disegno la mette **sotto** l'immagine, non sopra: cosi' un PNG trasparente la
lascia intravedere — che e' il caso in cui serve — e una foto la copre tutta. Il
comando `Image: toggle background grid` la spegne, e la scelta si ricorda.

**Verificato a schermo** con un PNG a sfondo trasparente creato apposta nella
sandbox: acceso, il cerchio rosso sta su una griglia; spento, lo sfondo trasparente
resta piatto.

### Il visualizzatore di testo — *Ricostruito*

Un file di testo non è una cosa sola: può essere UTF-8 con o senza marcatore, UTF-16
nei due versi, o "ANSI" — la codifica locale, che qui è latin-1. E può **non essere
testo affatto**, che è il caso più importante da riconoscere: aprire un eseguibile
come se fosse prosa riempirebbe il pannello di rumore, e sembrerebbe un difetto
nostro.

Quindi non si guarda l'estensione, si guarda il **contenuto**: si legge l'inizio del
file, si decide la codifica (marcatore d'ordine, alternanza dei byte zero per
l'UTF-16 senza marcatore, validità dell'UTF-8 e ripiego sulla codifica locale), e si
rifiuta quello che ha più del 2% di caratteri di controllo. Verificato: un `.bin`
non mostra nessun visualizzatore, e un `.txt` con dentro byte binari non lo
mostrerebbe nemmeno.

La riga di stato dice le tre cose che si vogliono sapere prima di leggere:
`UTF-8   10 lines   CRLF`. I terminatori si **contano** e si dicono, perché un file
con due convenzioni dentro è una cosa che si scopre dopo aprendolo con un editor
vero — e la prima versione di prova l'ha dimostrato suo malgrado: dichiarava `misto`
e aveva ragione, perché il file di prova era stato scritto con Python che traduce i
terminatori da solo.

**Un testo si legge in un carattere a spaziatura fissa**, e i quattro strati
dell'atlante erano tutti occupati: il quinto è Consolas. Senza, le colonne non si
allineano e un visualizzatore di testo serve a poco; se Consolas non c'è si ripiega
sul carattere secondario, e le colonne non si allineano — cosa che vale la pena
sapere, ed è scritta dove sta la decisione.

Due limiti dichiarati: si legge **un megabyte** (una lettura sincrona più lunga
bloccherebbe la finestra, e questo non è un editor), e oltre quello la testata lo
dice. E le righe si impaginano **una volta per misura** — con l'andare a capo acceso
una riga logica può essere più righe di schermo, e rifarlo a ogni fotogramma su un
file lungo sarebbe lavoro sprecato.

### Le proprietà che il file system non dice — *Ricostruito*

Un file system sa dire nome, dimensione e tre date. Non sa dire che un'immagine è
larga 64 pixel: quello sta nell'intestazione del file, e chi lo sa è il gestore di
quel tipo. La strada per chiederglielo è `IPropertyStore`
(`SHGetPropertyStoreFromParsingName` + `PKEY_*`), la stessa da cui Windows riempie
le colonne della sua lista, e le chiavi sono documentate in `propkey.h`.

Il **volume** invece è un'altra domanda con un'altra risposta: capacità, spazio
libero e file system si chiedono al sistema (`GetDiskFreeSpaceExW`,
`GetVolumeInformationW`) — lo dice anche `docs/03`, che ricostruisce il
raggruppamento per occupazione dei volumi proprio da quell'import.

Tutte e due le letture si fanno **una volta per percorso** e si tengono in una
mappa: l'inspector si ridisegna a ogni fotogramma.

**E una cosa che non si fa**: per un volume non si chiedono le statistiche
ricorsive. Non è una dimenticanza, è che la domanda non ha senso — e la prima
versione la faceva lo stesso, riceveva zeri e li scriveva (§7.31 di `docs/02`).

---

## M9 — Configurazione, hotkey, temi ✅

**Vincolo di progetto, deciso dall'autore: niente dati personali, niente tracce di
cosa si è guardato.** Vedi `docs/06-privacy.md`. I tre file JSON sotto
`%APPDATA%\Voidstar\FilePilot` sono il comportamento **dell'originale**, e
copiare quelli significherebbe scrivere su disco i percorsi visitati. Qui non si fa.

La persistenza, quando c'è, è **esplicita e visibile**: `%APPDATA%\Bolt\settings.ini`,
testo leggibile con una riga per preferenza, che non contiene nomi di file né
cartelle — solo come si vuole che il programma si comporti. L'applicazione dice dove
l'ha scritto, e cancellare il file riporta tutto ai valori di partenza. La scelta
della lingua l'ha resa necessaria, ed è la stessa cosa che `docs/06` §4-bis prevede.

- [x] il **pannello delle opzioni** (`Ctrl+,` e la voce «Open options» nella
      palette): una **lista cercabile** a righe alte due linee, ognuna con nome,
      descrizione e un **controllo tipizzato** — campo di testo, stepper, slider,
      interruttore, scelta con sottoelenco. I tipi sono `OptionControl`, e la
      verità sta in `AppState`: `RefreshOptionItem` la porta nella riga,
      `ApplyOptionFromItem` la riporta indietro
- [x] preferenze ricordate in `%APPDATA%\Bolt\settings.ini`
- [x] la **tavolozza chiara e scura**: i ruoli di colore stanno in `theme.h` (i
token dello spec che hanno un lettore, e la lista di quelli che ancora non ce
l'hanno), le due tavolozze in `theme.cpp`, e la scelta si fa da `Ctrl+,` o dalla
palette (`Tema: chiaro`, `Tema: scuro`, `Tema: segui il sistema`).
      `SystemColorScheme` legge `AppsUseLightTheme` dal registro e segue il sistema
      **anche a finestra aperta** (`WM_SETTINGCHANGE`); col sistema a comandare, la
      riga della tavolozza si mostra spenta e continua a dire quale tavolozza e' in
      uso. Il riquadro di DWM si allinea alla stessa decisione, o si ottiene una
      finestra chiara col bordo scuro. Tutto ricordato in `settings.ini`
      (`colorScheme`, `systemColorScheme`)
- [x] mappa tasti rimappabile — **in memoria**, per la sessione: la **mappa c'e'** ed
      e' la fonte unica (`src/keymap.*`), e l'**editor** pure — la riga `Key Map` fra le
      opzioni, e `CmdEditKeyMap` anche dalla palette. Vedi sotto
- [x] **la pagina di avvio** — la cartella che si apre all'avvio quando dalla riga di
      comando non arriva niente, scelta con due comandi (`Avvio: la cartella aperta`,
      `Avvio: Questo PC`) o dalla riga `Avvio` fra le opzioni, dove vuoto vuol dire
      «Questo PC». Sta in `settings.ini` come `startupFolder=`, e **non** e' «riapri
      l'ultima cartella»: quella sarebbe cronologia su disco, e `docs/06` la vieta. Una
      cartella che non esiste piu' non si apre in silenzio: si apre Questo PC e la barra
      di stato dice perche'. Visto a schermo: impostata su `documenti`, riavviato senza
      argomenti e si e' aperta `documenti` (`startup-riavvio.png`); impostata su una
      cartella che non esiste, riavviato e si e' aperto Questo PC col messaggio
      (`startup-inesistente.png`); «Avvio: Questo PC» toglie la chiave dal file
- [x] **layout salvabili per cartella** — **in memoria**: `AppState::folderLayouts`
      tiene, per le cartelle che ne hanno uno, la **vista**, la misura delle celle,
      l'**ordinamento**, il **raggruppamento** e la **larghezza delle colonne**
      (`FolderLayout`), e `LoadDirectory` lo riapplica entrando. Sono i due comandi
      che lo spec chiama `DirHostLayoutSave`/`DirHostLayoutDel`: *Salva il layout di
      questa cartella* e *Dimentica il layout di questa cartella*, in palette e nel
      menu contestuale del fondo. Il salvataggio e' **esplicito** e non automatico:
      una cartella che ricorda da sola com'e' stata guardata e' una preferenza presa
      al posto di chi usa il programma, e il riferimento ha due comandi apposta.
      Una cartella **senza layout** tiene quello che si sta usando, invece di
      riportare tutto a "dettagli".
      Verificato a schermo: in `immagini` ordinato per dimensione e salvato; il
      genitore in ordine di nome; rientrando in `immagini` torna l'ordine per
      dimensione (`layout-parent-name.png` / `layout-restored.png`); dopo *Dimentica*
      rientrando tiene l'ordine del genitore, cioe' il layout non c'e' piu'
      (`layout-forgotten4.png`). **Non** c'e' l'ereditarieta' verso le sottocartelle
      ne' l'applicazione ricorsiva: quelle vogliono un gesto esplicito ("applica a
      tutte"), che non c'e'
- [x] **`ShowFileExtension`** e **`DisplayCompactPaths`** — applicate il
      2026-10-18. La prima toglie l'estensione dal nome **come si scrive**
      (`DisplayName`): il nome vero resta quello di `Entry::name`, quindi aprire,
      rinominare e filtrare continuano a usarlo — un nome che il filtro non trova
      piu' sarebbe peggio di un nome lungo. La seconda stringe le briciole in testa
      in un `...` quando il percorso non entra, tenendo radice e coda; il `...` non e'
      cliccabile, perche' non c'e' una destinazione che corrisponda a «quelle in
      mezzo». Viste a schermo: nomi `dati`, `nota`, `relazione` (`opt-ext-off.png`);
      `... > Desktop > ... > tmp > sandbox` contro il percorso intero che finisce
      sotto il conto delle voci (`opt-compact-on.png`, `opt-compact-off.png`,
      a finestra stretta)
- [x] **`RoundCorners`, `InactivePanelContentOpacity` e `LongPathsSupport`** —
      applicate il 2026-10-18, con due stati di disegno nel renderer
      (`IRenderer::SetCornerScale` e `SetOpacity`, che valgono per un blocco di
      primitive) e un solo punto di I/O per i percorsi lunghi:
      - `RoundCorners` moltiplica i **raggi degli angoli** di ogni primitiva (0 =
        spigoli vivi). Le forme che usano quel numero come **spessore** — `+`, `x`,
        frecce, stella — sono escluse, o sarebbero sparite: si vede nell'angolo della
        riga scelta, ingrandito 6 volte (`corner-on.png` / `corner-off.png`).
      - `InactivePanelContentOpacity` attenua il pannello che **non ha il fuoco**, e il
        fuoco e' dove si e' cliccato l'ultima volta (`AppState::focusArea`). A opacita'
        0,52 il titolo dell'ispettore senza fuoco misura **146** su 255 contro **255**
        col fuoco, e il nome di una riga della lista **141** contro **234**
        (`opt-inactive-list.png` / `opt-inactive-inspector.png`). Il valore di partenza
        e' 1,00: nessun pannello si spegne senza che lo si sia chiesto.
      - `LongPathsSupport` mette il prefisso lungo di Windows (o quello per i percorsi
        di rete) **nei punti di I/O**, non su ogni stringa: `IOPath` in `app_ui`, usata
        da `ReadDirectory` e dalla validazione dell'argomento della riga di comando —
        che prima era limitata a 260 caratteri e faceva partire l'app su «Questo PC»
        (`docs/02` §7.60). Provato su una cartella di **271** caratteri: spenta non si
        apre, accesa si apre e mostra il file in fondo (`opt-long-on.png`).
      Resta **`Animations`** spenta: le animazioni di apertura e scorrimento sono una
      funzione, non un interruttore
- [x] **`Animations`** — i pannelli **compaiono in dissolvenza** (160 ms, uscita
      rapida) invece di apparire di colpo: palette, GoTo, opzioni, mappa dei tasti,
      valori di colonna, fissaggi e menu contestuale. Il fattore e' uno stato del
      renderer (`SetOpacity`, lo stesso di `InactivePanelContentOpacity`), applicato
      al **blocco** che disegna il pannello: velo, ombra, fondo, testo e righe
      spariscono insieme, senza correggere cinquanta colori. Il sottomenu non entra
      in dissolvenza: si apre sopra un pannello gia' visibile, e due fade annidati si
      vedrebbero come uno sfarfallio. E finche' un pannello sta comparendo il
      fotogramma dopo si chiede (`InvalidateRect` in `RenderFrame`), o l'animazione si
      fermerebbe al primo disegno, lasciando il pannello fermo a meta'.
      **Limite dichiarato:** lo **scorrimento** e il **cursore di testo** dello spec
      non sono animati — sono due comportamenti con una fisica propria, non una
      dissolvenza, e non ci sono.
- [x] nessun elenco di recenti, nessuna cronologia e nessuna anteprima lasciano la
      memoria del processo — verificato: `settings.ini` non contiene percorsi
      (`docs/06`), e le cache di anteprime, icone e statistiche vivono in `AppState`

*Stima: 3–4 settimane.*

### La mappa dei tasti, e l'editor — *Ricostruito*

Le scorciatoie non erano un dato: erano **quarantasei `case`** dentro `WindowProc`,
dove il tasto e cosa fa stavano nella stessa riga (`case 'C': if (ctrl)
DoCopyToClipboard(...)`). Finché nessuno le vuole cambiare va bene — e appunto,
cambiarle era la richiesta — perché lì **il tasto è il codice**, e non esiste un posto
in cui scrivere un tasto diverso.

Adesso il tasto è un dato, in `src/keymap.*`: una tabella `comando -> tasto` che si
legge per decidere, si legge per **scrivere la scorciatoia accanto a una voce**, e si
può cambiare. Due cose sono cadute fuori da sole, e sono la prova che il confine era
quello giusto:

- **l'esecutore è tornato uno.** I `case` che eseguivano un comando erano copie di
  quelli di `RunCommand` (il `case 'C'` e il `case CmdCopy:` facevano la stessa cosa in
  due punti). Adesso il tasto chiama **lo stesso** `RunCommand` della palette e dei
  menu: una strada sola, e due copie non possono più divergere.
- **le righe di fondo dei menu si scrivono da sole.** `Ctrl+Maiusc+P apre e chiude`
  era una frase con le lettere dentro; adesso è `keys.LabelFor(CmdPalette)`, e per
  `GoTo` dice `Ctrl+P o F4` perché ha due tasti. Se il tasto cambia, cambia quello che
  si legge.

Una cosa si è dovuta aggiungere perché la mappa fosse completa: la **tavolozza dei
comandi** era l'unica cosa che si apriva da un tasto **senza essere un comando**
(`Ctrl+Maiusc+P` chiamava `BuildPaletteMenu` e basta). Adesso è `CmdPalette`, con la
sua riga in tabella e il suo tasto nella mappa.

I tasti che **non sono comandi** restano nel gestore dei messaggi, e sono una scelta:
`Ctrl+A` che seleziona tutto, `Ctrl+G` che cicla il raggruppamento, `Ctrl+Home`,
le frecce, l'Invio. Sono comportamenti dell'input, non voci che si possono eseguire da
un menu, e metterli in tabella vorrebbe dire inventare comandi che non esistono.

**Quello che mancava era l'editor**, e adesso c'è: `Rebind`, `Clear`, `Reset` e
`Bindings()` sono chiamati da un posto nella finestra invece che solo dal codice.
Sta fra le opzioni — una riga `Key Map` con un pulsante `Modifica...`, che è il primo
controllo di tipo **`Action`**: un pulsante che non ha un valore da leggere ma apre
un'altra cosa — e lo stesso comando (`CmdEditKeyMap`) è nella palette, senza tasto di
partenza.

Com'è fatto, in quattro righe:

- **una riga per comando, non per tasto**, costruita dalla tabella dei comandi e non
dalle righe della mappa: un comando che un tasto non ce l'ha deve poterlo ricevere, e
il posto vuoto a destra **è** l'informazione che manca. Il tasto lo scrive `LabelFor`,
quindi quella riga non può dire una cosa diversa da quella che il tasto fa.
- **la cattura**: `Invio` su una riga, e il campo di ricerca diventa il messaggio
(«premi un tasto per *Rinomina*») invece di un secondo riquadro. Da lì il tasto
premuto è il **dato** che si sta scrivendo, non un comando da eseguire: la cattura
sta *prima* di ogni altra lettura della tastiera, `WM_SYSKEYDOWN` compreso (altrimenti
`Alt+qualcosa` non si potrebbe assegnare, e `Alt+F4` chiuderebbe la finestra mentre si
sta scegliendo un tasto). `Esc` annulla, `Canc` toglie, e `Rebind` si porta via il
tasto da chi lo aveva: due comandi sullo stesso tasto sarebbero un tasto che fa due
cose a seconda di chi guarda.
- **i tasti di partenza** sono la prima riga dell'elenco, e non un comando: sono
l'unica via d'uscita da una mappa che ha tolto i tasti per riaprire quella lista.
L'esito si legge nella barra di stato, come ogni altra azione.
- **la riga in fondo avvisa** quando il tasto che si sta spostando è proprio quello
che apre l'elenco (`Ctrl+,`, `Ctrl+Maiusc+P`, o il comando stesso): la mappa è di
sessione, quindi un riavvio la riporta ai valori di partenza — ma per rientrare
bisognerebbe chiudere l'applicazione, e questo è un limite **dichiarato**, non un
divieto.

Due limiti della mappa, detti dove vivono:

- **un tasto da scrivere sciolto non si assegna.** Senza `Ctrl` e senza `Alt` un tasto
lettera arriva alla casella di testo — il filtro e la rinomina — quindi un comando su
`C` renderebbe impossibile scrivere una parola che contiene una `c`. `Maiusc` non
conta: `Maiusc+C` **è** una `C` maiuscola. Un tasto rifiutato non sparisce in
silenzio: la riga in fondo dice cosa serve (un modificatore o un tasto funzione).
- **il nome di un tasto di punteggiatura lo dà Windows**, nella lingua della tastiera
in uso: `Label` ha i suoi nomi scritti a mano per i tasti che l'applicazione usa nei
tasti di partenza, e per tutti gli altri chiede `GetKeyNameTextW` sul codice di
scansione, dichiarando il bit che distingue i tasti **estesi** (senza, `Insert` si
chiamerebbe "0"). Un tasto senza nome non si potrebbe assegnare, e sarebbe un buco
travestito da rifiuto.

Una cosa si è dovuta correggere nel componente dei menu: la colonna di destra, quando
il dettaglio manca, prendeva il **gruppo** della voce. Va bene per la palette, dove il
gruppo è il contesto di un comando, ma non qui, dove quella colonna vuol dire una cosa
sola — il tasto — e un gruppo scritto dov'era il tasto sarebbe una bugia su un comando
senza tasti. Adesso il ripiego succede **solo** dove il menu lo chiede
(`GroupMenu::detailFallsBackToGroup`), e la palette è l'unico che lo chiede.

Le catture della verifica sono in `analysis/tmp/keys/`: la riga fra le opzioni, l'elenco
con i tasti accanto, il filtro, la cattura con il cursore sul comando, una rimappatura,
un tasto tolto con `Canc`, `F5` rubato ad `Aggiorna`, i tasti di partenza rimessi col
messaggio nella barra di stato, il tasto nuovo che rinomina davvero, il vecchio che non
fa piu' niente, l'avviso su un tasto d'ingresso, un tasto di punteggiatura rifiutato
sciolto e accettato con `Ctrl`, e le stesse schermate in inglese.

### Cosa si perde, e perché va bene

Rinunciare alla persistenza significa che a ogni avvio l'applicazione riparte
dalle impostazioni predefinite. È un costo reale, e va detto invece di essere
scoperto. In cambio: un file manager che **non lascia traccia di cosa hai aperto**
è la scelta giusta per uno strumento che vede l'intero disco, e il progetto è
personale, quindi la configurazione che si perde è quella di una persona sola.

Il precedente nella tabella dello spec resta `StoreRecents: Session`: i recenti di
sola sessione non sono una limitazione da colmare, sono **il comportamento
voluto**.

### La lingua e' finita — *Visto*

Il meccanismo c'era (`Tr`/`TrW`, coppie di stringhe dove si usano); mancavano le
superfici. Adesso la lingua copre:

- il **menu contestuale** (le nostre voci: `Apri`/`Open`, `Taglia`/`Cut`, ...); le
  voci della shell restano nella lingua del sistema, che le sa lei;
- l'**inspector**, per le parti in prosa (`Niente selezionato`, `N elementi
  selezionati`, `N file, M cartelle`, `conteggio...`, `%s liberi`);
- i **messaggi di stato** (operazioni su file, shell, proprieta', cascate,
  trascinamento) e i **conteggi** con la grammatica giusta (`Copiato 1 elemento` /
  `Copiati 3 elementi`);
- la **barra di stato** (`N voci`, `N selezionati`, i quattro nomi di vista) e il
  **pie' di pagina** (`Filtra N file...`, `Raggruppa: ...`);
- il **visualizzatore di testo** (`N righe`) e il pannello dei valori di una colonna
  (`Tutti`/`Nessuno`, `Scrivi per filtrare i valori...`);
- il **nome di partenza di una cartella nuova** (`Nuova cartella` / `New folder`).

**Un secondo passaggio, guardando le superfici una per una.** La prima stesura ne
aveva lasciate fuori cinque, tutte visibili e nessuna deducibile dal codice senza
cercarla — sono esattamente il tipo di cosa che «la lingua e' finita» nasconde:

- il **conto delle voci nella barra del percorso** (`5 voci`, dove c'era `5 items`);
- i **conti delle bande di gruppo** (`2 voci` / `1 voce`, con il singolare giusto);
- il **numero nel pie' di pagina della palette** (`84 di 84`, dove c'era `84 of 84`);
- i **liberi di un volume** nella barra laterale (`202,3 GB liberi`, dove c'era
  `202.3 GB free`) e i **conteggi del suggerimento** di una cartella
  (`0 cartelle   1 file`);
- gli **errori di lettura** di una cartella (`Accesso negato`, `Non si riesce a
  leggere questa cartella`) e delle posizioni di spazio dei nomi, che finiscono
  nell'area della lista; e i **terminatori di riga** del visualizzatore
  (`misto` / `mixed`).

Il modulo sta in `src/language.h`/`.cpp` perche' anche `file_ops.cpp` ha i suoi due o
tre messaggi: il secondo utilizzatore e' la ragione per cui e' un file a se'.

**Cosa resta in inglese, e perche'.** Poco, e va detto per nome.

- I **verbi della shell**, che arrivano dal sistema gia' nella sua lingua: sono
  «Apri», «Taglia» su un Windows italiano e "Open", "Cut" su un Windows inglese,
  perche' li' la lingua la sceglie Windows e noi non la tocchiamo. Le **nostre** voci
  accanto a loro sono tradotte. E anche le eccezioni inglesi che restano sono di
  **altri**: nel menu contestuale di una cartella si legge `Open Git Bash here` e
  `File Pilot here`, che sono i verbi delle estensioni di Git e dell'originale, con
  il nome che si sono dati (`analysis/tmp/keys/keys-menu-contestuale-shell.png`).
- La **diagnostica di avvio** (`ShowError`, il MessageBox `Startup failed` e il
  registro `startup-error.log`): parla prima che esista un'interfaccia, e i suoi
  messaggi vengono dai moduli del motore (shader, D3D11, atlante), non da testi che
  si scrivono qui. Tradurla vorrebbe dire mettere `language.h` dentro il renderer per
  un errore che si vede una volta nella vita, quando il programma non parte.
- I **nomi dei file e dei percorsi**, che sono di chi li ha scritti.

**Quello che il documento diceva prima era falso.** Questo paragrafo sosteneva che i
**nomi del riferimento** non si traducevano — «le sei intestazioni delle colonne, le
quattro sezioni della barra laterale, le righe dell'inspector (`Type`, `Size`,
`Created`), i gruppi della palette, i nomi delle bande di raggruppamento» — «perche'
sono *i suoi nomi*, e cambiarli renderebbe il clone meno fedele». La decisione
contraria e' dell'autore, il **2026-10-14** (`AGENTS.md` §3): i nomi del riferimento
**si traducono**, e **la chiave resta l'inglese** — e' con la chiave che si ricorda
cosa e' chiuso, quindi cambiare lingua non riapre niente. Le colonne, la barra
laterale, l'inspector e le bande di raggruppamento sono state tradotte subito; il
paragrafo e' rimasto li' a dire il contrario, e le superfici che elencava come
«restano in inglese» sono state l'ultima a essere tradotte — palette e opzioni
comprese. Vedi `docs/02` §7.56: e' la stessa forma di difetto di un commento che
resta a descrivere il codice di prima, e si paga con tre parole in inglese in mezzo
alle altre.

**Un terzo passaggio: i nomi che vengono dalla tabella, non dalle stringhe scritte a
mano.** Questi non si trovano cercando un testo in inglese, perche' **non sono** un
testo: sono il campo `group` di una voce di menu e il campo `label` di una riga di
opzione, cioe' le **chiavi** del riferimento — `View`, `Sort`, `GroupBy`,
`Show Tooltips` — disegnate cosi' come sono. Per trovarle bisogna guardare una
schermata e chiedersi cosa c'e' scritto. Erano tre, e sono le uniche superfici del
programma che si leggevano in inglese con la lingua su italiano:

- le **bande dei gruppi** di ogni menu (palette, GoTo, mappa dei tasti): 17 chiavi,
  ora una tabella `MenuGroupLabelIt` accanto a chi le disegna, in `view_menu.cpp`;
- le **etichette di riga del pannello delle opzioni**: 27 nomi, ora il campo `labelIt`
  nella tabella `kRows`, accanto alla descrizione italiana che c'era gia'. Dove la
  riga e' anche un comando della palette, la parola e' **quella del comando**
  (`Show Tooltips` e' «Mostra i suggerimenti» in tutt'e due i posti), o sarebbero due
  nomi per la stessa preferenza;
- **un comando**: `CmdPalette`, l'unico su 80 senza traduzione in `CommandLabelIt`.

La prova che conta e' che **la chiave non si tocca**: chiuso `Vista` in italiano, la
banda si chiama `View` in inglese ed e' **ancora chiusa**, e tornando in italiano si
richiama `Vista` chiusa. Il filtro segue quello che si legge — in italiano si cerca
`lingu`, in inglese `langu` — ed e' un effetto della stessa scelta, non un difetto.

**Verificato a schermo in tutte e due le lingue**, in tre giri. Del primo: pie' di
pagina, barra di stato, menu contestuale, inspector a selezione multipla, messaggio di
copia, e la testata del visualizzatore (`2 righe` / `2 lines`, `UTF-8`). Del secondo:
la barra del percorso (`5 voci`), i liberi di un volume (`202,3 GB liberi`), il pie' di
pagina della palette (`84 di 84`), le bande di gruppo (`2 voci`, `1 voce`), il
suggerimento di una cartella (`0 cartelle   1 file`) e i terminatori di riga del
visualizzatore (`3 righe   misto`).

Del **terzo** (catture in `analysis/tmp/keys/`): le bande `Vista`/`Lingua`/`Aspetto`/
`Schede` della palette con la riga «Palette dei comandi», le bande della mappa dei
tasti, quelle del GoTo (`Percorso`, `Recenti`), le 27 righe delle opzioni
(`Carattere`, `Misura del carattere`, `Spaziatura`, `Ingrandimento`, `Angoli
arrotondati`, `Tavolozza`, `Tema chiaro o scuro secondo il sistema`), e le stesse
schermate in inglese (`Command palette`, `View`, `Language`, `Font`, `Font Size`).
Vedi anche §7.41 di `docs/02`, per il nome della vista a elenco che l'inglese ha fatto
vedere sbagliato in tutte e due.

---

## M10 — Backend Direct3D 11 ✅

L'originale espone `GraphicsBackend` con `Direct3D` e `OpenGL` e importa
`D3DCompiler_47`. Un clone fedele deve poter scegliere.

- [x] astrazione del renderer dietro un'interfaccia (`IRenderer` in
      `ui_renderer.h`): `BeginFrame`, `Present`, `Begin`/`Draw`/`End`,
      `SetClip`/`ClearClip`, la texture array. Il **formato di vertice** non
      entra nell'interfaccia: e' la cosa che i due backend hanno in comune
- [x] backend D3D11 (`d3d11_renderer.*`) con la stessa semantica di quad: stesso
      batcher, stessi 34 float per vertice, stessa tabella di indici, chiaro e
      presentato dal backend. La swap chain segue la finestra (`ResizeBuffers`)
- [x] compilazione HLSL degli stessi shader (`D3DCompile`, `vs_5_0`/`ps_5_0`):
      stessa SDF, stesso bordo, stessa ombra, stesso dash. **Non e' una seconda
      implementazione visiva**, e' la stessa detta in un'altra lingua
      — con un difetto che ha reso quella casella falsa per un pezzo: lo shader
      incorporato chiamava `point` un parametro, che in HLSL e' una parola
      riservata, quindi la compilazione falliva **sempre** e il programma ripiegava
      in silenzio sull'OpenGL. **Risolto il 2026-10-18**: il parametro si chiama
      `p`, e il Direct3D 11 ha avuto il suo giro di verifica a schermo (lista,
      menu contestuale, opzioni, mappa dei tasti, tema chiaro, immagine grande,
      visualizzatore di testo, viste a icone) con `--- backend: Direct3D 11` nel
      registro e nessun errore HLSL. Vedi `docs/02` §7.52, che elenca anche le
      superfici **non** ripetute sotto questo backend
- [x] selezione a runtime e fallback: `BOLT_BACKEND=opengl|d3d11`, altrimenti
      Direct3D 11 e, se non parte, OpenGL. Un backend che non c'e' non impedisce
      di usare il programma, e la riga di registro dice quale sta disegnando
- [x] **il ritaglio per-quad**, fatto il 2026-10-20 — era il debito scritto in
      `AGENTS.md` §8 e in `docs/05` §2, non una funzione mancante. Il rettangolo di
      ritaglio e' un **campo del vertice** (34 float invece di 30), confrontato nel
      pixel shader in GLSL e in HLSL: `glScissor`, `ScissorEnable` e
      `RSSetScissorRects` non ci sono piu', `SetScissor`/`ClearScissor` diventano
      `SetClip`/`ClearClip`, e `main.cpp`/`view_inspector.cpp` non chiudono piu' il
      lotto per ritagliare. Misurato: **5 disegni per fotogramma → 1**
      (`BOLT_DEV_HUD=1`, stessa schermata). Verificato a schermo in due stati
      (griglia scorsa a meta' cella, visualizzatore di testo scorso e ingrandito):
      nessuna cella sopra la barra del titolo, nessuna riga sopra la testata
      `UTF-8 … righe`, e **0 pixel di differenza** con le catture fatte col vecchio
      scissor. Controllo negativo: spento il taglio nel pixel shader il difetto
      torna (`riga 005` sopra la testata, `riga 025` sotto il riquadro). Tutti e due
      i backend, con l'HLSL compilato prima con `fxc` (vs_5_0 e ps_5_0). Il
      contatore dei disegni e' nato qui (`IRenderer::DrawCalls`) ed e' costato una
      scoperta: `QuadCount` ripartiva a ogni `Begin` e mostrava **l'ultimo lotto**
      (379) invece del fotogramma (1.565) — `docs/02` §7.61

*Stima: 2–3 settimane* se M1 ha definito bene l'interfaccia; molto di più se no.
L'interfaccia c'era (un solo array di texture, un solo quad batcher), quindi il
D3D11 e' stato un secondo consumatore, non una riscrittura.

### Due trappole del port in HLSL — *Viste*

- **La matrice in un `cbuffer` e' column-major per default.** Il codice caricava
  la proiezione in ordine di riga, e il risultato era una finestra **vuota**: la
  geometria finiva fuori dallo schermo. Dichiararla `row_major` (o trasporla) e'
  l'unica cosa che serve, e senza saperlo si cerca a lungo.
- **`point` e' una parola riservata dell'HLSL.** Un parametro chiamato `point`
  produce un errore di sintassi, e — questa e' la parte peggiore — `D3DCompile`
  risponde `E_FAIL` **senza un messaggio**. Il modo per vederlo e' compilare lo
  shader con `fxc.exe` del Windows SDK, che il messaggio lo da': da li' in poi il
  ciclo e' stato `fxc` prima, `D3DCompile` dopo. E' la trappola che il 2026-10-18
  e' stata pagata fino in fondo: `fxc` sugli shader estratti dalla sorgente dice
  l'errore **senza aprire una finestra**, che e' il modo giusto di provare una
  compilazione.

### Cosa resta

Il cambio di backend e' **all'avvio** (`BOLT_BACKEND`), non un interruttore a
caldo: ricreare il backend a finestra viva vuol dire ricreare texture e buffer di
ogni atlante, ed e' un lavoro a se'. Dichiarato.

---

## M11 — Installazione e pacchetto ✅

Chi usa il programma non lo compila: lo installa. Finche' l'unico modo di avere Bolt era
`cmake --build`, il programma era completo ma non **installabile**.

- [x] **l'installer (Inno Setup 6)**: `installer/bolt.iss`, compilato da
      `tools/make-installer.ps1` in `out/Bolt-<versione>-win64-setup.exe`. Installazione
      **per utente** di default (`%LOCALAPPDATA%\Programs\Bolt`, nessun amministratore), con
      la scelta "per tutti" nel wizard per chi la vuole; cartella nel menu Start, icona
      sul desktop **solo se la si spunta**, voce in «App e funzionalita'» con la versione,
      il comando di disinstallazione e la dimensione. `AppId` stabile: una versione nuova
      **aggiorna** invece di installarsi accanto. Wizard in italiano e in inglese
- [x] la versione **non** e' scritta nell'installer: la legge `make-installer.ps1` da
      `CMakeLists.txt` e la passa con `/DAppVersion`
- [x] la disinstallazione toglie file, collegamento, voce di registro e i due log di
      diagnosi (`[UninstallDelete]`), e **chiede** se togliere anche le preferenze: in
      silenzioso non chiede e non cancella. `settings.ini` non lo tocca nessuno dei due
- [x] lo **zip portatile** (`out/Bolt-<versione>-win64-portable.zip`): stesso contenuto nel
      layout installato, si scompatta dove si vuole e si lancia `Bolt.exe`, senza
      installare niente e senza toccare il registro
- [x] `tools/verify-installer.ps1`: **24 controlli** che installano *davvero* il pacchetto
      (cartella e collegamento di prova, voce di registro vera, che poi toglie), avviano il
      Bolt installato con l'**OpenGL** — l'unico backend che legge gli shader da disco —
      leggono dal registro diagnostico quale cartella ha aperto, catturano la finestra,
      disinstallano e controllano che non sia rimasto niente, preferenze comprese (che
      devono restare e non essere riscritte). `try/finally`: la prova non lascia sporco
      nemmeno quando fallisce, e si rifiuta di partire se Bolt risulta gia' installato
- [x] **il difetto che impacchettare ha fatto emergere**: l'eseguibile portava dentro il
      percorso degli shader della macchina che l'aveva compilato, e su un altro computer
      l'OpenGL non avrebbe trovato niente. Corretto e verificato: `docs/02` §7.63

**Limiti dichiarati.**

- **L'eseguibile non e' firmato**: Windows puo' mostrare l'avviso di SmartScreen. La firma
  vuole un certificato, e per un progetto personale non c'e'. Il wizard lo dice nel README
  del pacchetto, con la strada per proseguire
- **Non e' un pacchetto MSIX**: niente aggiornamenti automatici e niente isolamento. Chi
  aggiorna lancia il setup nuovo, che riconosce l'installazione dall'`AppId`
- **Inno Setup e' una dipendenza di build, non di runtime**: serve a produrre il setup, non
  a Bolt per funzionare. Lo script cerca `ISCC.exe` nel registro e nei posti soliti, e se
  non c'e' si ferma dicendo dove prenderlo, invece di produrre un pacchetto a meta'

## M12 — La Home ✅

La pagina di avvio: quello che Esplora risorse chiama Home. E' una **posizione**, come
"Questo PC", non una cartella finta: le sue righe sono destinazioni che esistono gia'
(`Entry::location` porta il percorso vero, come per le voci del cestino), e il nome della
posizione e' nostro (`bolt:home`).

- [x] **tre bande**, in ordine: `Fissaggi` (i `pin` di sessione), `Recenti` (il ranking
      di M4), `Cartelle note` (i `places` della shell — Questo PC, le cartelle standard, il
      cestino). Una destinazione che starebbe in due bande compare **una volta sola**, nella
      prima: due righe per la stessa cartella sarebbero due nomi per la stessa cosa
- [x] **`Entry::band`** (0/1/2, `-1` per tutto il resto) e `RebuildView` che sulla Home
      raggruppa per banda invece di seguire `groupMode`. L'ordinamento resta quello delle
      colonne, ma **dentro** la banda; il filtro vale come in una cartella qualsiasi
- [x] **una riga `Home` in cima alla barra laterale**, fuori dalle sezioni: non e' un
      fissaggio ne' una cartella nota, e non deve sparire chiudendo `ACCESSO RAPIDO`.
      Ha la **casa** di Esplora risorse (il CLSID della cartella Home di Windows 11,
      `docs/02` §7.65)
- [x] **i nomi come li dice Windows**: le destinazioni della Home, le righe della barra
      laterale e il titolo del tab usano il nome tradotto dalla shell — `Documenti`, non
      `Documents` (`FriendlyName`, `docs/02` §7.65)
- [x] **niente operazioni su file**: copia, taglia, incolla, rinomina, elimina, cartella
      nuova, nuovo file, proprieta' e proprieta' di Windows si **rifiutano** con un
      messaggio (`RefuseInNamespace` esteso), e il menu contestuale spegne quelle voci. Le
      righe nominano cartelle vere: senza questa guardia `Elimina` cancellerebbe la cartella
      che la riga nomina, che non e' quella che si sta guardando
- [x] **niente sorveglianza**, la Home non e' un percorso: il watcher viene svuotato
      quando si entra nella Home, invece di continuare a guardare l'ultima cartella.
      ~~niente statistiche ricorsive~~: il 2026-10-21 la Home le chiede come le chiede una
      cartella — le colonne dei conteggi erano `--` e sembrava rotta (`docs/02` §7.74). Le
      destinazioni di spazio dei nomi (`Questo PC`, cestino) restano `--`: li' non c'e' un
      albero da camminare
- [x] **la Home si guarda a griglia**, il 2026-10-21 su richiesta dell'autore: e' una
      pagina di destinazioni, non un elenco di file, e una griglia di icone la legge meglio
      di una tabella con sette colonne. Il modo di visualizzazione vive **per cartella** —
      `homeViewMode` e `viewModeOutsideHome` — quindi uscendo dalla Home si ritrova quello
      delle cartelle e rientrando si ritrova la griglia (o quello che si e' scelto mentre
      si era li'). Un **layout salvato** per la Home vince sul valore di partenza: chi ha
      scelto ha scelto. Visto a schermo: la Home a griglia all'avvio, `sandbox` in
      dettagli cliccandola, e la griglia di nuovo tornando alla Home
      (`analysis/tmp/homegrid-*.png`).
- [x] **l'avvio**: `startupFolder` vuota significa **Home** (era "Questo PC"). "Avvio:
      Questo PC" adesso **scrive** il nome di spazio dei nomi invece di svuotare la
      preferenza, e la riga `Avvio` fra le opzioni accetta anche una posizione di spazio
      dei nomi. Una cartella di avvio che non esiste piu' lo dice e apre la Home
- [x] **la lingua**: le tre etichette delle bande seguono la lingua scelta, e cambiarla
      ricostruisce i gruppi. La colonna **Tipo** di una cartella adesso e' tradotta da noi
      (`File folder` / `Cartella di file`) invece di chiedere alla shell, che la scrive nella
      lingua di Windows: era italiano in mezzo a intestazioni inglesi
- [x] **il filtro guarda il nome mostrato**, non il percorso: una voce della Home ha il
      percorso in `wideName` (e' la sua identita'), e filtrare per quello non trovava il
      nome che si legge sopra. Il segnaposto dice `voci`, non `file`

**Verificato a schermo il 2026-10-20** (catture in `analysis/tmp/home-*.png`):

- avvio **senza argomenti** → Home, con le tre bande, la riga `Home` nella barra laterale,
  il titolo e il tab che dicono `Home`;
- tre bande popolate: un fissaggio, tre recenti e le cartelle note, con `sandbox` presente
  sia nei fissaggi sia nei recenti e mostrato **una volta**;
- apertura di una riga della Home, filtro per nome, `F5` che ricostruisce **tenendo** il
  filtro;
- `Canc`, `Ctrl+C` e il tasto destro → rifiuto con il messaggio, voci del menu spente
  (misurate sul pixel: 454 contro 695 di luminosità) e **26 file nella sandbox prima e
  dopo**;
- `Su` dalla Home porta a Questo PC; `Avvio: la cartella aperta` e `Avvio: Questo PC`
  riavviano dove dicono; togliendo la preferenza si torna alla Home;
- inglese e italiano a finestra aperta: `Pinned`/`Recent`/`Known folders`, `File folder`,
  `Groups: by band`, e il ritorno all'italiano senza riavviare;
- `BOLT_DEV_HUD=1`: **0 jobs** sulla prima cattura della Home (all'epoca nessuna
  statistica ricorsiva), **un solo disegno**; dal 2026-10-21 i job ci sono, perche' le
  colonne dei numeri si riempiono.

**Limiti dichiarati.**

- **Le bande non si scelgono**: la Home raggruppa per banda e il controllo "Raggruppa" lo
  dice invece di ciclare valori che la vista non legge
- **Recenti e fissaggi sono di sessione** (`docs/06`): riaprendo il programma la Home ha solo
  le cartelle note, finche' non si visita qualcosa
- **La Home non e' una cartella da cui si opera**: e' la scelta, non un limite da correggere
- **Scoperto mentre si verificava, e chiuso il 2026-10-21**: aprire una cartella da
  `ACCESSO RAPIDO` la faceva diventare una posizione codificata, e li' i comandi per
  percorso si rifiutavano — `docs/02` §7.64, con la correzione e le catture della verifica

## M13 — Aggiornamenti in app, forma B ✅

Deciso con l'autore il 2026-10-20, subito dopo la Home. La forma B e' quella che non
mente: **nessun controllo automatico** — una richiesta a GitHub e' telemetria di fatto
(`docs/06`) — ma un comando **esplicito** che, quando lo si chiede, controlla, scarica,
verifica e installa.

- [x] il comando `Controlla aggiornamenti` nella palette (`CmdCheckUpdates`), sotto il
      gruppo `Updates`, **senza tasto di partenza**: non parte ne' all'avvio ne' per
      sbaglio. Il lavoro e' `src/update.*`, su un thread, come `FileOps::Job`
- [x] la richiesta: `releases/latest` della repo `gianmarcotaf02/bolt` con `WinHTTP`
      (niente librerie nuove), `User-Agent: Bolt/<versione>`, rimandi seguiti, cinque
      secondi per connettersi e quindici per una risposta. Il corpo massimo accettato
      per JSON e impronte e' 4 MB
- [x] la release deve portare **tutte e due** le cose: un asset `*-setup.exe` e un
      asset `SHA256SUMS`. Se manca l'impronta **non si installa niente**, perche' senza
      impronta non c'e' niente da verificare
- [x] il setup si scarica in `%TEMP%\Bolt-update\`, si calcola lo **SHA-256** con
      `BCrypt`, e se non corrisponde a quello pubblicato il file si **cancella** e
      l'installazione non parte. E' la sola cosa che rende l'aggiornamento piu' sicuro
      del download manuale
- [x] il confronto di versione (`v1.2.3` contro `1.2.3`) legge il tag della release:
      se non e' piu' nuova, il messaggio e' "Bolt e' aggiornato" e non si scarica niente
- [x] la barra di stato dice a che punto e' (controllo, impronte, download con
      percentuale, verifica) e `Esc` annulla. Alla chiusura della finestra il thread si
      ferma e si aspetta, prima di `CoUninitialize`
- [x] se tutto torna, il setup parte con `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`;
      `CloseApplications=yes` e un `WM_CLOSE` chiudono Bolt mentre Inno lavora
- [x] `BOLT_UPDATE_API` sposta l'indirizzo del foglio delle release: e' l'attrezzo con
      cui si prova il percorso download → impronta → rifiuto senza pubblicare niente.
      Non aggira il confronto dell'impronta

**Verificato a schermo il 2026-10-20** (catture `analysis/tmp/upd-*.png`):

- il comando compare nella palette e la richiesta vera a GitHub arriva: nessuna release
  pubblicata → `Aggiornamento non riuscito: Nessuna release e' stata pubblicata per
  gianmarcotaf02/bolt.`
- con un foglio locale (`BOLT_UPDATE_API`) e un asset vero: **impronta sbagliata** →
  `L'impronta SHA-256 del setup non corrisponde a quella pubblicata: il file e' stato
  cancellato e non si installa.` e `%TEMP%\Bolt-update` vuota;
- release **senza** `SHA256SUMS` → `La release non pubblica le impronte SHA-256: non si
  installa niente.`;
- tag uguale alla versione installata → `Bolt e' aggiornato (0.1.0).`

**Anche il caso riuscito, con le release vere.** Il 2026-10-20 `gh` e' stato installato
(2.102.0) e autenticato, e sono state pubblicate **`v0.1.0`** e **`v0.1.1`** con setup,
zip portatile e `SHA256SUMS`:

- Bolt **0.1.0** installato davvero (`%LOCALAPPDATA%\Programs\Bolt`), `Controlla
  aggiornamenti` → ha visto `v0.1.1`, scaricato il setup, verificato lo SHA-256, avviato
  il setup in silenzioso; la finestra si e' chiusa, Inno ha aggiornato, e
  «App e funzionalita'» dice **0.1.1** (verificato anche dal registro, e col fatto che
  il setup silenzioso non rilancia l'applicazione: `skipifsilent`);
- rilanciata la **0.1.1** installata, lo stesso comando dice `Bolt e' aggiornato
  (0.1.1)` — il feed vero, non il foglio di prova;
- la 0.1.1 e' **la stessa base della 0.1.0** (nessuna correzione funzionale): esiste per
  provare l'aggiornamento fino in fondo, ed e' scritto nelle sue note;
- il 2026-10-21 la stessa strada ha portato la **0.1.2** (la correzione di §7.64) e poi
  la **0.1.3** (§7.65) sull'installazione: ogni volta download, impronta, setup in
  silenzioso, e «App e funzionalita'» dice la versione nuova. Tre aggiornamenti veri, con
  tre release diverse.

**Limiti dichiarati.** Il setup scaricato resta in `%TEMP%\Bolt-update\` dopo
l'installazione (lo sta leggendo il setup). Non c'e' un controllo periodico ne' una
notifica: solo il comando. Il comando non ha un tasto di partenza, chi lo vuole se lo
rimappa (`Ctrl+,` → Key Map). `BOLT_UPDATE_API` resta l'attrezzo da banco per provare i
rifiuti senza pubblicare niente.

---

## Ordine consigliato per un progetto con scadenza

Se il tempo è limitato, la sequenza che massimizza il risultato dimostrabile è
**M1 → M2 → M5 → M6**, con **M3** come approfondimento se resta tempo.

Il motivo è che M1 più M2 più M5 producono un file manager realmente usabile, con
un'architettura di rendering non banale da mostrare. M3 è la parte tecnicamente
più caratteristica del prodotto originale, ma resta un modulo isolato: va
affrontata con un percorso di fallback a enumerazione semplice, in modo che un
risultato parziale non comprometta il resto del progetto.

---

## Registro del reverse

Man mano che il reverse mirato procede, ogni domanda risolta va annotata qui con
la funzione di origine e il risultato.

| # | Domanda | Funzione | Stato | Risultato |
|---|---|---|---|---|
| 1 | ~~aggregazione MFT~~ | — | **falsificata** | Non c'è parsing MFT: manca la firma `FILE` in `.text` e le stringhe `NTFS`/`$MFT`/`USN`. Vedi `docs/01`. |
| 2 | dove viene controllato il budget di tempo di `SumDirStats` | `FUN_140070660` / `FUN_140070ae0` | **parziale** | Ricostruita la macchina: job creato da `FUN_140070660` con buffer ≥ 1 MB e callback `FUN_140070ae0`; l'invalidazione è per generazione (`DAT_140249268`). Il budget di tempo non è ancora localizzato. |
| 3 | parametro differenziante SSD/HDD/rete | da individuare via `Setting_CalcFolderSizesSSD` | aperta | — |
| 4 | formula del punteggio recenti | da individuare via `"Score": %d,` | aperta | — |
| 5 | semantica di `Ordinal` | idem | aperta | — |
| 6 | gestione reparse point e hard link | da individuare | aperta | — |
| 7 | uso preciso di `DeviceIoControl` | unico chiamante | aperta | Ipotesi: `FSCTL_GET_NTFS_FILE_RECORD` per la tabella `AT_*`. |
| 8 | quale funzione è `SumDirStats` e quale `LoadDirectory` | `FUN_1401e81c0` vs `FUN_1401fdfe0` | aperta | Entrambe chiamano `FindFirstFileExW`+`FindNextFileW` e sono ricorsive. Discriminare confrontando i callee (5 contro 18). |
| 9 | il worker di enumerazione | — | **identificato il set** | Solo due funzioni in tutto il binario chiamano `FindFirstFileExW`. Vedi `analysis/out/reverse/FINDINGS.md`. |

### Verifiche gia' eseguite su `AT_IndexRoot`

Tracciate per non ripeterle:

| Passo | Comando | Esito |
|---|---|---|
| import + overview | `rea inspect --provider ghidra --snapshot` | Ghidra 12.1.4, PE x86_64, 5 sezioni, import in 5 min 20 s |
| ricerca stringa | `rea search "AT_IndexRoot" --kind strings` | `0x14021a578` |
| riferimenti | `rea xrefs 0x14021a578` | solo `0x140247e90`, che sta in `.data` |
| tracciamento | `rea trace "AT_IndexRoot"` | nessun riferimento da codice, `not_in_procedure` |
| riferimenti al puntatore | `rea xrefs 0x140247e90` | vuoto |
| struttura tabella | scan `.data` | 16 puntatori contigui da `0x140247e50`, indicizzati per tipo di attributo |
| scan codice | displacement RIP-relative su tutto `.text` | 3 candidati, tutti **falsi positivi** (verificato decompilando) |
| nome del job | `rea search "SumDirStatsWorkUpdate"` | `0x14022d8e8` (messaggio) e `0x14022d918` (nome) |
| riferimenti al nome | `rea xrefs 0x14022d918` | `0x140070be9` -> completion handler |
| macro di enumerazione | `analysis/tools/callers.mjs FindFirstFileExW` | slot IAT `0x140219430`; solo **2** siti di chiamata |
| macro di enumerazione | `analysis/tools/callers.mjs FindFirstFileW` | 4 siti di chiamata |
| reperto | `analysis/out/reverse/FINDINGS.md` | ricostruzione completa di `SumDirStats` |

**Regola ricavata:** in questo binario non si entra dai dati. Le tabelle di
`.data` non hanno riferimenti statici; si entra dalle stringhe referenziate
dal codice (nomi di job, messaggi diagnostici) e dagli import.

Strumenti nuovi e riutilizzabili: `analysis/tools/callers.mjs` (trovai siti di
chiamata di una funzione importata, dallo slot IAT) e
`analysis/tools/refs-to.mjs` (riferimenti a un indirizzo interno).

### Il riferimento guardato dal vivo (v0.8.5), su richiesta dell'autore

L'autore ha chiesto di aprire il prodotto originale e navigarlo, per migliorare il
piano. L'ho aperto **sulla sandbox del progetto**, così che quello che si vedeva
fosse roba di progetto, e le catture sono state **guardate e cancellate**: due
mostravano i file recenti e le cartelle di chi lavora, e `AGENTS.md` §0 dice che una
cattura è una copia.

**Quello che conferma la ricostruzione.** La finestra principale ha le stesse
sezioni nella barra laterale (`QUICK ACCESS`, `BOOKMARKS`, `STORAGE` con la barra di
occupazione, `PLACES` col cestino in fondo), le stesse colonne
(`Name/Type/Modified/Folders/Files/Size`), il piè di pagina col filtro e
`Group: None  n / n`, l'inspector a destra e i pulsanti di navigazione a sinistra
del percorso. La nostra ricostruzione era già quella lì.

**Quello che non abbiamo, e che vale la pena mettere in fila.**

1. **Una barra inferiore** con, da sinistra: il campo di filtro, i selettori di
   vista (`2`, `3`, elenco, dettagli) e una **percentuale di ingrandimento** che
   riguarda le *anteprime*, non l'interfaccia (`0%` = nessuna anteprima). Da noi il
   filtro sta sotto la lista e i selettori di vista stanno solo nella palette.
2. **Le colonne hanno il filtro su tutte le intestazioni**, anche su `Name`: da noi
   il nome non ce l'ha, perché un valore per nome non filtra niente. Vale la pena
   riguardarlo: forse là il filtro del nome non è per valori ma per testo.
3. **Una modalità con la barra laterale di ricerca**: aprendo il pannello laterale
   compaiono `Recents` (file e cartelle recenti), una casella «Find Quick Access…»,
   `Bookmarks`, `Storage`, `Places` e le **schede**. Cioè: i recenti sono una
   sezione vera, non un elenco che si apre quando serve — ed è la cosa che M4
   chiama «ranking dei recenti».
4. Le schede ci sono anche in questa versione, e stanno sopra il percorso.

**Quello che non ho trovato.** La superficie delle **impostazioni**: il pulsante in
alto a sinistra apre e chiude la barra laterale — come da noi — e non ho trovato
dove File Pilot metta le preferenze. Quindi per M9 il piano resta quello che
abbiamo: la palette è la superficie delle preferenze, perché è lì che si accendono
le chiavi; e le preferenze **si ricordano** in `%APPDATA%\Bolt\settings.ini`,
testo leggibile con una riga per preferenza, che si cancella a mano.

### Le tre cose dell'originale che mancavano — *Viste*

**1. I selettori di vista e la percentuale delle anteprime, nella barra in fondo.**
Da noi la riga in fondo è la barra di stato, che è larga quanto la finestra, ed è
lì che stanno adesso: quattro pastiglie `Dettagli`/`Elenco`/`Riquadri`/`Icone` (la
vista attiva è accesa) e uno stepper `Anteprime − % +`. La percentuale va da **0 a
100**: `0%` spegne le anteprime e lascia l'icona del tipo, `50%` disegna la
miniatura a metà della sua casella, `100%` è la misura piena. Oltre il 100 non si
sale **dichiaratamente**: la casella di una cella è quella, e una miniatura più
grande finirebbe sopra l'etichetta. La scelta si ricorda in `settings.ini`.

**2. Il filtro sulla colonna `Name`.** Il nome non ha un elenco di valori — ogni
nome è diverso — quindi la sua linguetta non apre il pannello delle caselle come le
altre colonne: **accende la casella di testo in fondo**, che è il filtro del nome
che serve. La linguetta si accende quando il filtro è in uso, come quella delle
altre colonne quando filtrano.

**3. La sezione `RECENTS` nella barra laterale.** Le cartelle visitate in questa
sessione, la più recente per prima, con il nome della cartella (non il percorso). È
il **ranking dei recenti di M4** nella sua forma minima: solo cartelle, e solo la
sessione (`StoreRecents: Session`), perché `docs/06` vieta di lasciare traccia di
cosa si è aperto. Un recente che è la cartella aperta non si mostra: riproporre dove
si è già non serve.

### L'originale ha un pannello di opzioni, non una lista di comandi — *Visto*

L'autore ha aperto il prodotto originale e ne ha mandato due catture, su sua
richiesta. Da quelle si ricostruisce la superficie che M9 deve avere, e si corregge
una cosa che avevamo dato per buona.

**Il menu principale** (il pulsante in alto a sinistra) contiene:

| voce | scorciatoia |
|---|---|
| `GoTo` | `Ctrl+P`, `F4` |
| `Commands` | `Ctrl+Shift+P` |
| `Load Layout` / `Save Layout` | — |
| `Options` | `Ctrl+,` |
| `File Pilot Setup` | — |

**Le nostre due scorciatoie erano scambiate rispetto all'originale**: da noi la
palette dei comandi stava su `Ctrl+P` e il GoTo su `Ctrl+G`. Allineate: `Ctrl+P` (e
`F4`) al GoTo, `Ctrl+Shift+P` ai comandi, `Ctrl+,` alle opzioni. `Ctrl+G` resta
libero; ciclare il raggruppamento sta su `Ctrl+Shift+G`.

**Il pannello delle opzioni, adesso, c'e'.** E' una lista piatta e cercabile
(`GroupMenu` con `richRows`), alta due linee per voce, aperta con `Ctrl+,`. I
controlli sono tipizzati (`OptionControl`): testo (`Font`, `Inspector Font`),
stepper (`Font Size`, `Inspector Font Size`), slider (`Spacing`, `Zoom`, misura
della cella), interruttore (tutte le chiavi di interazione e `Alternating Row
Colors`, `Show The Sidebar`), scelta con sottoelenco (`Language`). Le voci che
l'originale ha e che qui non sono applicate si mostrano **spente**, con la ragione
accanto.

**Il pannello delle opzioni dell'originale** e' una **lista cercabile**, non una
griglia di controlli: in cima un campo «Select an option...», sotto una voce per
opzione, ognuna con tre cose —

1. il **nome** (`Font`, `Font Size`, `Spacing`, `Animations`, ...),
2. una **descrizione** che dice a cosa serve (`Defines the space between items in a
   list.`): il *perche'* sta accanto alla scelta, non nella documentazione,
3. un **controllo del tipo giusto** per quella opzione:
   - campo di testo (`Font` → un nome di font installato),
   - **stepper** `− 9 +` (`Font Size`),
   - **slider** (`Spacing`, `Inactive Panel Content Opacity`),
   - **interruttore** (`Animations`, `Round Corners`, `Alternating Row Colors`,
     `Show File Extension`, `Long Paths Support`, `Display Compact Paths`),
   - **pulsante con freccia** per le scelte che aprono qualcosa (`Color Scheme` →
     `Voidstar >`).

In fondo, la **tastiera scritta nel pannello**: `Up` `Down` navigano, `Invio` usa,
`Esc` chiude. E' la stessa promessa che il nostro `GroupMenu` fa nel suo piè di
pagina, quindi il componente c'e' gia': quello che manca sono i **controlli tipizzati**
dentro le righe, che oggi sono tutte testo.

**Una lezione di interfaccia da copiare**: `System Light / Dark Scheme` si vede
**spento**, perche' li' un tema e' gia' stato scelto a mano. Un'opzione che non si
puo' applicare si mostra disabilitata invece di sparire — la stessa regola per cui da
noi le voci di menu che vanno su un percorso sono spente dentro una cartella di
spazio dei nomi.

**Opzioni viste**, nell'ordine in cui compaiono: `Font`, `Font Size`, `Inspector
Font`, `Inspector Font Size`, `Spacing`, `Animations`, `Round Corners`, `System
Light / Dark Scheme`, `Color Scheme`, `Alternating Row Colors`, `Inactive Panel
Content Opacity`, `Show File Extension`, `Long Paths Support`, `Display Compact
Paths`, e altre sotto il bordo.

**La lingua** non compare fra queste: sta piu' in basso o altrove. Da noi la scelta
della lingua c'e' gia' ed e' nella palette, che per ora e' anche la nostra superficie
delle preferenze: quando arrivera' il pannello delle opzioni, la lingua ci si sposta.

### Il secondo confronto dal vivo (v0.8.5) — *Visto il 2026-10-21*

L'autore ha chiesto di riaprire il riferimento e confrontarlo con la nostra
applicazione, per trovare quello che ancora manca. Aperto **sulla sandbox**, in
un passaggio non programmato: un clic che doveva chiudere una sezione della barra
laterale e' caduto su `PLACES` e ha aperto la cartella utente di chi lavora: la
cattura e' stata cancellata appena vista e da li' in poi si e' catturato **solo il
riquadro a destra della barra laterale**, cosi' i nomi personali non entravano nel
file. La lezione e' quella di `AGENTS.md` §0: su una finestra di cui non si conosce
la geometria, il primo clic e' un tiro.

**Quello che l'originale ha e noi no** (in ordine di valore):

1. `Toggle Subfolders` (`Ctrl+E`): il contenuto delle sottocartelle dentro la lista,
   con il genitore accanto al nome (`vecchie > immagini`), il titolo della scheda che
   diventa `sandbox [...]` e la barra di stato a coppie *visibili/totali* (`4/5`
   cartelle, `3/26` file).
2. `Split Right` (`Ctrl+Shift+S`) e `Split Bottom` (`Ctrl+S`): piu' pannelli nella
   stessa finestra, ognuno coi suoi tab.
3. Il **menu contestuale e la lista dei comandi sono cercabili**, con la stella per
   fissare una voce e il `+` per assegnarle un tasto (`Select a system command...`).
4. Il **menu dell'intestazione di colonna** e' per le colonne (`Shift Column
   Left/Right`, `Size Column To Fit`, `Size All Columns To Fit`, `Close Column`,
   `Restore Default / Factory Columns`), e il filtro per **valori** sta
   sull'imbuto: due gesti, due pannelli.
5. Colonne che non abbiamo: `Date Deleted`, `Original Location`, `Free Space`, `File
   System`, `Percent Full`, `Volume Usage`, `Folder Usage`, `Empty 1..4`.
6. `Load Layout` / `Save Layout` (finestra intera, non per cartella) e
   `LoadOnStartup = LastSession` (tab e pannelli tornano com'erano).
7. `Dare accesso a ▸` fra i verbi della shell (adesso c'e': vedi sotto).
8. Ogni popup scrive in cima **che cosa fa** e **`Cmd: nome (tasto)`**: il menu
   insegna il comando nel punto in cui lo si usa.

**Quello che invece c'era gia', e la documentazione diceva di no.** Gli accenti nei
verbi della shell: `docs/02` §7.62 dice che si ripiegano su ASCII, ma
`ShellLabelToAscii` passa il latino-1 dal 2026-10-20 e l'atlante lo disegna. Il
commento in `shell_menu.cpp` era rimasto indietro rispetto al codice — un difetto
silenzioso della stessa famiglia di «un campo scritto e mai letto», solo al
contrario.

**Fatto in questo passaggio** (costruito, lanciato e guardato):

- il menu del **tasto destro sull'intestazione** (`BuildColumnOpsMenu`), con
  `Adatta questa colonna`, `Adatta tutte le colonne`, `Chiudi colonna`,
  `Ripristina le colonne` e `Colonne di fabbrica` spenta con la ragione accanto
  (`FitColumn`, `ResetColumns`, `ColumnState::hidden`);
- la **testata del comando** in cima ai menu (`GroupMenu::caption`, `command`,
  `commandKeys`, e `MenuListTop` come unica definizione della geometria): la usano
  il menu della colonna e quello dei valori;
- nella barra in fondo, i **conteggi di cartelle e file** (`2/4` con una selezione,
  `5 cartelle` senza) e il **pulsante della cartella nuova**;
- `Toggle Subfolders` (`Ctrl+E`, `CmdToggleSubfolders`): il contenuto delle
  sottocartelle dentro la lista, con il genitore accanto al nome (`appunti.txt >
  documenti`, `sbiadita.png > immagini\vecchie`). Le voci piatte portano il loro
  **percorso assoluto** in `Entry::fullPath`, e `EntryLocation` lo preferisce: senza
  quello, copiare o cancellare una riga piatta avrebbe agito su un file che non
  esiste. Due limiti dichiarati — profondita' 3 e 2000 voci — e quando mordono lo
  dicono nella barra di stato invece di mostrare un elenco a meta' come se fosse
  completo. Verificato: 7 voci, poi 31 con `Ctrl+E` (cinque cartelle e ventisei file,
  gli stessi numeri che da' il riferimento), poi di nuovo 7;
- la palette dei comandi mostra i **tasti come pastiglie** a destra di ogni voce
  (`GroupMenuItem::chips`, `GroupMenu::detailAsChips`), uno per tasto: `Vai a una
  cartella...` porta `Ctrl+P` e `F4` affiancati, che e' come li scrive il
  riferimento. Il dettaglio **non si disegnava affatto**, perche' alla palette
  mancava `detailed`: un campo che nessuno leggeva;
- `Dare accesso a ▸` e' tornato nel menu contestuale. Non era un verbo che mancava:
  la shell lo offre (`Windows.Share`), e `IsShellOwnSubmenuVerb` lo saltava come
  sottomenu "non eseguibile". Il sottomenu si popola benissimo con lo stesso
  `HandleMenuMsg(WM_INITMENUPOPUP, ...)` che gia' serviva a WinRAR e 7-Zip:
  verificato, due voci (`Rimuovi accesso`, `Utenti specifici...`) senza una riga di
  codice nuova. Il salto resta per `Windows.SendTo`, perche' "Invia a" lo
  costruiamo noi e due voci con lo stesso nome sarebbero un doppione;
- le tre colonne del **volume** — `Spazio libero`, `Occupazione`, `File system` —
  con le spunte di visibilita' nel menu dell'intestazione (`[x] Tipo`, `[ ] Spazio
  libero`, ...). Il valore e' del disco, non della voce, quindi le colonne nascono
  spente (`ColumnDef::defaultHidden`) e si leggono una volta per cartella
  (`RefreshVolumeFacts`, `AppState::volumeFacts`): tre letture di disco per
  fotogramma sarebbero tre letture per fotogramma. Verificato: `187.7 GB` — lo
  stesso numero che la barra laterale scrive per `Windows (C:)` — `80%` e `NTFS`.

**Quello che si e' scoperto facendolo**: il disegno delle celle fermava la lista
delle colonne alla dimensione (`drawRight(8, ...)` e basta), quindi una colonna
nuova sarebbe stata larga e **vuota**. Adesso le colonne dopo `Size` si disegnano
in un ciclo che chiede il valore a `ColumnValueOf`: una colonna nuova si vede senza
toccare il disegno, che e' il modo in cui il resto della tabella funziona gia'.

**Colonne dello spec ancora fuori**, con la ragione: `Date Deleted` e `Original
Location` esistono solo nel cestino e li' arrivano dalla shell, non da
`FindFirstFile`; `Folder Usage` non ha una definizione che possiamo verificare;
`Empty 1..4` sono segnaposto configurabili e senza una superficie che le configuri
sarebbero quattro colonne vuote per sempre — un campo scritto e mai letto.

**Resta da fare**, con la ragione: gli split toccano il modello dei pannelli (una
sessione di lavoro con spazio per riverificare tutto); palette e menu contestuale
cercabili vogliono le descrizioni dei 102 comandi e la memoria dei preferiti; le
colonne nuove vogliono i dati che oggi non leggiamo (`Date Deleted` e `Original
Location` esistono solo nel cestino, `Volume Usage` vuole l'occupazione del volume
per ogni voce); layout e sessione toccano la persistenza, che `docs/06` tiene a
freno apposta.

**Un limite noto di `Toggle Subfolders`**: la selezione e' per **nome**, e due file
con lo stesso nome in due sottocartelle diverse si selezionano insieme. E' il prezzo
di un modello in cui l'identita' di una voce e' il suo nome; si sana il giorno in cui
l'identita' diventa il percorso, e non e' una riga.

### Layout della finestra, sessione, e una vista "Elenco" che non c'era

Tre cose chieste dal confronto, fatte e guardate il 2026-10-21.

**`Save Layout` / `Load Layout`** (`CmdSaveLayout`, `CmdLoadLayout`, `CmdForgetLayout`):
la forma della finestra — vista, misura delle anteprime, ingrandimento, ordinamento,
gruppi, barra laterale, sottocartelle e **quali** colonne — salvata in `settings.ini`
(`layout.*`) e rimessa con un comando. Non porta percorsi dentro, quindi sta con le
preferenze. Un layout dice quali colonne, **non quanto sono larghe**: la prima
versione salvava anche le frazioni, e caricandolo in un'altra vista due intestazioni
finivano una sull'altra (`Data modifica` dentro `Cartelle`). La larghezza si riadatta
al contenuto al caricamento.

**`LoadOnStartup = LastSession`** (`CmdToggleRememberTabs`): le schede aperte si
ritrovano al prossimo avvio. Spento di partenza, e **l'unica cosa che questo
programma scrive su disco che non e' una preferenza**: per riaprire le schede bisogna
ricordarsi quali erano. Sta in un file suo, `%APPDATA%\Bolt\session.ini`, e spegnendo
l'interruttore il file viene **cancellato subito**, non alla prossima uscita. Vedi
`docs/06`, che ha una riga nuova apposta. Verificato: tre schede, chiusura, riapertura
senza argomenti → le tre schede tornano, con la terza attiva.

**Difetto trovato facendolo.** `SaveSession` scriveva un file con `active=0` e
**nessuna** scheda: la scheda attiva ha la cartella nello stato vivo, e il suo `Tab`
si riempie solo quando si cambia scheda (`SaveActiveTab`). Adesso la scheda attiva
prende `app.directory`.

**La vista `Elenco` non era una vista.** Disegnava l'intestazione delle colonne come
`Dettagli` e con le colonne strette si leggeva `Data modifica` dentro `Cartelle`; le
righe erano alte 20 px ma contenevano tutte le colonne. Adesso `Elenco` non ha
intestazione, e una riga e' **il nome e la dimensione in fondo** — che e' quello che
il riferimento mostra. `columnX` si riempie lo stesso, perche' e' il punto in cui il
disegno delle righe legge dove finisce il nome: a zero il nome non si sarebbe visto
affatto.

### Spostare le colonne, e i due dettagli di `Toggle Subfolders`

**`Shift Column Left/Right`** (`ShiftColumn`), che era l'unico pezzo del menu
dell'intestazione ancora fuori. Non serve un ordine da tenere da parte: **l'ordine e'
la frazione**. Spostare una colonna scambia la sua frazione con quella della vicina e
ricalcola il bordo dell'altra, cosi' ognuna tiene la sua larghezza; `ColumnEdges` mette
in fila le colonne per frazione invece che per indice, e `NextVisibleColumn`/
`PrevVisibleColumn` rispondono "la prossima **a destra**" invece di "`i + 1`". Le
frazioni crescenti sono il caso normale, quindi tutto il resto non si accorge di
niente; `ApplyColumnWidths` scrive le frazioni in ordine **di posizione**, o adattare
le larghezze rimetterebbe l'ordine di partenza. Il nome resta la prima colonna: e'
quella che prende lo spazio che resta. Verificato: `Data modifica` spostata davanti a
`Tipo`, valori al loro posto, larghezze intatte.

**I due dettagli di `Toggle Subfolders`** che mancavano: il titolo della scheda
diventa `sandbox [...]`, e la barra in fondo passa alle coppie **dirette/tutte**
(`4/5` cartelle, `3/26` file) — gli stessi due numeri del riferimento. Senza
sottocartelle la coppia torna a voler dire scelte/totale.

### La stella dei preferiti e il `+`, nella palette

Due dei tre pezzi del punto «palette e menu contestuale cercabili con i preferiti»:
ogni riga della palette porta a destra una **stella** (piena se il comando e' fra i
preferiti) e un **`+`** che apre la mappa dei tasti gia' posata su quel comando e
comincia la cattura — lo stesso `BeginKeyCapture` dell'editor, non una seconda strada.

I preferiti stanno in `settings.ini` come `favorites=56,71`: **id di comando**, non
percorsi, quindi il file resta senza traccia di cosa si e' guardato. Si mostrano in un
gruppo `Preferiti` in cima, e un preferito **non** compare anche nel suo gruppo —
due righe per lo stesso comando sono due cose da tenere allineate.

**Quello che resta di quel punto**, con la ragione: le descrizioni a due linee (una per
comando, 107 righe di testo da scrivere e da tenere vere) e il **campo di ricerca nel
menu contestuale**. Quest'ultimo non e' una casella da aggiungere: `view_context_menu.*`
indicizza le voci con `LevelItem(depth, index)` e un filtro vuol dire un indice di
righe **visibili** frapposto fra il disegno, l'hover, il clic e `ContextMenuActivate`,
che oggi leggono tutti lo stesso indice. Va fatto con spazio per riprovare la tastiera
e i sottomenu.

### La palette a due linee, e le testate del comando

La palette mostra **nome e contesto** su due linee, con le pastiglie dei tasti, la
stella e il `+` sulla destra: e' la forma della lista dei comandi del riferimento.
Il contesto e' quello del **gruppo** (`GroupDescription`, 19 frasi), non una frase per
comando: 107 frasi sarebbero 107 cose da tenere vere una per una, e meta' sarebbero la
ripetizione del nome.

Le testate `Cmd: <comando> (<tasti>)` adesso ci sono su quattro menu — valori di una
colonna, operazioni sulla colonna, palette, opzioni, mappa dei tasti. **Non** sul menu
contestuale: quello non e' un `GroupMenu` e la testata li' vuol dire spostare la
geometria delle righe in `view_context_menu.cpp`.

Un difetto trovato guardando: «Dove sta il file delle impostazioni» stava nel gruppo
`Language`, e la seconda linea diceva «In che lingua si legge il programma». Adesso
sta in `Options`. E' il rischio di una descrizione per gruppo: se il gruppo e' sbagliato,
la riga lo dice ad alta voce.

**Resta di questi due punti**: il **campo di ricerca nel menu contestuale** (il modello
delle righe va filtrato, e lo leggono quattro funzioni) e la testata `Cmd:` in quel menu.

### Pannello dell'ispettore ridimensionabile, e la barra laterale

Il bordo sinistro dell'ispettore si prende e si trascina (`AppState::inspectorWidth`,
una striscia di sei pixel perche' un bordo di un pixel non si prende col mouse). La
larghezza si ricorda in `settings.ini` (`inspectorWidth`), e si chiude **sul rilascio**:
aspettare il primo movimento del mouse dopo lasciava il pannello largo e la preferenza
vecchia — visto provando. Prima era un `292` scritto dentro `ComputeLayout`, e l'unico
modo di avere piu' spazio era chiudere il pannello.

La barra laterale nasce **aperta** (`sidebarVisible = true`): una preferenza salvata a
`0` durante le prove la faceva comparire chiusa, e sembrava un difetto.

### Cosa manca di "A": la ricerca globale

Decisa la forma, non ancora scritta: barra in alto a destra nella riga dei pulsanti,
ricerca **ricorsiva** dalla cartella aperta per nome, risultati nella lista con il
genitore accanto (`Entry::parentHint`, lo stesso di `Toggle Subfolders`).

La strada scelta, per non aprire un thread: un **passo alla volta dal ciclo dei
messaggi** — un `SearchJob` con la coda delle cartelle da leggere, qualche decina per
giro, i risultati che si accumulano, `Esc` che ferma. La finestra resta viva senza
`FileOps::Job` ne' sincronizzazione, e l'annullamento e' una bandierina. Servono: il
campo e il suo bersaglio nella riga dei pulsanti, il testo che si scrive (come il
filtro in fondo), il passo nel ciclo, i risultati al posto di `app.entries`, la riga di
stato che dice quanti ne ha trovati, e il limite dichiarato (voci e cartelle lette).

### La ricerca globale funziona

Barra in alto a destra, ricerca **ricorsiva** dalla cartella aperta per nome, risultati
nella lista con il genitore accanto (`> molte`), `Esc` che ferma e riporta la cartella.
Provata: `voce` nella sandbox → **15 voci in 6 cartelle**, i quindici `voce-*.txt`;
`Esc` → le 7 voci della cartella.

**Due difetti trovati provandola, nessuno dei due si vedeva leggendo il codice:**

1. la query arrivava **vuota** a `StartSearch`: la funzione comincia azzerando
   `app.searchText`, e il testo da cercare era **proprio** `app.searchText` passato per
   riferimento — il primo `clear()` lo svuotava. Con la query vuota `find(L"")` avrebbe
   trovato tutto (visto: 31 voci invece di 15). Adesso il testo arriva **per valore**, e
   un controllo rifiuta la query vuota invece di cercare il nulla.
2. l'avanzamento era su un `SetTimer` e il passo non arrivava mai: la barra si svuotava
   e la lista restava quella di prima. Adesso `AdvanceSearch` si chiama nel **ciclo dei
   fotogrammi**, che e' un giro che succede comunque.

Il passo legge 24 cartelle per giro, con due limiti dichiarati (4000 cartelle lette,
5000 voci trovate): quando uno morde lo dice, invece di far finta di aver finito.
