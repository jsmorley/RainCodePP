"""Write the RainCode++ palettes using stable Scintilla style IDs."""
from pathlib import Path
import xml.etree.ElementTree as ET

# Style, light foreground, dark foreground, Notepad++ font flags (1 bold, 2 italic).
STYLES = [
    ("Default", "20262D", "DCE4ED", 0),
    ("Comment", "506B58", "90A69A", 2),
    ("Section", "164472", "A2CDFF", 1),
    ("Option", "194C81", "8CBEFC", 0),
    ("Equals", "586374", "A7B5C4", 0),
    ("Invalid Value", "A6223C", "FFA1B0", 1),
    ("Value", "095E51", "8BDCC2", 0),
    ("Bang", "6C2F83", "DCAEFF", 1),
    ("Built-in Variable", "8A3A1C", "FFBC91", 1),
    ("User Variable", "8A3A1C", "FFBC91", 0),
    ("Number", "7C4619", "EDC18D", 0),
    ("Deprecated Option", "53668C", "A1AFD6", 2),
    ("Deprecated Value", "49695F", "A1C3B9", 2),
    ("Deprecated Bang", "6D526D", "C1A8C7", 2),
    ("Character Escape", "78306B", "E7ADDA", 0),
    ("List Separator", "52677E", "AFC5DF", 1),
    ("Section Variable", "174D88", "A8CCFF", 0),
    ("Dollar Variable", "814273", "EAB0DA", 0),
    ("Bang Bracket", "6C2F83", "DCAEFF", 0),
    ("Bang Argument", "3F5268", "BACDE0", 0),
    ("Quoted String", "4B6753", "A8CCAC", 0),
    ("Operator", "4C5E75", "A7B5C4", 1),
    ("Function", "71358D", "E0B6FF", 1),
    ("Selector", "0F6574", "91D9E6", 1),
    ("Environment Variable", "87601C", "EACC81", 0),
    ("Escaped Reference", "5C6673", "9EA9B7", 2),
    ("Format Specifier", "7A3A72", "E8B2DC", 0),
    ("Directive", "0D6273", "8EDBEA", 1),
    ("Reference", "305982", "A5CAF3", 0),
    ("Color", "7C4619", "EDC18D", 1),
    ("Red Channel", "A02B40", "FFA3AA", 0),
    ("Green Channel", "236837", "8BDAA0", 0),
    ("Blue Channel", "215D9A", "9BC8FF", 0),
    ("Alpha Channel", "6E4789", "D8AAF5", 0),
    ("Relative Start (r)", "9B4A23", "F9BA86", 1),
    ("Relative End (R)", "075F75", "77DCD6", 1),
    ("Regular Expression", "71358D", "E0B6FF", 0),
    ("URL", "095C94", "90D1FF", 0),
]


JSMORLEY = [
    ("000000", 0), ("008040", 2), ("6363B1", 1), ("547CC0", 1),
    ("757575", 0), ("FF0000", 0), ("547CC0", 1), ("00AAB4", 1),
    ("E65719", 1), ("E65719", 0), ("000000", 0), ("0000A0", 2),
    ("0080C0", 2), ("008080", 2), ("E65719", 0), ("757575", 0),
    ("0000FF", 0), ("0000FF", 0), ("00868D", 0), ("38475A", 0),
    ("507557", 0), ("757575", 0), ("6363B1", 1), ("416CAA", 1),
    ("AC4D1C", 0), ("757575", 2), ("9B4B81", 0), ("398196", 1),
    ("516AA1", 0), ("AC4D1C", 1),
]

# Background followed by ink, muted text, comment, blue, teal, purple, orange,
# ochre, and error accents. Each palette uses all nine accents consistently.
PALETTES = [
    ("Light", "Clear Skies", "FAFBFC", None),
    ("Light", "Morning Fog", "EEF1F4", "273240 53616E 4F685D 305474 26676C 605080 85452F 73551C A12846"),
    ("Light", "Paper Lantern", "F9F1E0", "33281F 64564D 506442 29587A 28675A 744770 8F4227 755413 A52938"),
    ("Light", "Tidal Pool", "EBF7F6", "203844 485F68 466956 1D5C83 0A665B 65467F 8D4628 73571D 9B2B4C"),
    ("Light", "Apricot Dawn", "FFF3ED", "3B2929 665254 506845 3C578D 246759 744777 8E4123 765118 A62B45"),
    ("Light", "Meadow Glass", "F1F5E9", "283728 53634F 4A6B45 285B7A 1C655F 644686 88462C 72591B A02C43"),
    ("Dark", "Midnight Rain", "20252D", None),
    ("Dark", "Aurora Watch", "181F31", "E3EAFE A7B5CE 9AB8A9 9ABCFF 82E3CE D6ABFF F6B69B F4D293 FFA3BD"),
    ("Dark", "Cinder and Copper", "282124", "F1E2DE BAA8AD A3BA9D B2C9FA 95D8BD DDBBEE FFAD84 EED09B F9A4AA"),
    ("Dark", "Deep Current", "142830", "DAEEF3 9EBFC9 88B9A7 8BCFF5 8CDFD4 CDB5EF F9B69B F1D291 FBA2BC"),
    ("Dark", "Violet Hour", "262035", "EDE4FA B9ABCE A4BCAC ABAFFF 96DFD1 E9B4FC F7AEAB F4D29F FFA2C0"),
    ("Dark", "Forest After Rain", "1D2A25", "E2ECE0 A6B8A8 91B799 A7C7FC 8EE1BE D9B8F3 F0B199 E7D395 F9A8B7"),
    ("Example", "JSMorley", "FFFFFF", None),
]
ROLES = [0, 2, 3, 3, 1, 8, 4, 5, 6, 6, 7, 1, 1, 5, 5, 1,
         3, 5, 5, 1, 2, 1, 5, 4, 7, 1, 5, 4, 3, 7]


def luminance(color):
    rgb = [int(color[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    linear = [c / 12.92 if c < 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in rgb]
    return sum(c * weight for c, weight in zip(linear, (0.2126, 0.7152, 0.0722)))


def main():
    root = Path(__file__).resolve().parents[1]
    config = root / "Config/Presets"
    config.mkdir(exist_ok=True)
    for family, theme, background, accents in PALETTES:
        document = ET.Element("NotepadPlus")
        languages = ET.SubElement(document, "Languages")
        ET.SubElement(languages, "Language", name="Rainmeter5", ext="ini inc",
                      commentLine=";", commentStart="", commentEnd="")
        styles = ET.SubElement(document, "LexerStyles")
        lexer = ET.SubElement(styles, "LexerType", name="Rainmeter5", desc="Rainmeter 5",
                              ext="ini inc", excluded="no")
        for id_, (name, light, dark, flags) in enumerate(STYLES):
            color = dark if family == "Dark" else light
            face = size = ""
            if accents and id_ < len(ROLES):
                color = accents.split()[ROLES[id_]]
            if theme == "JSMorley":
                if id_ < len(JSMORLEY):
                    color, flags = JSMORLEY[id_]
                face = "DejaVu Sans Mono"
                size = "14"
            if theme != "JSMorley" or id_ >= len(JSMORLEY):
                high, low = sorted((luminance(color), luminance(background)), reverse=True)
                if (high + 0.05) / (low + 0.05) < 4.5:
                    raise ValueError(f"{theme} has low contrast for {name}: #{color} on #{background}")
            # Leave room for Scintilla's predefined editor styles.
            style_id = id_ if id_ < len(ROLES) else id_ - len(ROLES) + 40
            ET.SubElement(lexer, "WordsStyle", name=name, styleID=str(style_id), fgColor=color,
                          bgColor=background, fontName=face, fontStyle=str(flags), fontSize=size)
        ET.indent(document, space="    ")
        ET.ElementTree(document).write(config / f"RainCodePP-{theme}.xml",
                                       encoding="utf-8", xml_declaration=True)
    manifest = "\n".join(f"{family}|{name}" for family, name, _, _ in PALETTES)
    (config / "Palettes.txt").write_text(manifest + "\n", encoding="ascii")
    options = "\n".join('    ${NSD_CB_AddString} $PaletteControl "' + name + '"'
                        for _, name, _, _ in PALETTES)
    (root / "Installer/PaletteOptions.nsh").write_text(
        "; Generated by Build/WriteThemes.py\n!macro AddPalettes\n" + options + "\n!macroend\n",
        encoding="ascii")


if __name__ == "__main__":
    main()

