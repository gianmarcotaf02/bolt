# Specifica funzionale ricostruita

Le 1124 chiavi interne del binario *sono* la specifica del prodotto: ogni
impostazione, comando e dialogo ha un identificatore stabile. Questo documento ne
organizza il contenuto.

Fonte: `analysis/out/identifiers.json`, `settings-spec.json`, `strings-all.txt`.

---

## 1. Impostazioni globali

46 chiavi in `Setting_*`, con 15 valori enumerati. Ricostruite dalla coppia di
format string `Setting_%S` e `Setting_EnumValue%S`: il programma costruisce le
chiavi a runtime da una tabella, quindi i letterali sono l'elenco completo.

| Gruppo | Impostazioni | Valori |
|---|---|---|
| Tipografia | `FontName`, `FontSize`, `InspectorFontName`, `InspectorFontSize`, `Spacing` | — |
| Aspetto | `RoundCorners`, `Animations`, `AlternatingRowColors`, `InactiveOpacity`, `SystemColorScheme`, `ColorScheme`, `ColorSchemeLight`, `ColorSchemeDark` | — |
| Contenuto | `ShowFileExtension`, `ShowHiddenFiles`, `ShowSystemFiles`, `ShowRecents`, `SortRecents`, `ShowShortenedPaths`, `ShowBreadcrumbs`, `LongPathSupport` | — |
| Interazione | `ActOnPress`, `SingleClick`, `SingleClickFoldersOnly`, `HoverToSelect`, `UnderlineOnHover`, `DoubleClickToParent`, `ShowCheckboxes`, `ShowTooltips` | — |
| Tastiera | `CharacterKeyAction` | `TriggerHotkey`, `TypeInFilter`, `SelectTypedItem` |
| Scorrimento | `Overscroll`, `ReverseScroll` | — |
| Grafica | `GraphicsBackend`, `VSync` | `Direct3D`, `OpenGL` |
| Statistiche | `CalcFolderSizes`, `CalcFolderSizesSSD`, `CalcFolderSizesHDD`, `CalcFolderSizesNetwork` | — |
| Cache | `AssetCacheSize`, `LoadThumbnails` | — |
| Sessione | `LoadOnStartup`, `NewTabShow` | `LastSession`; `Root`, `Home`, `Current` |
| Recenti | `StoreRecents` | `LocalDisk`, `Persistent`, `Session` |
| Inspector | `InspectorLayout` | `Auto`, `Hor`, `Vert` |

Due letture di progetto:

- **`CharacterKeyAction`** rivela che digitare un carattere alfabetico sulla lista
  può fare tre cose diverse. È una scelta di interazione non ovvia e un buon
  esempio di API di prodotto che un clone dovrebbe riprodurre per essere fedele.
- **`CalcFolderSizes` ha tre varianti per tipo di dispositivo** più un valore
  principale. Non è una preferenza cosmetic; è una strategia di calcolo diversa.
  Vedi `docs/01` per la spiegazione dedotta.

---

## 2. Sistema di temi

35 token di colore, ciascuno con un ruolo preciso. Elenco completo:

```
Background  Surface  SurfaceSeparator  Inner  Separator  Outline  Border
Foreground  Text  Secondary  Disabled  ForegroundDisabled  Clear
ContentSelection  ContentHover  ContentDisabledSelection  RectSelection
Selection  Hover  AlternatingRow  File  Folder  Hidden  IconTint
Group  Match  MatchHover  MatchSelection  MatchDisabledSelection
OutlineSelection  OutlineHover  OutlineDisabledSelection  Caption
Progress  Warning
```

Il modello è quello di un tema semantico: `Match*` è una famiglia di quattro
token che copre gli stati di una riga che corrisponde a un filtro
(normale, hover, selezionata, selezionata ma disabilitata). `OutlineSelection` e
`ContentSelection` sono separati, quindi la selezione ha riempimento e contorno
indipendenti. `Progress` e `Warning` suggeriscono che la stessa tavolozza copre
barre di avanzamento e stati di avviso.

Sono 35 token più due varianti chiare e scure: una tavolozza di ~70 valori.

---

## 3. Comandi e mappatura tasti

**319 comandi**, ciascuno con un tooltip dedicato (237 tooltip aggiuntivi per
controlli non-comando). Raggruppati per prefisso:

| Famiglia | Esempi | Cosa copre |
|---|---|---|
| `HK_GoTo*` | `GoToMoveBack/Forward/Up`, `GoToExpand`, `Clear`, `Groups` | la finestra GoTo e la navigazione |
| `HK_CmdWindow*` | `Activate`, `Cancel`, `Exit`, `ExpandGroup`, `CollapseGroup`, `ToggleDetails` | la command palette, con gruppi espandibili e modalità dettaglio |
| `HK_ContextMenu*` | `Open`, `Cancel`, `Exit`, `ExpandGroup`, `CollapseGroup`, `Activate` | menu contestuale con la stessa struttura a gruppi |
| `HK_FilesPopup*` | `Open`, `Activate`, `Cancel`, `Exit`, `Next`, `Prev` | popup di selezione file |
| `HK_Bookmarks*` | `Toggle`, `Clear` | segnalibri |
| `HK_FolderView*` | `Toggle`, `Clear` | viste per cartella salvate |
| `HK_CreateFile`, `HK_CreateFolder`, `HK_Rename*` | | operazioni |
| `HK_ExplorerOpen`, `HK_ConsoleOpen`, `HK_ConfigOpen`, `HK_ConfigCreate` | | apertura in Explorer, terminale, file di configurazione |
| `HK_CalcDirsContents` | | attiva il calcolo delle statistiche |
| `HK_CloseInTree`, `HK_CloseAllInTree` | | chiusura di tab in un albero |
| `HK_IncreaseFontSize`, `HK_DecreaseFontSize`, `HK_DecreaseSpacing` | | zoom della UI |

Il pattern `ExpandGroup`/`CollapseGroup`/`ToggleDetails` ripetuto su `CmdWindow`,
`ContextMenu` e `GoTo` indica **un unico componente riusabile** per i menu a
gruppi collassabili, usato in tre contesti. È una decisione architetturale, non
tre implementazioni parallele.

**19 gruppi della palette**: Bookmarks, FilesPopup, Folder, GoTo, GroupBy,
Inspector, Misc, Options, Panels, Places, QuickCommands, Recents, Rename, Scroll,
Sort, Storage, Tabs, Text, View.

---

## 4. Raggruppamento

54 etichette, su cinque dimensioni:

| Dimensione | Etichette |
|---|---|
| Conteggio | `Empty`, `Few`, `Dozens`, `Hundreds`, `Thousands`, `TensOfThousands`, `HundredsOfThousands`, `Millions`, `Unspecified` |
| Data | `Today`, `Yesterday`, `EarlierThisWeek`, `LastWeek`, `EarlierThisMonth`, `LastMonth`, `EarlierThisYear`, `LastYear`, `Older`, `Unknown` |
| Nome | `09`, `AH`, `IP`, `QZ`, `Other` |
| Dimensione | `Empty`, `Tiny`, `Small`, `Medium`, `Large`, `Huge`, `Gigantic`, `Unspecified` |
| Occupazione volume | `PercentFree` e `PercentFull`, ciascuno in dieci fasce più `100` |

L'ultima dimensione è la più interessante: **si possono raggruppare i file per
quanto è pieno il volume che li contiene**, in fasce di dieci punti percentuali.
È una feature che esiste perché il programma conosce l'occupazione dei volumi,
ottenuta con `GetDiskFreeSpaceExW` (importato) per ogni volume enumerato con
`FindFirstVolumeW`.

Nota: questa dimensione era stata inizialmente letta come prova indiretta di un
parsing del Master File Table. È **falso**: vedi la correzione in `docs/01`.

---

## 5. Pannello lista e inspector

Dalle chiavi `Panel_HeaderColumn*`, 23 colonne disponibili:

```
Name  Type  Size  FoldersCount  FilesCount  TotalItems
DateCreated  DateModified  DateAccessed  DateDeleted  OriginalLocation
FreeSpace  FileSystem  PercentFull  VolumeUsage  FolderUsage
Empty1  Empty2  Empty3  Empty4
```

Le quattro colonne `Empty1..4` sono segnaposto configurabili: l'utente può
assegnare loro un contenuto. `DateDeleted` e `OriginalLocation` hanno senso solo
con il cestino reale, che è confermato dall'integrazione shell.

**Come sono state implementate** (`src/app_state.h`, `kColumns`): le venti colonne qui
sopra ci sono tutte, nello stesso ordine, e le otto che mancavano stanno in fondo —
`PercentFull`, `FolderUsage`, `DateDeleted`, `OriginalLocation`, `Empty1..4`. Le due del
cestino si leggono dalla shell per **nome canonico** (`System.Recycle.DateDeleted`,
`System.Recycle.DeletedFrom`): non sono proprietà di sistema, `propkey.h` non le ha, e
chiederle per indice di colonna vorrebbe dire indovinare una tabella i cui nomi sono
tradotti. `FolderUsage` è la sola con una definizione **ricostruita** e non verificata:
quanto del volume occupa la voce (vedi `docs/04` M5b).

Le 20 etichette `PanelInspector_Label*` coprono: cartella singola o conteggio,
file singolo o conteggio, percorso, tipo o tipi, dimensione o somma, dimensioni
dell'immagine, sottocartelle, sottofile, creazione, modifica, percorso originale,
file system, spazio libero, numero di righe, conteggio dei tipi. Più
`PanelInspector_TextEncodingUTF8`: l'inspector ha un visualizzatore di testo con
evidenziazione di codifica, spazi e tabulazioni
(`InspectorShowWhitespace`, `InspectorHideLineNumbers`, `InspectorHideMinimap`,
`InspectorWordWrap`, `InspectorZoomLevel`, `InspectorHideGrid`).

Le opzioni di testo confermano anche un **minimap nell'inspector**.

`InspectorHideGrid` non e' del testo: e' la **griglia dietro l'anteprima immagine**,
che serve a distinguere lo sfondo dell'immagine (bianco o trasparente) da quello del
pannello. Il comando corrispondente e' `HK_InspectorToggleImageGrid`, «Toggle display
of image grid to better differentiate background from the image contents».

---

## 6. Dialoghi

21 `ConfirmPopup_*`: azioni distruttive o irreversibili — chiusura
dell'applicazione, eliminazione di un layout, di una mappa tasti, di uno schema
colore, di un comando rapido, svuotamento di segnalibri e recenti, pin e unpin di
voci, sovrascrittura di un layout. Più tre coppie per voci GoTo mancanti, non
corrispondenti o offline.

12 `WarningPopup_*`: sei condizioni di dati corrotti o in formato vecchio, ognuna
con due messaggi (un titolo e un corpo). Coprono configurazione, sessione e dati
utente separatamente, il che è coerente con l'averne tre file distinti.

---

## 7. Persistenza e formattazione

`SearchTabs_*` contiene dieci etichette di tempo relativo (`SecondAgo`,
`MinutesAgo`, `HoursAgo`, `DaysAgo` e i plurali) più `Hint` e `TextEdit`: le tab
di ricerca mostrano da quanto tempo non vengono usate.

`Installer_*` (36 chiavi): il programma è anche il proprio installer, come
dichiara il produttore. Il flusso di installazione è nel binario principale.

---

## 8. Cosa manca in questa specifica

Le chiavi danno *cosa esiste*, non *come si comporta*. Restano da determinare per
via sperimentale, osservando l'applicazione:

- le soglie esatte delle fasce di conteggio e di dimensione;
- l'ordine e la composizione dei gruppi della command palette;
- i tasti predefiniti associati ai 319 comandi;
- la formula del punteggio dei recenti;
- il comportamento dell'ereditarietà delle viste per cartella.

Un'osservazione comparativa sistematica del target è prevista come milestone 6.
