Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "PaletteOptions.nsh"

!define PBS_MARQUEE 0x08

Name "RainCode++ for Notepad++"
OutFile "..\out\RainCode++-1.0.16-Setup.exe"
VIProductVersion "1.0.16.0"
VIAddVersionKey "ProductName" "RainCode++"
VIAddVersionKey "FileDescription" "RainCode++ Setup for Notepad++"
VIAddVersionKey "FileVersion" "1.0.16"
VIAddVersionKey "LegalCopyright" "Copyright 2026 Jeffrey Morley. Based on RainLexer by the Rainmeter Team."
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose the folder containing notepad++.exe. Setup installs or updates RainCode++ in that copy of Notepad++."
!define MUI_FINISHPAGE_TEXT "RainCode++ is ready. Open Notepad++ and use Language > Rainmeter 5 or Plugins > RainCode++ > Settings sidebar."
!define MUI_UNCONFIRMPAGE_TEXT_TOP "Uninstall closes Notepad++ normally, removes RainCode++, and reopens Notepad++. You can save open files when prompted."
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE CheckDirectory
!insertmacro MUI_PAGE_DIRECTORY
Page custom PalettePage SavePalette
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Var Palette
Var PaletteControl
Var ProgramFilesOnly

!macro FindPowerShell
    ; A 32-bit PowerShell process cannot reliably inspect 64-bit Notepad++ processes.
    StrCpy $2 "$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
    ${IfNot} ${FileExists} "$2"
        StrCpy $2 "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe"
    ${EndIf}
!macroend

Function UseDetectedDirectoryCase
    # The Notepad++ registry entry can be all caps. Read each existing folder
    # name from the filesystem so the directory page shows its real spelling.
    GetFullPathName $0 "$INSTDIR"
    ${GetRoot} $0 $1
    ${If} $1 == ""
        Return
    ${EndIf}
    StrCpy $2 ""
    ${DoWhile} $0 != $1
        ClearErrors
        FindFirst $3 $4 "$0"
        ${If} ${Errors}
            Return
        ${EndIf}
        FindClose $3
        StrCpy $2 "\$4$2"
        ${GetParent} $0 $0
        ${If} $0 == ""
            Return
        ${EndIf}
    ${Loop}
    StrCpy $INSTDIR "$1$2"
FunctionEnd

Function .onInit
    StrCpy $Palette "Keep"
    ${GetParameters} $0
    ${GetOptions} $0 "/ProgramFilesOnly" $1
    ${IfNot} ${Errors}
        StrCpy $ProgramFilesOnly "-ProgramFilesOnly"
        ${GetOptions} $0 "/Palette=" $1
        ${IfNot} ${Errors}
            StrCpy $Palette $1
        ${EndIf}
        ${If} $INSTDIR == ""
            SetErrorLevel 1
            Abort
        ${EndIf}
        SetSilent silent
        Return
    ${EndIf}
    ${If} $INSTDIR != ""
        Return
    ${EndIf}
    SetRegView 64
    ReadRegStr $0 HKLM "SOFTWARE\Notepad++" ""
    ${If} $0 == ""
        SetRegView 32
        ReadRegStr $0 HKLM "SOFTWARE\Notepad++" ""
    ${EndIf}
    ${If} $0 != ""
        StrCpy $INSTDIR $0
        Call UseDetectedDirectoryCase
    ${ElseIf} ${FileExists} "$PROGRAMFILES64\Notepad++\notepad++.exe"
        StrCpy $INSTDIR "$PROGRAMFILES64\Notepad++"
    ${Else}
        StrCpy $INSTDIR "$PROGRAMFILES\Notepad++"
    ${EndIf}
FunctionEnd

Function CheckDirectory
    ${IfNot} ${FileExists} "$INSTDIR\notepad++.exe"
        MessageBox MB_ICONEXCLAMATION "Choose the folder containing notepad++.exe."
        Abort
    ${EndIf}
FunctionEnd

Function PalettePage
    !insertmacro MUI_HEADER_TEXT "Rainmeter syntax colors" "Choose the palette for Rainmeter 5 files."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}
    ${NSD_CreateLabel} 0 6u 100% 22u "Choose how RainCode++ should color Rainmeter files."
    Pop $0
    ${NSD_CreateDropList} 0 32u 100% 130u ""
    Pop $PaletteControl
    StrCpy $1 "$APPDATA\Notepad++"
    ${If} ${FileExists} "$INSTDIR\doLocalConf.xml"
        StrCpy $1 "$INSTDIR"
    ${EndIf}
    ${If} ${FileExists} "$1\plugins\config\RainCodePP.xml"
    ${OrIf} ${FileExists} "$1\plugins\config\RainCodePP-palette.xml"
    ${OrIf} ${FileExists} "$1\plugins\config\RainLexer.xml"
        ${NSD_CB_AddString} $PaletteControl "Keep Current Style"
        StrCpy $3 "Keep Current Style preserves your RainCode++ palette or imports your RainLexer styles on a first install."
    ${Else}
        ${NSD_CB_AddString} $PaletteControl "Match Notepad++ mode"
        StrCpy $3 "Match Notepad++ mode picks a light or dark default based on your Notepad++ setting."
    ${EndIf}
    !insertmacro AddPalettes
    ${NSD_CB_SetSelectionIndex} $PaletteControl 0
    ${NSD_CB_SelectStringExact} $PaletteControl $Palette
    ${NSD_CreateLabel} 0 58u 100% 72u "$3$\r$\n$\r$\nLexer configuration: $1\plugins\Config"
    Pop $0
    nsDialogs::Show
FunctionEnd

Function SavePalette
    ${NSD_GetText} $PaletteControl $Palette
FunctionEnd

Function RunInstallScript
    Pop $3
    StrCpy $4 0
    ${IfNot} ${Silent}
        StrCpy $4 $mui.InstFilesPage.ProgressBar
        ${If} $4 != 0
            ${NSD_ProgressBar_GetPos} $4 $5
            ${NSD_AddStyle} $4 ${PBS_MARQUEE}
            SendMessage $4 ${PBM_SETMARQUEE} 1 0
        ${EndIf}
    ${EndIf}

    !insertmacro FindPowerShell
    nsExec::ExecToStack '"$2" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\Install.ps1" -NppDirectory "$INSTDIR\." -Palette "$Palette" -SetupExecutable "$EXEPATH" $ProgramFilesOnly $3'
    Pop $0
    Pop $1

    ${If} $4 != 0
        SendMessage $4 ${PBM_SETMARQUEE} 0 0
        ${NSD_RemoveStyle} $4 ${PBS_MARQUEE}
        ${NSD_ProgressBar_SetPos} $4 $5
    ${EndIf}
    DetailPrint "$1"
FunctionEnd

Section "RainCode++"
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File "Install.ps1"
    File "ClassicBackup.ps1"
    File "NotepadProcesses.ps1"
    File "InstallerPrivileges.ps1"
    WriteUninstaller "$PLUGINSDIR\Uninstall RainCode++.exe"
    File "..\LICENSE"
    File "..\LICENSE-GPL-2.0"
    File "..\ThirdParty\LICENSE-Scintilla.txt"
    File "..\README.md"
    File "..\HowToInstall.md"
    File "..\manual.md"
    SetOutPath "$PLUGINSDIR\Presets"
    File "..\Config\Presets\*.xml"
    File "..\Config\Presets\Palettes.txt"
    SetOutPath "$PLUGINSDIR\x86"
    File "..\x32-Release\RainCodePP.dll"
    File "..\x32-Release\RainCodePPSettings.exe"
    SetOutPath "$PLUGINSDIR\x64"
    File "..\x64-Release\RainCodePP.dll"
    File "..\x64-Release\RainCodePPSettings.exe"
    DetailPrint "Installing RainCode++ and applying your syntax palette..."
    Push ""
    Call RunInstallScript
    ${If} $ProgramFilesOnly != ""
        ${If} $0 != 0
            SetErrorLevel 1
            Abort
        ${EndIf}
        Goto install_done
    ${EndIf}
    ${If} $0 == 2
        MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Notepad++ or RainCode++ Settings is open.$\r$\n$\r$\nClose them and continue setup? You can save any open work when prompted." /SD IDNO IDYES close_apps
        SetErrorLevel 2
        Abort
close_apps:
        DetailPrint "Waiting for Notepad++ and RainCode++ Settings to close. Save open work if prompted."
        Push "-CloseRunningApps"
        Call RunInstallScript
        ${If} $0 == 3
            MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "An app is still open. Finish saving or closing it, then choose Retry." /SD IDCANCEL IDRETRY close_apps
            SetErrorLevel 2
            Abort
        ${EndIf}
    ${EndIf}
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "RainCode++ setup failed.$\r$\n$1" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${IfNot} ${FileExists} "$INSTDIR\doLocalConf.xml"
        SetRegView 64
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP" "DisplayName" "RainCode++ for Notepad++"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP" "DisplayVersion" "1.0.16"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP" "InstallLocation" "$INSTDIR"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP" "UninstallString" '"$INSTDIR\plugins\RainCodePP\Uninstall RainCode++.exe"'
        WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP" "NoModify" 1
        WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP" "NoRepair" 1
    ${EndIf}
install_done:
SectionEnd

Function un.onInit
    GetFullPathName $INSTDIR "$INSTDIR\..\.."
    ${GetParameters} $0
    ${GetOptions} $0 "/ProgramFilesOnly" $1
    ${IfNot} ${Errors}
        StrCpy $ProgramFilesOnly "-ProgramFilesOnly"
        SetSilent silent
    ${EndIf}
FunctionEnd

Section "Uninstall"
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File "Uninstall.ps1"
    File "ClassicBackup.ps1"
    File "NotepadProcesses.ps1"
    File "InstallerPrivileges.ps1"
    !insertmacro FindPowerShell
    nsExec::ExecToStack '"$2" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\Uninstall.ps1" -NppDirectory "$INSTDIR\." $ProgramFilesOnly'
    Pop $0
    Pop $1
    DetailPrint "$1"
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "RainCode++ uninstall failed.$\r$\n$1" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} $ProgramFilesOnly != ""
        Goto uninstall_done
    ${EndIf}
    SetRegView 64
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP" "InstallLocation"
    ${If} $0 == $INSTDIR
        DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP"
    ${EndIf}
uninstall_done:
SectionEnd

