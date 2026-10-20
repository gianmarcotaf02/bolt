; ---------------------------------------------------------------------------
; Installer di Bolt - Inno Setup 6.
;
; Il setup e' **per utente** di default: niente amministratore, e la cartella
; predefinita e' quella dell'utente corrente (con `{autopf}` e
; `PrivilegesRequired=lowest` Inno la risolve in `%LOCALAPPDATA%\Programs\Bolt`).
; Chi vuole installarlo per tutti lo sceglie nel wizard e mette la password di
; amministratore: e' l'unica differenza, e la fa Inno.
;
; La versione **non si scrive qui**: la passa `tools/make-installer.ps1` con
; `/DAppVersion=...`, che a sua volta la legge da `CMakeLists.txt`. Una versione
; scritta in due posti e' una versione che prima o poi diverge.
;
; Cosa scrive, per intero: i file in `{app}`, la voce di disinstallazione,
; il gruppo nel menu Start e - solo se l'utente spunta la casella - l'icona sul
; desktop. Le preferenze di Bolt (`%APPDATA%\Bolt\settings.ini`) **non** le tocca
; ne' l'installazione ne' la disinstallazione: la disinstallazione chiede, e se
; la risposta e' no le lascia dove sono.
; ---------------------------------------------------------------------------
#define AppName "Bolt"
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppBuildRoot
  #define AppBuildRoot "..\build\RelWithDebInfo"
#endif

[Setup]
; AppId **stabile**: e' quello che dice a Windows "questa e' la stessa
; applicazione", quindi una versione nuova aggiorna invece di installarne una
; seconda accanto. Non va cambiato, mai.
AppId={{1FA71931-15BF-4EBD-8085-F9BC5865CFD6}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Progetto personale (uso personale, non commerciale)
AppPublisherURL=https://github.com/gianmarcotaf02/bolt
AppSupportURL=https://github.com/gianmarcotaf02/bolt
AppUpdatesURL=https://github.com/gianmarcotaf02/bolt
VersionInfoVersion={#AppVersion}

; `{autopf}` con `PrivilegesRequired=lowest` = cartella dell'utente.
DefaultDirName={autopf}\Bolt
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir=..\out
OutputBaseFilename=Bolt-{#AppVersion}-win64-setup
SetupIconFile=..\brand\bolt.ico
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\Bolt.exe

; `lzma2/max` su 2,5 MB non e' un lusso: il pacchetto e' piccolo e il tempo di
; decompressione e' comunque un istante.
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
AllowNoIcons=yes

; Aggiornamento sopra un'installazione esistente: Inno chiede di chiudere Bolt se
; e' aperto, invece di lasciare un exe in uso a meta' copia.
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no
ShowLanguageDialog=auto

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#AppBuildRoot}\Bolt.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppBuildRoot}\shaders\ui.vert"; DestDir: "{app}\shaders"; Flags: ignoreversion
Source: "{#AppBuildRoot}\shaders\ui.frag"; DestDir: "{app}\shaders"; Flags: ignoreversion
Source: "..\installer\README.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; La cartella nel menu Start e' quella dell'applicazione (`{group}` con
; `DisableProgramGroupPage=yes` = `{autoprograms}\Bolt`), non l'intero menu.
Name: "{group}\{#AppName}"; Filename: "{app}\Bolt.exe"; WorkingDir: "{app}"; Comment: "File manager"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Bolt.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Bolt.exe"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; I due file che Bolt scrive **solo quando c'e' qualcosa da dire** (`AGENTS.md` §4).
; Non li installa il setup, quindi Inno non li toglierebbe: sono nostri, e se ne vanno
; con il programma. Senza queste due righe la cartella resta li', con dentro un log, e
; sembra un disinstallatore che ha funzionato a meta'.
Type: files; Name: "{app}\bolt-diag.log"
Type: files; Name: "{app}\startup-error.log"

[Code]
// La disinstallazione chiede delle preferenze **prima** di toglierle, e in
// modalita' silenziosa non chiede niente: non cancella. Un disinstallatore che
// decide da solo di buttare le impostazioni di chi lo usa e' peggio di uno che
// le lascia: la cartella si cancella a mano in dieci secondi, e il README del
// pacchetto dice dove sta.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Preferenze: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  Preferenze := ExpandConstant('{userappdata}\Bolt');
  if UninstallSilent or (not DirExists(Preferenze)) then
    Exit;

  if MsgBox('Vuoi togliere anche le preferenze di Bolt?' + #13#10 + Preferenze,
            mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    DelTree(Preferenze, True, True, True);
end;
