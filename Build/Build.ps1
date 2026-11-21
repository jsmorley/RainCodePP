param(
    [ValidateSet('All','Binaries','Test')]
    [string]$Target = 'All',
    [switch]$UpdateSnapshots
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$studio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $studio) { throw 'Install Visual Studio with the Desktop development with C++ workload.' }
$msbuild = Join-Path $studio 'MSBuild\Current\Bin\amd64\MSBuild.exe'
$devCmd = Join-Path $studio 'VC\Auxiliary\Build\vcvars64.bat'

function Check-Result($step) {
    if ($LASTEXITCODE -ne 0) { throw "$step failed ($LASTEXITCODE)." }
}

Push-Location $root
try {
    New-Item -ItemType Directory -Path 'out' -Force | Out-Null
    if ($Target -ne 'Binaries') {
        $testFlags = '/nologo /std:c++20 /EHsc /W4 /MT /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS'
        $includes = '/IThirdParty\Scintilla\include /IThirdParty\lexilla\include /IRainLexer'
        $sources = 'Test\TestRunner.cpp RainLexer\Lexer.cpp RainLexer\Syntax.cpp'
        @(
            '@echo off'
            ('call "{0}" >nul || exit /b 1' -f $devCmd)
            ('cl.exe {0} {1} {2} /Foout\ /Feout\LexerTests.exe' -f $testFlags,$includes,$sources)
        ) | Set-Content -LiteralPath out\build-tests.cmd
        & cmd.exe /d /c out\build-tests.cmd
        Check-Result 'Test build'
        $cases = @(Get-ChildItem -LiteralPath 'Test\Cases' -Filter '*.ini' | Sort-Object Name | ForEach-Object { $_.FullName })
        $arguments = if ($UpdateSnapshots) { @('--update') + $cases } else { $cases }
        & .\out\LexerTests.exe @arguments
        Check-Result 'Lexer tests'
    }
    if ($Target -ne 'Test') {
        foreach ($platform in @('Win32', 'x64')) {
            & $msbuild RainCodePP.sln /m /nologo /v:m /p:Configuration=Release /p:PreferredToolArchitecture=x64 "/p:Platform=$platform"
            Check-Result "$platform plugin build"
            & $msbuild RainLexer\RainCodePPSettings.vcxproj /m /nologo /v:m /p:Configuration=Release /p:PreferredToolArchitecture=x64 "/p:Platform=$platform"
            Check-Result "$platform settings build"
        }
    }
    if ($Target -eq 'All') {
        $nsis = Join-Path ${env:ProgramFiles(x86)} 'NSIS\Bin\makensis.exe'
        & $nsis /V3 Installer\RainCodePP.nsi
        Check-Result 'Installer build'
    }
} finally {
    Pop-Location
}

