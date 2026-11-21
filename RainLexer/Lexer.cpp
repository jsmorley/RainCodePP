// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).
#define NOMINMAX
#include <Windows.h>
#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"
#include "Syntax.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace RainLexer {
using namespace Scintilla;
namespace {
constexpr std::array Names = {
    "Default", "Comment", "Section", "Option", "Equals", "Invalid Value", "Value", "Bang",
    "Built-in Variable", "User Variable", "Number", "Deprecated Option", "Deprecated Value",
    "Deprecated Bang", "Character Escape", "List Separator", "Section Variable", "Dollar Variable",
    "Bang Bracket", "Bang Argument", "Quoted String", "Operator", "Function", "Selector",
    "Environment Variable", "Escaped Reference", "Format Specifier", "Directive", "Reference", "Color",
    "Red Channel", "Green Channel", "Blue Channel", "Alpha Channel",
    "Relative Start (r)", "Relative End (R)", "Regular Expression", "URL"
};
static_assert(Names.size() == StyleCount);

std::string GetLine(IDocument* doc, Sci_Position line)
{
    const auto start = doc->LineStart(line);
    std::string value(static_cast<size_t>(doc->LineEnd(line) - start), '\0');
    if (!value.empty()) doc->GetCharRange(value.data(), start, static_cast<Sci_Position>(value.size()));
    return value;
}

struct SectionStyles {
    Sci_Position first;
    Sci_Position after;
    int state;
    bool needsRefresh;
};

constexpr int VariablesState = 1;
constexpr int OptionsState = 2;
constexpr int RegexSubstituteState = 4;

SectionStyles StylesForSection(IDocument* doc, Sci_Position line)
{
    auto first = line;
    auto header = GetLine(doc, first);
    while (first > 0 && SectionName(header).empty()) header = GetLine(doc, --first);
    const auto section = SectionName(header);
    const bool variables = section.size() == 9 &&
        std::equal(section.begin(), section.end(), "variables",
            [](char c, char lower) { return c == lower || c == lower - ('a' - 'A'); });
    bool regexSubstitute = false;
    bool mixed = false;
    int knownState = 0;
    auto after = first;
    for (; doc->LineStart(after) < doc->Length(); ++after)
    {
        const auto value = GetLine(doc, after);
        if (after != first && !SectionName(value).empty()) break;
        if (!variables)
        {
            if (const auto flag = RegexSubstituteSetting(value)) regexSubstitute = *flag;
        }
        if (const int previous = doc->GetLineState(after))
        {
            mixed = mixed || (knownState != 0 && knownState != previous);
            knownState = previous;
        }
    }
    const int state = variables ? VariablesState : OptionsState | (regexSubstitute ? RegexSubstituteState : 0);
    return {first, after, state, mixed || (knownState != 0 && knownState != state)};
}

void StyleSection(IDocument* doc, SectionStyles section, Sci_Position line, Sci_Position end)
{
    doc->StartStyling(doc->LineStart(line));
    for (; line < section.after && doc->LineStart(line) < end; ++line)
    {
        const auto current = doc->LineStart(line);
        const auto next = doc->LineStart(line + 1);
        doc->SetLineState(line, section.state);
        auto styles = StyleLine(GetLine(doc, line), section.state == VariablesState,
            (section.state & RegexSubstituteState) != 0);
        for (auto& style : styles) style = static_cast<unsigned char>(ScintillaStyle(style));
        if (!styles.empty())
            doc->SetStyles(static_cast<Sci_Position>(styles.size()), reinterpret_cast<const char*>(styles.data()));
        const auto eol = next - current - static_cast<Sci_Position>(styles.size());
        if (eol > 0) doc->SetStyleFor(eol, Default);
    }
}

class Lexer final : public ILexer5 {
    int SCI_METHOD Version() const override { return lvRelease5; }
    void SCI_METHOD Release() override { delete this; }
    const char* SCI_METHOD PropertyNames() override { return ""; }
    int SCI_METHOD PropertyType(const char*) override { return SC_TYPE_BOOLEAN; }
    const char* SCI_METHOD DescribeProperty(const char*) override { return ""; }
    Sci_Position SCI_METHOD PropertySet(const char*, const char*) override { return -1; }
    const char* SCI_METHOD DescribeWordListSets() override { return ""; }
    Sci_Position SCI_METHOD WordListSet(int, const char*) override { return -1; }

    void SCI_METHOD Lex(Sci_PositionU start, Sci_Position size, int, IDocument* doc) override
    {
        if (size <= 0 || start >= static_cast<Sci_PositionU>(doc->Length())) return;
        const auto end = std::min(doc->Length(), static_cast<Sci_Position>(start) + size);
        auto line = doc->LineFromPosition(start);
        auto section = StylesForSection(doc, line);
        if (line > 0 && section.first == line)
        {
            // A new header can move the flag out of the preceding section.
            const auto previous = StylesForSection(doc, line - 1);
            if (previous.needsRefresh) StyleSection(doc, previous, previous.first, doc->LineStart(line));
        }
        for (;;)
        {
            // RegExpSubstitute may follow its Substitute pairs. If its value changed,
            // restyle the whole affected section, including lines already on screen.
            if (section.needsRefresh) StyleSection(doc, section, section.first, doc->LineStart(section.after));
            else StyleSection(doc, section, line, end);
            line = section.after;
            if (doc->LineStart(line) >= end) break;
            section = StylesForSection(doc, line);
        }
    }

    void SCI_METHOD Fold(Sci_PositionU start, Sci_Position size, int, IDocument* doc) override
    {
        if (size <= 0) return;
        const auto end = std::min(doc->Length(), static_cast<Sci_Position>(start) + size);
        for (auto line = doc->LineFromPosition(start); doc->LineStart(line) < end; ++line)
        {
            const bool header = !SectionName(GetLine(doc, line)).empty();
            const int level = SC_FOLDLEVELBASE | (header ? SC_FOLDLEVELHEADERFLAG : 1);
            doc->SetLevel(line, level);
            if (doc->LineStart(line + 1) <= doc->LineStart(line)) break;
        }
    }
    void* SCI_METHOD PrivateCall(int, void*) override { return nullptr; }
    int SCI_METHOD LineEndTypesSupported() override { return SC_LINE_END_TYPE_DEFAULT; }
    int SCI_METHOD AllocateSubStyles(int, int) override { return -1; }
    int SCI_METHOD SubStylesStart(int) override { return -1; }
    int SCI_METHOD SubStylesLength(int) override { return 0; }
    int SCI_METHOD StyleFromSubStyle(int style) override { return style; }
    int SCI_METHOD PrimaryStyleFromStyle(int style) override { return style; }
    void SCI_METHOD FreeSubStyles() override {}
    void SCI_METHOD SetIdentifiers(int, const char*) override {}
    int SCI_METHOD DistanceToSecondaryStyles() override { return 0; }
    const char* SCI_METHOD GetSubStyleBases() override { return ""; }
    int SCI_METHOD NamedStyles() override { return ScintillaStyle(StyleCount); }
    const char* SCI_METHOD NameOfStyle(int style) override
    {
        const int index = PaletteStyle(style);
        return index >= 0 && index < StyleCount ? Names[index] : "";
    }
    const char* SCI_METHOD TagsOfStyle(int) override { return ""; }
    const char* SCI_METHOD DescriptionOfStyle(int style) override { return NameOfStyle(style); }
    const char* SCI_METHOD GetName() override { return LexerName; }
    int SCI_METHOD GetIdentifier() override { return SCLEX_AUTOMATIC; }
    const char* SCI_METHOD PropertyGet(const char*) override { return ""; }
};

ILexer5* Factory() { return new Lexer; }
}

extern "C" int SCI_METHOD GetLexerCount() { return 1; }
extern "C" void SCI_METHOD GetLexerName(unsigned int, char* name, int length)
{
    if (length > 0) strncpy_s(name, length, LexerName, _TRUNCATE);
}
extern "C" void SCI_METHOD GetLexerStatusText(unsigned int, wchar_t* description, int length)
{
    if (length > 0) wcsncpy_s(description, length, L"Rainmeter 5 skin file", _TRUNCATE);
}
extern "C" auto SCI_METHOD GetLexerFactory(unsigned int index) -> ILexer5*(*)()
{
    return index == 0 ? Factory : nullptr;
}
extern "C" ILexer5* SCI_METHOD CreateLexer(const char* name)
{
    return name && std::strcmp(name, LexerName) == 0 ? Factory() : nullptr;
}
}

