Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "PaletteOptions.nsh"

!define PBS_MARQUEE 0x08

Name "RainSyntax for Notepad++"
OutFile "..\out\RainSyntax-1.0.29-Setup.exe"
VIProductVersion "1.0.29.0"
VIAddVersionKey "ProductName" "RainSyntax"
VIAddVersionKey "FileDescription" "RainSyntax Setup for Notepad++"
VIAddVersionKey "FileVersion" "1.0.29"
VIAddVersionKey "LegalCopyright" "Copyright 2026 Jeffrey Morley. Based on RainLexer by the Rainmeter Team."
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose the folder containing notepad++.exe. Setup installs or updates RainSyntax in that copy of Notepad++."
!define MUI_FINISHPAGE_TEXT "RainSyntax is ready. Open Notepad++ and use Language > Rainmeter 5 or Plugins > RainSyntax > Settings sidebar."
!define MUI_UNCONFIRMPAGE_TEXT_TOP "Uninstall closes Notepad++ normally, removes RainSyntax, and reopens Notepad++. You can save open files when prompted."
!define MUI_ICON "..\RainSyntax\Rainmeter.ico"
!define MUI_UNICON "..\RainSyntax\Rainmeter.ico"
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
Var PaletteLabel
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
    ${NSD_CreateLabel} 0 5u 100% 12u "Choose a starting palette."
    Pop $0
    ${NSD_CreateListBox} 0 20u 100% 84u ""
    Pop $PaletteControl
    StrCpy $1 "$APPDATA\Notepad++"
    ${If} ${FileExists} "$INSTDIR\doLocalConf.xml"
        StrCpy $1 "$INSTDIR"
    ${EndIf}
    StrCpy $3 ""
    ${If} ${FileExists} "$1\plugins\config\RainSyntax.xml"
    ${OrIf} ${FileExists} "$1\plugins\config\RainSyntax-palette.xml"
    ${OrIf} ${FileExists} "$1\plugins\config\RainCodePP.xml"
    ${OrIf} ${FileExists} "$1\plugins\config\RainCodePP-palette.xml"
    ${OrIf} ${FileExists} "$INSTDIR\plugins\config\RainCodePP.xml"
        ${NSD_LB_AddString} $PaletteControl "Keep Current Style"
        StrCpy $3 "Keep Current Style uses your saved RainSyntax palette."
    ${EndIf}
    ${If} ${FileExists} "$INSTDIR\plugins\config\RainLexer.xml"
    ${OrIf} ${FileExists} "$1\plugins\config\RainLexer.xml"
        ${NSD_LB_AddString} $PaletteControl "Import RainLexer Style"
        StrCpy $3 "$3 Import RainLexer Style copies the original RainLexer palette."
    ${EndIf}
    ${If} $3 == ""
        ${NSD_LB_AddString} $PaletteControl "Match Notepad++ mode"
        StrCpy $3 "Match Notepad++ mode picks a light or dark default based on your Notepad++ setting."
    ${EndIf}
    !insertmacro AddPalettes
    SendMessage $PaletteControl ${LB_FINDSTRINGEXACT} -1 "STR:$PaletteLabel" $2
    ${If} $2 == -1
        StrCpy $2 0
    ${EndIf}
    ${NSD_LB_SetSelectionIndex} $PaletteControl $2
    ${NSD_CreateLabel} 0 110u 100% 29u "$3$\r$\nLexer configuration: $1\plugins\Config"
    Pop $0
    nsDialogs::Show
FunctionEnd

Function SavePalette
    ${NSD_LB_GetSelection} $PaletteControl $PaletteLabel
    StrCpy $Palette $PaletteLabel
    ; Only bundled palettes have a mode suffix. PowerShell receives the base name.
    StrCpy $0 $PaletteLabel 8 -8
    ${If} $0 == " (Light)"
        StrCpy $Palette $PaletteLabel -8
    ${Else}
        StrCpy $0 $PaletteLabel 7 -7
        ${If} $0 == " (Dark)"
            StrCpy $Palette $PaletteLabel -7
        ${EndIf}
    ${EndIf}
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

Section "RainSyntax"
    InitPluginsDir
    SetOutPath "$PLUGINSDIR"
    File "Install.ps1"
    File "ClassicBackup.ps1"
    File "NotepadProcesses.ps1"
    File "InstallerPrivileges.ps1"
    File "PreviousInstall.ps1"
    WriteUninstaller "$PLUGINSDIR\Uninstall RainSyntax.exe"
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
    File "..\x32-Release\RainSyntax.dll"
    File "..\x32-Release\RainSyntaxSettings.exe"
    SetOutPath "$PLUGINSDIR\x64"
    File "..\x64-Release\RainSyntax.dll"
    File "..\x64-Release\RainSyntaxSettings.exe"
    DetailPrint "Installing RainSyntax and applying your syntax palette..."
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
        MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Notepad++ or RainSyntax Settings is open.$\r$\n$\r$\nClose them and continue setup? You can save any open work when prompted." /SD IDNO IDYES close_apps
        SetErrorLevel 2
        Abort
close_apps:
        DetailPrint "Waiting for Notepad++ and RainSyntax Settings to close. Save open work if prompted."
        Push "-CloseRunningApps"
        Call RunInstallScript
        ${If} $0 == 3
            MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "An app is still open. Finish saving or closing it, then choose Retry." /SD IDCANCEL IDRETRY close_apps
            SetErrorLevel 2
            Abort
        ${EndIf}
    ${EndIf}
    ${If} $0 != 0
        MessageBox MB_ICONSTOP "RainSyntax setup failed.$\r$\n$1" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${IfNot} ${FileExists} "$INSTDIR\doLocalConf.xml"
        SetRegView 64
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainSyntax" "DisplayName" "RainSyntax for Notepad++"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainSyntax" "DisplayVersion" "1.0.29"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainSyntax" "InstallLocation" "$INSTDIR"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainSyntax" "UninstallString" '"$INSTDIR\plugins\RainSyntax\Uninstall RainSyntax.exe"'
        WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainSyntax" "NoModify" 1
        WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainSyntax" "NoRepair" 1
        ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP" "InstallLocation"
        ${If} $0 == $INSTDIR
            DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainCodePP"
        ${EndIf}
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
        MessageBox MB_ICONSTOP "RainSyntax uninstall failed.$\r$\n$1" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${If} $ProgramFilesOnly != ""
        Goto uninstall_done
    ${EndIf}
    SetRegView 64
    ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainSyntax" "InstallLocation"
    ${If} $0 == $INSTDIR
        DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RainSyntax"
    ${EndIf}
uninstall_done:
SectionEnd

