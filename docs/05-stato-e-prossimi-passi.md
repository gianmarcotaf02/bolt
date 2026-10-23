# Stato del lavoro e prossimi passi

Documento di lavoro, non di progetto: serve a riprendere senza rileggere la
conversazione. Aggiornarlo è l'ultima cosa da fare prima di una pausa lunga o di
una compattazione del contesto.

## Come riprendere in una nuova chat

Incolla questa frase:

> Riprendi Bolt. Leggi `docs/05` §1 e §3 (aggiornati il 2026-10-23) e `AGENTS.md` §0,
> §2 e §2-bis. Lo stato vero: M1–M13 fatte (M2 e M7 restano 🟡 per una scelta e un
> limite dichiarato, non per lavoro mancante), registro difetti `docs/02` §7 a **74 voci,
> zero aperte**, versione **0.1.16** pubblicata e installata, `gh` autenticato. Non serve
> ricostruire niente. Non resta lavoro **funzionale**: le uniche due caselle di `docs/04`
> sono una scelta gia' presa (icone monocromatiche: non si fa) e un limite dichiarato
> (`ShellNew` con `Command`). Il debito e' in `docs/05` §2; i prossimi passi candidati
> sono in §3. Regole: `AGENTS.md` §0 (niente file personali, prove solo in
> `analysis/tmp/sandbox`), §2 (niente e' fatto finche' non e' visto), §2-bis (graphify
> prima del sorgente). Build con
> `"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo`,
> release con `tools/make-installer.ps1` + `gh release create` (setup, zip, `SHA256SUMS`),
> e alla fine `docs/04`, `docs/05` e `graphify update .` dalla root.

**Stato della macchina**: Bolt **0.1.16** installato in `%LOCALAPPDATA%\Programs\Bolt`
(ci e' arrivato aggiornandosi da solo dalla 0.1.14); `gh` 2.102.0 in
`C:\Program Files\GitHub CLI`, autenticato come `gianmarcotaf02`; release `v0.1.0`,
`v0.1.1`, `v0.1.2`, `v0.1.3`, `v0.1.4`, `v0.1.5`, `v0.1.6`, `v0.1.7`, `v0.1.8`,
`v0.1.9`, `v0.1.10`, `v0.1.11`, `v0.1.12`, `v0.1.13`, `v0.1.14`, `v0.1.15`; sandbox pronta in `analysis/tmp/sandbox`;
`settings.ini` con `language=italian` e nessun `startupFolder`. `verify-installer.ps1`
si rifiuta di partire finche' Bolt e' installato: prima si disinstalla.

---

Ultimo aggiornamento: **2026-10-23**, dopo **le venti colonne di `docs/03` e la ricerca nel
menu contestuale**. Le colonne che mancavano ci sono — `Percent Full` separata da `Volume
Usage`, `Folder Usage`, `Data eliminazione` e `Posizione originale` (lette dalla shell per
nome canonico, viste nel cestino su un file della sandbox) e i quattro `Empty` col
contenuto scegliibile dal menu dell'intestazione — e il menu del tasto destro ha il campo
di ricerca in cima. **Nessuna versione pubblicata**: e' lavoro nel repository, non
installato (`docs/04` M5b e M7).

Prima, il 2026-10-21: **il menu dell'applicazione** — il pulsante ☰ apre il menu del
riferimento, con `Vai a...`, `Comandi`, i due layout, `Opzioni`, `Controlla aggiornamenti`
e la versione in fondo, e la barra laterale ha il suo pulsante accanto (`docs/04` M6).
Pubblicata la **0.1.16**.

Prima, nello stesso giorno, **niente piu' preselezione**: entrando in una
sezione o in una cartella non c'e' niente di scelto — prima la prima riga si selezionava
da sola, e un `Canc` appena arrivati cancellava un file che nessuno aveva scelto
(`docs/04` M5). Pubblicata la **0.1.15**.

Prima, nello stesso giorno, **il rettangolo che seleziona mentre si
trascina**: le righe coperte si accendono subito, non al rilascio, e la selezione si rifa'
a ogni movimento da una base — stringendo il rettangolo si deseleziona, e con `Ctrl` si
somma (`docs/04` M5d). Il banco ha guadagnato `-Ctrl` in `tools/drag-mouse.ps1` per
provarlo. Pubblicata la **0.1.14**.

Prima, nello stesso giorno, **la Home a griglia**: la pagina di avvio si
guarda a icone invece che a colonne, e il modo di visualizzazione vive **per cartella** —
uscendo si ritrova quello delle cartelle, rientrando si ritrova la griglia (`docs/04`
M12). Nel farlo e' saltato fuori `docs/02` §7.76: nella cache delle icone grandi la chiave
di una cartella era il *tipo*, quindi tutte prendevano l'icona della prima — la Home
mostrava nove volte il cestino. Pubblicata la **0.1.13**.

Prima, nello stesso giorno, **la chiusura da venti secondi**: con le
statistiche della Home in corso, `DirStatsService::Stop` aspettava che la coda si
svuotasse — cioe' finiva di contare la cartella personale prima di uscire — e il setup di
Inno, in silenzioso, rinunciava a sostituire l'eseguibile di un processo ancora vivo:
l'aggiornamento in app si fermava senza dire niente (`docs/02` §7.75). Adesso chiudere
vuol dire abbandonare la coda: **0,2 s** invece di 20 — e il setup adesso si avvia due
secondi dopo che la finestra si e' chiusa, invece che mentre sta uscendo. Pubblicate la
**0.1.11** e la **0.1.12**, e l'aggiornamento in app 0.1.11 -> 0.1.12 e' la prova che
funziona.

Prima, nello stesso giorno, **le colonne che si adattano**: la larghezza
di partenza e' «adatta» — misurata sul contenuto della cartella aperta, col nome che
prende quello che resta — e il trascinamento di un bordo passa alla scelta a mano, il
doppio clic torna ad adattare (`docs/04` M5b). Pubblicata la **0.1.10**.

Prima, nello stesso giorno, **la Home con le date e i numeri**: «Data
modifica» era vuota in **tutte** le liste — una colonna nascosta non ha posizione, e la
colonna prima prendeva da lei la sua larghezza (`columnX[i+1]` = 0) — e la Home non
chiedeva ne' le date ne' le statistiche, quindi mostrava `--` in quattro colonne
(`docs/02` §7.74). Adesso le righe della Home hanno data e dimensione chieste al percorso,
e i conteggi si riempiono come in una cartella. Pubblicata la **0.1.9**.

Prima, nello stesso giorno, **le icone della Home**: le righe della Home
hanno le stesse icone della barra laterale — `Desktop`, `Documenti`, `Download`,
`Immagini`, `Musica`, `Video` si disegnavano con la cartella gialla generica, perche' la
lista chiedeva l'icona al **percorso** e la barra laterale all'**oggetto** della shell
(`docs/02` §7.73). Pubblicata la **0.1.8**.

Prima, nello stesso giorno, **le due colonne spente di partenza**: `Data
creazione` e `Data accesso` non si vedono finche' non le si accende dalle opzioni —
resta la data di **modifica** — e la colonna nascosta non occupa spazio (`docs/04` M5b).
Nello stesso giro, §7.72: la freccia dell'ordine non si fa piu' mangiare dal taglio del
nome. Pubblicata la **0.1.7**.

Prima, nello stesso giorno, **il pannello non piu' modale e la coda delle
operazioni**: durante una copia la finestra si usa come sempre — navigare, selezionare,
copiare — e quello che si chiede nel frattempo va in coda e parte quando la precedente ha
finito (`docs/02` §7.69). E' stata la decisione che mancava, presa e scritta li'. Nello
stesso giro, due difetti che solo la finestra usabile rendeva visibili: la selezione che
tornava alla prima riga a ogni cambiamento della cartella (§7.70, `ReloadKeeping` si
passava i campi che stava per azzerare) e il conto delle voci che diceva «3001 di 3000»
(§7.71). Pubblicata la **0.1.6**.

Prima, nello stesso giorno, **il conto dei byte, le righe spente e il
recente col nome giusto**:
il pannello dell'operazione conta i byte delle voci finite invece di ricavarne una
frazione dal tempo della shell (`docs/02` §7.66: al 90% dei byte veri diceva 23%),
`GroupMenuItem::enabled` vale in **tutti** i menu — cursore, clic e `Invio`, non solo
nel pannello delle opzioni (`docs/02` §7.67), cosi' il menu dei fissaggi non scrive piu' la
ragione dentro il nome della voce, e un percorso con le barre in avanti non arriva piu'
alle API che lo rifiutano: nella sezione `RECENTI` c'era una riga col percorso al posto
del nome (`docs/02` §7.68). Pubblicate la **0.1.4** e la **0.1.5**, e l'installazione si e'
aggiornata da sola dalla 0.1.3.
**La coda delle operazioni su file c'e'**: il 2026-10-21 il pannello ha smesso di essere
modale e le richieste che arrivano mentre una gira aspettano il loro turno (§7.69). La
prima versione della coda era stata scritta e tolta poche ore prima, ed era la cosa
giusta da fare: senza togliere la modalita' sarebbe stata irraggiungibile.

---

Prima, nello stesso giorno: **la Home** (M12, `docs/04`): la pagina di
avvio e' una **posizione** (`bolt:home`) con tre bande — Fissaggi, Recenti, Cartelle note —
e una riga `Home` in cima alla barra laterale; `startupFolder` vuota apre **lei**, e le
operazioni su file si rifiutano con un messaggio. Prima, nello stesso giorno: il ritaglio
per-quad, i comandi di File Pilot fuori dal menu contestuale, le date europee, le icone del
menu, il pacchetto con Inno Setup e la pagina di avvio. Subito dopo la Home, **gli
aggiornamenti in app** (M13, forma B): comando esplicito, `releases/latest`, SHA-256,
rifiuto se non torna, setup in silenzioso — pubblicate `v0.1.0`, `v0.1.1`, `v0.1.2`,
`v0.1.3` e `v0.1.4`.
**Il 2026-10-21 sono chiusi §7.64 e §7.65**: una cartella aperta da `ACCESSO RAPIDO` non
e' piu' una posizione codificata, l'ispettore torna alle nostre righe, il cestino si
comporta come prima, i `.lnk` hanno l'icona della destinazione con la freccetta, e la Home
ha la casa di Esplora risorse e i nomi tradotti (`Documenti`, non `Documents`).

Prima, in ordine: **`Animations`**, l'ultima riga di M9: i pannelli compaiono in
dissolvenza (160 ms) invece di apparire di colpo. **M9 e' chiusa**; restano M6 (fissaggi di
sessione) e M7 (`ShellNew` con `Command`, ormai l'unica casella).

Prima, in ordine: **i layout per cartella**: *Salva il layout
di questa cartella* e *Dimentica il layout* (`DirHostLayoutSave`/`DirHostLayoutDel`)
ricordano vista, misura delle celle, ordinamento, gruppi e larghezze delle colonne —
in memoria, perche' `docs/06` vieta di scriverli. M9 resta con la sola `Animations`.

Prima, in ordine: **tre colonne e la rinomina**: la lista ha
nove colonne (Data creazione, Data accesso e Totale, tutte ordinabili e filtrabili) e
la rinomina sul posto seleziona il nome senza estensione, come Esplora risorse —
`nota.txt` + `F2` + `promemoria` + `Invio` da' `promemoria.txt`.

Prima, in ordine: **le opzioni del riferimento**: cinque
righe che il pannello mostrava spente adesso si applicano — estensione nei nomi,
percorsi compatti, angoli arrotondati, opacita' dei pannelli inattivi e percorsi
lunghi — con due stati di disegno nel renderer e un solo punto di I/O per i percorsi
lunghi (`docs/02` §7.60: l'argomento oltre i 260 caratteri veniva ignorato). Resta
spenta solo `Animations`, che e' una funzione e non un interruttore.

Prima, in ordine: **il ranking dei recenti** (`docs/04` M4):
la formula di `Score`/`Decay`/`Ordinal` e' stata ricostruita con REA — non dedotta —
ed e' in `src/recents.*`, con i tre modi di `StoreRecents`, la sezione `RECENTI`
ordinata per punteggio, **`ShowRecents`** (il nome si spegne secondo il punteggio),
**`SortRecents`** (i recenti in cima alla lista), i **file aperti** come recenti e il
**GoTo** che li apre, sa cancellarli e mostra anche le cartelle fissate. Chiuso anche
il **toglimento di M6**: il tasto destro su una riga della barra laterale apre il menu
di **quella** riga (fissare, togliere, togliere dai recenti), e i comandi agiscono sul
suo percorso — visto a schermo con la cartella aperta che restava un'altra. Il
decadimento e l'opacita' sono **misurati sui pixel** (picco 220 → 161 fra punteggio 9
e 5; 132 contro 234 fra un recente e una cartella mai visitata).

Prima, in ordine: **la caduta sul `Canc`** (`docs/02` §7.59),
chiusa con AddressSanitizer. Era un `container-overflow`: il fotogramma ridisegnava la
lista con le righe costruite sull'elenco di **prima**, perche' fra il punto in cui la
vista si ricostruiva e il disegno c'e' `FinishOperation`, che ricarica la cartella. La
vista adesso si ricostruisce in `RenderFrame`, un attimo prima di disegnare — e la
sequenza che cadeva 4 volte su 4 adesso non cade piu' (4 su 4 vive).

Prima, in ordine: **`src/commands.*`**: `RunCommand` e i `Do*` sono usciti da
`main.cpp` (591 righe, il file e' a **4.348**), `SetColorScheme` e' andato in
`theme.*`, e la cucitura di `app_actions.h` si e' ricomposta con le superfici che un
comando deve poter aprire.

Prima, in ordine: **i cinque pulsanti di navigazione**
disegnati come forme (chiesto dall'autore: «non sono belli» → «più *minimal*, non
così spesse, sulla stessa riga dell'indirizzo»). Erano i caratteri `←` `→` `↑` `↻` `★`:
quattro famiglie di segni con quattro spessori e quattro altezze ottiche in una fila
sola. Adesso sono tre forme SDF (freccia, aggiorna, stella) in **tutt'e due** gli
shader, in una scatola di 12 px con spessore 1,2, centrate sulla riga dell'indirizzo
**misurata** e non a occhio — e la stella delle righe fissate nella barra laterale,
che era ancora `★`, è la stessa forma. `docs/02` §7.58, catture in
`analysis/tmp/nav-*.png`.

Prima, in ordine: **i valori delle colonne nel carattere dei
dettagli** (chiesto dall'autore): tipo, data, cartelle, file e dimensione si leggono
più piccoli del nome, e la colonna `Tipo` si è allargata a 0,45 perché `Cartella di
file` e `Documento di testo` ci entrino interi (`docs/04` M5c, catture in
`analysis/tmp/font-*.png`). Il nome resta nel corpo.

Prima, in ordine: **le due piccole bugie** — e sono finite in
quattro. `docs/02` §7.55, che era l'ultima voce **aperta** del registro: l'ispettore
mostrava una voce che il filtro aveva nascosto. La strada è quella che il documento
indicava come più coerente — **il cursore si sposta** sulla prima riga visibile, come
in Esplora risorse — e sta dentro `RebuildView`, perché a nascondere righe non è solo
il filtro: ci arrivano anche i filtri di colonna e un gruppo chiuso. La **selezione
non si tocca**, ed è la decisione che la voce chiedeva. Nel verificarla sono uscite
tre superfici con lo stesso difetto, chiuse con la stessa regola: **il suggerimento**
restava appeso alla voce di prima quando il filtro ne toglieva la riga da sotto il
puntatore; il **suggerimento sopra il pannello delle proprietà** (la regola per farlo
tacere adesso è una sola, `TooltipsAllowed`, e nomina i pannelli); la **barra di stato**
contava le scelte nascoste («3 selezionati» davanti a una riga sola). E una cosa
vista e corretta mentre si provava: azzerare il cursore quando nessuna riga è visibile
faceva perdere anche `Invio` e non tornava togliendo il filtro — adesso il cursore
resta dov'è.

**Il registro dei difetti e' a zero.** Sessantacinque voci, tutte chiuse: le ultime sono
§7.61 (il ritaglio per-quad, debito saldato), §7.62 (il menu contestuale che offriva i
comandi di File Pilot) e §7.64 (l'Accesso rapido che apriva le cartelle come posizioni
codificate) e §7.65 (icone dei `.lnk`, nomi della Home, icona della Home). Restano i
**limiti dichiarati**, che sono un'altra cosa
e stanno dove vivono.

Prima, in ordine: la **chiusura del Direct3D 11** (M10, `docs/02` §7.52). Era il primo
lavoro dell'ordine deciso il 2026-10-18, ed era l'unico punto in cui il programma
**diceva il falso**: si dichiarava con due backend e ne girava uno, e l'altro non
compilava — `point` e' una parola riservata in HLSL. Adesso il parametro si chiama
`p`, i due shader compilano, il backend **disegna** lui (`--- backend: Direct3D 11` nel
registro) e il giro di verifica è stato fatto su otto superfici. Il passo che ha reso
la cosa economica è stato compilare gli shader con `fxc` **senza aprire finestre**.

Prima ancora, in ordine: la **mappa dei tasti** (M9) e il suo
**editor**. Le scorciatoie non sono piu' quarantasei `case` dentro `WindowProc` ma una
tabella in `src/keymap.*`, e il tasto chiama **lo stesso** `RunCommand` della palette
(le due copie dello stesso corpo sono sparite). Le righe di fondo dei menu si scrivono
da lei. E l'**editor** adesso c'e': una riga `Key Map` fra le opzioni (e
`CmdEditKeyMap` nella palette) apre i comandi col tasto accanto, e da li' si rimappa,
si toglie e si torna ai tasti di partenza — `docs/04` M9 ha cosa fa e i limiti che
dichiara. Nella stessa sessione l'autore ha **chiesto** perché la palette dei comandi
fosse ancora in inglese con la lingua su italiano, e la risposta era in tre posti:
le **bande dei gruppi** di ogni menu, le **righe del pannello delle opzioni** e
**un comando** su ottanta — tutte cose che si disegnavano da una **chiave** del
riferimento invece che da una frase, e che il paragrafo «Cosa resta in inglese» di
`docs/04` diceva di lasciare in inglese. Adesso sono tradotte e la chiave non si e'
mossa: chiuso `Vista` in italiano, in inglese la banda si chiama `View` ed e' **ancora
chiusa**. Vedi `docs/02` §7.56. Poi, **sempre in ordine**, il **fissaggio in qualunque
sezione** (M6): `Fissa in...` e `Togli da...` mettono una cartella in accesso rapido o
nei segnalibri, e nell'accesso rapido le nostre righe stanno in cima a quelle di
Windows con la stella che le distingue — `docs/04` M6, e `docs/02` §7.57 per il difetto
che si e' pagato strada facendo. **Poi il trascinamento fuori dal cestino** (§7.34),
che era il primo della lista come *diagnosi*: il difetto non c'era piu' — una voce
riciclata trascinata in una cartella si ripristina li' — e i due giri precedenti
avevano sbagliato per colpa del **banco**, non di OLE (il primo piano non vinto, e una
riga misurata a occhio invece che a pixel). Nel farlo e' successa una cosa che vale piu'
della diagnosi: **una finestra di prova e' nata sulla scrivania virtuale su cui l'autore
stava studiando**, e portarla in primo piano gliel'ha messa davanti. Da li' la regola
nuova in `AGENTS.md` §0 — la scrivania di chi lavora non e' un banco di prova — e
`tools/focus-window.ps1`, che si rifiuta di attivare una finestra che non sta sulla
scrivania corrente. L'autore aveva anche
**deciso**: per **M6** il fissaggio vale per **qualunque sezione** (accesso rapido,
segnalibri, o anche una cartella standard come Desktop), con le due conseguenze
scritte in `docs/04` — la lista della shell non si scrive, e i fissaggi restano in
memoria; **M4, M7 e M9** restano come sono. Prima, il secondo blocco di
**spezzamento**: la **barra di stato** (`src/view_statusbar.*`) e i **menu a gruppi**
(`src/view_menu.*`, 535 righe), e `main.cpp` da 8.080 a **7.414**. Prima,
l'**ispettore** intero (`src/view_inspector.*`, 1.053 righe), che l'aveva portato da
9.134 a 8.080. Prima, il **contenuto della
cartella nell'ispettore**: scegliendo una cartella, il pannello ne mostra in fondo le
prime voci **in elenco**, con la loro icona o miniatura, **senza aprirla** (funzione
chiesta dall'autore; `PeekEntry` in `src/app_state.h`). Prima, il **dialogo di
progressione di M7**:
copia, spostamento e cestino girano su un **thread di lavoro** con il conto dei byte,
la voce in corso e un annullamento che ferma davvero (`FileOps::Job` in
`src/file_ops.*`, il pannello in `main.cpp`). E' la prima cosa di M7 che si tocca con
le mani — prima l'operazione teneva ferma la finestra — e ha portato con se' due
cose che il documento di progetto dava per vere e non lo erano: `IFileOperation`
**non ha** un `Abort`, e i "punti" di `UpdateProgress` **non sono** byte (§5).
Prima, il primo **spezzamento di `main.cpp`**: il confine in
`src/app_state.h`, i supporti in `src/app_ui.*`, e fuori quattro moduli —
`view_sidebar`, `view_titlebar`, `tabs` e la **lista con le colonne**,
`src/view_list.*`. Poi il **tema chiaro**
(`src/theme.*`: i ruoli di colore che hanno un lettore, le due tavolozze, la scelta
dal pannello e dalla palette, `SystemColorScheme` che segue Windows anche a
finestra aperta) e **tre difetti trovati mentre si faceva**: il **suggerimento che
leggeva la riga 1 di 1** (`docs/02` §7.51, una caduta riproducibile entrando in una
cartella dopo aver scelto un file di testo), il **backend Direct3D 11 che non
poteva partire** (§7.52, `point` come nome di parametro in HLSL) e il **pidl
liberato e poi usato** (§7.53, in `ShellLocation::Read`: il ripristino dal cestino
riceveva l'identita' di un blocco riusabile). Prima: la
**sorveglianza delle cartelle** (M3 chiusa: i numeri si aggiornano da soli, §7.47 e
§7.48), le **linguette come elementi** invece che
come caratteri (un triangolo nello shader, §7.49) e i **pulsanti della barra del
titolo** come forme (piu' e croce, §7.50), e il **corpo dei dettagli piu' piccolo**
di 4 px sotto il corpo principale, derivato (§7.12). Prima ancora: il
**pannello delle opzioni** (M9, `Ctrl+,`), l'**allineamento delle scorciatoie**,
la **lingua su tutte le superfici**, la **salita da una radice di volume**, le
**tre cose viste nell'originale**, l'**albero dei tab verificato**, le **icone**
— oggetto, applicazione, e grandi alla misura giusta (§7.43, §7.44,
§7.45) —, i **byte allocati** (§7.46), **`InspectorHideGrid`** e il **backend
Direct3D 11** (M10).
Registro: **sessanta** voci, **tutte chiuse**. §7.59 (la caduta sul `Canc`) e §7.60
(l'ispettore e il filtro) sono state le ultime, il 2026-10-18; le superfici sorelle
di §7.55 — suggerimento, suggerimento sopra i pannelli, barra di stato — sono
entrate nella stessa voce perché sono la stessa regola. La caduta
rara all'avvio (§7.54) e' chiusa il 2026-10-18: era §7.53 visto dall'altro lato, con
la pila che era gia' in `%LOCALAPPDATA%\CrashDumps`. §7.34 non ha piu' un residuo
dichiarato: il trascinamento fuori dal cestino e' stato visto funzionare. **Una** cosa
da fare, piu' quelle che vogliono una sessione propria: i limiti dichiarati stanno dove
vivono.

---

## 1. Dove siamo

| Milestone | Stato |
|---|---|
| M1 — shell e rendering | ✅ |
| M2 — icone e anteprime | 🟡 oggetti e applicazioni fatti, manca la resa monocromatica (provata e tolta su richiesta dell'autore) |
| M2b — navigazione | ✅ |
| M3 — statistiche di cartella | ✅ byte logici e **allocati**, e i numeri si aggiornano da soli quando la cartella cambia da fuori (`ReadDirectoryChangesW`, con il limite dichiarato di due livelli) |
| M3b — attributi NTFS | ✅ il record del file letto dal volume raw (`FSCTL_GET_NTFS_FILE_RECORD`), i nomi della tabella `AT_*`, la lista nell'ispettore e la degradazione dichiarata senza privilegi (`src/ntfs_attributes.*`, `docs/04` M3b) |
| M4 — ranking dei recenti | ✅ la formula di `Score`, `Decay` e `Ordinal` ricostruita da REA (`FUN_140189bb0`, `docs/04` M4), in `src/recents.*`: punteggio a 3 per visita che satura a 10, decadimento dei fratelli **misurato**, `Ordinal` come contatore delle visite, la sezione `RECENTI` ordinata per punteggio, **`ShowRecents`** (il nome si spegne secondo il punteggio) e **`SortRecents`** (i recenti in cima alla lista); i **tre modi** di `StoreRecents` con la preferenza che non si salva (`docs/06`), i **file aperti** come recenti (peso 1) e il **GoTo** che li apre e ha «Cancella i recenti» in fondo al gruppo. Resta solo il fissaggio di un recente nei gruppi di accesso rapido |
| M5 — filtro, ordinamento, raggruppamento, selezione | ✅ |
| M5c — modalità di visualizzazione | ✅ quattro viste, celle **derivate** da una misura continua (icona, larghezza e altezza), raggruppamento in griglia e nell'elenco, ridimensionamento continuo con `Ctrl`+rotella, **selettori di vista e misura delle anteprime nella barra in fondo**, e **icone grandi chieste alla misura giusta** invece di ingrandite (`docs/02` §7.45) |
| M5d — sovrappasso e comportamento del clic | ✅ evidenzia righe e celle, e le otto chiavi di interazione: `HoverToSelect`, `UnderlineOnHover`, `ShowTooltips`, `SingleClick`, `SingleClickFoldersOnly`, `ActOnPress`, `DoubleClickToParent`, `ShowCheckboxes` |
| M5b — colonne | ✅ ridimensionabili col mouse, con filtri per valore nelle intestazioni e **filtro di testo sul nome**; **venti** colonne (`docs/03` §5), tutte ordinabili. Le undici che non si vedono di partenza si accendono dal menu dell'intestazione — che ha la ricerca — comprese `Percentuale`, `Uso cartella`, `Data eliminazione` e `Posizione originale` (le due del cestino, lette dalla shell per nome canonico) e i quattro `Empty`, il cui contenuto si sceglie dallo stesso menu (`docs/04` M5b). Limite dichiarato: il contenuto di un `Empty` vive nella sessione |
| M6 — tab, barra del titolo, palette, GoTo | ✅ tab, titolo, palette, GoTo, **barra laterale a sezioni** (fissaggi, recenti, segnalibri, volumi, cartelle standard), **pulsanti di navigazione**, **albero dei tab verificato** (creazione per trascinamento, chiusura per ramo, distacco), il **fissaggio in qualunque sezione** e il **menu contestuale delle righe** della barra laterale: `Fissa in...` / `Togli da...` mettono e tolgono una cartella in accesso rapido o nei segnalibri — dalla palette (la cartella aperta) e dal tasto destro su una riga (quella riga) — con la stella che distingue le nostre righe da quelle della shell. Il limite che resta e' dichiarato in `docs/04` M6: i fissaggi sono di sessione |
| M7 — operazioni su file | 🟡 copia, sposta, rinomina, cestino, clipboard interoperabile, **menu contestuale a più livelli e cercabile** (campo in cima, filtro sul livello 0: `docs/04`) con **`Proprietà` in-app** e **`Proprietà di Windows`**, le **voci di primo livello della shell**, il **menu della cartella** sul fondo, i **sottomenu delle estensioni**, **`Nuovo file >`** (tipi dal registro, creazione con
contenuto), **`Invia a >`** (destinazioni della cartella SendTo, eseguite lasciandoci
cadere sopra la selezione), **drag&drop dentro e fuori** con OLE, la **navigazione
nello spazio dei nomi** col **ripristino dal cestino**, il **trascinamento sul
riquadro di navigazione** (che sul cestino è il gesto per buttare via, non una
copia) e il **dialogo di progressione**: copia, spostamento e cestino su un thread di
lavoro, il conto dei byte, la voce in corso, `Esc` e `Annulla` che fermano davvero, e
il file parziale che la shell cancella da sola, e il **trascinamento fuori dal
cestino** (una voce riciclata trascinata in una cartella **si ripristina** li': visto
il 2026-10-17, `docs/02` §7.34) e **le cartelle riciclate si aprono** (posizione come
PIDL codificato, `shell:pidl:`). Restano i modelli `ShellNew` con `Command` |
| M8 — inspector | ✅ metadati, proprietà `PROPSYS`, conteggio per tipo, **visualizzatore di testo** (righe, minimap, spazi visibili, andare a capo, ingrandimento, codifica), **anteprima immagine** decodificata con adattamento e ingrandimento, **griglia di sfondo** (`InspectorHideGrid`) e il **contenuto della cartella scelta** in fondo al pannello, **in elenco** (le prime voci con la loro icona o miniatura, senza aprirla, **a richiesta dell'autore**). Le voci di spazio dei nomi mostrano le colonne della shell (percorso originale, data di eliminazione) |
| M9 — configurazione e temi | ✅ pannello delle opzioni (`Ctrl+,`) con i controlli tipizzati, preferenze ricordate in `%APPDATA%\Bolt\settings.ini`, **cinque opzioni del riferimento applicate** (estensione nei nomi, percorsi compatti, angoli arrotondati, opacita' dei pannelli inattivi, percorsi lunghi), **layout per cartella** (`folderLayouts`, in memoria: vista, misura, ordinamento, gruppi e larghezze, con *salva* e *dimentica*), **lingua su tutte le superfici** — comandi, bande dei gruppi di ogni menu, righe delle opzioni, barra laterale, colonne, inspector, con la **chiave** che resta l'inglese (`docs/02` §7.56) —, **tavolozza chiara e scura** con `SystemColorScheme` che segue Windows e l'**editor della mappa dei tasti** (la riga `Key Map` fra le opzioni, `CmdEditKeyMap` nella palette: elenco dei comandi col tasto accanto, cattura del tasto nuovo, `Canc` che toglie, tasti di partenza in cima), **animazioni di apertura** dei pannelli in dissolvenza (scorrimento e cursore di testo esclusi, dichiarato) |
| M10 — backend D3D11 | ✅ `IRenderer` dietro cui stanno **OpenGL** e **Direct3D 11** (stessi shader in HLSL, stesso batcher di quadrilateri), scelta all'avvio (`BOLT_BACKEND`) e fallback all'OpenGL. Lo shader HLSL incorporato non compilava — `point` e' una parola riservata — e il fallback faceva sembrare tutto a posto; **corretto e verificato il 2026-10-18** (`p` al posto di `point`), con le catture delle otto superfici e `--- backend: Direct3D 11` nel registro. Ora **disegna lui**. `docs/02` §7.52 |
| M11 — installazione | ✅ l'installer **Inno Setup 6** (`installer/bolt.iss`, compilato in `out/Bolt-<versione>-win64-setup.exe` da `tools/make-installer.ps1`): installazione per utente senza amministratore, collegamento nel menu Start, voce in «App e funzionalita'» con versione e comando di disinstallazione, `AppId` stabile quindi una versione nuova **aggiorna**; wizard in italiano e inglese; in disinstallazione chiede se togliere le preferenze e in silenzioso non le tocca. Piu' lo zip portatile. `tools/verify-installer.ps1`: **24 controlli** con installazione vera, avvio in OpenGL, cattura, disinstallazione e pulizia garantita da `finally`. Impacchettando e' emerso un difetto vero — l'exe portava il percorso degli shader della macchina di compilazione (`docs/02` §7.63). Limiti: eseguibile non firmato (SmartScreen), niente MSIX, Inno Setup come dipendenza **di build** (`docs/04` M11) |
| M12 — la Home | ✅ la pagina di avvio e' una **posizione** (`bolt:home`, non una cartella finta): tre bande — **Fissaggi** (i pin di sessione), **Recenti** (il ranking di M4), **Cartelle note** (i `places`) —, deduplicate, con `Entry::band` e un raggruppamento che sulla Home non si sceglie. Aprire, filtrare e `F5` funzionano; **copia, elimina, rinomina e proprieta' si rifiutano** (le righe nominano cartelle vere), e non partono ne' la sorveglianza ne' le statistiche ricorsive. Riga `Home` in cima alla barra laterale; `startupFolder` vuota = Home. `docs/04` M12 |
| M13 — aggiornamenti in app | ✅ la **forma B**: comando **esplicito** `Controlla aggiornamenti` (nessun timer, nessuna richiesta all'avvio), `releases/latest` della repo, download del setup, **SHA-256** confrontato con quello pubblicato e rifiuto con cancellazione se non torna, poi setup in silenzioso. Lavoro su un thread, avanzamento nella barra di stato, `Esc` che annulla. **Visto tutto**, incluso il caso riuscito: con `v0.1.0` e `v0.1.1` pubblicate, la 0.1.0 installata si e' aggiornata da sola alla 0.1.1 e «App e funzionalita'» lo dice. `docs/04` M13 |

L'applicazione è **usabile**: si naviga, si filtra, si raggruppa, si seleziona, si
aprono tab, si salta con GoTo, si guardano anteprime e statistiche ricorsive, e si
lavorano i file — inclusa l'apertura delle proprietà di Windows.

---

## 2. Debito tecnico dichiarato

Ordinato per quanto morde.

0. **La caduta rara all'avvio — chiusa il 2026-10-18.** `docs/02` §7.54: i nove dump
   gia' presenti in `%LOCALAPPDATA%\CrashDumps` mostrano quattro cadute dentro
   `windows.storage.dll`, tre delle quali **in `ILGetSize`** chiamata dal nostro
   codice: era §7.53 visto dall'altro lato, un pidl liberato e poi letto. Rimettendo
   l'ordine di prima la caduta torna (6 su 120 avvii), togliendolo sparisce (0 su
   515). Resta dichiarata, e non riprodotta, una **seconda famiglia** di dump caduti
   dentro il nostro eseguibile (cinque, tutti prima del 2026-10-16): vedi la voce.
1. **`src/main.cpp` e' a 4.914 righe** (misurate il 2026-10-21, dopo Home, aggiornamenti e icone; erano 4.348 quando e'
   uscito `src/commands.*`, e le due superfici dei giorni dopo ne hanno aggiunte un
   paio di centinaio), **ed erano 11.394** — scesa a 7.344 con lo
   spezzamento, risalita a 7.895 con l'editor dei tasti e il fissaggio, scesa di
   **3.079** il 2026-10-18 in cinque tagli (`view_options`, `shell_menu`,
   `view_context_menu`, `settings`, `navigation`, `input_targets`) e di altre **536**
   con il blocco dei **comandi**: `src/commands.*`, 591 righe, con dentro `RunCommand`,
   i `Do*` (copia, incolla, elimina, cartella nuova), la rinomina in linea,
   l'apertura delle voci e la creazione delle voci nuove. `SetColorScheme` e' andato in
   `theme.*`, che e' il modulo a cui la tavolozza appartiene.

   **La cucitura si e' ricomposta, e va letta nell'ordine giusto.** Sono usciti
   `RunCommand`, `ActivateSelection` e `CreateNewItem` (ora in `commands.h`); sono
   entrate le superfici che un comando deve poter **aprire** — `BuildPaletteMenu`,
   `BuildGoToMenu`, `BuildPinMenu`, `BeginOperation` — che restano in `main.cpp`
   finche' non hanno il loro modulo (`view_palette.*`, `view_goto.*`, `view_pins.*`,
   `view_operation.*`). La lista quindi non si e' accorciata di molto: si e' **chiarita**.
   Le voci che restano sono 10, ognuna**con la sua destinazione scritta accanto** in
   `app_actions.h`, e il prossimo taglio e' quello delle quattro superfici.

   **Un difetto silenzioso trovato mentre si spostava.** `RefuseInNamespace` era
   dichiarata, commentata ("i comandi che agiscono su un percorso non valgono dove un
   percorso non c'e'") e **mai chiamata**: il caso che descriveva, un comando su una
   cartella dello spazio dei nomi, falliva in silenzio o con l'errore della shell.
   Adesso la chiamano incolla, elimina e cartella nuova, e la frase la dice.

   Sono usciti **quindici** moduli: `src/view_sidebar.*`, `src/view_titlebar.*`, la
   **lista con le colonne** (`src/view_list.*`), la parte pura dei tab (`src/tabs.*`),
   **l'ispettore intero** (`src/view_inspector.*`), la **barra di stato**
   (`src/view_statusbar.*`), i **menu a gruppi** (`src/view_menu.*`), il **pannello
   delle opzioni** (`src/view_options.*`), i **verbi della shell** (`src/shell_menu.*`),
   il **menu contestuale** (`src/view_context_menu.*`), le **impostazioni**
   (`src/settings.*`), la **navigazione** (`src/navigation.*`), il **registro dei
   bersagli** (`src/input_targets.*`) e i **comandi** (`src/commands.*`), piu' i
   supporti (`src/app_ui.*`) e il confine (`src/app_state.h`).

   **La decisione sul confine, presa il 2026-10-18.** `WindowProc` non si sposta: e' la
   colla della finestra, e le sue 1.319 righe non sono una unita' ma **i suoi `case`** —
   `WM_LBUTTONDOWN` 319 righe, `WM_KEYDOWN` 296, `WM_MOUSEMOVE` 226. Va spezzato **per
   messaggio**, e ogni messaggio va dove sta la cosa che quel messaggio muove. Ma il
   corpo di un `case` **e'** l'azione della superficie che riceve l'evento: finche' la
   superficie e' una funzione interna a `main.cpp`, spostare il `case` vuol dire
   trascinarsi dietro la superficie. Quindi l'ordine e':

   1. **prima le superfici** — una per volta, come si e' fatto finora (`view_menu` e,
      il 2026-10-18, `view_options`);
   2. **poi gli eventi** — con le superfici fuori, `WM_KEYDOWN` puo' diventare
      `input_keys.*` e i messaggi del mouse `input_mouse.*`, perche' non hanno piu'
      niente da trascinarsi;
   3. **`main.cpp` resta la finestra**: `wWinMain`, la classe, i messaggi che
      riguardano la finestra e basta (`WM_NCCALCSIZE`, `WM_NCHITTEST`, `WM_SIZE`,
      `WM_SETTINGCHANGE`, `WM_DESTROY`) e lo `switch` che smista agli altri.

   **La cucitura, dichiarata invece che implicita.** Un modulo che ha bisogno di una
   funzione rimasta in `main.cpp` non puo' chiamarla: e' interna. La scelta non e' fra
   chiamarla e non chiamarla, e' fra **dichiararla** e lasciarla invisibile, e allora
   c'e' `src/app_actions.h`: l'elenco delle azioni ancora dentro il file, con accanto il
   modulo che dovrebbero avere (`RememberSettings` → `settings.*`,
   `BuildKeyMapMenu`/`CloseKeyMap` → `view_keymap.*`, `ClosePinMenu` → `view_pins.*`).

   Il 2026-10-18 la lista e' cresciuta di sei voci — `RunCommand`,
   `ActivateSelection`, `ReloadKeeping`, `ComputeLayout`, `OpenPropertiesPanel`,
   `CreateNewItem` — e non e' un errore: la superficie del menu contestuale *chiede ai
   comandi di fare*, e i comandi non hanno ancora un modulo. La regola resta quella, ma
   il conto va letto nell'ordine giusto: la lista si accorcia **quando il pezzo che
   manca arriva**, e il pezzo che manca qui e' `src/commands.*` — `RunCommand` e i
   `Do*`, che sono anche l'ultima cosa che `WindowProc` chiama. Se dopo quello la lista
   non si accorcia, la cucitura e' nel posto sbagliato. Intanto si e' gia' accorciata di
   **due**: `RememberSettings` e' uscita con `src/settings.*` e `ReloadKeeping` con
   `src/navigation.*` — i due casi in cui la cucitura funzionava come previsto, un pezzo
   che trova il suo modulo e sparisce dall'elenco.

   Restano dentro **i comandi e l'input**: i **comandi** (`RunCommand` e i `Do*`, piu' le
   azioni che il menu chiama) e i `case` di `WindowProc`. Le superfici sono fuori tutte,
   quindi il passo 2 della decisione e' **sbloccato**: `WM_KEYDOWN` e i messaggi del
   mouse possono diventare `input_keys.*` e `input_mouse.*` senza trascinarsi niente
   dietro. I confini dei componenti ci sono (`GroupMenu`,
   `ContextMenu`, `file_ops`, `dir_stats`, `shell_*`, `drag_drop`) ma quelli della vista
   vivono ancora nello stesso file. Spostarli e' un lavoro meccanico ma largo, e va
   fatto quando c'e' spazio per riverificare tutto: il numero era **risalito** due volte
   — 156 righe col tema, 302 col dialogo di progressione, 200 col contenuto della
   cartella — ed e' il prezzo di scrivere un pezzo nuovo dove c'e' ancora il collo di
   bottiglia. Spostarne uno subito dopo costa meno che aspettare.
2. **Nessun controllo di versione locale.** Il repository è su GitHub
   (`gianmarcotaf02/bolt`) e il sistema salva con commit automatici a ogni turno,
   ma non c'è una convenzione di branch: si lavora su `main`.
3. **Lo zoom ricampiona il testo.** A `scale` diverso da 1.0 sia il corpo che il
   secondo corpo vengono scalati, quindi il ricampionamento torna. Eliminarlo
   richiede una rasterizzazione a runtime per ogni dimensione: è un altro
   mestiere, non un difetto da correggere adesso.
4. **Le operazioni su file — chiuso il 2026-10-21** (`docs/02` §7.66, §7.69 e §7.71).
   Il conto dei byte segue le voci finite invece della frazione dei punti (misurato:
   *90% dei byte veri contro 23%*), i conteggi tornano, e c'e' la **coda**: il pannello
   non e' piu' modale, quindi si naviga e si lavora mentre una copia gira e quello che
   si chiede nel frattempo parte quando quella ha finito. La decisione — pannello
   modale come Esplora risorse **no**, finestra che lascia lavorare **si'** — e' scritta
   in §7.69 con le regole: clic e rotella sopra il pannello si fermano li', `Esc` e il
   pulsante annullano e buttano via la coda, la vista si rifa' solo se si sta ancora
   guardando la cartella da cui si e' partiti.
5. **Il componente dei menu sa disegnare una riga spenta — chiuso il 2026-10-21**
   (`docs/02` §7.67). `GroupMenuItem::enabled` lo leggeva solo il pannello delle
   opzioni; adesso lo leggono tutte e tre le vie d'ingresso (cursore, clic, `Invio`) e
   il fissaggio non scrive piu' la ragione dentro il nome della voce.

---

## 3. Prossimi passi candidati

**Tre numeri, che sono tre cose diverse e non vanno confusi.**

- **Sessantotto** voci nel registro `docs/02` §7, una per una con sintomo, causa e
  correzione: **tutte chiuse, nessuna aperta**. È un cumulativo, non una lista di lavoro.
- **Due** caselle aperte in tutto `docs/04`: la resa monocromatica delle icone (scelta
  gia' presa: non si fa) e i modelli `ShellNew` con valore `Command` (limite
  dichiarato). Tutto il resto sono limiti dichiarati, che e' un'altra cosa.
- I **limiti dichiarati**: cose che si è scelto di non fare, scritte dove vivono con
  la ragione accanto (il tetto di 1 MB nella lettura dei testi, i modelli `ShellNew`
  con valore `Command`, le 20 colonne della specifica — tutte fatte il 2026-10-23, e
  l'unico residuo e' che un `Empty` ricorda il suo contenuto per la sola sessione —, le sei
  opzioni che il pannello mostra spente, i fissaggi e i recenti che vivono **solo in
  memoria**, le **icone del menu contestuale** solo dove la shell le dà: le voci del menu
  moderno di Windows 11 non consegnano la bitmap a chi legge il menu classico, quindi
  `7-Zip` resta senza icona mentre `WinRAR` ce l'ha — `docs/04` M7). Ogni volta che nei
  documenti c'è scritto "dichiarato", vuol dire questo:
  non risolto, e detto. Il ritaglio per-quad **non è più** in questa lista: era l'unico
  debito tecnico che viveva solo in `AGENTS.md` §8, ed è stato saldato il 2026-10-20
  (`docs/02` §7.61).

Alcune voci portano con sé un residuo dichiarato — §7.34 ne aveva uno, la voce
riciclata che non veniva presa, e il 2026-10-17 si è visto che non c'era più: una voce
riciclata trascinata fuori si ripristina — e il conteggio non lo nasconde, perché il
residuo è scritto nella voce stessa e sparisce quando sparisce.

In ordine di valore, non di facilità.

> **M9 — il pannello delle opzioni** (`Ctrl+,`), **l'allineamento delle scorciatoie**
> (`Ctrl+P`/`F4` al GoTo, `Ctrl+Shift+P` ai comandi, `Ctrl+,` alle opzioni), **finire
> la lingua**, **salire da una radice di volume**, **le tre cose viste nell'originale**
> (selettori di vista e anteprime in fondo, filtro del nome, `RECENTS`),
> **l'albero dei tab**, **`InspectorHideGrid`**, il **backend Direct3D 11** e il
> **tema chiaro** erano voci
> di questa lista, e sono fatte: vedi `docs/04` M9, M6, M8 e M10, e §7.40/§7.41/§7.42
> di `docs/02`. Della M9 resta solo **`Animations`**: i **layout per cartella** ci
> sono (`folderLayouts`, in memoria). La **mappa tasti rimappabile** ha il suo
> editor: vedi sotto. Di M4 **e' fatto**
> tutto (`src/recents.*`): punteggio, `ShowRecents`, `SortRecents`, i file aperti, le
> voci dei recenti e dei fissaggi nel GoTo, e il **menu contestuale delle righe** della
> barra laterale (fissare, togliere, togliere dai recenti) che ha chiuso anche il
> limite di M6 sul toglimento.

### Cosa resta davvero, in ordine di valore

Non c'e' lavoro funzionale mancante: le milestone sono chiuse e il registro dei difetti e'
a zero. Quello che resta e' **debito** e **limiti**, e questi sono i candidati, in ordine
di quanto mordono:

1. **Spezzare l'input di `main.cpp`** (`input_keys.*`, `input_mouse.*`). La decisione e'
   gia' scritta in §2: prima le superfici (fatte), poi i `case` di `WindowProc`; adesso
   `WM_KEYDOWN` e i messaggi del mouse non hanno piu' niente da trascinarsi dietro.
   `main.cpp` e' a **5.694** righe, e la curva non si e' fermata: il menu dell'applicazione,
   le sottocartelle nella lista, il layout, la sessione e la ricerca nel menu contestuale
   ne hanno aggiunte altre. E' il primo della lista per una ragione misurata, non per gusto.
2. **Gli split** (`Split Right`/`Split Bottom`, `docs/04` M7): l'unica funzione del
   riferimento che non c'e'. Toccano il modello dei pannelli — una sessione di lavoro con
   spazio per riverificare tutto, non una serata.
3. **Un tetto all'atlante delle icone**: un `.lnk` costa una cella e non c'e' sfratto
   (`docs/02` §7.65). Con molte cartelle piene di collegamenti si puo' riempire.
4. **Lo zoom ricampiona il testo** (§2): richiede una rasterizzazione a runtime per ogni
   misura, e' un altro mestiere.
5. **Non sono lavoro da fare, sono scelte**: icone monocromatiche (deciso: no), `ShellNew`
   con `Command` (limite dichiarato), fissaggi e recenti su disco (vietato da `docs/06`).

**Le colonne non sono piu' in questa lista** (2026-10-23): le venti di `docs/03` ci sono
tutte — `Percent Full` separata da `Volume Usage`, `Folder Usage`, le due del cestino
lette dalla shell per nome canonico e i quattro `Empty` configurabili dal menu
dell'intestazione. Resta un limite dichiarato: la scelta del contenuto di un `Empty`
vive nella sessione. **E la ricerca nel menu contestuale c'e'**, che era l'ultimo pezzo
di «menu e palette cercabili»: la palette ce l'aveva gia'.

Se invece si vuole una funzione **nuova**, non e' nello spec: va decisa prima con l'autore,
e poi scritta con la solita regola — prima lo spec, poi il codice, poi la prova a schermo.

### L'ordine deciso il 2026-10-16, e dove siamo

**Fatto per primo, com'era nell'ordine: l'editore della mappa dei tasti** (M9,
`docs/04`). La mappa era gia' un dato, e `Rebind`/`Clear`/`Reset`/`Bindings()` erano
scritti e non li chiamava nessuno; adesso c'e' il posto da cui rimappare. Una riga
`Key Map` fra le opzioni (lo stesso comando, `CmdEditKeyMap`, e' anche nella palette)
apre l'elenco dei comandi col tasto accanto; `Invio` comincia la cattura del tasto
(«premi un tasto per *Rinomina*»), `Esc` annulla, `Canc` toglie, e i tasti di partenza
sono la prima riga dell'elenco. Verificato a schermo, non a impressione: rimappato
`Rinomina` su un tasto nuovo e visto quel tasto rinominare davvero, e il vecchio non
piu'; tolto un tasto con `Canc`; rubato `F5` ad `Aggiorna`; rimesso tutto con la riga
dei tasti di partenza, con l'esito nella barra di stato. I limiti — mappa di sessione,
un tasto da scrivere sciolto che non si assegna, e l'avviso quando si sposta il tasto
che apre l'elenco — sono scritti in `docs/04` M9.

**Poi, com'era nell'ordine: il fissaggio in qualunque sezione** (M6, `docs/04`). Le due
decisioni che andavano prese prima di scrivere sono applicate: i fissaggi sono
**nostri** (la lista della shell non si tocca) e vivono **in memoria**, per la
sessione. `Fissa in...` e `Togli da...`, dalla palette e dal menu contestuale, mettono
una cartella in **accesso rapido** o nei **segnalibri**; nell'accesso rapido le nostre
righe stanno in cima a quelle della shell, con la stella che le distingue. Verificato a
schermo: quattro cartelle fissate, la stella della barra strumenti che si accende
fissando nei segnalibri, una riga tolta con `Togli da...` e sparita dalla barra
laterale, la riga fissata che porta alla cartella cliccandola, le due voci presenti sul
fondo del menu contestuale e **non** con una selezione, e le stesse schermate in
inglese. La decisione (b) — scrivere il jump list di Windows — non si e' provata e non
serve: il verbo della shell per farlo c'e' gia' nel menu contestuale (`Aggiungi a
Preferiti`) e lo scrive **il sistema**. I limiti sono in `docs/04` M6; il difetto
trovato strada facendo (un menu nuovo si registra in quattro posti, e me n'era sfuggito
uno) e' in `docs/02` §7.57.

**Poi il trascinamento fuori dal cestino** (§7.34), che era il primo della lista come
*diagnosi*: e' **fatto**, e il difetto non c'era piu'. Una voce riciclata trascinata in
una cartella si ripristina li' — 28 byte, contenuto identico, sparita dal cestino,
`DROPEFFECT_MOVE` nel registro — e una prova di controllo con un file normale arriva
uguale. I due tentativi precedenti avevano concluso il contrario per colpa del **banco**:
`SetForegroundWindow` non bastava a vincere il primo piano, e la riga sotto il puntatore
era stata misurata a occhio da una cattura piccola. Adesso c'e'
`tools/focus-window.ps1`. Una prova sola per caso, pero': la ripetizione e' saltata
perche' la finestra di prova era nata su un'altra scrivania virtuale (`AGENTS.md` §0).

**Il prossimo lavoro, in ordine** — deciso il 2026-10-18, dopo che lo spezzamento e'
arrivato a un punto in cui il file non e' piu' il problema principale:

1. ~~**Chiudere il Direct3D 11**~~ (`docs/02` §7.52, `docs/04` M10) — **fatto il
   2026-10-18.** Era l'unico punto in cui il programma **diceva il falso**: si
   dichiarava con due backend e ne girava uno, e l'altro falliva in silenzio —
   visibile solo nel registro. La correzione era una parola (`point` e' riservata in
   HLSL, il parametro delle funzioni di distanza si chiama `p`), la parte che
   contava era il **giro di verifica**: `BOLT_BACKEND=d3d11` su otto superfici
   (lista, menu contestuale, opzioni, mappa dei tasti, tema chiaro, immagine grande,
   visualizzatore di testo, viste a icone), `--- backend: Direct3D 11` nel registro,
   nessun errore HLSL, nessuna finestra lasciata aperta. Il Direct3D 11 **disegna
   lui** adesso, e l'OpenGL resta il ripiego. Le superfici non ripetute sotto questo
   backend sono elencate in §7.52. Il passo prima del giro e' stato `fxc` sugli
   shader estratti dalla sorgente: dice l'errore **senza aprire una finestra**, ed e'
   il modo di provare una compilazione che non chieda lo schermo a chi lavora.
2. ~~**Le due piccole bugie**~~ — **fatte il 2026-10-18**, e sono diventate quattro
   superfici (vedi `docs/02` §7.55). `§7.55` — l'ispettore che mostra una voce che il
   filtro ha nascosto: è stato scelto quello che il documento raccomandava, **il
   cursore si sposta** sulla prima riga visibile, come fa Esplora, e la **selezione
   non si tocca** (la decisione che la voce chiedeva). La regola sta in
   `RebuildView` perché a nascondere righe non è solo il filtro: ci arrivano anche i
   filtri di colonna e un gruppo chiuso. Il **suggerimento sopra il pannello delle
   proprietà**, e il suggerimento **rimasto appeso** alla voce di prima quando il
   filtro ne toglieva la riga: una regola sola, `TooltipsAllowed`, più il controllo
   che la voce col puntatore sopra sia una riga della vista. E la **barra di stato**,
   che contava le scelte nascoste. **Il registro dei difetti è a zero.**
3. ~~**`src/commands.*`**~~ — **fatto il 2026-10-18**, e non per il numero: perche'
   **M4 ci passa dentro** (`Cancella recenti`, i menu dei recenti). Sono usciti
   `RunCommand` e i `Do*` in 591 righe; `main.cpp` e' sceso a **4.348**. La cucitura
   si e' ricomposta, non accorciata: le superfici che un comando deve aprire
   (`BuildPaletteMenu`, `BuildGoToMenu`, `BuildPinMenu`, `BeginOperation`) sono
   entrate in `app_actions.h` con la destinazione accanto. `RefuseInNamespace`, che
   era scritta e mai chiamata, adesso la chiamano incolla, elimina e cartella nuova.
4. ~~**La caduta sul `Canc`**~~ (`docs/02` §7.59) — **chiusa il 2026-10-18.** Era un
   `container-overflow`: la vista si ricostruiva **prima** di `FinishOperation`, che
   ricarica la cartella, e il disegno indicizzava l'elenco nuovo con gli indici del
   vecchio. Adesso la vista si ricostruisce in `RenderFrame`, subito prima di
   disegnare. Prima: 4 cadute su 4; dopo: 4 su 4 vive, e i referti ASan sono passati da
   3 su 3 a 0 su 2. La procedura che l'ha trovata e' quella del §4.
5. ~~**M4 — il ranking dei recenti**~~ — **fatto il 2026-10-18**, e non con
   un'inferenza: la formula e' quella di `FUN_140189bb0` (punteggio +3 per visita che
   satura a 10, `Decay` azzerato dalla visita e cresciuto di uno per ogni cartella
   **sorella** aperta, poi un punto di punteggio per ogni passaggio oltre il decimo,
   `Ordinal` = il piu' grande + 1), l'ordine e' `Score` decrescente e poi `Ordinal`,
   e il codice e' `src/recents.*`. Le prove stanno in `docs/04` M4, indirizzo per
   indirizzo. Verificato a schermo, come chiede `AGENTS.md` §2: nella sandbox,
   `documenti` e `immagini` (catture `analysis/tmp/m4-recents-score.png` e
   `m4-recents-score-2.png`), prima `documenti` che vale 6 contro 3, poi `immagini`
   che sale a 9 e passa davanti. L'ordine di visita avrebbe dato l'altro risultato,
   quindi la differenza e' del punteggio e non del tempo.

   Il **decadimento** era l'unica regola scritta e non vista, e adesso e' misurata:
   quindici cartelle sorelle, `c01` portata a 9, dieci sorelle aperte (nessuna
   erosione: il credito e' esattamente dieci), poi quattro altre — e il nome di
   `c01`, letto sul pixel, passa da **220** a **161**, che e' il rapporto fra le
   opacita' di 5 e 9. Le catture sono `analysis/tmp/m4-decay-prima.png` e
   `m4-decay-dopo.png`; il conto sta in `docs/04` M4.

   Il blocco che aveva fermato la sessione prima era del **client**, non di REA: la
   prima interrogazione che tocca Ghidra paga **~210 s** di import e auto-analisi, e
   il client di pi chiudeva a 60 s perche' la chiave del timeout era scritta nel
   formato sbagliato (`timeout` in secondi invece di `requestTimeoutMs` in
   millisecondi). Corretto; il referto e' in `AGENTS.md` §5.
6. **M4 — chiusa**: `SortRecents`, i file aperti, «Cancella i recenti» e la banda
   `Accesso rapido` nel GoTo sono fatti e visti; «Mostra tutti i recenti» non si fa,
   perche' il gruppo mostra gia' tutto. Resta fra i limiti dichiarati il fissaggio di
   una riga della barra laterale (servirebbe un menu contestuale li').

**Cosa non si fa adesso, e perche'**: M2 (la resa monocromatica delle icone) e' una
scelta gia' presa; le sei opzioni spente, le 17 colonne mancanti e i `ShellNew` con
`Command` sono limiti **dichiarati**; di M9 resta `Animations`, che e' una funzione.

### Il lavoro fatto il 2026-10-20: la Home (M12)

Era il punto di consegna della sessione prima, con le scelte gia' prese. Adesso c'e', e le
scelte sono rispettate:

- **cos'e'**: una **posizione** (`bolt:home`), non una cartella finta. Le voci sono
  `Entry` sintetiche con `Entry::location` pieno, che e' il campo che serve gia' al cestino;
- **cosa contiene**: tre bande — **Fissaggi** (i `pin` di sessione), **Recenti** (il
  ranking di M4), **Cartelle note** (i `places`). Una destinazione che starebbe in due
  bande compare **una volta sola**, nella prima; `Entry::band` dice a `RebuildView` come
  raggruppare, e sulla Home il raggruppamento non si sceglie (`Gruppi: per banda`);
- **cosa si puo' fare**: aprire, filtrare, `F5` ricostruisce. **Niente operazioni su
  file**: copia, taglia, incolla, rinomina, elimina, cartella nuova, nuovo file e
  proprieta' si rifiutano con un messaggio, e il menu contestuale spegne quelle voci.
  Senza la guardia, `Elimina` su una riga della Home cancellerebbe la cartella **vera**
  che quella riga nomina;
- **cosa non parte**: la sorveglianza della cartella (il bersaglio si svuota entrando
  nella Home) e le statistiche ricorsive (le colonne dei conteggi restano `--`, e il HUD
  dice `0 jobs`);
- **barra laterale**: una riga **Home** in cima, fuori dalle sezioni, cosi' chiudere
  `ACCESSO RAPIDO` non la porta via;
- **avvio**: `startupFolder` vuota = **Home**. `Avvio: Questo PC` adesso **scrive** il nome
  di spazio dei nomi invece di svuotare la preferenza, e la riga `Avvio` fra le opzioni
  accetta anche una posizione. Una cartella di avvio sparita lo dice e apre la Home.

**Una cosa in piu', trovata guardando**: in inglese la colonna **Tipo** diceva ancora
`Cartella di file`, perche' il nome lo scrive la shell nella lingua di **Windows**. Per una
cartella adesso lo scriviamo noi (`File folder` / `Cartella di file`); per i file la
domanda resta alla shell, che e' l'unica a sapere che un `.png` e' un'immagine. Cambiare
lingua ricostruisce anche le intestazioni dei gruppi, che sono un valore costruito.

**Verificato a schermo** (catture `analysis/tmp/home-*.png`): avvio senza argomenti sulla
Home; tre bande popolate con una destinazione deduplicata; apertura di una riga, filtro per
nome, `F5` che tiene il filtro; `Canc`/`Ctrl+C`/tasto destro rifiutati con il messaggio,
voci spente (luminosita' 454 contro 695) e **26 file nella sandbox prima e dopo**; `Su`
dalla Home a Questo PC; `Avvio: la cartella aperta` e `Avvio: Questo PC` che riavviano
dove dicono, e la preferenza tolta che riporta alla Home; italiano e inglese a finestra
aperta in tutt'e due i versi.

**Il difetto trovato li' e' stato chiuso il 2026-10-21**: aprire una cartella da
`ACCESSO RAPIDO` la trasformava in una posizione codificata (`shell:pidl:...`) e dentro i
comandi per percorso si rifiutavano. Adesso la voce e' navigabile per percorso, l'ispettore
torna a mostrare le nostre righe, e il cestino si comporta come prima — `docs/02` §7.64,
con le catture della verifica (`fix-*.png`).

**Fatto subito dopo la Home, il 2026-10-20: gli aggiornamenti in app (M13)** nella forma B
— comando `Controlla aggiornamenti` **esplicito** (mai automatico: una richiesta a GitHub
e' telemetria di fatto), `releases/latest` della repo **`bolt`**, download dell'asset,
verifica dello **SHA-256** pubblicato nella release, rifiuto se non torna, poi il setup in
silenzioso (Inno riconosce l'installazione dall'`AppId`).

**E' stato provato tutto, caso riuscito compreso.** `gh` (2.102.0) e' installato e
autenticato, e la repo ha le release **`v0.1.0`** e **`v0.1.1`** con setup, zip e
`SHA256SUMS`. La 0.1.0 **installata davvero** ha visto la 0.1.1, scaricato il setup,
verificato l'impronta, avviato il setup in silenzioso, ed e' arrivata alla **0.1.1**
(registro e «App e funzionalita'»); rilanciata la 0.1.1, lo stesso comando dice "Bolt e'
aggiornato (0.1.1)". Visti anche i tre rifiuti (nessuna release, impronta sbagliata con
file cancellato, release senza impronte) e il messaggio di versione uguale. La 0.1.1 e'
la stessa base della 0.1.0, e lo dicono le sue note: esiste per questa prova.

Il 2026-10-21 la stessa strada ha portato la **0.1.2** (la correzione di `docs/02` §7.64)
e poi la **0.1.3** (§7.65: icone dei `.lnk`, nomi della Home, casa di Esplora risorse),
la **0.1.4** (§7.66 e §7.67: il conto dei byte e le righe spente) e la **0.1.5** (§7.68:
il percorso con le barre in avanti)
sull'installazione: ogni volta `Controlla aggiornamenti` → download, impronta, setup in
silenzioso → «App e funzionalita'» dice la versione nuova. Il caso riuscito e' stato visto
**cinque volte**, con cinque release diverse.

**Stato della macchina, da sapere prima di lanciare le prove.** Bolt **0.1.5 e' installato**
davvero (`%LOCALAPPDATA%\Programs\Bolt`, collegamento nel menu Start, voce in «App e
funzionalita'»), e ci e' arrivato **aggiornandosi da solo** dalla 0.1.4 durante la prova di
M13. `tools/verify-installer.ps1` **si rifiuta di partire** se trova un'installazione:
prima si disinstalla (App e funzionalita' -> Bolt), poi si prova. Se il codice cambia, il
setup va ricostruito (`tools/make-installer.ps1`) e reinstallato: l'installato resta la
0.1.5 finche' non lo si sostituisce. `gh` e' installato (`C:\Program Files\GitHub CLI`),
autenticato come `gianmarcotaf02`.

### La decisione presa il 2026-10-20: il ritaglio per-quad

Restavano cinque strade, e **quattro di queste non sono lavoro da fare ma decisioni gia'
prese**, che valgono la pena di essere rilette invece di essere riaperte:

1. **La resa monocromatica delle icone (M2)** — l'autore l'ha provata e fatta togliere:
   e' una scelta, non un residuo. Riaprirla senza una richiesta e' rimettere una cosa
   che qualcuno ha gia' guardato e scartato.
2. **i `ShellNew` con valore `Command`** — il limite e' scritto in `docs/04` M7 e la
   ragione e' tecnica e politica insieme: quel valore non crea un file, **avvia un
   programma** (`msaccess.exe /NEWDB 1`, `Wab.exe /CreateContact "%1"`). Eseguirlo vuol
   dire eseguire righe di registro arbitrarie da un file manager, e non e' un prezzo che
   questo progetto paga per una voce di menu.
3. **I fissaggi persistenti** — qui la risposta non e' «non conviene» ma **«non si puo'»**:
   `docs/06` vieta di scrivere su disco elenchi di segnalibri, recenti o ultima cartella,
   e lo dice come **requisito**, non come limite tecnico. Salvare i fissaggi in
   `settings.ini` sarebbe esattamente la cosa da cui quel documento difende chi usa il
   programma. La scelta giusta era gia' quella scritta: vivono in memoria.
4. **Le altre colonne delle 23** — lavoro vero e utile, ma non il primo: le 17 che
   mancano sono in buona parte `Empty1..4` (segnaposto che chiedono una configurazione
   dell'utente), `VolumeUsage`/`FolderUsage`/`PercentFull` (che hanno senso solo sopra un
   volume, cioe' in un contesto che la lista oggi non ha) e le due del cestino, che
   vogliono una prova sul cestino stesso. Resta una coda ordinabile, non un blocco.
5. **Il ritaglio per-quad** — **scelto e fatto.** Era l'unico debito tecnico del
   progetto, non una funzione mancante: stava scritto in `AGENTS.md` §8 e in §2 di questo
   documento, con la soluzione gia' indicata («il posto giusto per un ritaglio e' il
   quad»). Saldarlo rende **vere** due frasi che il progetto gia' scriveva: un disegno per
   fotogramma, e il ritaglio come proprieta' di chi disegna.

**Come e' stato verificato** e' la parte che conta (`AGENTS.md` §2): il numero prima e
dopo (`5 draws` → `1 draws`, con la stessa schermata), il confronto al pixel con le
catture fatte col vecchio scissor (0 pixel diversi su lista, testo e intestazione), il
  **controllo negativo** che rimette il difetto quando il taglio si spegne, e tutti e due i
  backend. Il racconto sta in `docs/02` §7.61.

### L'ordine deciso il 2026-10-16 (storico)

Discusso e scelto con l'autore, in risposta a «come procediamo». In ordine:

1. **Un blocco dello spezzamento: la lista con le colonne** (il pezzo piu' grosso e
   piu' intrecciato: ordinamento, filtri, gruppi, viste, intestazioni passano tutti di
   li') — **fatto**, ed e' `src/view_list.*`. Il residuo e' dichiarato nella voce 1 di
   §2: restano dentro l'inspector, la barra di stato, i menu e l'input.
2. **Il tema chiaro** (M9, i 35 token esistono gia' nella spec: e' cablaggio, non
   disegno) — **fatto**, ed e' `src/theme.*`, con le due tavolozze, la scelta dal
   pannello e dalla palette, `SystemColorScheme` che segue Windows e il riquadro di
   DWM allineato. Il residuo e' dichiarato in `theme.h`: dei 35 token dello spec
   esistono quelli che **hanno un lettore**, gli altri sono elencati e aspettano un
   posto che li legga.
3. **Il dialogo di progressione** di M7 — **fatto**. E' `FileOps::Job`
   (`src/file_ops.*`): copia, spostamento e cestino su un **thread di lavoro**,
   un `IFileOperationProgressSink` registrato con `Advise`, `FOF_SILENT` al posto
   della finestra della shell, e il pannello disegnato da noi con `Esc` e un
   pulsante. Il primo passo scritto qui prevedeva due cose che la misura ha
   corretto: il pulsante **non** chiama `IFileOperation::Abort` — quel metodo non
   esiste — e lo stato non sta in `AppState` ma dentro il `Job`, che e' l'unico a
   doverlo condividere col sink. Verificato nella sandbox con un file da 4 GB: il
   conto si muove **dentro il singolo file** (938 MB → 1,8 GB sulla stessa copia),
   `Esc` e il pulsante fermano davvero, e il file parziale sparisce. Il residuo e'
   dichiarato nella voce 5 di §2.
4. Il motivo dell'ordine: lo spezzamento non si vede, quindi farlo tutto di fila sono
   tre sessioni senza niente da guardare; e cominciarlo **adesso** costa meno di
   cominciarlo dopo, perche' ogni funzione nuova che arriva prima paga il file unico.
5. **Il pannello dell'ispettore fuori da `main.cpp`** — **fatto**, ed e'
   `src/view_inspector.*`: **1.053 righe** uscite intere (metadati, immagine grande,
   visualizzatore di testo, contenuto della cartella), `main.cpp` da **9.134 a
   8.080**. E' uscito **intero** invece che a pezzi perche' i pezzi si chiamano fra
   loro — le righe le costruisce chi le disegna — e dividere a meta' avrebbe voluto
   dire una funzione in due file. Una cosa si e' mossa fuori dal modulo:
   `VolumeOf`, che e' andato in `shell_location` perche' lo chiede anche l'elenco,
   quando decide se una voce e' un'unita'. Verificato con la regola che vale per uno
   spostamento: **le stesse cinque catture di prima, pixel per pixel** (§4,
   `rif-*.png`), e le sedici funzioni contate una per una prima e dopo.
6. **La barra di stato e i menu a gruppi** — **fatto**: `src/view_statusbar.*` (151
   righe) e `src/view_menu.*` (535), `main.cpp` da **8.080 a 7.414**. I menu sono
   usciti come **widget**: stato, filtro, geometria e disegno, con
   `GroupMenuClick`/`GroupMenuKeyDown` che restituiscono un `MenuAction` e non
   eseguono niente. Erano gia' scritti cosi', quindi la logica non e' stata toccata —
   e si vede dal fatto che le **20 funzioni sono identiche al testo di partenza**,
   carattere per carattere, non solo "della stessa lunghezza". Una e' rimasta in
   `main.cpp`: `GroupMenuClick`, che chiede dove sono le righe a `HitTest`, cioe' al
   registro dei bersagli, che sta con l'input. Verificato a schermo su **cinque**
   superfici: palette, opzioni, GoTo, valori di una colonna e menu contestuale
   (§4, `menu-*.png`).
3. **B e C si possono invertire** se si preferisce vedere subito qualcosa di grosso:
   il tema chiaro si tocca con gli occhi, il dialogo di progressione con le mani.

La cosa che resta da fare, e che non e' una diagnosi:

1. **Spezzare `main.cpp`** — **7.116 righe** (erano 11.394, scese a 7.344, risalite a
   7.895, e scese di 780 il 2026-10-18) e ventinove moduli: il confine c'è, e il file
   che lo contiene resta il collo di bottiglia dichiarato fin dall'inizio, ma non piu'
   intatto: la decisione e l'ordine dei prossimi passi stanno nella voce 1 di §2.


---

## 4. Come si costruisce e si verifica

> **Prima di qualunque prova: `AGENTS.md` §0.** Nessun file personale va toccato,
> e nessuna informazione personale va salvata. Le prove si fanno nella sandbox
> del progetto, creata con `tools/make-test-sandbox.ps1`.

Non c'è `cmake` nel PATH della shell. Il percorso è:

```powershell
CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build build --config RelWithDebInfo
```

Il ciclo visivo è: costruire, lanciare su una cartella nota, catturare, guardare.

```powershell
./build/RelWithDebInfo/Bolt.exe "D:\una\cartella"
powershell -ExecutionPolicy Bypass -File tools/capture-window.ps1 `
    -ProcessName Bolt -OutputPath analysis/tmp/shot.png
```

Le **shader** si leggono dalla sorgente (`REA_SHADER_DIR`), quindi una modifica a
`src/shaders/*` non richiede ricompilare.

**Una build con AddressSanitizer, per cacciare una caduta.** Serve una cartella di
build separata e il runtime **dinamico** (il CRT statico del progetto non e' compatibile):

```bash
# 1) in CMakeLists.txt, solo per questa build: MSVC_RUNTIME_LIBRARY "MultiThreadedDLL..."
# 2) configurare a parte (build/ e' esclusa dal controllo di versione, build/asan pure)
"$CMAKE" -S . -B build/asan -G "Visual Studio 17 2022" -A x64 -DCMAKE_CXX_FLAGS=-fsanitize=address
"$CMAKE" --build build/asan --config RelWithDebInfo
# 3) lanciare con l'esito su file: l'applicazione e' WIN32 e non ha una console
ASAN_OPTIONS=log_path=analysis/tmp/asan ./build/asan/RelWithDebInfo/Bolt.exe <cartella>
```

**La cartella `build/asan` va cancellata subito dopo** e `CMakeLists.txt` rimesso
com'era: un eseguibile compilato cosi' **pretende `clang_rt.asan_dynamic-x86_64.dll`**
e, senza, Windows mostra `Bolt.exe - Errore di sistema` — un dialogo che sembra un
guasto del programma e invece e' un residuo di diagnosi. E' successo il 2026-10-16:
il dialogo e' comparso a chi aveva in mano quella build. L'eseguibile buono si
riconosce con `dumpbin -dependents`: non deve nominare `clang_rt`.

`analysis/tmp/` raccoglie i riscontri: `qualita-prima-dopo.png`,
`viste-tiles.png`, `m5-*.png`, `m6-*.png`, `m3-progress.png`, `menu-*.png` (i
verbi della shell: menu aperto, fondo del menu, rotella, finestra ridotta
a 460 px), `list-*.png` (la lista con le colonne: dettagli, elenco, icone, gruppi
nelle due viste, filtro per valore, filtro di testo, scorrimento in griglia),
`theme-*.png`, `ch-*.png`, `sc-*.png` e `d3d-*.png` (il tema: le due tavolozze, il
pannello delle opzioni, la scelta, il filtro della palette, il suggerimento, e il
giro del secondo backend dopo §7.52), e `op*.png` (il dialogo di progressione: il
conto che si muove dentro un file da 4 GB, il pannello annullato col tasto e col
pulsante, lo stato "Operazione annullata", l'incolla normale, l'eliminazione nel
cestino e la caduta su una riga). Le prove con un file grande si fanno **solo** nella
sandbox: `fsutil file createnew analysis\tmp\sandbox\big.bin <byte>` la crea in un
terzo di secondo, e va cancellata prima di lasciare la sessione, e `rif-*.png` e' lo spostamento
dell'ispettore (le stesse catture prima e dopo, da confrontare a pixel), `menu-*.png` sono
le cinque superfici dei menu dopo lo spostamento (palette, opzioni, GoTo, valori di una
colonna, contestuale), `key-*.png` sono i tasti che passano dalla mappa (le due righe di
fondo scritte da lei, la rinomina, l'incolla, le tre viste, la scheda nuova e chiusa,
l'eliminazione), `peek-*.png` e' il
contenuto della cartella nell'ispettore (una cartella con immagini e miniature, una
con dodici voci e la riga «e altre voci», una con due file di testo, una vuota, un
file singolo — dove non c'e' — e sei voci scelte, dove non c'e' perche' l'ispettore
mostra l'aggregato). `split-opzioni.png` e' il pannello delle opzioni dopo lo spostamento in
`src/view_options.*`, ritagliato al solo pannello; `shell-menu-02.png` e' il menu
contestuale di un file dopo lo spostamento dei verbi della shell in
`src/shell_menu.*` — comandi nostri sopra, blocco della shell sotto;
`cm-submenu.png` e' la superficie del menu contestuale dopo lo spostamento in
`src/view_context_menu.*`, con un sottomenu aperto dalla tastiera (`Destra`);
`nav-tab.png` e' la striscia dei tab e la barra del percorso dopo lo spostamento della
navigazione in `src/navigation.*` (una scheda sola, dopo `Ctrl+T` e `Ctrl+W`). `m3b-inspettore-ntfs.png` e
`m3b-degradato.png` sono le due facce degli
attributi NTFS nell'ispettore (la lista, e «serve l'amministratore» senza
privilegi), ritagliate al solo pannello perche' il resto della finestra porta i
nomi dell'accesso rapido di chi lavora (`AGENTS.md` §0). Il banco per una caduta rara all'avvio sta in
`analysis/tmp/crashprobe/` (`probe.exe` lancia l'eseguibile su un desktop Win32
**invisibile**, `dumpscan.exe` legge un dump senza debugger: exception stream,
module list e pila con `StackWalk64`, e `qa.exe` chiama `ShellLocation::Read` in un
ciclo stretto): sono diagnostici, non versionati, e si riscrivono in mezz'ora — la
ricetta sta in `docs/02` §7.54.

---

## 5. Trappole già pagate — non ripagarle

**Cattura schermo.** La finestra deve essere in primo piano o la cattura riprende
ciò che sta sopra. Con `SetWindowPos(h, HWND_TOPMOST, …)` e poi
`HWND_NOTOPMOST` funziona sempre; va tolto il topmost alla fine.

**Il banco di prova deve mostrare ciò che vede l'utente.** `capture-window.ps1`
leggeva `GetClientRect`, quindi ritagliava via l'area non-cliente — e per tutta la
durata di M6 il difetto delle **due barre del titolo** è vissuto esattamente lì,
invisibile a ogni verifica e visibile a chiunque usasse il programma. L'ha
trovato l'utente. Ora il default è la finestra intera e `-ClientOnly` è l'opzione
per i confronti al pixel. Vedi §7.14 di `docs/02`.

**Quando una condizione sembra giusta e il comportamento è sbagliato, scrivi il
valore invece di dedurlo.** `if (wparam == TRUE)` era corretto secondo la
documentazione e secondo ogni esempio in circolazione; il messaggio arrivava con
`wparam` a `FALSE`. Una riga di registro ha risolto quello che il ragionamento
non stava risolvendo.

**Un difetto può nasconderne un altro.** Togliere l'area non-cliente ha tolto
anche ombra e angoli arrotondati: il primo difetto era così evidente da rendere
invisibile il secondo. Dopo aver corretto qualcosa, rimisurare — il bordo
*attorno* alla correzione è la parte che non si guarda mai.

**Input sintetico, tre trappole distinte:**

- `SendKeys` **non** dà il focus a una finestra OpenGL: usare `keybd_event`.
- `keybd_event` va preceduto da `SetForegroundWindow`, altrimenti i tasti vanno
  altrove. Un falso negativo da mancanza di focus sembra un bug dell'app e non lo
  è: **verificare il codice prima di correggerlo**.
- `SetForegroundWindow` **da solo non basta** quando il primo piano ce l'ha un'altra
  applicazione (la chiamata riesce, la finestra non viene davanti, i tasti vanno
  altrove): serve `AttachThreadInput`, che è quello che fa `tools/focus-window.ps1`.
  Lo stesso script **si rifiuta** di agire su una finestra che non sta sulla scrivania
  virtuale corrente, perché attivarla riporterebbe lì chi sta lavorando altrove
  (`AGENTS.md` §0).
- `GetKeyState` **non** vede un modificatore iniettato con `SendMessage`: per i
  tasti con `Ctrl`/`Shift` serve `keybd_event`. Per i tasti senza modificatore
  `SendMessage` con `WM_KEYDOWN`/`WM_CHAR` è più affidabile.
- Un blocco C# in `Add-Type` con una direttiva `using` malformata fallisce in
  silenzio e i tasti non partono mai: sembra che l'app ignori l'input.

**`Alt`+`←`/`→` arrivano come `WM_SYSKEYDOWN`**, non `WM_KEYDOWN`, perché la
finestra ha un menu di sistema.

**Un `Esc` che chiude l'app** è una trappola: corretto, ora azzera il filtro e si
ferma.

**Il pixel più esterno della finestra è vetro, e si vede cosa c'è dietro.** Il
bordo di 1 px che `DwmExtendFrameIntoClientArea` rende trasparente mostra ciò che
sta **dietro** la finestra: spostandola, quel pixel cambia colore. È voluto (ombra e
angoli arrotondati, `docs/02` §7.14), ma vuol dire che un confronto al pixel sul
**bordo** della finestra misura anche il desktop, non solo l'applicazione.

**Le riscritture massive di chiamate annidate duplicano il testo.** Sostituendo
`app.font.Draw(...)` con un secondo atlante, una regex che trova sia la chiamata
esterna sia la `Measure` annidata dentro produce due intervalli **sovrapposti**, e
il secondo incolla il proprio testo sopra la parte già riscritta del primo. Il
compilatore non se ne accorge subito e il danno sembra un difetto di layout.
Prima di riscrivere in massa: cercare i match annidati, e **rileggere il diff**
riga per riga. Il diff ha trovato anche un secondo errore — `scale * 0.5f` in un
centraggio scambiato per una scala di carattere — che nessun test visivo avrebbe
attribuito alla causa giusta.

**Un pulsante nuovo nella barra del titolo non riceve i clic finché non è nell'elenco di
`WM_NCHITTEST`.** La striscia del titolo si dichiara `HTCAPTION` — è la maniglia che
sposta la finestra, e porta con sé il doppio clic che massimizza — **tranne** per i
bersagli elencati lì dentro. Un pulsante disegnato e registrato in `clickTargets` ma non
elencato si vede, si sovrappassa, e il clic non arriva mai: Windows lo prende per un
trascinamento della finestra. È successo il 2026-10-21 al pulsante della barra laterale,
e il sintomo era «il pulsante non fa niente», senza nessun errore da nessuna parte. La
domanda giusta, davanti a un bersaglio muto, è **dove finisce il clic**, non se il
disegno è giusto.

**Un'applicazione che sembra congelata può essere dentro un ciclo modale.** Due
catture identiche a distanza di secondi distinguono "fermo" da "lento"; una
`SendMessageTimeout` dice se risponde, ma **non** se sta lavorando. Quando l'app
non ridisegna e risponde ai messaggi, la prima cosa da fare è **enumerare le
finestre del processo** e leggerne i titoli: il 2025-10-11 c'era scritto
`OperationStatusWindow — 2 azioni interrotte`, e la finestra che bloccava tutto
non era visibile sullo schermo. Vedi §7.13 di `docs/02`.

**I test devono usare una cartella usa e getta.** Le operazioni di M7 cancellano
davvero: la prova è stata fatta su `analysis/tmp/m7b`, mai su file personali, e la
sandbox va ricreata prima di ogni giro. Cancellare `alfa - Copia.txt` per sbaglio
è il modo in cui un test diventa un danno.

**Una domanda dell'utente può trovare una lacuna che nessun test trova.** Il
sovrappasso del mouse non era nella roadmap: non l'avevo previsto. Lo spec
funzionale dell'originale lo elencava da sempre (`HoverToSelect`,
`UnderlineOnHover`, `ShowTooltips`) e il tema aveva già quattro token dedicati. Il
campo `app.hovered` era **calcolato e mai letto**.

Morale: quando manca una funzione, la domanda da farsi non è «l'avevo prevista?»
ma «lo spec la elencava?». Lo spec è la lista delle cose che mancano; la roadmap è
solo l'ordine in cui pensavo di farle.

**Un campo scritto e mai letto è un difetto silenzioso.** `app.hovered` veniva
assegnato a ogni movimento del mouse e non letto da nessuna parte: nessun warning,
nessun errore, solo una funzione che non c'era. Un `grep` dei lettori, non degli
scrittori, è quello che lo trova.

**Una funzione che non si può scrivere oggi va dichiarata, non improvvisata.** Il
ripristino dal cestino sembra una casella da spuntare e non lo è: il cestino non
ha un percorso, e invocare il verbo sbagliato fra quelli del suo menu contestuale
significa **cancellare davvero**. Il blocco vero è il browsing dello spazio dei
nomi. Vedi `docs/04-roadmap.md`, sezione M7.

**Una voce che risponde `S_OK` può non fare niente.** L'esecuzione di una voce
di un sottomenu della shell restituisce successo e non produce nulla: per
accorgersene bisogna guardare l'**effetto**, e servono due prove — una che non
deve produrre niente e una che deve produrre qualcosa. La seconda dice se lo
strumento sa vedere. Vedi §7.19 di `docs/02`.

**Il numero di voci di un menu non è una costante.** Le estensioni moderne di
Windows 11 si registrano solo dalla seconda apertura, quindi la stessa voce sta a
un indice diverso fra il primo e il secondo tasto destro. Contare le frecce con un
numero fisso sbaglia voce; per una voce precisa si legge il registro diagnostico
dell'apertura in corso, e si naviga con `Fine` e le frecce.

**Un nome di file si prova anche con un accento dentro.** È il difetto che nessuna
prova aveva visto, perché i nomi di prova erano tutti ASCII: `perché.txt` si
disegnava `perch .txt`, e la cosa non dipendeva dal nome del file ma
dall'**atlante dei glifi**, che era ASCII puro (`docs/02` §7.29). Un file manager
italiano senza lettere accentate è rotto per metà dei documenti di chi lo usa, e il
modo di accorgersene è scrivere un nome come lo scriverebbe una persona.

**Trascinare si prova fotografando a metà gesto.** Un trascinamento è un gesto
continuo: la sua evidenza visiva — la destinazione tratteggiata — esiste solo
mentre il tasto è ancora giù, e tenerlo premuto fra due script non si può.
`tools/drag-mouse.ps1` preme, muove a passi, **fotografa**, e solo allora rilascia.
Con `-Ctrl` tiene premuto il modificatore per tutto il gesto: serve al rettangolo di
selezione additivo, che senza `Ctrl` sostituisce la selezione e con `Ctrl` la somma.
E due finestre si misurano prima di premerci sopra: `CW_USEDEFAULT` sposta la
finestra a ogni avvio, e la prima volta ho trascinato verso un punto che nel
frattempo era diventato un altro.

**Il riquadro di navigazione di Esplora risorse è pieno di nomi personali**: i
ritagli delle prove si tagliano sulla lista dei file, non sulla finestra intera.
Per lo stesso motivo le destinazioni di "Invia a" non finiscono nel registro
diagnostico — si contano, non si trascrivono.

**Un clic è finito nel riquadro di navigazione, e da lì nella cartella personale.**
Provando le caselle di scelta ho cliccato a `x=18` credendo di essere sulla casella:
la lista comincia a `x≈198`, e le prime righe a sinistra sono il riquadro di
navigazione. Il clic ha aperto la cartella dell'utente, e le due catture di verifica
successive hanno preso nomi di cartelle personali. Eliminate subito, e **non erano
versionate** (`analysis/` è escluso dal controllo di versione, verificato con
`git check-ignore`): nessuna copia è entrata nel repository. Rifatta la prova con la
coordinata giusta. La lezione: **prima di cliccare si guarda in che riquadro si
sta cliccando**, non solo il numero.

**Un file di prova è finito sul Desktop di chi lavora.** È successo provando il
trascinamento sul cestino: `QUICK ACCESS` era rimasto aperto, quindi la riga a
quell'`y` non era "Cestino" ma "Desktop", e ci è finito dentro un file finto di
sedici byte. Rimosso subito, per percorso esatto, senza aprire né leggere nient'altro
di quella cartella. Due errori si erano sommati: la sezione non era stata chiusa, e
`send-mouse.ps1` era stato chiamato con coordinate schermo mentre vuole coordinate
cliente — così anche il clic che doveva chiudere la sezione era finito dentro la
lista. Da allora: **prima si chiude `QUICK ACCESS` e si guarda la freccia**, e le
destinazioni sulle voci standard si scelgono solo dopo aver visto il riquadro.

**Il cestino è dati personali, e una cattura li copia.** È successo il
2026-10-12: dopo un ripristino la lista del cestino non era più filtrata — per un
difetto vero, `docs/02` §7.28 — e la cattura ha preso i nomi di quello che qualcuno
ha buttato via. Cancellate subito, e da allora la regola è: nel cestino ci si mette
**un file della sandbox**, si restringe la lista **col filtro**, e solo allora si
fotografa; il ripristino si verifica **contando i file nella sandbox**. Un filtro
che sparisce non è una comodità che manca: è una telecamera accesa in una stanza
che non è la tua.

**Un comando che risponde "fatto" non ha fatto niente.** Due volte, in due
punti diversi: il sottomenu della shell che risponde `S_OK` senza eseguire nulla
(`docs/02` §7.19) e la voce di "Invia a" che `ShellExecuteEx` "avvia" senza darle
niente da fare (§7.22). In tutti e due i casi l'errore non è nel codice — la
chiamata è quella documentata — ma nella **premessa**: si presume che l'esito
riportato corrisponda a qualcosa di osservabile. L'unico modo di accorgersene è
guardare il risultato sul disco e contare i byte.

**Una cattura a dimensione intera inganna sulle cose piccole.** Il campo di
rinomina sul posto (un riquadro scuro con un cursore di **un pixel**) non si
vedeva nella cattura della finestra, e per due volte ho concluso che la rinomina
non fosse partita. Ingrandendo la riga con `PIL` — `crop(...).resize(..., NEAREST)`
— il riquadro e il cursore sono evidenti. Prima di dire "non c'è", si ingrandisce
la zona: vale per i cursori, i bordi di un pixel e i separatori.

**`tools/send-keys.ps1`** esiste perché l'escaping di PowerShell dentro bash
mangiava i caratteri e i tasti non arrivavano: uno script su file è l'unico modo
affidabile di ripetere una prova di input.

**`tools/send-mouse.ps1`** esiste per la stessa ragione, applicata al puntatore:
il menu contestuale si apre col tasto destro, e i tasti `Menu`/`Shift`+`F10` — che
pure ci sono — non verificano la via che usa la gente. Le coordinate sono in area
**cliente**, così la stessa prova si ripete anche se la finestra si sposta. La
rotella vuole `-Clicks` col **segno**, positivo in su: senza, `wheel` fa una tacca
in su, e su una lista già in cima non succede niente — sembra che lo scorrimento
non funzioni. È successo provando lo scorrimento della lista appena estratta, e la
prima conclusione («la rotella non scorre») era sbagliata: il difetto era nel modo
in cui chiamavo lo strumento, non nella lista.

**Chiaro o scuro si legge da un pixel, non dalla miniatura.** In questa sessione ho
detto **due volte** che una cattura era chiara mentre il file diceva `(35, 36, 40)`:
l'immagine che si guarda passando per l'anteprima della conversazione non è la
prova, il pixel lo è. Per dire quale tavolozza sta disegnando l'applicazione si
legge un punto del fondo: `(255, 255, 255)` è la chiara, `(35, 36, 40)` la scura. E
vale per tutto il resto: un colore, un bordo di un pixel, un cursore.

**Due catture si confrontano solo a parità di stato.** Confrontando i due backend
ho concluso che il Direct3D non disegnava i segni degli spazi (`·` e `→`) e invece
l'opzione «segna spazi» era semplicemente **spenta** nel giro nuovo: non è una
preferenza che si ricorda fra le sessioni. La misura l'ha detto: col segno acceso,
le quattro `·` compaiono anche sotto Direct3D. Prima di attribuire una differenza a
ciò che si sta provando, si guarda cos'**altro** è diverso fra i due giri — il
percorso, il filtro, l'opzione, la selezione.

**Una caduta rara non si attribuisce con tre tentativi.** Provando il tema ho visto
l'applicazione sparire all'avvio e ho concluso che era il backend Direct3D: `3 su 8`
contro `0 su 8` in OpenGL. Con più giri la differenza è sparita — `5 su 55` con le
modifiche di questa sessione, `0 su 30` col commit precedente, `0 su 12` sotto
AddressSanitizer. Tre tentativi per parte non distinguono il 40% dal 10%, e una
diagnosi sbagliata manda a cercare il difetto nel posto sbagliato. Per attribuire
una caduta rara servono **campioni**, e per trovarne la causa una **pila** (un
debugger, ASan): se sotto lo strumento non si riproduce, si dichiara con i numeri
che si hanno. La caduta dell'accesso rapido (`docs/02` §7.54) e' stata chiusa cosi',
e vale la pena scrivere come: la pila **c'era gia'**, e la lezione e' nelle due voci
qui sotto.

**Prima di armare un banco, si guarda cosa c'e' gia' nella cartella dei dump.**
`%LOCALAPPDATA%\CrashDumps` conteneva **nove** dump di Bolt, scritti nei giorni in
cui la caduta si misurava, da una configurazione di `LocalDumps` che qualcuno aveva
messo e poi tolto. Tre di quelli danno lo stesso indirizzo dentro
`windows.storage.dll`, e hanno chiuso la questione che un pomeriggio di avvii non
aveva chiuso: cercare li' costa un `ls`, riprodurre una caduta rara costa ore.

**WER rate-limita i dump locali, e in una raffica si mangia proprio quelli che
servono.** Con `LocalDumps\Bolt.exe` configurato in `HKCU` (`DumpType=1`), **sei**
cadute riprodotte di fila non hanno scritto **nessun** dump — nemmeno per un
programma di tre righe che dereferenzia il nullo. La traccia sta in una riga sola,
`HKCU\Software\Microsoft\Windows\Windows Error Reporting\
LastRateLimitedDumpGenerationTime`. Quindi: se i dump non arrivano, non e' detto che
le cadute non ci siano; e i dump che servono si cercano fra quelli **vecchi**.

**Una caduta che AddressSanitizer non riproduce non e' una caduta che non c'e':**
puo' essere nell'allocatore di **qualcun altro**. Il pidl di §7.54 lo alloca la
shell, e ASAN strumenta il CRT nostro: liberare e rileggere un blocco di
`windows.storage.dll` non e' un errore *per ASAN*. `0 su 12` voleva dire «questo
strumento non lo vede», non «non c'e'».

**Un banco che legge l'accesso rapido stampa i nomi delle cartelle di chi lavora.**
E' successo il 2026-10-18: una prova che serviva a misurare la dimensione dei pidl
ha scritto l'elenco dei fissaggi veri, e il file e' stato cancellato subito. Un
banco di questo tipo scrive **numeri** — quante voci, quanti byte — e non nomi:
la regola di `AGENTS.md` §0 vale anche per un `.log` in `analysis/tmp`.

**Il banco non sa fare ogni gesto, e quando non lo sa lo dice a meta'.** Il
2026-10-18, provando le opzioni: `send-keys.ps1` non ha un'azione per `Ctrl+,`
(`ctrl+oem_comma` risponde `azione sconosciuta`, e **poi** stampa `ok`: leggere la
prima riga, non l'ultima), e `backspace` non svuota il filtro della palette. Le due
strade che funzionano: si arriva alle opzioni dalla **palette** (`Ctrl+Maiusc+P`, poi
`opz`, poi `Invio`), e il filtro si svuota **chiudendo e riaprendo** invece di
cancellare. Aggiungere l'azione `comma` al banco e' la correzione vera, ed e' una
riga.

**La cattura riprende quello che sta sopra, e "procedi" non vuol dire "la macchina e'
libera".** Il 2026-10-18, dopo un'autorizzazione data a inizio sessione, una cattura ha
ripreso la finestra di un'altra applicazione — il materiale di studio di chi lavora —
perche' era in primo piano sopra Bolt: `capture-window.ps1` legge il **rettangolo**
della finestra di Bolt, e dentro quel rettangolo c'era quello che c'era. Il file e'
stato cancellato subito e la preferenza cambiata dalla prova rimessa com'era, ma la
lezione e' l'opposto di una comodita': **la domanda "posso aprire una finestra adesso?"
si rifa' a ogni giro di catture**, non si eredita da un via libera di mezz'ora prima.
Un `GetForegroundWindow` prima di catturare, e la cattura si rifiuta se il primo piano
non e' la finestra che si vuole fotografare, costerebbe una riga — ed e' la correzione
giusta per il banco.

**Un'applicazione elevata non riceve i clic sintetici, e non si chiude con
`taskkill`.** Serve l'amministratore per leggere un volume raw, quindi per provare
M3b Bolt e' stato avviato col prompt `UAC`. Da li' in poi il banco non elevato ha
sbattuto su due muri, tutti e due dell'UIPI: i clic di `send-mouse.ps1` arrivavano
alla finestra e **non facevano niente** (la selezione non cambiava, e sembrava che
l'applicazione ignorasse l'input), e `taskkill` rispondeva `Accesso negato`.
Le due uscite: aprire Bolt **direttamente sulla cartella di prova**, cosi' il
cursore cade sulla prima riga e non c'e' niente da cliccare (ed e' cosi' che la
prova e' stata fatta); e mettere in conto **un `UAC` in piu'** per chiudere, che va
fatto **prima** di avviare, non dopo.

**Il registro diagnostico è il posto giusto per una caduta intermittente.**
L'applicazione spariva entrando in una cartella (`docs/02` §7.51), senza nessun
errore di Windows: il registro, scritto a ogni fotogramma, ha detto l'ultima cosa
fatta prima della morte — e la riga `--- tooltip: riga 1 di 1 voci` ha spiegato da
sola la causa. Un `printf` in più costa un giro; dieci minuti di ragionamento su
una caduta che non si riproduce costano un pomeriggio.

**Un registro aperto in append a ogni riga può perdere le righe.** Il primo
diagnostico dei verbi della shell apriva, scriveva una riga e chiudeva; il file
risultante conteneva **118 byte su venti voci** — il riassunto e un frammento di
due voci, il resto sparito. Il secondo giro dello stesso problema non costa nulla
se le righe si accumulano e si riscrivono tutte insieme: un registro incompleto è
peggio di nessun registro, perché sembra dire che quelle voci non esistono.

**Un menu più alto della finestra non è un dettaglio estetico.** Le voci che
sporgono sotto il bordo non si leggono e non si cliccano, e il menu contestuale
con i verbi della shell è passato da undici a trentuno voci. Un menu che scorre
va previsto nel componente, non aggiunto quando serve: la correzione è costata
una riscrittura della geometria (`firstItem`, altezza derivata dalle voci che
entrano, barra sul bordo destro). Vedi §7.16 di `docs/02`.

**Con più di una modalità di scorrimento, le regole sono più di una.** Le frecce
devono portarsi dietro la voce scelta; la rotella no. Usare una regola sola per
entrambe ha prodotto un menu che si rifiutava di scorrere sotto il puntatore. E
una prova che non distingue il caso rotto dal caso giusto non è una prova: il
menu straripava di **una** voce, quindi la rotella toccava il tetto comunque. La
finestra è stata ridotta a 460 px per rendere la misura decidibile.

**Scegliere una voce col puntatore è un bersaglio mobile.** Il menu contestuale
non ha la stessa altezza a ogni apertura: dalla seconda in poi arrivano le
estensioni moderne di Windows 11, quindi le voci sotto il puntatore scendono di
due righe. Un clic a coordinate fisse ha sbagliato voce, e la cattura sembrava
dire che il comando non era partito. Per scegliere una voce precisa si apre il
menu e si usa la **tastiera** (`Fine`, poi tante `giù` quante servono): è
deterministico e non dipende dalla geometria.

**Una sonda che cerca per titolo trova anche la finestra dell'applicazione.**
Il titolo di Bolt contiene il percorso della cartella aperta, quindi la ricerca
di `sandbox` ha trovato due finestre — la nostra e il dialogo delle proprietà — e
il `WM_CLOSE` di pulizia le ha chiuse **entrambe**: l'applicazione è morta
durante una prova, e per un momento è sembrato che fosse il dialogo a
ucciderla. `tools/find-window.ps1` ora salta la classe `BoltWindow`.

**`GetWindowTextW` con `CharSet` sbagliato tronca i titoli alla prima lettera.**
Una enumerazione di finestre restituiva `B`, `N`, `_`, `M`, `D`: sembrava che il
processo avesse sette finestre senza nome, e invece il marshalling ANSI leggeva a
sedici bit una stringa scritta a sedici bit. Il difetto era nello strumento, come
il §7.14: prima di dichiarare che una finestra non esiste, si verifica che la
sonda la sappia leggere.

**`IFileOperation` non ha un metodo per fermarsi.** Non esiste `Abort`: chi annulla e'
il **sink**, che risponde con un codice d'errore. Il documento dice che un errore da
`Pre*Item` annulla l'operazione e **tutte quelle in coda**, e `UpdateProgress` passa
di li' anche a meta' di un file solo. Il primo passo di M7, scritto in §3 di questo
documento, diceva `IFileOperation::Abort`: e' stato il **compilatore** a dire che quel
metodo non c'e', dopo che la riga era stata scritta in un documento di progetto e
riletta come se fosse vera. Un documento di progetto e' una fonte come il grafo: una
derivazione, non la verita'.

**I "punti" di `UpdateProgress` non sono byte.** Misurati: un file da 4 GB sono
**131074 punti** (32 KB l'uno), un file da 23 byte sono **2 punti**. La frazione e'
una frazione e vale come stima del tempo; per avere dei **byte** serve un denominatore
misurato da noi. Un conto che sembra esatto e non lo e' e' peggio di un conto
dichiarato approssimativo, e il modo di accorgersene e' una riga di registro che scrive
le due cose **nella stessa riga**: `operazione: 4294967296 byte misurati, 1 voci,
131074 punti dichiarati dalla shell, 69760 fatti`.

**Un'operazione lunga su un thread di lavoro rende possibile la seconda operazione.**
Sostituire quella in corso vuol dire annullarla **senza dirlo**, e per una caduta e'
peggio che per un comando: chi ha mollato il tasto legge dalla destinazione se
l'operazione e' riuscita, e su `DROPEFFECT_MOVE` cancella gli originali. Una caduta
rifiutata deve dichiarare `DROPEFFECT_NONE`, o il rifiuto diventa una perdita di dati.

**Due catture si confrontano solo a parita' di stato — e l'ispettore ha una riga che
cambia da sola.** Per verificare lo spostamento dell'ispettore ho confrontato le
stesse cinque catture prima e dopo: 72 pixel diversi in ognuna, tutti nella riga
**Accesso**. Non era il refactor: era la **data di ultimo accesso** della cartella,
cambiata fra i due giri perche' nel frattempo qualcuno l'aveva letta (12:39 → 15:30).
Lo stesso binario, due giri di fila, da' catture **identiche** tranne il pixel di
vetro del bordo. Morale doppia: un confronto a pixel va fatto **nello stesso giro**
quando c'e' di mezzo un tempo, e una cattura che differisce in una riga di data non
dice niente sul codice.

**Un taglio con una graffa in meno non da' "graffa mancante": da' "chiamata ambigua
a funzione in overload".** Spostando i menu, la cucitura fra la prima funzione e il
resto ha perso una graffa: la funzione e la graffa che chiudeva il namespace anonimo si
sono scambiate i ruoli, e **tutto il resto del file e' finito dentro il namespace
anonimo**. Il compilatore l'ha detto cosi': `'Bolt::anonymous-namespace'::GroupMenuMove':
chiamata ambigua a funzione in overload`, con due candidati identici. Un errore che nomina
l'overload quando il problema e' una graffa si risolve leggendo la **cucitura**, non la
funzione che l'errore nomina. E vale la regola di sempre: dopo un taglio si legge cio'
che si e' tolto e cosa sta **attorno** alla cucitura.

**La finestra si sposta a ogni avvio, e una prova con l'origine sbagliata non
fallisce: misura un'altra cosa.** `send-keys.ps1` e `send-mouse.ps1` vogliono
coordinate **cliente** e le convertono da soli, ma `capture-rect.ps1` e
`drag-mouse.ps1` vogliono coordinate **schermo**, e l'origine va **rimisurata dopo
ogni avvio** (`window-rect.ps1`): `CW_USEDEFAULT` la sposta. In questa sessione due
catture sono uscite sul riquadro sbagliato — la barra delle schede invece della lista
— perche' l'origine era quella dell'istanza precedente. Una cattura sbagliata sembra
una prova, ed e' per questo che fa perdere piu' tempo di un errore.

**Due istanze di Bolt, e i tasti vanno a una sola.** `send-keys.ps1` e
`send-mouse.ps1` prendono il processo con `Get-Process Bolt | Select-Object -First 1`:
con due istanze vive — l'istanza vecchia ancora in chiusura e la nuova appena partita —
meta' dei tasti finisce in una finestra e meta' nell'altra, e il risultato sembra
un'applicazione che ignora i tasti a caso. Prima di una prova con l'input: si uccide
il processo e si **aspetta che non ci sia piu'** (`while (Get-Process Bolt) { Start-Sleep
-200 }`), poi si avvia. E' successo mentre si costruiva il contenuto della cartella nell'ispettore: tre `giu'` di fila
hanno spostato il cursore di **una** riga, e la cattura sembrava dire che il pannello
non si aggiornava.

**`small` e' una macro di `rpcndr.h`.** `const Rect small{...}` non compila, e l'errore
dice `'Rect' non puo' essere seguito da 'char'` — nomina il tipo, **non** la macro, e
sembra un errore di sintassi. Vale per gli identificatori che il C aveva gia' preso
(`small`, `near`, `far`): un errore che nomina il tipo accanto a un identificatore
innocuo si risolve guardando l'identificatore, non il tipo.

**Un controllo che disattiva meta' della condizione non e' un controllo.** Per provare
che a ritagliare era la riga nuova del pixel shader, la si e' spenta con
`if (false && (A || B) || C)` — che per la precedenza degli operatori spegne solo `A` e
lascia in piedi `B` e `C`. Il difetto non e' tornato, e la conclusione ovvia era
sbagliata: sembrava che la riga fosse inerte. Il controllo giusto ha le parentesi
attorno a **tutta** la condizione (`false && (A || B || C)`), e il primo tentativo ha
fatto perdere un giro di build e due catture. Dopo una sostituzione meccanica di una
condizione, si legge il **testo risultante** — non quello che si voleva scrivere.

**Un contatore che riparte a ogni lotto non conta il fotogramma.** `QuadCount` azzera
in `Begin`, e con tre lotti per fotogramma mostrava l'ultimo: 379 quadrilateri su circa
quattromila. Il numero c'era, era piccolo, e nessuno aveva motivo di sospettarlo: era
un difetto dello **strumento** e non del disegno, ed e' la specie che fa concludere il
falso con la coscienza tranquilla. Quando un contatore serve a decidere se un lavoro e'
riuscito, la prima cosa da verificare e' **cosa misura** — e il modo e' confrontarlo
con un cambiamento che si conosce (qui: da 5 disegni a 1).

---

**Impacchettare e' un modo di provare.** Le trappole pagate la prima volta che il
programma e' uscito dal repository (2026-10-20).

- **Il difetto che si vede solo fuori casa.** L'eseguibile portava il percorso degli
  shader della macchina che l'aveva compilato: qui funzionava, su un altro computer
  l'OpenGL non avrebbe trovato niente. Un pacchetto si prova **da una cartella in cui la
  sorgente non c'e'** — `docs/02` §7.63.
- **`IExpress` risponde `1` e non dice niente.** SED sbagliata, file in una sottocartella
  o opzione che non gli piace: stesso silenzio, nessun file. Provato con un pacchetto
  minimo — **un file solo** — e fallisce uguale: su questa build di Windows non e'
  utilizzabile. E' il motivo per cui l'installer e' Inno Setup, e non e' stato tempo
  perso: senza quella prova si sarebbe creduto a un errore nel proprio pacchetto.
- **Il setup di Inno si rilancia come `<nome>.tmp`.** La finestra del wizard appartiene a
  quel processo, non a quello che si e' lanciato: `Get-Process <setup.exe>` ha
  `MainWindowHandle` a zero e la cattura risponde «nessuna finestra». Si cerca per
  **titolo** (`tools/find-window.ps1`) o per `MainWindowTitle`.
- **`/GROUP` e' ignorato con `DisableProgramGroupPage=yes`.** Misurato: il collegamento
  finisce comunque in `...\Start Menu\Programs\Bolt`. Quindi la prova che installa il
  pacchetto **crea il collegamento vero**, ed e' per questo che lo script si rifiuta di
  partire se Bolt risulta gia' installato: un test che sovrascrive l'installazione di
  qualcuno e' peggio di un test che manca.
- **Il disinstallatore di Inno lascia la cartella se ci trova file che non ha messo.** I
  due log di diagnosi (`bolt-diag.log`, `startup-error.log`) li scrive il programma, non
  il setup: senza `[UninstallDelete]` la cartella resta li' con dentro un log, e sembra un
  disinstallatore fermo a meta'.
- **`Start-Process -ArgumentList` unisce gli argomenti con spazi, senza virgolette.** Un
  percorso con uno spazio dentro (`...\Bolt installato`) diventa due argomenti e il
  comando fa un'altra cosa, senza dirlo. Le virgolette vanno **dentro** la stringa
  dell'argomento. Vale per ogni script di questo banco che passa percorsi.

---

## 6. Convenzioni del progetto

- Commenti e documentazione in **italiano**; identificatori e stringhe di codice
  in inglese.
- Ogni funzione nuova che risolve un problema non ovvio porta un commento che dice
  **perché**, non cosa. Il codice dice cosa.
- I numeri **derivati** battono i numeri fissati a occhio (vedi §7.6 di `docs/02`).
- Ciò che non è verificato a schermo o contro disco non è "fatto": è "scritto".
- I limiti si dichiarano nell'interfaccia o nella roadmap, non si nascondono.
- Build **senza warning** (`/W4`): un warning nuovo è un difetto nuovo.
- **Il banco di prova si verifica quanto il prodotto.** Un difetto visibile a
  chiunque usi il programma e invisibile allo strumento che lo controlla non è un
  difetto sfuggito: è uno **strumento sbagliato**. La cattura parte dalla finestra
  intera, non dall'area cliente.
- Quando un valore è in dubbio, **scriverlo in un log e leggerlo** batte dedurlo.
- Le **date** si scrivono europee, `gg/mm/aaaa` (e `gg/mm/aaaa hh:mm` con l'ora), in
  tutte e due le lingue: il formato di una data non è una parola da tradurre, e uno
  che cambia con la lingua cambierebbe anche la larghezza delle colonne. Una sola
  funzione le produce (`FormatFileTime`, `src/app_ui.cpp`), quindi lista, suggerimenti
  e ispettore non possono divergere.
