# RainSyntax for Notepad++

RainSyntax is a native Windows lexer for Rainmeter skin and include files. It is based on the Rainmeter Team's RainLexer and uses the Rainmeter source and manual for its vocabulary.

## Install or update

See [HowToInstall.md](HowToInstall.md) for the full setup, upgrade, RainLexer migration, and uninstall instructions.

1. Save any work in Notepad++. Setup can close it normally if it is still open.
2. Download the Windows installer from the [latest GitHub release](https://github.com/jsmorley/RainSyntax/releases/latest). Run it and choose the folder containing `notepad++.exe`.
3. Choose **Keep Current Style** to import your RainLexer colors and fonts on a first installation or preserve your existing RainSyntax palette on an upgrade. If there is no current style, **Match Notepad++ mode** picks a light or dark default. You can also choose any included preset. Typing preferences and vocabulary changes survive updates.
4. Open an INI or INC file. If the tab is already assigned to INI or another language, select the Rainmeter entry installed by RainSyntax in the **Language** menu.

The installer supports x64 and x86 copies of Notepad++. Use a current release of Notepad++ on a supported Windows system. Setup puts the DLL and the settings application in `plugins\RainSyntax`. For portable Notepad++, with `doLocalConf.xml`, it installs `RainSyntax.xml` in the application's own `plugins\config`. For a standard installation, Setup uses `%APPDATA%\Notepad++\plugins\config` for the Windows user who launches it. If the older RainLexer is also installed, choose the newer Rainmeter entry in the **Language** menu to use RainSyntax.

## Return to RainLexer

Setup keeps the original RainLexer installed and creates a first-install backup of its DLL and associated vocabulary, style, and preference files. Use **Open RainLexer backup** in the sidebar or settings app. The application snapshot is in the Notepad++ installation's `plugins\RainSyntax-backup` folder. A standard installation also keeps a snapshot of matching profile files under `%APPDATA%\Notepad++\plugins\config\RainSyntax-backup`.

Choose **Uninstall RainSyntax** in the sidebar or settings app. After confirmation, the uninstaller closes Notepad++ normally so you can save, restores any missing original RainLexer files from the backup, removes RainSyntax, and reopens Notepad++. Tabs using RainSyntax return to RainLexer if it is installed. The backup and RainSyntax preferences remain available. Standard installations also expose the uninstaller in Windows Installed apps.

## Edit a skin

With Rainmeter running, the sidebar can **Refresh skin** for the active INI, **Refresh all**, or **Restart Rainmeter**. Refresh reads the files on disk, so save edits first.

The lexer recognizes options and accepted values, deprecated terms, section headers, directives, numeric and color literals, formulas, format specifiers, section selectors, variables, Shape modifiers and path commands, and brackets and arguments inside chained or nested bangs. Setter bangs use the target option's value rules. Built-in and user variables have separate styles, as do section, mouse, display, skin, environment, escaped and character references. Section headers can be folded.

Select **Plugins → RainSyntax → Settings sidebar** for the controls. **Ctrl+Alt+Space** requests suggestions for recognized contexts. Live suggestions, capitalization correction, and obvious typo correction can each be enabled separately. All three start enabled on a fresh installation. Any option you turn off stays off during upgrades. Type a prefix such as `Font` to see alternatives, use the arrow keys to highlight one, and press Tab to insert it. Capitalization is corrected once per occurrence while the document remains open. Typo correction requires a unique one-edit match in an option name, bang, or fixed-value position. Section names and user-defined variables stay as typed. Use the shortcut mapper to change the completion shortcut.

The lexer works on source text. Rainmeter resolves references to included files, custom plugin commands, Lua functions, and values constructed at runtime. Highlighting such expressions does not require running the skin or opening its includes. The vocabulary editor can add terms for a particular setup.

## Edit vocabulary and palettes

The sidebar opens **RainSyntax Settings**, a Windows application installed beside the plugin. Select a vocabulary category, edit one term per line, and save. Use **Option=Value** in the fixed-value categories. Use **Options with numbered variants** for options such as `IfCondition2`, entering the base name `IfCondition`. Changes apply after restarting Notepad++. The app stores the difference from the built-in vocabulary so unchanged terms can receive future updates.

Choose a syntax category to preview its color and font. The color buttons open a color picker. **Set palette background** gives the entire Rainmeter tab one background and clears individual background overrides. **Set font** sets the face, size, bold, italic, and underline. Browse seven light presets, including Ivory, and six dark presets. The preset picker shows a live syntax preview, including nested bangs, variables, and formulas. Choose **Use preset** to edit or apply the selection. You can export a palette XML or load a saved palette. Choose **Apply** to save the palette and update Rainmeter tabs. If the palette and Notepad++ use different light or dark modes, RainSyntax offers to switch modes and restart. Switching selects a manual mode and supersedes **Follow Windows**. The built-in **Style Configurator** handles global styles, other languages, and file extensions.

## Build from source

Run `Build\Build.ps1` from PowerShell with the Visual Studio C++ toolset selected by the solution, a Windows SDK, and NSIS installed. Use `-Target Test` for the lexer tests or `-Target Binaries` for both DLLs and settings executables. `-Target All` runs the tests and produces the installer in `out`. Python is used only to regenerate the shipped vocabulary and theme XML.

The upstream RainLexer word lists are in `Config\Languages.xml`. To update the compiled vocabulary, run `python Build\UpdateVocabulary.py <Rainmeter-source> --manual <manual-source>`. The checked-in vocabulary was generated against Rainmeter source commit `96ec0b34`.

Based on [RainLexer](https://github.com/rainmeter/rainlexer) by the Rainmeter Team. The source headers identify the applicable GNU GPL terms. See the repository's GNU GPL license files and the [Scintilla and Lexilla license](ThirdParty/LICENSE-Scintilla.txt). The vendored Notepad++ headers retain their own notices.

