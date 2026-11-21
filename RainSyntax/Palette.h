// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).
#pragma once
#include <Windows.h>
#include <msxml6.h>
#include <wrl/client.h>
#include <comutil.h>

#include "Syntax.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace RainSyntax {
inline constexpr wchar_t StyleFile[] = L"RainSyntax.xml";
inline constexpr wchar_t LivePaletteFile[] = L"RainSyntax-palette.xml";
inline constexpr wchar_t StyleXPath[] = L"/NotepadPlus/LexerStyles/LexerType[@name='Rainmeter5']/WordsStyle";

using Microsoft::WRL::ComPtr;

struct StyleSettings {
    std::wstring name, family, size, colorStyle;
    COLORREF foreground = RGB(0, 0, 0);
    COLORREF background = RGB(255, 255, 255);
    unsigned emphasis = 0;
};
using Palette = std::array<StyleSettings, StyleCount>;

inline std::string Utf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    if (count) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}

inline std::optional<COLORREF> ParseHexColor(std::wstring_view value)
{
    if (value.size() != 6 || !std::all_of(value.begin(), value.end(), [](wchar_t ch) { return !!iswxdigit(ch); }))
        return std::nullopt;
    const auto rgb = wcstoul(std::wstring(value).c_str(), nullptr, 16);
    return RGB(rgb >> 16, rgb >> 8, rgb);
}

inline COLORREF ColorFrom(std::wstring_view value, COLORREF fallback)
{
    return ParseHexColor(value).value_or(fallback);
}

inline ComPtr<IXMLDOMDocument2> OpenXml(const std::filesystem::path& path)
{
    ComPtr<IXMLDOMDocument2> document;
    CLSID classId{};
    if (FAILED(CLSIDFromProgID(L"Msxml2.DOMDocument.6.0", &classId)) ||
        FAILED(CoCreateInstance(classId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&document))))
        throw std::runtime_error("Windows XML support is unavailable.");
    document->put_async(VARIANT_FALSE);
    document->put_resolveExternals(VARIANT_FALSE);
    document->put_validateOnParse(VARIANT_FALSE);
    document->put_preserveWhiteSpace(VARIANT_TRUE);
    VARIANT_BOOL loaded = VARIANT_FALSE;
    if (FAILED(document->load(_variant_t(path.c_str()), &loaded)) || !loaded)
        throw std::runtime_error("Could not read the RainSyntax palette XML.");
    return document;
}

inline std::wstring Attribute(IXMLDOMElement* element, const wchar_t* name)
{
    _variant_t value;
    if (FAILED(element->getAttribute(_bstr_t(name), &value)) || value.vt != VT_BSTR || !value.bstrVal)
        return {};
    return value.bstrVal;
}

template<typename Callback>
std::array<bool, StyleCount> ForStyles(IXMLDOMDocument2* document, Callback callback)
{
    ComPtr<IXMLDOMNodeList> nodes;
    if (FAILED(document->selectNodes(_bstr_t(StyleXPath), &nodes)) || !nodes)
        throw std::runtime_error("This file does not contain RainSyntax styles.");
    std::array<bool, StyleCount> seen{};
    for (;;)
    {
        ComPtr<IXMLDOMNode> node;
        if (nodes->nextNode(&node) != S_OK || !node) break;
        ComPtr<IXMLDOMElement> style;
        if (FAILED(node.As(&style))) continue;
        const auto raw = Attribute(style.Get(), L"styleID");
        wchar_t* tail = nullptr;
        const auto styleId = wcstoul(raw.c_str(), &tail, 10);
        if (raw.empty() || *tail || styleId >= static_cast<unsigned long>(ScintillaStyle(StyleCount))) continue;
        const auto id = PaletteStyle(static_cast<int>(styleId));
        if (id < 0) continue;
        if (seen[id]) throw std::runtime_error("The palette contains a duplicate style.");
        seen[id] = true;
        callback(id, style.Get());
    }
    if (std::find(seen.begin(), seen.begin() + Red, false) != seen.begin() + Red)
        throw std::runtime_error("This palette is missing one or more RainSyntax styles.");
    return seen;
}

inline bool IsDark(COLORREF background)
{
    const int brightness = 2126 * GetRValue(background) + 7152 * GetGValue(background) + 722 * GetBValue(background);
    return brightness < 1280000;
}

inline Palette ReadPalette(const std::filesystem::path& path)
{
    const auto xml = OpenXml(path);
    Palette palette;
    const auto seen = ForStyles(xml.Get(), [&](size_t i, IXMLDOMElement* element) {
        auto& style = palette[i];
        style.name = Attribute(element, L"name");
        style.family = Attribute(element, L"fontName");
        style.size = Attribute(element, L"fontSize");
        style.colorStyle = Attribute(element, L"colorStyle");
        style.foreground = ColorFrom(Attribute(element, L"fgColor"), RGB(0, 0, 0));
        style.background = ColorFrom(Attribute(element, L"bgColor"), RGB(255, 255, 255));
        style.emphasis = wcstoul(Attribute(element, L"fontStyle").c_str(), nullptr, 10) & 7;
    });
    constexpr struct {
        const wchar_t* name;
        const wchar_t* light;
        const wchar_t* dark;
        unsigned emphasis;
    } addedStyles[] = {
        {L"Red Channel", L"A02B40", L"FFA3AA", 0},
        {L"Green Channel", L"236837", L"8BDAA0", 0},
        {L"Blue Channel", L"215D9A", L"9BC8FF", 0},
        {L"Alpha Channel", L"6E4789", L"D8AAF5", 0},
        {L"Relative Start (r)", L"9B4A23", L"F9BA86", 1},
        {L"Relative End (R)", L"075F75", L"77DCD6", 1},
        {L"Regular Expression", L"71358D", L"E0B6FF", 0},
        {L"URL", L"095C94", L"90D1FF", 0},
    };
    static_assert(std::size(addedStyles) == StyleCount - Red);
    const auto& page = palette[Default];
    const bool dark = IsDark(page.background);
    for (size_t i = Red; i < palette.size(); ++i)
    {
        if (seen[i]) continue;
        palette[i] = page;
        const auto& defaults = addedStyles[i - Red];
        palette[i].name = defaults.name;
        palette[i].foreground = ColorFrom(dark ? defaults.dark : defaults.light, page.foreground);
        palette[i].emphasis = defaults.emphasis;
        palette[i].colorStyle.clear();
    }
    return palette;
}

}
