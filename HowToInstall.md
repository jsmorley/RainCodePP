# Installing RainSyntax for Notepad++

RainSyntax adds Rainmeter 5 syntax highlighting and optional typing assistance for `.ini` and `.inc` files. The Windows installer supports 32-bit and 64-bit Notepad++ 8.4 or later on Windows 10 or 11, including portable copies.

## Install or update

1. Save your work in Notepad++. Setup can ask to close Notepad++ and RainSyntax Settings before it replaces files.
2. Download the Windows `.exe` from the [RainSyntax releases page](https://github.com/jsmorley/RainSyntax/releases/latest) and run it. The installer is unsigned, so Windows may show an unrecognized publisher prompt.
3. Select the folder containing the `notepad++.exe` you want to use. For portable Notepad++, select the portable app folder. Setup detects the architecture and shows where it will put the lexer XML. For a standard installation, the XML goes into the Windows profile that launched Setup, even if Windows asks for another administrator account to update the program files.
4. Pick your starting palette. On an upgrade, choose **Keep Current Style** to preserve the active RainSyntax colors and fonts. On a first install with RainLexer, the same choice imports the corresponding styles from RainLexer. If there is no current style, **Match Notepad++ mode** chooses a light or dark palette. You can also pick any named preset.
5. Open Notepad++ and load a Rainmeter skin or include. If the tab opens as INI or uses the old lexer, choose **Language → Rainmeter 5**. Open **Plugins → RainSyntax → Settings sidebar** to access typing preferences, palettes, and Help.

Automatic completion, capitalization correction, and obvious typo correction start enabled on a fresh install. Each setting can be turned off independently. Upgrades preserve those choices and your RainSyntax vocabulary edits.

RainSyntax replaces RainCode++. Install it over the same Notepad++ copy to carry forward your palette, vocabulary, typing preferences, and plugin shortcuts. Setup removes the old plugin from Notepad++'s active plugin folders. Existing tabs still use the **Rainmeter 5** language.

## Using RainSyntax alongside RainLexer

Both plugins can stay installed. Choose **Language → Rainmeter** to use the original RainLexer on a tab, or **Language → Rainmeter 5** to use RainSyntax. Notepad++ can remember an existing tab’s language between sessions, so you may need to select the new lexer on a tab you already had open.

On the first install, Setup backs up the original RainLexer plugin and its style, vocabulary, and preference files when present. Use **Open RainLexer backup** in the RainSyntax sidebar or settings app to find the backup. The program backup lives in `plugins\RainSyntax-backup\Original RainLexer` under Notepad++. A standard installation also keeps backed-up profile files under `%APPDATA%\Notepad++\plugins\config\RainSyntax-backup\Original RainLexer`. For a portable installation, the backup stays with the portable copy.

**Keep Current Style** starts RainSyntax with your existing RainLexer colors and fonts when the original palette is available. RainSyntax gives its additional syntax types their own styles. RainLexer keeps its original vocabulary. To customize the new lexer’s vocabulary, choose **Edit vocabulary and palettes** in the RainSyntax sidebar.

## Uninstall and return to RainLexer

1. In the RainSyntax sidebar or settings app, choose **Uninstall RainSyntax**. A standard installation also offers **RainSyntax for Notepad++** in Windows **Settings → Apps → Installed apps**. For a portable copy, you can run `plugins\RainSyntax\Uninstall RainSyntax.exe` from that copy’s app folder.
2. Confirm the uninstall. Notepad++ closes normally and can prompt you to save any unsaved work.
3. The uninstaller removes RainSyntax, restores any missing RainLexer files from the backup if available, and reopens Notepad++. When the original lexer is available and the Notepad++ session file contains RainSyntax tabs, those tabs are switched to RainLexer. You can also select **Language → Rainmeter** manually.

The original RainLexer backup and your RainSyntax preferences are kept, so you can reinstall RainSyntax later with your saved settings.

For palette editing, completion shortcuts, and skin refresh commands, see the [RainSyntax manual](manual.md).
