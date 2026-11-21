param(
    [Parameter(Mandatory)][string]$NppDirectory,
    [string]$Palette = 'Keep',
    [switch]$CloseRunningApps
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ClassicBackup.ps1')
. (Join-Path $PSScriptRoot 'NotepadProcesses.ps1')

function Remove-InstallScratch([string]$Path) {
    try { [IO.File]::Delete($Path) }
    catch { Write-Verbose "The temporary file could not be removed: $Path" }
}

function Read-StyleXml([string]$Path) {
    $document = [System.Xml.XmlDocument]::new()
    $document.XmlResolver = $null
    $document.Load($Path)
    return ,$document
}

function Install-RainLexer {
    $directory = (Resolve-Path -LiteralPath $NppDirectory).Path
    $exe = Join-Path $directory 'notepad++.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Choose the folder containing notepad++.exe.' }
    if ([version](Get-Item -LiteralPath $exe).VersionInfo.FileVersion -lt [version]'8.4') {
        throw 'RainCode++ requires Notepad++ 8.4 or later.'
    }
    $settingsApp = Join-Path $directory 'plugins\RainCodePP\RainCodePPSettings.exe'
    $running = @(Get-NotepadApps $exe $settingsApp)
    if ($running.Count -gt 0) {
        if (-not $CloseRunningApps) {
            Write-Output 'Notepad++ or RainCode++ Settings is open for this installation.'
            exit 2
        }
        if (-not (Wait-ForNotepadAppsToClose $running)) {
            Write-Output 'Notepad++ or RainCode++ Settings is still open.'
            exit 3
        }
    }
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($exe))
    try {
        $reader.BaseStream.Position = 60
        $reader.BaseStream.Position = $reader.ReadUInt32()
        if ($reader.ReadUInt32() -ne 0x4550) { throw 'Invalid Notepad++ executable.' }
        $architecture = switch ($reader.ReadUInt16()) {
            0x8664 { 'x64' }
            0x014c { 'x86' }
            default { throw 'This installer supports x64 and x86 Notepad++.' }
        }
    } finally { $reader.Dispose() }

    $configRoot = Get-NotepadProfile $directory
    $config = Join-Path $configRoot 'plugins\config'
    $plugin = Join-Path $directory 'plugins\RainCodePP'
    $installedXml = Join-Path $config 'RainCodePP.xml'
    $savedPalette = Join-Path $config 'RainCodePP-palette.xml'
    $classicXml = Join-Path $config 'RainLexer.xml'
    $oldStyles = $null
    $selected = $Palette
    $previous = if (Test-Path -LiteralPath $installedXml -PathType Leaf) { Read-StyleXml $installedXml }
    $hasSavedPalette = Test-Path -LiteralPath $savedPalette -PathType Leaf
    if ($Palette -in @('Keep Current Style','Keep') -and ($previous -or $hasSavedPalette)) {
        $source = if ($hasSavedPalette) { Read-StyleXml $savedPalette } else { $previous }
        $oldStyles = $source.SelectSingleNode("/NotepadPlus/LexerStyles/LexerType[@name='Rainmeter5']")
    } elseif ($Palette -eq 'Keep Current Style') {
        if (-not (Test-Path -LiteralPath $classicXml -PathType Leaf)) {
            throw 'RainLexer.xml could not be found in the selected Notepad++ profile.'
        }
        $legacy = Read-StyleXml $classicXml
        $oldStyles = $legacy.SelectSingleNode("/NotepadPlus/LexerStyles/LexerType[@name='Rainmeter']")
        if ($null -eq $oldStyles -or $oldStyles.SelectNodes('./WordsStyle').Count -eq 0) {
            throw 'RainLexer.xml does not contain a RainLexer style palette.'
        }
    }
    $sourceDefault = if ($oldStyles) { $oldStyles.SelectSingleNode("./WordsStyle[@styleID='0']") }
    if ($Palette -in @('Keep','Keep Current Style','Match Notepad++ mode')) {
        $selected = 'Clear Skies'
        $background = if ($sourceDefault) { $sourceDefault.GetAttribute('bgColor') }
        if ($background -match '^[0-9a-f]{6}$') {
            $red = [Convert]::ToInt32($background.Substring(0, 2), 16)
            $green = [Convert]::ToInt32($background.Substring(2, 2), 16)
            $blue = [Convert]::ToInt32($background.Substring(4, 2), 16)
            if ((2126 * $red + 7152 * $green + 722 * $blue) -lt 1280000) { $selected = 'Midnight Rain' }
        } elseif (Test-Path -LiteralPath (Join-Path $configRoot 'config.xml')) {
            [xml]$nppConfig = Get-Content -LiteralPath (Join-Path $configRoot 'config.xml') -Raw
            $dark = $nppConfig.SelectSingleNode("//GUIConfig[@name='DarkMode']")
            if ($null -ne $dark -and $dark.GetAttribute('enable') -eq 'yes') { $selected = 'Midnight Rain' }
        }
    }
    $presets = Join-Path $PSScriptRoot 'Presets'
    $manifest = Join-Path $presets 'Palettes.txt'
    $names = @(Get-Content -LiteralPath $manifest | ForEach-Object { ($_ -split '\|', 2)[1] })
    if ($selected -cnotin $names) { throw "Unknown palette: $selected" }
    [xml]$styles = Get-Content -LiteralPath (Join-Path $presets "RainCodePP-$selected.xml") -Raw
    if ($previous) {
        foreach ($path in @('/NotepadPlus/Languages/Language', '/NotepadPlus/LexerStyles/LexerType')) {
            $oldLanguage = $previous.SelectSingleNode($path)
            if ($null -ne $oldLanguage -and $oldLanguage.HasAttribute('ext')) {
                $styles.SelectSingleNode($path).SetAttribute('ext', $oldLanguage.GetAttribute('ext'))
            }
        }
    }
    if ($oldStyles) {
        foreach ($style in $styles.SelectNodes('//WordsStyle')) {
            $old = $oldStyles.SelectSingleNode("./WordsStyle[@styleID='$($style.styleID)']")
            $attributes = @('fgColor','bgColor','fontName','fontSize','fontStyle','colorStyle')
            if ($null -eq $old) {
                # New RainCode++ categories keep their syntax colors and use the
                # imported palette's page background and type settings.
                $old = $sourceDefault
                $attributes = @('bgColor','fontName','fontSize')
            }
            if ($old) {
                foreach ($attribute in $attributes) {
                    if ($old.HasAttribute($attribute)) { $style.SetAttribute($attribute, $old.GetAttribute($attribute)) }
                }
            }
        }
    }

    Backup-ClassicRainLexer $directory $configRoot
    New-Item -ItemType Directory -Path $config, $plugin -Force | Out-Null
    $styles.Save((Join-Path $PSScriptRoot 'active.xml'))
    $payload = [ordered]@{}
    foreach ($name in @('LICENSE','LICENSE-GPL-2.0','LICENSE-Scintilla.txt','README.md','HowToInstall.md','manual.md','Uninstall RainCode++.exe')) {
        $payload[(Join-Path $plugin $name)] = Join-Path $PSScriptRoot $name
    }
    $installedPresets = Join-Path $plugin 'Presets'
    New-Item -ItemType Directory -Path $installedPresets -Force | Out-Null
    $payload[(Join-Path $installedPresets 'Palettes.txt')] = $manifest
    foreach ($name in $names) {
        $file = "RainCodePP-$name.xml"
        $payload[(Join-Path $installedPresets $file)] = Join-Path $presets $file
    }
    $payload[(Join-Path $plugin 'RainCodePPSettings.exe')] = Join-Path $PSScriptRoot "$architecture\RainCodePPSettings.exe"
    $payload[$installedXml] = Join-Path $PSScriptRoot 'active.xml'
    if ($hasSavedPalette) { $payload[$savedPalette] = Join-Path $PSScriptRoot 'active.xml' }
    $payload[(Join-Path $plugin 'RainCodePP.dll')] = Join-Path $PSScriptRoot "$architecture\RainCodePP.dll"

    $suffix = [guid]::NewGuid().ToString('N')
    $applied = [Collections.Generic.List[string]]::new()
    try {
        foreach ($target in $payload.Keys) {
            [IO.File]::Copy($payload[$target], "$target.$suffix.new")
        }
        foreach ($target in $payload.Keys) {
            if ([IO.File]::Exists($target)) {
                [IO.File]::Replace("$target.$suffix.new", $target, "$target.$suffix.bak")
            } else {
                [IO.File]::Move("$target.$suffix.new", $target)
            }
            $applied.Add($target)
        }
    } catch {
        for ($i = $applied.Count - 1; $i -ge 0; --$i) {
            $target = $applied[$i]
            if ([IO.File]::Exists("$target.$suffix.bak")) {
                [IO.File]::Replace("$target.$suffix.bak", $target, [NullString]::Value)
            } else {
                [IO.File]::Delete($target)
            }
        }
        throw
    } finally {
        foreach ($target in $payload.Keys) {
            Remove-InstallScratch "$target.$suffix.new"
        }
    }
    foreach ($target in $payload.Keys) { Remove-InstallScratch "$target.$suffix.bak" }
    Write-Output "Installed RainCode++ ($architecture)."
}

try { Install-RainLexer }
catch { Write-Output $_.Exception.Message; exit 1 }

