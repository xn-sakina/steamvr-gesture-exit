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
DefaultDirName={userpf}\SteamVR Gesture Exit
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
DialogFontSize=10
zhtrad.LanguageName=中文
zhtrad.LanguageID=$0404

[CustomMessages]
en.VersionChange=Installed version: %1%nVersion to install: %2
en.FreshInstall=Version to install: %1
en.ProgressTitle=Installing SteamVR Gesture Exit
en.ProgressDescription=Please wait while Setup checks your system and installs the app.
en.PreflightStatus=Checking the installation environment...
en.TransactionStatus=Installing files and registering with SteamVR...
en.RollbackFailed=The operation did not complete. Run this installer again.
en.UninstallStatus=Removing this app from SteamVR...
en.CoordinatorFailed=Installation was unsuccessful. It may have been blocked by antivirus software.
en.OperationBusy=Another installer is running. Close it before installing or uninstalling.
zh.VersionChange=已安装版本：%1%n将安装版本：%2
zh.FreshInstall=将安装版本：%1
zh.ProgressTitle=正在安装 SteamVR Gesture Exit
zh.ProgressDescription=安装程序正在检查系统并安装本软件，请稍候。
zh.PreflightStatus=正在检查安装环境…
zh.TransactionStatus=正在安装文件并注册到 SteamVR…
zh.RollbackFailed=操作未完成，请再次运行此安装程序。
zh.UninstallStatus=正在从 SteamVR 移除本软件…
zh.CoordinatorFailed=无法成功安装，也许被杀毒软件拦截。
zh.OperationBusy=另一个安装程序正在运行，请关闭后再安装或卸载。
zhtrad.VersionChange=已安装版本：%1%n将安装版本：%2
zhtrad.FreshInstall=将安装版本：%1
zhtrad.ProgressTitle=正在安装 SteamVR Gesture Exit
zhtrad.ProgressDescription=安裝程式正在檢查系統並安裝本軟體，請稍候。
zhtrad.PreflightStatus=正在檢查安裝環境…
zhtrad.TransactionStatus=正在安装文件并注册到 SteamVR…
zhtrad.RollbackFailed=操作未完成，请再次运行此安装程序。
zhtrad.UninstallStatus=正在从 SteamVR 移除本软件…
zhtrad.CoordinatorFailed=无法成功安装，也许被杀毒软件拦截。
zhtrad.OperationBusy=另一个安装程序正在运行，请关闭后再安装或卸载。

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
  InstallProgressPage: TOutputProgressWizardPage;

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
  InstallProgressPage := CreateOutputProgressPage(
    CustomMessage('ProgressTitle'), CustomMessage('ProgressDescription'));
  if PreviousVersion = '' then
    VersionText := FmtMessage(CustomMessage('FreshInstall'), ['{#AppVersion}'])
  else
    VersionText := FmtMessage(CustomMessage('VersionChange'), [PreviousVersion, '{#AppVersion}']);
  WizardForm.WelcomeLabel2.Caption := VersionText;
end;

procedure ShowInstallProgress(const StatusText: String; const Position, Maximum: Integer);
begin
  if WizardSilent then
    Exit;
  InstallProgressPage.SetText(StatusText, '');
  InstallProgressPage.SetProgress(Position, Maximum);
  InstallProgressPage.Show;
  WizardForm.Update;
end;

procedure UpdateInstallProgress(const StatusText: String; const Position, Maximum: Integer);
begin
  if WizardSilent then
    Exit;
  InstallProgressPage.SetText(StatusText, '');
  InstallProgressPage.SetProgress(Position, Maximum);
  WizardForm.Update;
end;

procedure HideInstallProgress;
begin
  if not WizardSilent then
    InstallProgressPage.Hide;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ErrorText: String;
begin
  Result := '';
  ShowInstallProgress(CustomMessage('PreflightStatus'), 0, 4);
  try
    ExtractTemporaryFile('transaction.ps1');
    ExtractTemporaryFile('payload.json');
    ExtractTemporaryFile('coordinator_runner.exe');
    TransactionScript := ExpandConstant('{tmp}\transaction.ps1');
    CoordinatorRunner := ExpandConstant('{tmp}\coordinator_runner.exe');
    UpdateInstallProgress(CustomMessage('PreflightStatus'), 1, 4);
    if not RunCoordinator('Recover', '', '', '', ErrorText) then
      Result := ErrorText
    else
    begin
      UpdateInstallProgress(CustomMessage('PreflightStatus'), 2, 4);
      if not RunCoordinator('Preflight', ExpandConstant('{app}'), '', '{#AppVersion}', ErrorText) then
        Result := ErrorText
      else
        UpdateInstallProgress(CustomMessage('PreflightStatus'), 3, 4);
    end;
  finally
    HideInstallProgress;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorText: String;
  ArchivePath: String;
begin
  if CurStep = ssInstall then
  begin
    ShowInstallProgress(CustomMessage('TransactionStatus'), 3, 4);
    try
      ExtractTemporaryFile('payload.zip');
      ArchivePath := ExpandConstant('{tmp}\payload.zip');
      TransactionStarted := True;
      if not RunCoordinator('Install', ExpandConstant('{app}'), ArchivePath, '{#AppVersion}', ErrorText) then
        RaiseException(ErrorText);
      UpdateInstallProgress(CustomMessage('TransactionStatus'), 4, 4);
    finally
      HideInstallProgress;
    end;
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
