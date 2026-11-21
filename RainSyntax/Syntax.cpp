// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).
#include "Syntax.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <unordered_map>
#include <utility>

namespace RainSyntax {
namespace {
using View = std::string_view;
using Terms = std::unordered_map<std::string, std::string>;

#include "Vocabulary.inc"

constexpr View Functions = "atan2 atan cos sin tan abs exp ln log sqrt frac trunc floor ceil round asin acos rad deg sgn neg min max clamp e pi";
constexpr View Selectors = "X Y W H XW YH MaxValue MinValue EscapeRegExp EncodeUrl TimeStamp";
constexpr View Shapes = "Arc Ellipse Combine Curve Line Path Path1 Rectangle";
constexpr View ShapeModifiers = "Fill Stroke StrokeWidth StrokeStartCap StrokeEndCap StrokeDashCap StrokeLineJoin StrokeDashes StrokeDashOffset Extend Offset Rotate Scale Skew TransformOrder Union Xor Intersect Exclude";
constexpr View ShapeValues = "Color LinearGradient LinearGradient1 RadialGradient RadialGradient1 None Flat Square Round Triangle Bevel Miter MiterOrBevel";
constexpr View PathCommands = "LineTo ArcTo CurveTo SetNoStroke SetRoundJoin ClosePath";
constexpr View InlineSettings = "Case Color CharacterSpacing Face GradientColor Italic None Oblique Shadow Size Stretch Strikethrough Typography Underline Weight";
constexpr View StringEffectValues = "None Border Shadow";
constexpr View WebParserFlags = "ForceReload Resync NoCookies Hyperlink TempFile NoAuth NoCacheWrite PragmaNoCache Secure IgnoreCertName IgnoreCertDate IgnoreHTTPRedirect IgnoreHTTPSRedirect";
constexpr View DollarTerms = "Input MouseX MouseY SkinX SkinY SkinW SkinH SkinPhysicalX SkinPhysicalY SkinPhysicalW SkinPhysicalH SkinZoomedW SkinZoomedH SkinZPos SkinDpiFactor SkinZoomFactor SkinVisible DisplayCount DisplayVirtualScreenX DisplayVirtualScreenY DisplayVirtualScreenW DisplayVirtualScreenH DisplayVirtualScreenPhysicalX DisplayVirtualScreenPhysicalY DisplayVirtualScreenPhysicalW DisplayVirtualScreenPhysicalH";
constexpr View Plugins = "ActionTimer AdvancedCPU AudioLevel CoreTemp DragDrop FileView FolderInfo InputText iTunes MediaKey Mouse NowPlaying PerfMon Quote RecycleManager ResMon RunCommand SpeedFan UsageMonitor WebParser WiFiStatus WindowMessage";

constexpr VocabularyGroup Groups[] = {
    {L"Options", Vocabulary[0]},
    {L"Options with numbered variants", Vocabulary[1]},
    {L"Options with fixed values", Vocabulary[2]},
    {L"Fixed values (Option=Value)", Vocabulary[3]},
    {L"Bangs (omit !)", Vocabulary[4]},
    {L"Built-in #variables (omit #)", Vocabulary[5]},
    {L"Deprecated options", Vocabulary[6]},
    {L"Deprecated values (Option=Value)", Vocabulary[7]},
    {L"Deprecated bangs (omit !)", Vocabulary[8]},
    {L"Formula functions and constants", Functions},
    {L"Section-variable selectors", Selectors},
    {L"Shape primitives", Shapes},
    {L"Shape modifiers", ShapeModifiers},
    {L"Shape modifier values", ShapeValues},
    {L"Shape path commands", PathCommands},
    {L"Inline settings", InlineSettings},
    {L"WebParser flags", WebParserFlags},
    {L"Built-in $variables (omit $)", DollarTerms},
    {L"Plugin names (omit .dll)", Plugins}
};

bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool Space(char c) { return c == ' ' || c == '\t'; }
bool Word(char c) { return Alpha(c) || Digit(c) || c == '_'; }
bool TermChar(char c) { return Word(c) || c == '-' || c == '@'; }
char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
bool HexDigit(char c) { return Digit(c) || (Lower(c) >= 'a' && Lower(c) <= 'f'); }
std::string Lower(View text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](char c) { return Lower(c); });
    return result;
}

bool HasColorSuffix(View name)
{
    while (!name.empty() && Digit(name.back())) name.remove_suffix(1);
    return name.ends_with("color") || name.ends_with("tint");
}

bool IsColorVariable(View name)
{
    const auto lower = Lower(name);
    return lower.starts_with("color") || lower.starts_with("tint") || HasColorSuffix(lower);
}

bool IsRegexOption(View option)
{
    return option == "regexp" || option == "ifmatch" || option == "regexpfilter" ||
        option == "inputregexp" || option == "inlinepattern";
}

size_t LinkPrefixLength(View value)
{
    constexpr View prefixes[] = {"https://", "http://", "ftp://", "file://", "mailto:"};
    for (const auto prefix : prefixes)
    {
        if (value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin(),
            [](char lower, char ch) { return lower == Lower(ch); })) return prefix.size();
    }
    return 0;
}

View Trim(View text)
{
    while (!text.empty() && Space(text.front())) text.remove_prefix(1);
    while (!text.empty() && Space(text.back())) text.remove_suffix(1);
    return text;
}

template<typename Callback>
void ForWords(View text, Callback callback)
{
    while (!text.empty())
    {
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == View::npos) return;
        text.remove_prefix(first);
        size_t size = text.find_first_of(" \t\r\n");
        if (size == View::npos) size = text.size();
        callback(text.substr(0, size));
        text.remove_prefix(size);
    }
}

void Insert(Terms& terms, View word)
{
    terms.try_emplace(Lower(word), word);
}

Terms MakeTerms(View words)
{
    Terms result;
    ForWords(words, [&](View word) { Insert(result, word); });
    return result;
}

struct Catalog {
    Terms options, numbered, deprecatedOptions, bangs, skinBangs, deprecatedBangs, variables, dollars;
    Terms functions, selectors, shapes, modifiers, shapeValues, path, inlines, flags;
    std::unordered_map<std::string, Terms> values, deprecatedValues;

    explicit Catalog(const std::vector<std::string>& groups)
    {
        auto load = [&](int index, Terms& terms) {
            ForWords(groups[index], [&](View word) { Insert(terms, word); });
        };
        load(6, deprecatedOptions);
        load(0, options);
        load(1, numbered);
        load(1, options);
        load(2, options);
        load(4, bangs);
        load(8, deprecatedBangs);
        for (const auto& [key, bang] : bangs)
            if (key.starts_with("skin:")) Insert(skinBangs, bang);
        ForWords(SkinBangTargets, [&](View target) {
            if (const auto bang = bangs.find(Lower(target)); bang != bangs.end())
                Insert(skinBangs, "Skin:" + bang->second);
        });
        load(5, variables);
        load(9, functions);
        load(10, selectors);
        load(11, shapes);
        load(12, modifiers);
        load(13, shapeValues);
        load(14, path);
        load(15, inlines);
        load(16, flags);
        load(17, dollars);
        load(18, values["plugin"]);
        const auto loadValues = [&](int index, auto& table) {
            ForWords(groups[index], [&](View word) {
                const size_t separator = word.find('=');
                if (separator > 0 && separator < word.size() - 1)
                    Insert(table[Lower(word.substr(0, separator))], word.substr(separator + 1));
            });
        };
        loadValues(3, values);
        loadValues(7, deprecatedValues);
        ForWords(StringEffectValues, [&](View word) { Insert(values["stringeffect"], word); });
        for (View modifier : {"", "Physical", "WorkArea", "WorkAreaPhysical"})
        {
            for (View coordinate : {"X", "Y", "W", "H"})
            {
                Insert(dollars, "Display" + std::string(modifier) + std::string(coordinate));
            }
        }
        ForWords("DisplayName DisplayNumber DisplayDpiFactor DisplayDeviceName DisplayDeviceNumber DisplayDeviceDpiFactor",
            [&](View word) { Insert(dollars, word); });
    }
};

std::unique_ptr<const Catalog> customCatalog;

const Catalog& Words()
{
    if (customCatalog) return *customCatalog;
    static const Catalog defaults(ReadVocabulary({}));
    return defaults;
}

std::string NumberedBase(std::string name)
{
    size_t suffix = name.size();
    while (suffix > 0 && Digit(name[suffix - 1])) --suffix;
    if (Words().numbered.contains(name.substr(0, suffix))) name.resize(suffix);
    return name;
}

bool BuiltInHash(const std::string& name)
{
    if (Words().variables.contains(name)) return true;
    const size_t at = name.find('@');
    if (at == std::string::npos || at + 1 == name.size()) return false;
    const View base = View(name).substr(0, at);
    return (base.starts_with("workarea") || base.starts_with("screenarea")) &&
        Words().variables.contains(std::string(base)) &&
        std::all_of(name.begin() + at + 1, name.end(), Digit);
}

bool BuiltInDollar(std::string name)
{
    if (name == "mousex:%" || name == "mousey:%") return true;
    if (name.starts_with("display"))
    {
        size_t first = name.starts_with("displaydevice") ? 13 : 7;
        size_t last = first;
        while (last < name.size() && Digit(name[last])) ++last;
        if (last > first && (name.substr(last) == "count" || name.substr(last).starts_with("virtualscreen"))) return false;
        name.erase(first, last - first);
    }
    return Words().dollars.contains(name);
}

struct Range { size_t start, end; };
constexpr size_t Missing = View::npos;

bool OneEditApart(View a, View b)
{
    if (a.size() > b.size()) std::swap(a, b);
    if (b.size() - a.size() > 1) return false;
    const auto first = std::mismatch(a.begin(), a.end(), b.begin());
    const auto offset = static_cast<size_t>(first.first - a.begin());
    a.remove_prefix(offset);
    b.remove_prefix(offset);
    if (b.size() != a.size()) return a == b.substr(1);
    if (a.empty()) return false;
    return a.substr(1) == b.substr(1) ||
        (a.size() >= 2 && a[0] == b[1] && a[1] == b[0] && a.substr(2) == b.substr(2));
}

class LineParser {
public:
    LineParser(View input, size_t caret = Missing) :
        text(input), styles(input.size()), closes(input.size(), Missing), cursor(caret)
    {
        struct Frame { size_t position; bool bang; size_t quote; };
        std::vector<Frame> brackets;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '"' && !brackets.empty() && brackets.back().bang)
            {
                auto& frame = brackets.back();
                const auto width = text.substr(i, 3) == "\"\"\"" ? size_t{3} : size_t{1};
                if (!frame.quote || frame.quote == width)
                {
                    frame.quote = frame.quote ? 0 : width;
                    i += width - 1;
                }
            }
            else if (text[i] == '[')
            {
                const bool bang = i + 1 < text.size() && text[i + 1] == '!';
                if (!brackets.empty() && brackets.back().quote && !bang &&
                    (i + 1 == text.size() || !(Word(text[i + 1]) ||
                        View("@#$&%\\*[").find(text[i + 1]) != Missing))) continue;
                brackets.push_back({i, bang, 0});
            }
            else if (text[i] == ']' && !brackets.empty())
            {
                if (brackets.back().quote) continue;
                closes[brackets.back().position] = i;
                brackets.pop_back();
            }
        }
    }

    void Parse(bool variablesSection, bool regexSubstitute = false)
    {
        Range line = TrimRange({0, text.size()});
        if (line.start == line.end) return;
        if (text[line.start] == ';')
        {
            Paint(line, Comment);
            return;
        }
        if (text[line.start] == '!' || Slice(line).starts_with("[!"))
        {
            Paint(line, InvalidValue);
            return;
        }
        if (text[line.start] == '[')
        {
            if (!SectionName(Slice(line)).empty()) Paint({line.start, closes[line.start] + 1}, Section);
            return;
        }

        const size_t equals = text.find('=', line.start);
        const Range key = TrimRange({line.start, equals == Missing ? line.end : equals});
        if (equals == Missing)
        {
            if (!variablesSection || Slice(key).starts_with("@")) ReadOption(key);
            return;
        }
        Paint({equals, equals + 1}, Equals);
        std::string option;
        const bool variable = variablesSection && !Slice(key).starts_with("@");
        if (variable) Paint(key, UserVariable);
        else option = ReadOption(key);
        const auto value = TrimRange({equals + 1, line.end});
        if (regexSubstitute && option == "substitute") ReadRegexSubstitutions(value);
        else ReadValue(value, option, 0, variable && IsColorVariable(Slice(key)));
    }

    std::vector<unsigned char> styles;
    EditingTerms editing;

private:
    View text;
    std::vector<size_t> closes;
    size_t cursor;

    View Slice(Range range) const { return text.substr(range.start, range.end - range.start); }
    Range TrimRange(Range range) const
    {
        while (range.start < range.end && Space(text[range.start])) ++range.start;
        while (range.start < range.end && Space(text[range.end - 1])) --range.end;
        return range;
    }

    void Paint(Range range, Style style)
    {
        std::fill(styles.begin() + range.start, styles.begin() + range.end, style);
    }

    Range Term(size_t start, size_t end, bool colon = false) const
    {
        size_t i = start;
        while (i < end && (TermChar(text[i]) || (colon && text[i] == ':'))) ++i;
        return {start, i};
    }

    void Offer(Range range, const Terms& list, bool typos = false, bool preferExact = true)
    {
        if (cursor == Missing || cursor < range.start || cursor > range.end) return;
        editing = {};
        editing.start = range.start;
        editing.end = range.end;
        const auto typed = Lower(text.substr(range.start, cursor - range.start));
        for (const auto& [key, word] : list)
        {
            if (key.starts_with(typed)) editing.candidates.push_back(word);
            if (preferExact && key == typed && cursor == range.end) editing.canonical = word;
        }
        if (typos && cursor == range.end && typed.size() >= 5 && editing.candidates.empty())
        {
            for (const auto& [key, word] : list)
            {
                if (!OneEditApart(typed, key)) continue;
                if (!editing.typo.empty()) { editing.typo.clear(); break; }
                editing.typo = word;
            }
        }
    }

    bool Match(Range range, const Terms& list, Style style, bool typos = false)
    {
        Offer(range, list, typos);
        if (!list.contains(Lower(Slice(range)))) return false;
        Paint(range, style);
        return true;
    }

    std::string ReadOption(Range range)
    {
        if (Term(range.start, range.end).end != range.end) return {};
        Offer(range, Words().options, true);
        const auto name = Lower(Slice(range));
        const auto base = NumberedBase(name);
        if (Words().deprecatedOptions.contains(name) || Words().deprecatedOptions.contains(base))
        {
            Paint(range, DeprecatedOption);
            editing.canonical.clear();
            editing.typo.clear();
        }
        else if (Words().options.contains(name) || Words().options.contains(base))
        {
            Paint(range, name.starts_with('@') ? Directive : Option);
            if (base != name && cursor == range.end && editing.canonical.empty())
            {
                const auto found = Words().options.find(base);
                if (found != Words().options.end()) editing.canonical = found->second + name.substr(base.size());
            }
        }
        return base;
    }

    void EnumValue(Range range, const std::string& option)
    {
        const auto& catalog = Words();
        const auto old = catalog.deprecatedValues.find(option);
        const auto current = catalog.values.find(option);
        if (old != catalog.deprecatedValues.end() && old->second.contains(Lower(Slice(range))))
        {
            Paint(range, DeprecatedValue);
            return;
        }
        if (current == catalog.values.end()) return;
        if (Term(range.start, range.end).end != range.end) return;
        if (Match(range, current->second, Value, option != "plugin" && option != "playername")) return;
        if (option != "plugin" && option != "playername") Paint(range, InvalidValue);
    }

    void ReadValue(Range range, const std::string& option, unsigned depth, bool colorVariable = false)
    {
        if (range.start == range.end || depth >= 32) return;
        const bool regex = IsRegexOption(option);
        if (Slice(range).starts_with("\"") && Slice(range).ends_with("\"") && range.end - range.start >= 2)
        {
            Paint({range.start, range.start + 1}, String);
            Paint({range.end - 1, range.end}, String);
            range = {range.start + 1, range.end - 1};
            if (!regex) range = TrimRange(range);
        }
        if (regex)
        {
            Paint(range, RegularExpression);
            return;
        }
        if (option == "url")
        {
            PaintUrl(range, depth);
            return;
        }

        if ((option == "x" || option == "y") && range.start < range.end)
        {
            // Rainmeter interprets only the final character as a positioning suffix.
            const char suffix = text[range.end - 1];
            if (suffix == 'r' || suffix == 'R')
            {
                Paint({range.end - 1, range.end}, suffix == 'r' ? RelativeStart : RelativeEnd);
                range = TrimRange({range.start, range.end - 1});
            }
        }

        const bool formula = option == "formula" || option == "ifcondition";
        const bool format = option == "format" || option == "timestampformat";
        const bool literal = format || option == "text" || option == "string" || option == "substitute" ||
            option == "tooltiptext" || option == "tooltiptitle" || option == "fontface" ||
            option == "parameter" || option == "program" || option == "filename" ||
            option.ends_with("path") || option.ends_with("action") || option.ends_with("name");
        const bool isShape = option == "shape";
        const bool isInline = option == "inlinesetting";
        const bool list = isShape || isInline || option == "flags" || option == "group" || option == "@inherit" ||
            option == "meterstyle" || option == "actionlist" || option == "information" || option == "update" ||
            option == "blacklist" || option == "whitelist";
        const bool color = colorVariable || HasColorSuffix(option);
        const bool references = option == "group" || option == "@inherit" || option == "meterstyle" ||
            option == "container" || option.starts_with("measurename");

        EnumValue(range, option);
        if (isShape || isInline)
        {
            const Range term = Term(range.start, range.end);
            if (!Match(term, isShape ? Words().shapes : Words().inlines, Value) && term.start != term.end)
                Paint(term, InvalidValue);
        }
        if (color && TryColor(range, depth)) return;
        if (references) Paint(range, Reference);
        const bool inlineColor = isInline && Lower(Slice(Term(range.start, range.end))) == "color";
        Scan(range, {.formula = formula, .numbers = !literal && !references, .pipes = list,
            .format = format, .shape = isShape, .inlines = isInline, .flags = option == "flags",
            .colors = inlineColor, .paths = !literal && !references && !isShape && !isInline}, depth);
    }

    struct Context {
        bool formula = false, numbers = false, pipes = false, format = false;
        bool shape = false, inlines = false, flags = false, calls = false, colors = false, paths = false;
        Style numericStyle = Number;
    };

    void ReadRegexSubstitutions(Range range)
    {
        size_t i = range.start;
        const auto skipSpace = [&] {
            while (i < range.end && Space(text[i])) ++i;
        };
        const auto token = [&](char separator) {
            skipSpace();
            if (i < range.end && (text[i] == '"' || text[i] == '\''))
            {
                const size_t open = i;
                const auto close = std::min(text.find(text[open], open + 1), range.end);
                Paint({open, open + 1}, String);
                i = close < range.end ? close + 1 : close;
                if (close < range.end) Paint({close, i}, String);
                return Range{open + 1, close};
            }
            const size_t start = i;
            i = std::min(text.find(separator, i), range.end);
            return TrimRange({start, i});
        };
        while (i < range.end)
        {
            Paint(token(':'), RegularExpression);
            skipSpace();
            if (i == range.end || text[i] != ':') break;
            Paint({i, i + 1}, Operator);
            ++i;
            Scan(token(','), {}, 1);
            skipSpace();
            if (i == range.end || text[i] != ',') break;
            Paint({i, i + 1}, Operator);
            ++i;
        }
    }

    size_t LinkEnd(size_t start, size_t end) const
    {
        size_t i = start;
        unsigned parentheses = 0;
        for (; i < end; ++i)
        {
            const char ch = text[i];
            if (ch == '[' && closes[i] < end) { i = closes[i]; continue; }
            if (Space(ch) || View("\"'<>]").find(ch) != Missing || (ch == ')' && !parentheses)) break;
            if (ch == '(') ++parentheses;
            else if (ch == ')') --parentheses;
        }
        while (i > start && View(".,;").find(text[i - 1]) != Missing) --i;
        return i;
    }

    void PaintUrl(Range range, unsigned depth)
    {
        Paint(range, Url);
        const auto prefix = LinkPrefixLength(Slice(range));
        for (size_t i = range.start + prefix; i < range.end;)
        {
            const char ch = text[i];
            if (ch == '[' && closes[i] < range.end)
            {
                if (prefix && i == range.start + prefix)
                {
                    const auto address = Slice({i + 1, closes[i]});
                    const auto ipv6 = address.substr(0, address.find('%'));
                    if (ipv6.find(':') != View::npos && std::all_of(ipv6.begin(), ipv6.end(),
                        [](char c) { return HexDigit(c) || c == ':' || c == '.'; }))
                    {
                        i = closes[i] + 1;
                        continue;
                    }
                }
                i = Bracket(i, range.end, depth);
                continue;
            }
            if (View("#$%").find(ch) != Missing)
            {
                size_t nameEnd = i + 1;
                while (nameEnd < range.end && (TermChar(text[nameEnd]) || text[nameEnd] == '*')) ++nameEnd;
                const auto name = Slice({i + 1, nameEnd});
                const bool closed = nameEnd < range.end && text[nameEnd] == ch;
                const bool adjacentPercentEscapes = ch == '%' && name.size() == 2 &&
                    nameEnd + 2 < range.end && std::all_of(name.begin(), name.end(), HexDigit) &&
                    HexDigit(text[nameEnd + 1]) && HexDigit(text[nameEnd + 2]);
                const bool reference = !name.empty() && (ch == '$' ? BuiltInDollar(Lower(name)) :
                    closed && (ch == '#' || (!Digit(name.front()) && !adjacentPercentEscapes)));
                if (reference)
                {
                    i = ReadLegacyVariable(i, range.end, ch);
                    continue;
                }
            }
            ++i;
        }
    }

    size_t FindColorDelimiter(Range range, char delimiter) const
    {
        unsigned parentheses = 0;
        for (size_t i = range.start; i < range.end; ++i)
        {
            const char ch = text[i];
            if (ch == '[' && closes[i] < range.end) i = closes[i];
            else if (ch == '(') ++parentheses;
            else if (ch == ')' && parentheses) --parentheses;
            else if (ch == delimiter && !parentheses) return i;
        }
        return range.end;
    }

    bool TryColor(Range range, unsigned depth)
    {
        range = TrimRange(range);
        constexpr Style channels[] = {Red, Green, Blue, AlphaChannel};
        const bool prefix = Slice(range).starts_with("0x");
        const Range digits{range.start + (prefix ? 2U : 0U), range.end};
        const View hex = Slice(digits);
        if ((hex.size() == 6 || hex.size() == 8) &&
            std::all_of(hex.begin(), hex.end(), HexDigit))
        {
            if (prefix) Paint({range.start, digits.start}, Color);
            for (size_t i = 0; i < hex.size() / 2; ++i)
                Paint({digits.start + i * 2, digits.start + (i + 1) * 2}, channels[i]);
            return true;
        }

        std::array<Range, 4> parts{};
        std::array<size_t, 4> delimiters{};
        size_t count = 0;
        size_t start = range.start;
        while (true)
        {
            if (count == parts.size()) return false;
            const size_t end = FindColorDelimiter({start, range.end}, ',');
            const auto part = TrimRange({start, end});
            const auto value = Slice(part);
            if (value.empty()) return false;
            const char first = value.front();
            if (first != '(' && View("[#$%").find(first) == Missing)
            {
                const auto digitsStart = part.start + (first == '-' || first == '+');
                if (digitsStart == part.end || !Digit(text[digitsStart]) || ReadNumber(digitsStart, part.end) != part.end)
                    return false;
            }
            parts[count] = part;
            delimiters[count++] = end;
            if (end == range.end) break;
            start = end + 1;
        }
        if (count < 3) return false;
        for (size_t i = 0; i < count; ++i)
        {
            const auto part = parts[i];
            if (Digit(text[part.start]) || text[part.start] == '-' || text[part.start] == '+') Paint(part, channels[i]);
            else
            {
                Context context{};
                context.numbers = true;
                context.numericStyle = channels[i];
                Scan(part, context, depth + 1);
            }
            if (i + 1 < count) Paint({delimiters[i], delimiters[i] + 1}, Operator);
        }
        return true;
    }

    size_t ReadNumber(size_t i, size_t end) const
    {
        const size_t start = i;
        if (i + 2 < end && text[i] == '0')
        {
            const char radix = Lower(text[i + 1]);
            if (radix == 'x' || radix == 'o' || radix == 'b')
            {
                i += 2;
                const char limit = radix == 'b' ? '1' : radix == 'o' ? '7' : '9';
                while (i < end && ((text[i] >= '0' && text[i] <= limit) ||
                    (radix == 'x' && Lower(text[i]) >= 'a' && Lower(text[i]) <= 'f'))) ++i;
                return i > start + 2 ? i : start + 1;
            }
        }
        while (i < end && Digit(text[i])) ++i;
        if (i < end && text[i] == '.')
        {
            ++i;
            while (i < end && Digit(text[i])) ++i;
        }
        if (i < end && Lower(text[i]) == 'e')
        {
            size_t exponent = i + 1;
            if (exponent < end && (text[exponent] == '+' || text[exponent] == '-')) ++exponent;
            if (exponent < end && Digit(text[exponent]))
            {
                i = exponent + 1;
                while (i < end && Digit(text[i])) ++i;
            }
        }
        return i;
    }

    size_t Bracket(size_t start, size_t end, unsigned depth)
    {
        if (start + 1 >= end || depth >= 32) return start + 1;
        const size_t close = closes[start];
        const bool complete = close < end;
        const char key = text[start + 1];
        if (key == '!')
        {
            Paint({start, start + 1}, BangBracket);
            ReadBang(start + 1, complete ? close : end, depth + 1);
            if (complete) Paint({close, close + 1}, BangBracket);
            return complete ? close + 1 : end;
        }
        const bool explicitVariable = View("#$&%\\").find(key) != Missing;
        const size_t body = start + 1 + explicitVariable;
        if (!complete)
        {
            if (explicitVariable)
            {
                const Range term = Term(body, end);
                if (key == '#') Offer(term, Words().variables);
                if (key == '$') Offer(term, Words().dollars);
            }
            if (cursor != Missing && (key == '&' || Alpha(key) || key == '_' || key == '@'))
            {
                const size_t colon = text.find(':', body);
                if (colon < end) Offer(Term(colon + 1, end), Words().selectors, false, false);
            }
            return start + 1;
        }

        const View name = Slice({body, close});
        if (name.empty()) return close + 1;
        if ((key == '*' && name.ends_with('*')) ||
            (explicitVariable && name.starts_with('*') && name.ends_with('*')))
        {
            Paint({start, close + 1}, Escaped);
            return close + 1;
        }
        if (!explicitVariable && !Word(key) && key != '@' && key != '[' &&
            static_cast<unsigned char>(key) < 128)
            return start + 1;
        if (key == '\\')
        {
            View digits = name;
            int radix = 10;
            if (Lower(digits.front()) == 'x') { radix = 16; digits.remove_prefix(1); }
            unsigned code = 0;
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), code, radix);
            if (parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() && code < 0xFFFF)
                Paint({start, close + 1}, Character);
            return close + 1;
        }

        Style style = SectionVariable;
        if (key == '#')
        {
            style = BuiltInHash(Lower(name)) ? BuiltInVariable : UserVariable;
            Offer({body, close}, Words().variables);
        }
        if (key == '$')
        {
            style = BuiltInDollar(Lower(name)) ? DollarVariable : Default;
            Offer({body, close}, Words().dollars);
        }
        if (key == '%') style = EnvironmentVariable;
        Paint({start, close + 1}, style);
        if (style == SectionVariable)
        {
            size_t colon = body;
            while (colon < close && text[colon] != ':')
            {
                if (text[colon] == '[' && closes[colon] < close) colon = closes[colon];
                ++colon;
            }
            if (colon < close)
            {
                Paint({colon, colon + 1}, Operator);
                const Range selector = Term(colon + 1, close);
                if (!Match(selector, Words().selectors, Selector) && selector.end < close && text[selector.end] == '(')
                    Paint(selector, Function);
                Scan({selector.start == selector.end ? colon + 1 : selector.end, close},
                    {.formula = true, .numbers = true, .calls = true}, depth + 1);
            }
            Scan({body, colon}, {}, depth + 1);
        }
        else
        {
            Scan({body, close}, {}, depth + 1);
        }
        return close + 1;
    }

    size_t ReadLegacyVariable(size_t i, size_t end, char mark)
    {
        size_t last = i + 1;
        while (last < end && (TermChar(text[last]) || text[last] == '*')) ++last;
        const bool closed = last < end && text[last] == mark;
        if (mark == '%' && !closed) return i;
        if (last == i + 1)
        {
            if (mark == '#') Offer({last, last}, Words().variables);
            if (mark == '$') Offer({last, last}, Words().dollars);
            return i;
        }
        const Range name{i + 1, last};
        const auto word = Lower(Slice(name));
        const bool escaped = word.starts_with('*') && word.ends_with('*');
        if (mark == '#' && !escaped) Offer(name, Words().variables);
        if (mark == '$' && !escaped) Offer(name, Words().dollars);
        if (escaped && closed) Paint({i, last + 1}, Escaped);
        else if (mark == '#' && closed) Paint({i, last + 1}, BuiltInHash(word) ? BuiltInVariable : UserVariable);
        else if (mark == '$' && BuiltInDollar(word)) Paint({i, last + closed}, DollarVariable);
        else if (mark == '%' && closed && !Digit(text[i + 1])) Paint({i, last + 1}, EnvironmentVariable);
        return last + closed;
    }

    void Scan(Range range, Context context, unsigned depth)
    {
        if (depth >= 32) return;
        unsigned parentheses = 0;
        bool modifier = false;
        bool shapeValue = false;
        bool colorNext = false;
        const auto& catalog = Words();
        for (size_t i = range.start; i < range.end;)
        {
            const char ch = text[i];
            if (Alpha(ch) && (i == range.start || !Word(text[i - 1])))
            {
                const auto prefix = LinkPrefixLength(Slice({i, range.end}));
                if (prefix)
                {
                    const auto end = LinkEnd(i + prefix, range.end);
                    PaintUrl({i, end}, depth);
                    i = end;
                    continue;
                }
            }
            if (colorNext && !Space(ch) && ch != '|')
            {
                colorNext = false;
                const auto end = FindColorDelimiter({i, range.end}, '|');
                if (TryColor({i, end}, depth))
                {
                    i = end;
                    continue;
                }
            }
            if (ch == '[') { i = Bracket(i, range.end, depth); continue; }
            if (ch == '!' && i == range.start && !context.format &&
                (i + 1 == range.end || Alpha(text[i + 1])))
            {
                ReadBang(i, range.end, depth + 1);
                return;
            }
            if (context.format && ch == '%' && i + 1 < range.end)
            {
                const size_t count = text[i + 1] == '#' && i + 2 < range.end ? 3 : 2;
                Paint({i, i + count}, Format);
                i += count;
                continue;
            }
            if (ch == '#' || ch == '$' || (ch == '%' && !context.formula))
            {
                const size_t next = ReadLegacyVariable(i, range.end, ch);
                if (next > i) { i = next; continue; }
            }

            const bool formula = context.formula || parentheses != 0;
            if (ch == '(' && (context.numbers || context.formula || context.calls)) ++parentheses;
            else if (ch == ')' && parentheses) --parentheses;
            if ((formula || parentheses) && View("()+-*/%^~<>=!&|?:,").find(ch) != Missing)
                Paint({i, i + 1}, Operator);
            else if (ch == '|' && (context.pipes || context.paths))
            {
                size_t first = i + 1;
                while (first < range.end && Space(text[first])) ++first;
                const auto part = Term(first, range.end);
                const bool path = context.paths && catalog.path.contains(Lower(Slice(part)));
                if (context.pipes || path) Paint({i, i + 1}, Pipe);
                if (path) Match(part, catalog.path, Selector);
                modifier = context.shape;
                shapeValue = false;
                colorNext = context.colors;
            }
            else if (ch == '%' && i + 1 < range.end && Digit(text[i + 1]))
            {
                const Range placeholder = Term(i + 1, range.end);
                Paint({i, placeholder.end}, Reference);
                i = placeholder.end;
                continue;
            }
            else if (View(",").find(ch) != Missing && context.numbers)
                Paint({i, i + 1}, Operator);

            if (context.calls && (ch == '\'' || ch == '"'))
            {
                const size_t close = text.find(ch, i + 1);
                if (close < range.end)
                {
                    Paint({i, close + 1}, String);
                    Scan({i + 1, close}, {}, depth + 1);
                    i = close + 1;
                    continue;
                }
            }
            if ((formula || context.numbers) && (Digit(ch) || (ch == '.' && i + 1 < range.end && Digit(text[i + 1]))) &&
                (i == range.start || !Word(text[i - 1])))
            {
                const Range number{i, ReadNumber(i, range.end)};
                if (number.end == range.end || !Word(text[number.end]) || formula ||
                    ((number.end + 1 == range.end || !Word(text[number.end + 1])) &&
                        (Lower(text[number.end]) == 'r' || text[number.end] == '%')))
                    Paint(number, context.numericStyle);
                i = number.end;
                continue;
            }
            if (Alpha(ch) || ch == '_')
            {
                size_t j = i;
                while (j < range.end && Word(text[j])) ++j;
                Range word{i, j};
                const auto lower = Lower(Slice(word));
                if (context.calls && j < range.end && text[j] == '(') Paint(word, Function);
                if (formula) Match(word, catalog.functions, Function);
                if (context.flags) Match(word, catalog.flags, Value);
                if (modifier)
                {
                    Match(word, catalog.modifiers, Selector);
                    shapeValue = lower == "fill" || lower == "stroke" || lower.starts_with("strokecap") ||
                        lower.starts_with("strokestartcap") || lower.starts_with("strokeendcap") ||
                        lower.starts_with("strokedashcap") || lower == "strokelinejoin";
                    modifier = false;
                }
                else if (shapeValue)
                {
                    Match(word, catalog.shapeValues, Value);
                    shapeValue = false;
                    colorNext = lower == "color";
                }
                else if (context.shape && lower.starts_with("shape") && lower.size() > 5 &&
                    std::all_of(lower.begin() + 5, lower.end(), Digit))
                    Paint(word, Reference);
                else if (context.shape && catalog.path.contains(lower))
                    Paint(word, Selector);
                i = word.end;
                continue;
            }
            ++i;
        }
    }

    std::vector<Range> Arguments(size_t start, size_t end)
    {
        std::vector<Range> args;
        size_t i = start;
        while (i < end)
        {
            while (i < end && Space(text[i])) ++i;
            if (i == end) break;
            const size_t first = i;
            const View quote = text.substr(i, 3) == "\"\"\"" ? View("\"\"\"") :
                text[i] == '"' ? View("\"") : View();
            if (!quote.empty()) i += quote.size();
            unsigned parentheses = 0;
            while (i < end)
            {
                if (!quote.empty() && text.substr(i, quote.size()) == quote)
                {
                    i += quote.size();
                    break;
                }
                if (text[i] == '[' && closes[i] < end) { i = closes[i] + 1; continue; }
                if (quote.empty())
                {
                    if (!parentheses && Space(text[i])) break;
                    if (text[i] == '(') ++parentheses;
                    if (text[i] == ')' && parentheses) --parentheses;
                }
                ++i;
            }
            args.push_back({first, i});
        }
        return args;
    }

    void ReadBang(size_t start, size_t end, unsigned depth)
    {
        if (depth >= 32) return;
        const Range command = Term(start + 1, end, true);
        auto lower = Lower(Slice(command));
        size_t prefix = 0;
        const auto& catalog = Words();
        if (lower.starts_with("rainmeter")) { lower.erase(0, 9); prefix += 9; }
        const bool namespacedSkin = lower.starts_with("skin:");
        const bool redirected = namespacedSkin && !catalog.bangs.contains(lower);
        const auto& candidates = namespacedSkin ? catalog.skinBangs : catalog.bangs;
        const bool recognized = candidates.contains(lower);
        if (redirected) lower.erase(0, 5);
        const bool deprecated = catalog.deprecatedBangs.contains(lower);
        Offer({command.start + prefix, command.end}, candidates, !deprecated);
        if (recognized) Paint({start, command.end}, Bang);
        else if (deprecated) Paint({start, command.end}, DeprecatedBang);

        const auto args = Arguments(command.end, end);
        size_t key = Missing, value = Missing;
        const size_t first = redirected ? 1 : 0;
        const bool skinAlias = recognized && (lower == "skin:load" || lower == "skin:unload" ||
            lower == "skin:toggle" || lower == "skin:edit");
        const bool textEdit = recognized && lower.starts_with("textedit:") && lower != "textedit:dismiss";
        const size_t textEditArgs = lower == "textedit:select" ? 2 :
            lower == "textedit:settext" || lower == "textedit:scrollbyline" ? 1 : 0;
        const bool setsVariable = lower == "setvariable" || lower == "setvariablegroup";
        const bool setsColorVariable = setsVariable && first < args.size() && IsColorVariable(WithoutQuotes(args[first]));
        if (lower == "setoption" || lower == "setoptiongroup")
        {
            key = first + 1;
            // SetOption can target the section running the action with a two-argument form.
            if (lower == "setoption" && first < args.size() && args.size() <= first + 2 &&
                catalog.options.contains(NumberedBase(Lower(WithoutQuotes(args[first]))))) key = first;
            value = key + 1;
        }
        else if (lower == "writekeyvalue") { key = first + 1; value = first + 2; }

        std::string option;
        for (size_t i = 0; i < args.size(); ++i)
        {
            const Range raw = args[i];
            Range inner = raw;
            if (text[raw.start] == '"')
            {
                const size_t quote = Slice(raw).starts_with("\"\"\"") ? 3 : 1;
                inner.start += quote;
                const bool closed = inner.end - inner.start >= quote &&
                    text.substr(inner.end - quote, quote) == text.substr(raw.start, quote);
                if (closed) inner.end -= quote;
                Paint({raw.start, inner.start}, String);
                if (closed) Paint({inner.end, raw.end}, String);
            }
            if (i == value && IsRegexOption(option))
            {
                Paint(inner, RegularExpression);
                continue;
            }
            inner = TrimRange(inner);
            Paint(inner, Argument);
            if (i == key) option = ReadOption(inner);
            if (i == value)
            {
                ReadValue(inner, option, depth + 1);
                continue;
            }
            if (i == first && setsVariable) Paint(inner, UserVariable);
            if (i == first + 1 && setsColorVariable && TryColor(inner, depth)) continue;
            const bool reference = ((lower.ends_with("meter") || lower.ends_with("measure") ||
                lower.ends_with("metergroup") || lower.ends_with("measuregroup") ||
                lower == "setoption" || lower == "setoptiongroup" || lower == "writekeyvalue")
                && i == first && i != key) || ((redirected || skinAlias) && i == 0) ||
                (skinAlias && i == 1 && lower != "skin:unload") ||
                (textEdit && i == first && args.size() > first + textEditArgs);
            if (reference) Paint(inner, Reference);
            Scan(inner, {.numbers = true, .calls = lower.starts_with("commandmeasure") && i > first}, depth + 1);
        }
    }

    View WithoutQuotes(Range range) const
    {
        auto textValue = Slice(range);
        while (textValue.starts_with('"')) textValue.remove_prefix(1);
        while (textValue.ends_with('"')) textValue.remove_suffix(1);
        return Trim(textValue);
    }
};
}

std::span<const VocabularyGroup> VocabularyGroups() { return Groups; }

std::vector<std::string> ReadVocabulary(std::string_view overrides)
{
    std::vector<Terms> selected;
    for (const auto& group : Groups) selected.push_back(MakeTerms(group.defaults));
    while (!overrides.empty())
    {
        const auto newline = overrides.find('\n');
        auto line = overrides.substr(0, newline);
        overrides.remove_prefix(newline == View::npos ? overrides.size() : newline + 1);
        if (line.ends_with('\r')) line.remove_suffix(1);
        const auto space = line.find(' ');
        if (space == View::npos || space < 2 || space + 1 == line.size()) continue;
        unsigned group = 0;
        const auto number = std::from_chars(line.data() + 1, line.data() + space, group);
        if (number.ec != std::errc{} || number.ptr != line.data() + space || group >= selected.size()) continue;
        const auto term = line.substr(space + 1);
        if (term.find_first_of(" \t\r\n") != View::npos) continue;
        if (line.front() == '+') selected[group].insert_or_assign(Lower(term), term);
        if (line.front() == '-') selected[group].erase(Lower(term));
    }
    std::vector<std::string> groups;
    for (const auto& terms : selected)
    {
        std::vector<View> sorted;
        for (const auto& [key, name] : terms) sorted.push_back(name);
        std::sort(sorted.begin(), sorted.end(), [](View a, View b) { return Lower(a) < Lower(b); });
        auto& group = groups.emplace_back();
        for (auto term : sorted) { group += term; group += "\r\n"; }
    }
    return groups;
}

std::string WriteVocabulary(const std::vector<std::string>& groups)
{
    std::string output;
    for (size_t i = 0; i < std::size(Groups) && i < groups.size(); ++i)
    {
        const auto defaults = MakeTerms(Groups[i].defaults);
        const auto edited = MakeTerms(groups[i]);
        const auto label = std::to_string(i) + " ";
        std::vector<std::string> changes;
        for (const auto& [key, name] : defaults)
            if (!edited.contains(key)) changes.push_back("-" + label + name + "\n");
        for (const auto& [key, name] : edited)
        {
            const auto original = defaults.find(key);
            if (original == defaults.end() || original->second != name)
                changes.push_back("+" + label + name + "\n");
        }
        std::sort(changes.begin(), changes.end());
        for (const auto& change : changes) output += change;
    }
    return output;
}

void ConfigureVocabulary(std::string_view overrides)
{
    if (overrides.empty()) customCatalog.reset();
    else customCatalog = std::make_unique<Catalog>(ReadVocabulary(overrides));
}

std::string_view SectionName(std::string_view line)
{
    line = Trim(line);
    if (!line.starts_with('[') || line.starts_with("[!")) return {};
    const size_t end = line.find(']');
    if (end == View::npos || line.substr(1, end - 1).find('[') != View::npos) return {};
    return Trim(line.substr(1, end - 1));
}

std::optional<bool> RegexSubstituteSetting(std::string_view line)
{
    const auto equal = line.find('=');
    if (equal == View::npos) return std::nullopt;
    const auto key = Trim(line.substr(0, equal));
    constexpr View name = "regexpsubstitute";
    if (key.size() != name.size() || !std::equal(key.begin(), key.end(), name.begin(),
        [](char ch, char lower) { return Lower(ch) == lower; })) return std::nullopt;
    auto value = Trim(line.substr(equal + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = Trim(value.substr(1, value.size() - 2));
    if (value.starts_with('+')) value.remove_prefix(1);
    int enabled = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), enabled);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && enabled != 0;
}

std::vector<unsigned char> StyleLine(std::string_view line, bool variablesSection, bool regexSubstitute)
{
    LineParser parser(line);
    parser.Parse(variablesSection, regexSubstitute);
    return std::move(parser.styles);
}

EditingTerms TermsAt(std::string_view line, size_t caret, bool variablesSection)
{
    LineParser parser(line, caret);
    parser.Parse(variablesSection);
    auto terms = std::move(parser.editing);
    if (terms.candidates.empty() && terms.canonical.empty() && terms.typo.empty() && caret <= line.size() &&
        (caret == 0 || (!TermChar(line[caret - 1]) && line[caret - 1] != ':')) &&
        (caret == line.size() || !TermChar(line[caret])))
    {
        // A placeholder reveals the grammatical position of an empty completion prefix.
        std::string expanded(line);
        expanded.insert(caret, "rainlexercursor");
        LineParser empty(expanded, caret);
        empty.Parse(variablesSection);
        terms = std::move(empty.editing);
        if (terms.start == caret) terms.end = caret;
    }
    std::sort(terms.candidates.begin(), terms.candidates.end(),
        [](const auto& a, const auto& b) { return Lower(a) < Lower(b); });
    return terms;
}
}

