# Graph Report - filepilot-clone  (2026-10-12)

## Corpus Check
- 31 files · ~54,591 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 6 file(s) not represented in the graph (top: (none) 2, .ico 1, .rc 1)

## Summary
- 887 nodes · 1759 edges · 53 communities (45 shown, 8 thin omitted)
- Extraction: 97% EXTRACTED · 3% INFERRED · 0% AMBIGUOUS · INFERRED: 59 edges (avg confidence: 0.84)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- Application State
- File Operations & Clipboard
- Font Atlas & Result Type
- Command Identifiers
- Shell Icons & GL Loader
- Quad Batcher Renderer
- Window Lifecycle & Tabs
- Group Menu
- Preview Cache
- Path & Format Helpers
- Input & Hit Testing
- UI Hit Targets
- Navigation & File Commands
- Color & Shape Primitives
- Draw Instruction Struct
- Documented Pitfalls & Principles
- Tab State
- Layout Regions & Context Menu
- Preview Worker Thread
- Dir Stats Internals
- Visual Identity & Architecture Docs
- Volume Profiles
- Rect & Entry Cell Drawing
- Theme Colors
- Frame Drawing
- File Entry Record
- Dir Stats Job Runner
- Functional Spec Reconstruction
- Roadmap Milestones
- List Columns
- Context Menu State
- Preview Entry State
- Methodology & Build Docs
- Dir Stats Service
- Preview Cell Types
- Dir Stats Lanes
- Dir Stats Job
- View Metrics
- Group Modes
- Binary Analysis Docs
- Dir Stats Result
- OpenGL Context Setup
- View Modes
- DIB Reader
- Privacy & Config Docs
- Preview Result Types
- Command Definitions
- View Row
- Dir Stats Lane Config

## God Nodes (most connected - your core abstractions)
1. `AppState` - 154 edges
2. `WindowProc()` - 51 edges
3. `CommandId` - 50 edges
4. `ShellPreviewCache` - 48 edges
5. `UIRenderer` - 38 edges
6. `Layout` - 36 edges
7. `OperationSink` - 31 edges
8. `GroupMenu` - 31 edges
9. `UIDraw` - 29 edges
10. `Rect` - 26 edges

## Surprising Connections (you probably didn't know these)
- `Palette dei colori del prodotto` --semantically_similar_to--> `35 token di tema`  [INFERRED] [semantically similar]
  brand/logo-brief.md → docs/03-spec-funzionale.md
- `Regola zero: niente e fatto finche non e visto` --rationale_for--> `Bug trovati e risolti`  [INFERRED]
  AGENTS.md → docs/02-architettura-ricostruita.md
- `Bolt — identita visiva e logo` --conceptually_related_to--> `README — Bolt`  [INFERRED]
  brand/logo-brief.md → README.md
- `ReloadKeeping()` --calls--> `present`  [INFERRED]
  src/main.cpp → src/dir_stats.h
- `wWinMain()` --calls--> `LoadOpenGLFunctions()`  [INFERRED]
  src/main.cpp → src/gl_loader.cpp

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Pipeline di rendering di Bolt** — docs_02_architettura_ricostruita_quad_batcher, docs_02_architettura_ricostruita_roundedboxsdf, docs_02_architettura_ricostruita_texture_array, cmakelists_shader_dir_definition, agents_rendering_architecture [EXTRACTED 0.85]
- **Vincoli di privacy del progetto Bolt** — agents_privacy_rule, docs_06_privacy_no_disk_writes, docs_06_privacy_store_recents_session, agents_test_sandbox [EXTRACTED 0.85]
- **Flusso del metodo di reverse guidato dalle evidenze** — docs_00_metodologia_four_step_method, docs_01_analisi_binario_mft_falsified, docs_02_architettura_ricostruita_sumdirstats, docs_04_roadmap_reverse_log [EXTRACTED 0.85]

## Communities (53 total, 8 thin omitted)

### Community 0 - "Application State"
Cohesion: 0.03
Nodes (65): AppState, activeTab, bookmarks, clickTargets, clipboardCut, collapsedGroups, contextMenu, cursor (+57 more)

### Community 1 - "File Operations & Clipboard"
Cohesion: 0.09
Nodes (18): ClearClipboard(), ComPtr, ptr_, Copy(), CreateFolder(), Delete(), DescribeHresult(), Move() (+10 more)

### Community 2 - "Font Atlas & Result Type"
Cohesion: 0.05
Nodes (28): Result, Cancelled, Failed, Ok, CreateDib(), DibSection, bitmap, dc (+20 more)

### Community 3 - "Command Identifiers"
Cohesion: 0.04
Nodes (49): CommandId, CmdBack, CmdClearBookmarks, CmdClearFilter, CmdClearRecents, CmdCloseTab, CmdCopy, CmdCut (+41 more)

### Community 4 - "Shell Icons & GL Loader"
Cohesion: 0.06
Nodes (23): LoadOpenGLFunctions(), CreateDib(), DibSection, bitmap, dc, height, pixels, width (+15 more)

### Community 5 - "Quad Batcher Renderer"
Cohesion: 0.07
Nodes (21): Attrib, components, location, offsetFloats, BuildProjection(), CompileShader(), ReadTextFile(), UIRenderer (+13 more)

### Community 6 - "Window Lifecycle & Tabs"
Cohesion: 0.09
Nodes (27): ActivateTab(), BuildContextMenu(), BuildPaletteMenu(), BuildPlaces(), CloseTab(), ComputeLayout(), CurrentDayIndex(), DrawStatusBar() (+19 more)

### Community 7 - "Group Menu"
Cohesion: 0.07
Nodes (30): GroupMenu, collapsed, detailed, dirty, filter, groups, hint, items (+22 more)

### Community 8 - "Preview Cache"
Cohesion: 0.08
Nodes (20): list, ShellPreviewCache, atlasSize_, cache_, cellSize_, columns_, failed_, freeCells_ (+12 more)

### Community 9 - "Path & Format Helpers"
Cohesion: 0.15
Nodes (21): BuildGoToMenu(), FilterMatch(), FormatFileTime(), GlobMatch(), Group, label, GroupMenuOpen(), GroupMenuRebuild() (+13 more)

### Community 10 - "Input & Hit Testing"
Cohesion: 0.14
Nodes (18): CancelRename(), ComputeViewMetrics(), ContextMenuClose(), ContextMenuMove(), CursorIndex(), EnsureSelectionVisible(), FindEntryByName(), count (+10 more)

### Community 11 - "UI Hit Targets"
Cohesion: 0.09
Nodes (22): ClickTarget, index, kind, label, path, rect, Kind, Breadcrumb (+14 more)

### Community 12 - "Navigation & File Commands"
Cohesion: 0.20
Nodes (21): ActivateSelection(), BeginRename(), CommitRename(), ContextMenuActivate(), DoCopyToClipboard(), DoDelete(), DoNewFolder(), DoPaste() (+13 more)

### Community 13 - "Color & Shape Primitives"
Cohesion: 0.15
Nodes (12): GroupMenuDraw(), GroupMenuPanel(), Color, a, b, g, r, Corners (+4 more)

### Community 14 - "Draw Instruction Struct"
Cohesion: 0.11
Nodes (19): UIDraw, color, dashOff, dashOn, layer, mono, radius, rect (+11 more)

### Community 15 - "Documented Pitfalls & Principles"
Cohesion: 0.15
Nodes (11): AGENTS.md — istruzioni operative, Riscritture massive di chiamate annidate, Architettura di rendering in breve, Trappole dell'input sintetico, Sandbox usa e getta per le prove, Il difetto delle due barre del titolo, Bug trovati e risolti, main.cpp a ~4.500 righe: debito tecnico (+3 more)

### Community 16 - "Tab State"
Cohesion: 0.12
Nodes (16): Tab, cursor, directory, entries, errorText, filter, filterActive, groupMode (+8 more)

### Community 17 - "Layout Regions & Context Menu"
Cohesion: 0.17
Nodes (15): ContextMenuClick(), ContextMenuDraw(), ContextMenuPanel(), ContextMenuRowRect(), ContextMenuWidth(), Layout, filterBar, header (+7 more)

### Community 19 - "Dir Stats Internals"
Cohesion: 0.15
Nodes (9): impl_, active, entries, lanes, mapMutex, nextToken, revision, stopping (+1 more)

### Community 20 - "Visual Identity & Architecture Docs"
Cohesion: 0.18
Nodes (13): Bolt — identita visiva e logo, Cartella che si risolve in un fulmine, Consegne dell'icona multi-dimensione, Palette dei colori del prodotto, Architettura ricostruita, Enumerazione con FindFirstFileExW e large fetch, Invalidazione della cache per generazione, RoundedBoxSDF (+5 more)

### Community 21 - "Volume Profiles"
Cohesion: 0.19
Nodes (8): ProfileIndex(), VolumeIncursSeekPenalty(), VolumeProfile, Hdd, Network, Removable, Ssd, Unknown

### Community 22 - "Rect & Entry Cell Drawing"
Cohesion: 0.26
Nodes (9): DrawEntryCell(), DrawList(), IsEntrySelected(), RectsIntersect(), Rect, h, w, x (+1 more)

### Community 23 - "Theme Colors"
Cohesion: 0.15
Nodes (13): Theme, accent, headerBg, panelBg, panelBorder, rowHover, rowOdd, rowSelected (+5 more)

### Community 24 - "Frame Drawing"
Cohesion: 0.39
Nodes (8): DrawFilterBar(), DrawFrame(), DrawHeader(), DrawInspector(), DrawSidebar(), DrawTitleBar(), DrawToolbar(), TextY()

### Community 25 - "File Entry Record"
Cohesion: 0.17
Nodes (12): Entry, bytes, extension, isDirectory, modified, modifiedFull, modifiedText, name (+4 more)

### Community 27 - "Functional Spec Reconstruction"
Cohesion: 0.18
Nodes (10): Tre file di configurazione, Specifica funzionale ricostruita, 23 colonne della lista, 319 comandi HK_*, ConfirmPopup e WarningPopup, Componente riusabile per i menu a gruppi, 54 etichette di raggruppamento, Installer_* incorporato (+2 more)

### Community 28 - "Roadmap Milestones"
Cohesion: 0.20
Nodes (11): Roadmap, M10 — backend Direct3D 11, M1 — shell e architettura di rendering, M2 — icone e anteprime reali, M3b — attributi NTFS di un file, M5 — filtro, ordinamento, raggruppamento, selezione, M6 — tab, quick access, palette, GoTo, M7 — operazioni su file (+3 more)

### Community 29 - "List Columns"
Cohesion: 0.18
Nodes (11): ColumnDef, fraction, label, sort, SortColumn, Files, Folders, Modified (+3 more)

### Community 30 - "Context Menu State"
Cohesion: 0.18
Nodes (11): ContextMenu, items, open, selected, subtitle, x, y, ContextMenuItem (+3 more)

### Community 31 - "Preview Entry State"
Cohesion: 0.22
Nodes (5): IsPreviewableExtension(), State, Pending, Ready, Unavailable

### Community 32 - "Methodology & Build Docs"
Cohesion: 0.20
Nodes (9): Target eseguibile Bolt, Definizione REA_SHADER_DIR, Metodologia, extract.mjs, Etichette Verificato, Inferito, Da verificare, Shader GLSL recuperati dal binario, Un quad porta il proprio stile, README — Bolt (+1 more)

### Community 34 - "Preview Cell Types"
Cohesion: 0.20
Nodes (9): Entry, cell, lru, uv, IconUV, u0, u1, v0 (+1 more)

### Community 35 - "Dir Stats Lanes"
Cohesion: 0.20
Nodes (5): Lane, cv, mutex, queue, threads

### Community 36 - "Dir Stats Job"
Cohesion: 0.22
Nodes (9): DirStatsJob, files, folders, logicalBytes, path, pending, profile, started (+1 more)

### Community 37 - "View Metrics"
Cohesion: 0.22
Nodes (9): ItemRect(), ViewMetrics, cellHeight, cellWidth, columns, grid, iconSize, padding (+1 more)

### Community 38 - "Group Modes"
Cohesion: 0.25
Nodes (8): GroupMode, Count, Date, Name, None, Size, Volume, GroupModeName()

### Community 39 - "Binary Analysis Docs"
Cohesion: 0.29
Nodes (5): Analisi del binario, Tabella dei job asincroni, Tabella AT_* dei tipi di attributo NTFS, Caratterizzazione del PE target, La macchina di SumDirStats

### Community 40 - "Dir Stats Result"
Cohesion: 0.29
Nodes (7): DirStats, complete, files, folders, logicalBytes, partial, present

### Community 42 - "OpenGL Context Setup"
Cohesion: 0.33
Nodes (3): CreateOpenGLContext(), ShowProperties(), StateFromWindow()

### Community 43 - "View Modes"
Cohesion: 0.33
Nodes (6): ViewMode, Details, Icons, List, Tiles, ViewModeName()

### Community 44 - "DIB Reader"
Cohesion: 0.33
Nodes (3): DibReader, bitmap, dc

### Community 46 - "Preview Result Types"
Cohesion: 0.40
Nodes (3): Result, key, rgba

### Community 47 - "Command Definitions"
Cohesion: 0.50
Nodes (4): CommandDef, group, id, label

### Community 48 - "View Row"
Cohesion: 0.50
Nodes (4): Row, entry, group, header

### Community 49 - "Dir Stats Lane Config"
Cohesion: 0.67
Nodes (3): LaneConfig, sliceMs, workers

## Knowledge Gaps
- **421 isolated node(s):** `workers`, `sliceMs`, `path`, `token`, `profile` (+416 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 497 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **8 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `AppState` connect `Application State` to `Font Atlas & Result Type`, `Shell Icons & GL Loader`, `Quad Batcher Renderer`, `Window Lifecycle & Tabs`, `Group Menu`, `Preview Cache`, `Path & Format Helpers`, `Input & Hit Testing`, `UI Hit Targets`, `Navigation & File Commands`, `Color & Shape Primitives`, `Tab State`, `Layout Regions & Context Menu`, `Rect & Entry Cell Drawing`, `Theme Colors`, `Frame Drawing`, `File Entry Record`, `List Columns`, `Context Menu State`, `Dir Stats Service`, `Group Modes`, `OpenGL Context Setup`, `View Modes`, `View Row`?**
  _High betweenness centrality (0.277) - this node is a cross-community bridge._
- **What connects `workers`, `sliceMs`, `path` to the rest of the system?**
  _421 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Application State` be split into smaller, more focused modules?**
  _Cohesion score 0.03076923076923077 - nodes in this community are weakly interconnected._
- **Why does `ShellPreviewCache` connect `Preview Cache` to `Application State`, `Preview Cell Types`, `Quad Batcher Renderer`, `Preview Result Types`, `Preview Worker Thread`, `Preview Entry State`?**
  _High betweenness centrality (0.109) - this node is a cross-community bridge._
- **Should `File Operations & Clipboard` be split into smaller, more focused modules?**
  _Cohesion score 0.09117475160724722 - nodes in this community are weakly interconnected._
- **Why does `CommandId` connect `Command Identifiers` to `Window Lifecycle & Tabs`, `Command Definitions`?**
  _High betweenness centrality (0.095) - this node is a cross-community bridge._
- **Should `Font Atlas & Result Type` be split into smaller, more focused modules?**
  _Cohesion score 0.052597402597402594 - nodes in this community are weakly interconnected._