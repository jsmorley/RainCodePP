param(
    [Parameter(Mandatory)][string]$NppDirectory,
    [string]$Palette = 'Keep',
    [string]$SetupExecutable,
    [switch]$CloseRunningApps,
    [switch]$ProgramFilesOnly,
    [switch]$CheckTheme,
    [ValidateSet('Light','Dark')][string]$ThemeMode
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ClassicBackup.ps1')
. (Join-Path $PSScriptRoot 'NotepadProcesses.ps1')
. (Join-Path $PSScriptRoot 'InstallerPrivileges.ps1')
. (Join-Path $PSScriptRoot 'PreviousInstall.ps1')

function Remove-InstallScratch([string]$Path) {
    try { [IO.File]::Delete($Path) }
    catch { Write-Verbose "The temporary file could not be removed: $Path" }
}

function Read-StyleXml([string]$Path, [switch]$PreserveWhitespace) {
    $document = [System.Xml.XmlDocument]::new()
    $document.XmlResolver = $null
    $document.PreserveWhitespace = $PreserveWhitespace.IsPresent
    $document.Load($Path)
    return ,$document
}

function Get-PaletteDarkMode([string]$Background) {
    if ($Background -notmatch '^[0-9a-f]{6}$') { return $null }
    $red = [Convert]::ToInt32($Background.Substring(0, 2), 16)
    $green = [Convert]::ToInt32($Background.Substring(2, 2), 16)
    $blue = [Convert]::ToInt32($Background.Substring(4, 2), 16)
    return (2126 * $red + 7152 * $green + 722 * $blue) -lt 1280000
}

function Get-NotepadDarkMode([string]$Profile) {
    $path = Join-Path $Profile 'config.xml'
    if (!(Test-Path -LiteralPath $path)) { return $false }
    try {
        $document = Read-StyleXml $path
        if ($document.DocumentElement.Name -ne 'NotepadPlus') { return $null }
        $mode = $document.SelectSingleNode("/NotepadPlus/GUIConfigs/GUIConfig[@name='DarkMode']")
        if (!$mode) { return $false }
        if ($mode.GetAttribute('enableWindowsMode') -eq 'yes') {
            $personalize = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
            return (Get-ItemPropertyValue -LiteralPath $personalize -Name AppsUseLightTheme -ErrorAction SilentlyContinue) -eq 0
        }
        return $mode.GetAttribute('enable') -eq 'yes'
    } catch {
        return $null
    }
}

function Add-NotepadMode($Payload, [string]$Profile, [string]$Mode) {
    $path = Join-Path $Profile 'config.xml'
    $source = if ($Payload.Contains($path)) { $Payload[$path] } else { $path }
    if (Test-Path -LiteralPath $source) {
        $document = Read-StyleXml $source -PreserveWhitespace
        if ($document.DocumentElement.Name -ne 'NotepadPlus') { throw 'Cannot update the Notepad++ settings XML.' }
    } else {
        $document = [System.Xml.XmlDocument]::new()
        $document.LoadXml('<NotepadPlus><GUIConfigs /></NotepadPlus>')
    }
    $configs = $document.SelectSingleNode('/NotepadPlus/GUIConfigs')
    if (!$configs) { $configs = $document.DocumentElement.AppendChild($document.CreateElement('GUIConfigs')) }
    $settings = $configs.SelectSingleNode("GUIConfig[@name='DarkMode']")
    if (!$settings) {
        $settings = $configs.AppendChild($document.CreateElement('GUIConfig'))
        $settings.SetAttribute('name', 'DarkMode')
    }
    $enabled = if ($Mode -eq 'Dark') { 'yes' } else { 'no' }
    $settings.SetAttribute('enable', $enabled)
    $settings.SetAttribute('enableWindowsMode', 'no')
    $staged = Join-Path $PSScriptRoot 'notepad-mode.xml'
    $document.Save($staged)
    $Payload[$path] = $staged
}

function Get-NotepadArchitecture([string]$Exe) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Exe))
    try {
        $reader.BaseStream.Position = 60
        $reader.BaseStream.Position = $reader.ReadUInt32()
        if ($reader.ReadUInt32() -ne 0x4550) { throw 'Invalid Notepad++ executable.' }
        switch ($reader.ReadUInt16()) {
            0x8664 { 'x64' }
            0x014c { 'x86' }
            default { throw 'This installer supports x64 and x86 Notepad++.' }
        }
    } finally { $reader.Dispose() }
}

function Add-ProfileFiles($Payload, [string]$ConfigRoot, [string]$Directory) {
    $config = Join-Path $configRoot 'plugins\config'
    $installedXml = Join-Path $config 'RainSyntax.xml'
    $savedPalette = Join-Path $config 'RainSyntax-palette.xml'
    $oldStyles = $null
    $selected = $Palette
    $previousPath = if (Test-Path -LiteralPath $installedXml -PathType Leaf) { $installedXml }
        else { Get-PreviousStyleFile $Directory $config }
    $previous = if ($previousPath) { Read-StyleXml $previousPath }
    $previousPalette = $savedPalette
    if (!(Test-Path -LiteralPath $previousPalette -PathType Leaf)) {
        $previousPalette = Join-Path $config 'RainCodePP-palette.xml'
    }
    $hasSavedPalette = Test-Path -LiteralPath $previousPalette -PathType Leaf
    if ($Palette -in @('Keep Current Style','Keep') -and ($previous -or $hasSavedPalette)) {
        $source = if ($hasSavedPalette) { Read-StyleXml $previousPalette } else { $previous }
        $oldStyles = $source.SelectSingleNode("/NotepadPlus/LexerStyles/LexerType[@name='Rainmeter5']")
    } elseif ($Palette -in @('Import RainLexer Style','Keep Current Style')) {
        # Match the external lexer loader's application-first lookup order.
        $classicXml = Join-Path $Directory 'plugins\Config\RainLexer.xml'
        if (!(Test-Path -LiteralPath $classicXml -PathType Leaf)) {
            $classicXml = Join-Path $config 'RainLexer.xml'
        }
        if (-not (Test-Path -LiteralPath $classicXml -PathType Leaf)) {
            throw 'RainLexer.xml could not be found in the selected Notepad++ installation or profile.'
        }
        $legacy = Read-StyleXml $classicXml
        $oldStyles = $legacy.SelectSingleNode("/NotepadPlus/LexerStyles/LexerType[@name='Rainmeter']")
        if ($null -eq $oldStyles -or $oldStyles.SelectNodes('./WordsStyle').Count -eq 0) {
            throw 'RainLexer.xml does not contain a RainLexer style palette.'
        }
    }
    $sourceDefault = if ($oldStyles) { $oldStyles.SelectSingleNode("./WordsStyle[@styleID='0']") }
    if ($Palette -in @('Keep','Keep Current Style','Import RainLexer Style','Match Notepad++ mode')) {
        $selected = 'Clear Skies'
        $background = if ($sourceDefault) { $sourceDefault.GetAttribute('bgColor') }
        $dark = Get-PaletteDarkMode $background
        if ($null -eq $dark) { $dark = Get-NotepadDarkMode $ConfigRoot }
        if ($dark) { $selected = 'Midnight Rain' }
    }
    $presets = Join-Path $PSScriptRoot 'Presets'
    $manifest = Join-Path $presets 'Palettes.txt'
    $names = @(Get-Content -LiteralPath $manifest | ForEach-Object { ($_ -split '\|', 2)[1] })
    if ($selected -cnotin $names) { throw "Unknown palette: $selected" }
    [xml]$styles = Get-Content -LiteralPath (Join-Path $presets "RainSyntax-$selected.xml") -Raw
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
                # New RainSyntax categories keep their syntax colors and use the
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

    $active = Join-Path $PSScriptRoot 'active.xml'
    # Older Notepad++ versions skip the lexer language metadata if this XML has a UTF-8 BOM.
    $writerSettings = [System.Xml.XmlWriterSettings]::new()
    $writerSettings.Encoding = [System.Text.UTF8Encoding]::new($false)
    $writerSettings.Indent = $true
    $writer = [System.Xml.XmlWriter]::Create($active, $writerSettings)
    try { $styles.Save($writer) }
    finally { $writer.Dispose() }
    Confirm-LexerXml $active
    $payload[$installedXml] = $active
    if ($hasSavedPalette) { $payload[$savedPalette] = $active }
    Add-PreviousPreferences $payload $config $ConfigRoot
}

function Add-ProgramFiles($Payload, [string]$Directory, [string]$Architecture) {
    $plugin = Join-Path $directory 'plugins\RainSyntax'
    foreach ($name in @('LICENSE','LICENSE-GPL-2.0','LICENSE-Scintilla.txt','README.md','HowToInstall.md','manual.md','Uninstall RainSyntax.exe')) {
        $payload[(Join-Path $plugin $name)] = Join-Path $PSScriptRoot $name
    }
    $presets = Join-Path $PSScriptRoot 'Presets'
    $manifest = Join-Path $presets 'Palettes.txt'
    $installedPresets = Join-Path $plugin 'Presets'
    $payload[(Join-Path $installedPresets 'Palettes.txt')] = $manifest
    foreach ($file in Get-ChildItem -LiteralPath $presets -Filter 'RainSyntax-*.xml' -File) {
        $payload[(Join-Path $installedPresets $file.Name)] = $file.FullName
    }
    $payload[(Join-Path $plugin 'RainSyntaxSettings.exe')] = Join-Path $PSScriptRoot "$architecture\RainSyntaxSettings.exe"
    $payload[(Join-Path $plugin 'RainSyntax.dll')] = Join-Path $PSScriptRoot "$architecture\RainSyntax.dll"
}

function Install-Files($Payload, [scriptblock]$Verify) {
    $suffix = [guid]::NewGuid().ToString('N')
    $applied = [Collections.Generic.List[string]]::new()
    try {
        foreach ($target in $payload.Keys) {
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
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
        & $Verify
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
}

function Confirm-LexerXml([string]$Path) {
    try {
        $xml = Read-StyleXml $Path
        if (!$xml.SelectSingleNode("/NotepadPlus/Languages/Language[@name='Rainmeter5']") -or
            !$xml.SelectSingleNode("/NotepadPlus/LexerStyles/LexerType[@name='Rainmeter5']/WordsStyle[@styleID='0']")) {
            throw 'The Rainmeter 5 language or default style is missing.'
        }
    } catch { throw "Cannot read the RainSyntax lexer XML at `"$Path`". $($_.Exception.Message)" }
}

function Install-RainSyntax {
    if ($ProgramFilesOnly -and !(Test-Administrator)) { throw 'The program files step requires administrator rights.' }
    $directory = (Resolve-Path -LiteralPath $NppDirectory).Path
    $exe = Join-Path $directory 'notepad++.exe'
    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Choose the folder containing notepad++.exe.' }
    $fileVersion = (Get-Item -LiteralPath $exe).VersionInfo
    # Older Notepad++ builds may report a string such as 8.33 for 8.3.3.
    $version = [version]::new($fileVersion.FileMajorPart, $fileVersion.FileMinorPart)
    if ($version -lt [version]'8.4') {
        throw 'RainSyntax requires Notepad++ 8.4 or later.'
    }
    $architecture = Get-NotepadArchitecture $exe
    $portable = Test-Path -LiteralPath (Join-Path $directory 'doLocalConf.xml') -PathType Leaf
    $appXml = Join-Path $directory 'plugins\Config\RainSyntax.xml'
    if (!$portable -and (Test-Path -LiteralPath $appXml)) {
        throw "Notepad++ will load `"$appXml`" before the per-user lexer XML. Move that file to a backup location and run Setup again."
    }
    $settingsApp = Join-Path $directory 'plugins\RainSyntax\RainSyntaxSettings.exe'
    $previousSettingsApp = Join-Path $directory 'plugins\RainCodePP\RainCodePPSettings.exe'
    $running = @(Get-NotepadApps $exe @($settingsApp,$previousSettingsApp))
    if ($running.Count -gt 0) {
        if (!$CloseRunningApps) {
            Write-Output 'Notepad++ or RainSyntax Settings is open for this installation.'
            exit 2
        }
        if (!(Wait-ForNotepadAppsToClose $running)) {
            Write-Output 'Notepad++ or RainSyntax Settings is still open.'
            exit 3
        }
    }

    $elevate = !$ProgramFilesOnly -and !(Test-ApplicationWritable $directory)
    $profile = if ($portable) { $directory } elseif (!$ProgramFilesOnly) { Get-NotepadProfile $directory }
    $config = if ($profile) { Join-Path $profile 'plugins\Config' }
    $lexerXml = if ($config) { Join-Path $config 'RainSyntax.xml' }
    $writeProfile = $profile -and (!$portable -or !$elevate)
    $payload = [ordered]@{}
    if ($CheckTheme) {
        $preview = if ($writeProfile) { $payload } else { [ordered]@{} }
        Add-ProfileFiles $preview $profile $directory
        $styles = Read-StyleXml $preview[$lexerXml]
        $default = $styles.SelectSingleNode("//LexerType[@name='Rainmeter5']/WordsStyle[@styleID='0']")
        $paletteDark = Get-PaletteDarkMode ($default.GetAttribute('bgColor'))
        $editorDark = Get-NotepadDarkMode $profile
        if ($null -ne $paletteDark -and $null -ne $editorDark -and $paletteDark -ne $editorDark) {
            if ($paletteDark) { exit 4 }
            exit 5
        }
    }
    if ($writeProfile) {
        if (!$CheckTheme) { Add-ProfileFiles $payload $profile $directory }
        if ($ThemeMode) { Add-NotepadMode $payload $profile $ThemeMode }
        if (!$portable -and !(Test-Path -LiteralPath (Join-Path $config 'RainCodePP-backup'))) {
            Write-ClassicBackup $profile (Get-ProfileBackup $profile)
        }
    }
    if (!$elevate) {
        $programPlugins = Join-Path $directory 'plugins'
        if (!(Test-Path -LiteralPath (Join-Path $programPlugins 'RainCodePP-backup'))) {
            Write-ClassicBackup $directory (Get-ApplicationBackup $directory)
        }
        Add-ProgramFiles $payload $directory $architecture
    }

    Install-Files $payload {
        # Verify the profile before requesting the separate administrator step.
        if ($writeProfile) { Confirm-LexerXml $lexerXml }
        if ($elevate) {
            if (!$SetupExecutable) { throw 'Administrator approval is required. Run RainSyntax Setup to continue.' }
            Write-Output 'Installing the program files with administrator approval.'
            $modeOption = if ($ThemeMode) { " /ThemeMode=$ThemeMode" } else { '' }
            Invoke-ElevatedInstaller $SetupExecutable ("/S /ProgramFilesOnly /Palette=`"$Palette`"$modeOption /D=$directory")
            if ($portable) { Confirm-LexerXml $lexerXml }
        } else {
            Move-PreviousBackup $programPlugins
            Retire-PreviousPlugin $directory $portable
        }
    }
    if ($writeProfile) { Complete-PreviousProfile $config }
    if ($lexerXml) { Write-Output "Installed RainSyntax ($architecture). Lexer XML: $lexerXml" }
}

try { Install-RainSyntax }
catch { Write-Output $_.Exception.Message; exit 1 }

