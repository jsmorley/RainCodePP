param([Parameter(Mandatory)][string]$NppDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ClassicBackup.ps1')
. (Join-Path $PSScriptRoot 'NotepadProcesses.ps1')

function Return-RainCodeTabsToRainLexer([string]$Directory, [string]$Profile) {
    $legacy = @($Directory,$Profile) | Where-Object {
        (Test-Path -LiteralPath (Join-Path $_ 'plugins\RainLexer\RainLexer.dll')) -or
        (Test-Path -LiteralPath (Join-Path $_ 'plugins\RainLexer.dll'))
    }
    $session = Join-Path $Profile 'session.xml'
    if (-not $legacy -or -not (Test-Path -LiteralPath $session -PathType Leaf)) { return }

    $xml = [System.Xml.XmlDocument]::new()
    $xml.PreserveWhitespace = $true
    $xml.XmlResolver = $null
    $xml.Load($session)
    $files = $xml.SelectNodes("/NotepadPlus/Session/*/File[@lang='Rainmeter5' or @lang='Rainmeter 5' or @lang='Rainmeter 5 skin file']")
    if ($files.Count -eq 0) { return }
    foreach ($file in $files) { $file.SetAttribute('lang', 'Rainmeter') }
    $temp = "$session.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        $xml.Save($temp)
        [IO.File]::Replace($temp, $session, [NullString]::Value)
    } finally { [IO.File]::Delete($temp) }
}

function Restart-Notepad([string]$Exe, [string]$Directory) {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Start-Process -FilePath $Exe -ArgumentList '-multiInst' -WorkingDirectory $Directory
        return
    }
    # Ask the desktop shell to open Notepad++ as the interactive user.
    $shell = New-Object -ComObject Shell.Application
    $location = [object]0
    $root = [object]0
    $handle = 0
    $desktop = $shell.Windows().FindWindowSW([ref]$location, [ref]$root, 8, [ref]$handle, 1)
    if ($null -eq $desktop) { throw 'The Windows desktop shell is unavailable.' }
    $desktop.Document.Application.ShellExecute($Exe, '-multiInst', $Directory, 'open', 1)
}

try {
    $directory = (Resolve-Path -LiteralPath $NppDirectory).Path
    $exe = Join-Path $directory 'notepad++.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Notepad++ was not found in the installation folder.' }
    $plugin = Join-Path $directory 'plugins\RainCodePP'
    $settingsApp = Join-Path $plugin 'RainCodePPSettings.exe'
    if (-not (Wait-ForNotepadAppsToClose @(Get-NotepadApps $exe $settingsApp))) {
        throw 'Notepad++ or RainCode++ Settings is still open. Save or close your work, then run the uninstaller again.'
    }
    $profile = Get-NotepadProfile $directory
    Restore-MissingClassicRainLexer $directory $profile
    try { Return-RainCodeTabsToRainLexer $directory $profile }
    catch { Write-Output "Could not restore the RainLexer language on open tabs: $($_.Exception.Message)" }

    foreach ($name in @('RainCodePP.dll','RainCodePPSettings.exe','README.md','HowToInstall.md','manual.md','LICENSE','LICENSE-GPL-2.0','LICENSE-Scintilla.txt')) {
        $file = Join-Path $plugin $name
        if (Test-Path -LiteralPath $file -PathType Leaf) { [IO.File]::Delete($file) }
    }
    if (Test-Path -LiteralPath $plugin -PathType Container) {
        Get-ChildItem -LiteralPath $plugin -File -Force | Where-Object {
            $_.Name -match '^(RainCodePP\.dll|RainCodePPSettings\.exe|Uninstall RainCode\+\+\.exe|README\.md|HowToInstall\.md|manual\.md|LICENSE|LICENSE-GPL-2\.0|LICENSE-Scintilla\.txt)\.[0-9a-f]{32}\.(bak|new)$'
        } | ForEach-Object {
            try { [IO.File]::Delete($_.FullName) }
            catch { Write-Verbose "The setup scratch file is still in use: $($_.FullName)" }
        }
    }
    $presets = Join-Path $plugin 'Presets'
    $manifest = Join-Path $presets 'Palettes.txt'
    if (Test-Path -LiteralPath $manifest) {
        foreach ($row in Get-Content -LiteralPath $manifest) {
            $name = ($row -split '\|', 2)[1]
            if ($name -match '^[A-Za-z0-9][A-Za-z0-9 ]*$') {
                [IO.File]::Delete((Join-Path $presets "RainCodePP-$name.xml"))
            }
        }
        [IO.File]::Delete($manifest)
    }
    $uninstaller = Join-Path $plugin 'Uninstall RainCode++.exe'
    if (Test-Path -LiteralPath $uninstaller -PathType Leaf) { [IO.File]::Delete($uninstaller) }
    foreach ($folder in @($presets,$plugin)) {
        if ((Test-Path -LiteralPath $folder) -and -not (Get-ChildItem -LiteralPath $folder -Force | Select-Object -First 1)) {
            [IO.Directory]::Delete($folder)
        }
    }
    Write-Output 'Removed RainCode++. Saved preferences and the RainLexer backup were kept.'
    try { Restart-Notepad $exe $directory }
    catch { Write-Output "Could not reopen Notepad++ automatically: $($_.Exception.Message)" }
} catch {
    Write-Output $_.Exception.Message
    exit 1
}

