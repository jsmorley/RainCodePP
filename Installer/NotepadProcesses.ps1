function Get-NotepadApps([string]$Exe, [string[]]$SettingsApps) {
    $paths = @($Exe) + $SettingsApps
    Get-Process 'notepad++','RainSyntaxSettings','RainCodePPSettings' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -in $paths }
}

function Wait-ForNotepadAppsToClose($Apps) {
    # Use each application's normal close flow, including its save prompts.
    foreach ($app in $Apps) { [void]$app.CloseMainWindow() }
    $deadline = [datetime]::UtcNow.AddSeconds(45)
    foreach ($app in $Apps) {
        $remaining = [Math]::Max(0, [int]($deadline - [datetime]::UtcNow).TotalMilliseconds)
        if (-not $app.WaitForExit($remaining)) { return $false }
    }
    return $true
}
