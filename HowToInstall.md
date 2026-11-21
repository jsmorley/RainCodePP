# Installing RainCode++ for Notepad++

RainCode++ adds Rainmeter 5 syntax highlighting and optional typing assistance for `.ini` and `.inc` files. The Windows installer supports 32-bit and 64-bit Notepad++ 8.4 or later on Windows 10 or 11, including portable copies.

## Install or update

1. Save your work in Notepad++. Setup can ask to close Notepad++ and RainCode++ Settings before it replaces files.
2. Download the Windows `.exe` from the [RainCode++ releases page](https://github.com/jsmorley/RainCodePP/releases/latest) and run it. The installer is unsigned, so Windows may show an unrecognized publisher prompt.
3. Select the folder containing the `notepad++.exe` you want to use. For portable Notepad++, select the portable app folder. Setup detects the architecture.
4. Pick your starting palette. On an upgrade, choose **Keep Current Style** to preserve the active RainCode++ colors and fonts. On a first install with RainLexer, the same choice imports the corresponding styles from RainLexer. If there is no current style, **Match Notepad++ mode** chooses a light or dark palette. You can also pick any named preset.
5. Open Notepad++ and load a Rainmeter skin or include. If the tab opens as INI or uses the old lexer, choose **Language → Rainmeter 5**. Open **Plugins → RainCode++ → Settings sidebar** to access typing preferences, palettes, and Help.

Automatic completion, capitalization correction, and obvious typo correction start enabled on a fresh install. Each setting can be turned off independently. Upgrades preserve those choices and your RainCode++ vocabulary edits.

## Using RainCode++ alongside RainLexer

Both plugins can stay installed. Choose **Language → Rainmeter** to use the original RainLexer on a tab, or **Language → Rainmeter 5** to use RainCode++. Notepad++ can remember an existing tab’s language between sessions, so you may need to select the new lexer on a tab you already had open.

On the first install, Setup backs up the original RainLexer plugin and its style, vocabulary, and preference files when present. Use **Open RainLexer backup** in the RainCode++ sidebar or settings app to find the backup. The program backup lives in `plugins\RainCodePP-backup\Original RainLexer` under Notepad++. A standard installation also keeps backed-up profile files under `%APPDATA%\Notepad++\plugins\config\RainCodePP-backup\Original RainLexer`. For a portable installation, the backup stays with the portable copy.

**Keep Current Style** starts RainCode++ with your existing RainLexer colors and fonts when the original palette is available. RainCode++ gives its additional syntax types their own styles. RainLexer keeps its original vocabulary. To customize the new lexer’s vocabulary, choose **Edit vocabulary and palettes** in the RainCode++ sidebar.

## Uninstall and return to RainLexer

1. In the RainCode++ sidebar or settings app, choose **Uninstall RainCode++**. A standard installation also offers **RainCode++ for Notepad++** in Windows **Settings → Apps → Installed apps**. For a portable copy, you can run `plugins\RainCodePP\Uninstall RainCode++.exe` from that copy’s app folder.
2. Confirm the uninstall. Notepad++ closes normally and can prompt you to save any unsaved work.
3. The uninstaller removes RainCode++, restores any missing RainLexer files from the backup if available, and reopens Notepad++. When the original lexer is available and the Notepad++ session file contains RainCode++ tabs, those tabs are switched to RainLexer. You can also select **Language → Rainmeter** manually.

The original RainLexer backup and your RainCode++ preferences are kept, so you can reinstall RainCode++ later with your saved settings.

For palette editing, completion shortcuts, and skin refresh commands, see the [RainCode++ manual](manual.md).
