function Get-PreviousStyleFile([string]$Directory, [string]$Config) {
    # Notepad++ checks the executable's plugin Config before the user profile.
    foreach ($path in @((Join-Path $Directory 'plugins\Config\RainCodePP.xml'),
                        (Join-Path $Config 'RainCodePP.xml'))) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
}

function Add-PreviousPreferences($Payload, [string]$Config, [string]$Profile) {
    foreach ($suffix in '-settings.ini','-vocabulary.txt') {
        $source = Join-Path $Config "RainCodePP$suffix"
        $target = Join-Path $Config "RainSyntax$suffix"
        if ((Test-Path -LiteralPath $source -PathType Leaf) -and !(Test-Path -LiteralPath $target)) {
            $payload[$target] = $source
        }
    }
    foreach ($name in 'config.xml','shortcuts.xml') {
        $target = Join-Path $Profile $name
        if (!(Test-Path -LiteralPath $target -PathType Leaf)) { continue }
        if ([IO.File]::ReadAllText($target) -notmatch 'RainCodePP\.dll') { continue }
        $xml = [System.Xml.XmlDocument]::new()
        $xml.XmlResolver = $null
        $xml.PreserveWhitespace = $true
        $xml.Load($target)
        $changed = $false
        foreach ($attribute in $xml.SelectNodes('//PluginDlg/@pluginName | //PluginCommand/@moduleName')) {
            if ($attribute.Value -eq 'RainCodePP.dll') {
                $attribute.Value = 'RainSyntax.dll'
                $changed = $true
            }
        }
        if ($changed) {
            $updated = Join-Path $PSScriptRoot "migrated-$name"
            $xml.Save($updated)
            $payload[$target] = $updated
        }
    }
}

function Move-PreviousPath([string]$Source, [string]$Destination, [string]$Root) {
    $sourcePath = (Resolve-Path -LiteralPath $Source).ProviderPath
    $rootPath = (Resolve-Path -LiteralPath $Root).ProviderPath.TrimEnd('\')
    $destinationPath = [IO.Path]::GetFullPath($Destination)
    foreach ($path in @($sourcePath,$destinationPath)) {
        if (!$path.StartsWith("$rootPath\", [StringComparison]::OrdinalIgnoreCase)) {
            throw "Cannot move previous installation files outside $rootPath."
        }
    }
    # Only rename ordinary files and directories within this installation or profile.
    foreach ($path in @($sourcePath,[IO.Path]::GetDirectoryName($destinationPath))) {
        $currentPath = $path
        while ($true) {
            if ((Get-Item -LiteralPath $currentPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Cannot move previous installation files through a redirected path at $currentPath."
            }
            if ($currentPath -eq $rootPath) { break }
            $currentPath = [IO.Path]::GetDirectoryName($currentPath)
        }
    }
    if ([IO.Directory]::Exists($sourcePath)) { [IO.Directory]::Move($sourcePath, $destinationPath) }
    else { [IO.File]::Move($sourcePath, $destinationPath) }
}

function Move-PreviousBackup([string]$Root) {
    $previous = Join-Path $Root 'RainCodePP-backup'
    if (!(Test-Path -LiteralPath $previous -PathType Container)) { return }
    $backup = Join-Path $Root 'RainSyntax-backup'
    if (!(Test-Path -LiteralPath $backup)) {
        Move-PreviousPath $previous $backup $Root
        return
    }
    $original = Join-Path $previous 'Original RainLexer'
    $current = Join-Path $backup 'Original RainLexer'
    if ((Test-Path -LiteralPath $original -PathType Container) -and !(Test-Path -LiteralPath $current)) {
        Move-PreviousPath $original $current $Root
    }
    if (Get-ChildItem -LiteralPath $previous -Force | Select-Object -First 1) {
        $destination = Join-Path $backup ('Previous backup ' + [guid]::NewGuid().ToString('N'))
        Move-PreviousPath $previous $destination $Root
    } else {
        [IO.Directory]::Delete($previous)
    }
}

function Complete-PreviousProfile([string]$Config) {
    Move-PreviousBackup $Config
    foreach ($suffix in '.xml','-palette.xml','-settings.ini','-vocabulary.txt') {
        $replacement = Join-Path $Config "RainSyntax$suffix"
        if (Test-Path -LiteralPath $replacement -PathType Leaf) {
            [IO.File]::Delete((Join-Path $Config "RainCodePP$suffix"))
        }
    }
}

function Retire-PreviousPlugin([string]$Directory, [bool]$Portable) {
    $plugins = Join-Path $Directory 'plugins'
    $paths = [ordered]@{
        'RainCodePP' = 'Plugin'
        'RainCodePP.dll' = 'Plugin.dll'
        'disabled\RainCodePP' = 'Disabled plugin'
        'disabled\RainCodePP.dll' = 'Disabled plugin.dll'
    }
    if (!$Portable) { $paths['Config\RainCodePP.xml'] = 'Previous lexer.xml' }
    $existing = @($paths.Keys | Where-Object { Test-Path -LiteralPath (Join-Path $plugins $_) })
    if (!$existing.Count) { return }
    $backup = Join-Path $plugins 'RainSyntax-backup'
    if ((Test-Path -LiteralPath $backup) -and
        ((Get-Item -LiteralPath $backup -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Cannot archive the previous plugin through a redirected folder at $backup."
    }
    [void][IO.Directory]::CreateDirectory($backup)
    $destination = Join-Path $backup 'Previous installation'
    if (Test-Path -LiteralPath $destination) {
        $destination += ' ' + [guid]::NewGuid().ToString('N')
    }
    [void][IO.Directory]::CreateDirectory($destination)
    $moved = [Collections.Generic.List[string]]::new()
    try {
        foreach ($path in $existing) {
            Move-PreviousPath (Join-Path $plugins $path) (Join-Path $destination $paths[$path]) $plugins
            $moved.Add($path)
        }
    } catch {
        for ($i = $moved.Count - 1; $i -ge 0; --$i) {
            $path = $moved[$i]
            Move-PreviousPath (Join-Path $destination $paths[$path]) (Join-Path $plugins $path) $plugins
        }
        [IO.Directory]::Delete($destination)
        throw
    }
}
