#ifndef AppVersion
  #error AppVersion must be provided by installer/build.ps1
#endif
#ifndef PayloadArchive
  #error PayloadArchive must be provided by installer/build.ps1
#endif
#ifndef OutputDir
  #error OutputDir must be provided by installer/build.ps1
#endif
#ifndef TransactionScript
  #error TransactionScript must be provided by installer/build.ps1
#endif
#ifndef PayloadManifest
  #error PayloadManifest must be provided by installer/build.ps1
#endif
#ifndef CoordinatorRunner
  #error CoordinatorRunner must be provided by installer/build.ps1
#endif
#ifndef PayloadInstallSize
  #error PayloadInstallSize must be provided by installer/build.ps1
#endif
#ifndef PayloadExpandedSize
  #error PayloadExpandedSize must be provided by installer/build.ps1
#endif
#ifndef PayloadArchiveSize
  #error PayloadArchiveSize must be provided by installer/build.ps1
#endif
#ifndef PayloadLargestFileSize
  #error PayloadLargestFileSize must be provided by installer/build.ps1
#endif

#define AppName "SteamVR Gesture Exit"
#define AppIdValue "{{3F4726BD-29BB-4E6D-A869-529D9145745E}"
#define SetupMutexName "SteamVRGestureExit.Setup.3F4726BD29BB4E6DA869529D9145745E"

[Setup]
AppId={#AppIdValue}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Kanamio
AppPublisherURL=https://github.com/xn-sakina/steamvr-gesture-exit
AppSupportURL=https://github.com/xn-sakina/steamvr-gesture-exit/issues
AppUpdatesURL=https://github.com/xn-sakina/steamvr-gesture-exit/releases
DefaultDirName={localappdata}\Programs\SteamVR Gesture Exit
UsePreviousAppDir=yes
DisableDirPage=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
SetupMutex={#SetupMutexName}
OutputDir={#OutputDir}
OutputBaseFilename=steamvr-gesture-exit-v{#AppVersion}-windows-x64-setup
SetupIconFile=..\assets\brand\steamvr-gesture-exit.ico
WizardSmallImageFile=..\assets\brand\steamvr-gesture-exit.png
UninstallDisplayIcon={app}\bin\win64\steamvr_gesture_exit.exe
UninstallDisplayName={#AppName}
UninstallFilesDir={app}\installer\uninstall
VersionInfoVersion={#AppVersion}
VersionInfoCompany=Kanamio
VersionInfoDescription={#AppName} Installer
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ExtraDiskSpaceRequired={#PayloadInstallSize}
UninstallDisplaySize={#PayloadInstallSize}
AllowCancelDuringInstall=yes
DirExistsWarning=yes
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "zhtrad"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[LangOptions]
zhtrad.LanguageName=中文
zhtrad.LanguageID=$0404

[CustomMessages]
en.VersionChange=Installed: %1%nCurrent installer: %2
en.FreshInstall=Installed: none%nCurrent installer: %1
en.TransactionStatus=Installing files and registering with SteamVR...
en.RollbackFailed=Rollback is incomplete. Run this installer again.
en.UninstallStatus=Removing SteamVR registration...
en.CoordinatorFailed=Installation helper could not run. Check antivirus settings.
en.OperationBusy=Another installation or uninstallation is already running.
zh.VersionChange=已安装：%1%n当前安装包：%2
zh.FreshInstall=已安装：无%n当前安装包：%1
zh.TransactionStatus=正在安装文件并注册到 SteamVR…
zh.RollbackFailed=回滚未完成，请再次运行此安装程序。
zh.UninstallStatus=正在移除 SteamVR 注册…
zh.CoordinatorFailed=无法运行安装辅助程序，请检查杀毒软件拦截。
zh.OperationBusy=另一个安装或卸载操作正在运行。
zhtrad.VersionChange=已安装：%1%n当前安装包：%2
zhtrad.FreshInstall=已安装：无%n当前安装包：%1
zhtrad.TransactionStatus=正在安装文件并注册到 SteamVR…
zhtrad.RollbackFailed=回滚未完成，请再次运行此安装程序。
zhtrad.UninstallStatus=正在移除 SteamVR 注册…
zhtrad.CoordinatorFailed=无法运行安装辅助程序，请检查杀毒软件拦截。
zhtrad.OperationBusy=另一个安装或卸载操作正在运行。

[Files]
Source: "{#PayloadArchive}"; DestName: "payload.zip"; Flags: dontcopy noencryption
Source: "{#TransactionScript}"; DestName: "transaction.ps1"; Flags: dontcopy noencryption
Source: "{#PayloadManifest}"; DestName: "payload.json"; Flags: dontcopy noencryption
Source: "{#CoordinatorRunner}"; DestName: "coordinator_runner.exe"; Flags: dontcopy noencryption

[UninstallDelete]
Type: files; Name: "{app}\.steamvr-gesture-exit-owner.json"
Type: files; Name: "{app}\bin\win64\steamvr_gesture_exit.exe"
Type: files; Name: "{app}\bin\win64\openvr_api.dll"
Type: files; Name: "{app}\manifest\steamvr_gesture_exit.vrmanifest"
Type: files; Name: "{app}\assets\brand\steamvr-gesture-exit.png"
Type: files; Name: "{app}\installer\steamvr_gesture_exit_cli.exe"
Type: files; Name: "{app}\installer\coordinator_runner.exe"
Type: files; Name: "{app}\installer\openvr_api.dll"
Type: files; Name: "{app}\installer\transaction.ps1"
Type: files; Name: "{app}\LICENSE"
Type: files; Name: "{app}\THIRD_PARTY_NOTICES.txt"
Type: dirifempty; Name: "{app}\bin\win64"
Type: dirifempty; Name: "{app}\bin"
Type: dirifempty; Name: "{app}\manifest"
Type: dirifempty; Name: "{app}\assets\brand"
Type: dirifempty; Name: "{app}\assets"
Type: dirifempty; Name: "{app}\installer\uninstall"
Type: dirifempty; Name: "{app}\installer"
Type: dirifempty; Name: "{app}"

[Code]
var
  TransactionStarted: Boolean;
  SetupCompleted: Boolean;
  UninstallPrepared: Boolean;
  TransactionScript: String;
  CoordinatorRunner: String;
  ResultFile: String;

function GetCurrentProcessId: DWORD;
  external 'GetCurrentProcessId@kernel32.dll stdcall';

function Quote(const Value: String): String;
begin
  Result := '"' + Value + '"';
end;

function CoordinatorLanguage: String;
begin
  if (ActiveLanguage = 'zh') or (ActiveLanguage = 'zhtrad') then
    Result := 'zh'
  else
    Result := 'en';
end;

function ReadResultOrDefault(const DefaultText: String): String;
var
  ResultLines: TArrayOfString;
begin
  Result := DefaultText;
  if LoadStringsFromFile(ResultFile, ResultLines) and
    (GetArrayLength(ResultLines) > 0) and (Trim(ResultLines[0]) <> '') then
    Result := Trim(ResultLines[0]);
end;

function RunCoordinator(const CoordinatorAction, TargetPath, ArchivePath, CoordinatorVersion: String;
  var ErrorText: String): Boolean;
var
  Params: String;
  ResultCode: Integer;
  PowerShellPath: String;
begin
  ResultFile := ExpandConstant('{tmp}\steamvr-gesture-exit-installer-result.txt');
  DeleteFile(ResultFile);
  PowerShellPath := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  Params := '--parent-pid ' + IntToStr(GetCurrentProcessId) + ' -- ' + Quote(PowerShellPath) +
    ' -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' +
    Quote(TransactionScript) + ' -Action ' + CoordinatorAction +
    ' -Language ' + CoordinatorLanguage + ' -ResultFile ' + Quote(ResultFile);
  if TargetPath <> '' then
    Params := Params + ' -Target ' + Quote(TargetPath);
  if ArchivePath <> '' then
    Params := Params + ' -PayloadArchive ' + Quote(ArchivePath);
  if CoordinatorVersion <> '' then
    Params := Params + ' -Version ' + Quote(CoordinatorVersion);
  if CoordinatorAction = 'Preflight' then
    Params := Params + ' -PayloadManifest ' + Quote(ExpandConstant('{tmp}\payload.json')) +
      ' -PayloadInstallSize {#PayloadInstallSize}' +
      ' -PayloadExpandedSize {#PayloadExpandedSize}' +
      ' -PayloadArchiveSize {#PayloadArchiveSize}' +
      ' -PayloadLargestFileSize {#PayloadLargestFileSize}';

  Result := Exec(CoordinatorRunner, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and
    (ResultCode = 0);
  if not Result then
    ErrorText := ReadResultOrDefault(CustomMessage('CoordinatorFailed'))
  else
    ErrorText := '';
end;

procedure RegisterExtraCloseApplicationsResources;
begin
  RegisterExtraCloseApplicationsResource(ExpandConstant('{app}\bin\win64\steamvr_gesture_exit.exe'));
  RegisterExtraCloseApplicationsResource(ExpandConstant('{app}\bin\win64\openvr_api.dll'));
  RegisterExtraCloseApplicationsResource(ExpandConstant('{app}\installer\steamvr_gesture_exit_cli.exe'));
  RegisterExtraCloseApplicationsResource(ExpandConstant('{app}\installer\coordinator_runner.exe'));
  RegisterExtraCloseApplicationsResource(ExpandConstant('{app}\installer\openvr_api.dll'));
  RegisterExtraCloseApplicationsResource(ExpandConstant('{app}\installer\transaction.ps1'));
end;

function PreviousVersion: String;
begin
  Result := GetPreviousData('Version', '');
end;

procedure InitializeWizard;
var
  VersionText: String;
begin
  if PreviousVersion = '' then
    VersionText := FmtMessage(CustomMessage('FreshInstall'), ['{#AppVersion}'])
  else
    VersionText := FmtMessage(CustomMessage('VersionChange'), [PreviousVersion, '{#AppVersion}']);
  WizardForm.WelcomeLabel2.Caption := VersionText;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ErrorText: String;
begin
  Result := '';
  ExtractTemporaryFile('transaction.ps1');
  ExtractTemporaryFile('payload.json');
  ExtractTemporaryFile('coordinator_runner.exe');
  TransactionScript := ExpandConstant('{tmp}\transaction.ps1');
  CoordinatorRunner := ExpandConstant('{tmp}\coordinator_runner.exe');
  if not RunCoordinator('Recover', '', '', '', ErrorText) then
    Result := ErrorText
  else if not RunCoordinator('Preflight', ExpandConstant('{app}'), '', '{#AppVersion}', ErrorText) then
    Result := ErrorText;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorText: String;
  ArchivePath: String;
begin
  if CurStep = ssInstall then
  begin
    WizardForm.StatusLabel.Caption := CustomMessage('TransactionStatus');
    ExtractTemporaryFile('payload.zip');
    ArchivePath := ExpandConstant('{tmp}\payload.zip');
    TransactionStarted := True;
    if not RunCoordinator('Install', ExpandConstant('{app}'), ArchivePath, '{#AppVersion}', ErrorText) then
      RaiseException(ErrorText);
  end
  else if CurStep = ssDone then
  begin
    SetupCompleted := True;
    if not RunCoordinator('Commit', ExpandConstant('{app}'), '', '', ErrorText) then
      Log('Transaction commit cleanup was deferred: ' + ErrorText);
  end;
end;

procedure DeinitializeSetup;
var
  ErrorText: String;
begin
  if TransactionStarted and (not SetupCompleted) and (TransactionScript <> '') then
  begin
    if not RunCoordinator('Rollback', ExpandConstant('{app}'), '', '', ErrorText) then
      MsgBox(CustomMessage('RollbackFailed'), mbError, MB_OK);
  end;
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Version', '{#AppVersion}');
end;

function InitializeUninstall: Boolean;
begin
  if CheckForMutexes('{#SetupMutexName}') then
  begin
    MsgBox(CustomMessage('OperationBusy'), mbError, MB_OK);
    Result := False;
    Exit;
  end;
  CreateMutex('{#SetupMutexName}');
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ErrorText: String;
begin
  if (CurUninstallStep <> usUninstall) or UninstallPrepared then
    Exit;
  UninstallPrepared := True;
  UninstallProgressForm.StatusLabel.Caption := CustomMessage('UninstallStatus');
  TransactionScript := ExpandConstant('{app}\installer\transaction.ps1');
  CoordinatorRunner := ExpandConstant('{app}\installer\coordinator_runner.exe');
  if not RunCoordinator('Uninstall', ExpandConstant('{app}'), '', '', ErrorText) then
  begin
    MsgBox(ErrorText, mbError, MB_OK);
    Abort;
  end;
end;
