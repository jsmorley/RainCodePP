// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).
#pragma once

#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <vector>

namespace RainSyntax {

inline constexpr char LexerName[] = "Rainmeter5";

enum Style : unsigned char {
    Default, Comment, Section, Option, Equals, InvalidValue, Value, Bang,
    BuiltInVariable, UserVariable, Number, DeprecatedOption, DeprecatedValue,
    DeprecatedBang, Character, Pipe, SectionVariable, DollarVariable,
    BangBracket, Argument, String, Operator, Function, Selector,
    EnvironmentVariable, Escaped, Format, Directive, Reference, Color,
    Red, Green, Blue, AlphaChannel, RelativeStart, RelativeEnd,
    RegularExpression, Url, InvisibleUpdate, StyleCount
};

// Scintilla reserves styles 32 through 39 for the editor.
constexpr int ScintillaStyle(int style) { return style < Red ? style : style - Red + 40; }
constexpr int PaletteStyle(int id) { return id < Red ? id : id >= 40 ? id - 40 + Red : -1; }

struct EditingTerms {
    size_t start = 0;
    size_t end = 0;
    std::vector<std::string> candidates;
    std::string canonical;
    std::string typo;
};

// Styles are indexed by byte, matching Scintilla positions for every supported encoding.
std::vector<unsigned char> StyleLine(std::string_view line, bool variablesSection, bool regexSubstitute = false);
struct SyntaxDetails {
    std::vector<Style> styles;
    const wchar_t* vocabulary = nullptr;
};
SyntaxDetails InspectSyntax(std::string_view line, size_t start, size_t end,
    bool variablesSection, bool regexSubstitute = false);
std::string_view SectionName(std::string_view line);
std::optional<bool> RegexSubstituteSetting(std::string_view line);
EditingTerms TermsAt(std::string_view line, size_t caret, bool variablesSection);
struct VocabularyGroup {
    const wchar_t* label;
    std::string_view defaults;
};
std::span<const VocabularyGroup> VocabularyGroups();
std::vector<std::string> ReadVocabulary(std::string_view overrides);
std::string WriteVocabulary(const std::vector<std::string>& groups);
void ConfigureVocabulary(std::string_view overrides);
}

