# RainSyntax manual

RainSyntax highlights Rainmeter 5 skin and include files in Notepad++. It works on `.ini` and `.inc` files. For a tab that already has another language assigned, choose **Language → Rainmeter 5**.

## 1. Open the sidebar

Choose **Plugins → RainSyntax → Settings sidebar**. Drag the panel by its title to dock it elsewhere. **Help** opens this manual in its own panel.

## 2. Type Rainmeter code

- **Automatic completion** offers choices as you type. Try `Font` for `FontColor`, `FontFace`, `FontSize`, and `FontWeight`. Narrow the list by typing more. Use the arrow keys and Tab to choose, or Esc to keep your text.
- **Ctrl+Alt+Space** opens completion on demand, even if automatic completion is off.
- **Correct capitalization** uses the Rainmeter 5 spelling. After the first correction of an occurrence, you can override it while the document stays open.
- **Correct obvious typos** corrects a completed option, bang, or fixed value when one supported spelling differs by a single edit. Use Undo to reverse an edit.

All three typing aids start enabled. Use the sidebar to turn them off individually. Updating RainSyntax preserves your choices.

## 3. Read complex expressions

Section names, option names, formula functions, variables, bang arguments, selectors, and several kinds of literals each have their own style. Nested syntax is highlighted inside actions and quoted arguments.

The red, green, blue, and alpha channels have separate styles in recognized color values such as `FontColor=255,128,32,200` and `FontColor=FF8020C8`. This also works for variables whose names start or end with `Color` or `Tint`, such as `colorBar` and `LabelColor`. Choose a channel under **Set style for syntax types** to customize it.

Relative `X` and `Y` suffixes have separate styles. Lowercase `r` measures from the previous meter’s left or top edge. Uppercase `R` measures from its right or bottom edge. The styles also apply to expressions such as `X=(#Gap# + 2)R` and to position values inside `!SetOption`.

The **Regular Expression** style colors the whole pattern in `RegExp`, `IfMatch`, `RegExpFilter`, `InputRegExp`, and `InlinePattern`. This includes numbered options such as `IfMatch2` and pattern values assigned inside `!SetOption`. When a section sets `RegExpSubstitute=1`, the same style applies to the pattern side of its `Substitute` pairs.

The **URL** style highlights link text and keeps variable colors inside it. For example, in `URL=https://example.com/[&City:EncodeURL]?units=#Units#`, the section reference and user variable retain their own styles. Notepad++ controls double-clicking literal URLs under **Settings → Preferences → Cloud & Link**.

```ini
[MeterCaption]
Meter=String
FontColor=4681AD
Text=[#Title] [&Clock:TimeStamp]
W=(max(180, [$SkinW]) / 2)
LeftMouseUpAction=[!SetOption MeterCaption Text "[#Title]"][!Redraw]
```

Rainmeter resolves actual skin values, variable nesting, and Lua or plugin commands when it runs. Highlighting follows the source text.

## 4. Change how RainSyntax looks

Open **Edit vocabulary and palettes** from the sidebar. Choose a preset to preview it on a sample skin. There are six light palettes, six dark palettes, and **JSMorley**, based on Jeffrey Morley's RainLexer colors.

- **Use preset** copies the preview into your editable palette.
- In **Set style for syntax types**, hover over a choice for a brief description. Select a type to change its foreground, background, font, size, bold, italic, or underline. **Inherit** returns its font face and size to the editor defaults. The **Default** background fills the editor page. Individual syntax categories can use their own backgrounds.
- Click **Foreground** or **Background** to paste a hex color such as `#4681AD` or `4681AD`. The swatch previews the color before you click **OK**. **Browse colors** opens the visual picker.
- **Save palette as** exports your palette to XML. **Load palette** opens a saved palette for editing.
- **Apply** saves the palette and updates Rainmeter tabs immediately. If you check **Also switch Notepad++ to dark mode** or its light equivalent, the button becomes **Apply and restart**. This selects a manual mode instead of Follow Windows. Save any open files if Notepad++ prompts you before reopening your tabs.

The **Notepad++ Style Configurator** link at the bottom of the sidebar opens the built-in editor for global styles, other languages, and file extensions.

## 5. Add or edit Rainmeter terms

In **Edit vocabulary and palettes**, choose a vocabulary category. Edit one term per line. In a fixed-value category use `Option=Value`. Enter the base name, such as `IfCondition`, in the numbered-options category. For bangs, leave out the leading `!`.

Use **Find next** to locate a term. **Restore category** reloads its built-in list. **Save vocabulary** keeps the additions, removals, and spelling changes. They take effect the next time Notepad++ opens.

## 6. Send commands to Rainmeter

- **Refresh skin** refreshes the active skin associated with the current INI file. Save your edits first.
- **Refresh all** refreshes all loaded skins.
- **Restart Rainmeter** restarts the running Rainmeter instance.

## 7. Return to the original RainLexer

Setup backs up existing RainLexer plugin, vocabulary, and style files. **Open RainLexer backup** opens the backup location. RainSyntax and the original RainLexer can coexist.

Choose **Uninstall RainSyntax** from the sidebar or settings app. Notepad++ closes normally and can prompt you to save. The uninstaller removes RainSyntax, restores missing RainLexer files from the backup, and reopens Notepad++. The RainLexer backup and your RainSyntax preferences are kept.

