// Copyright (c) Jeffrey Morley. Licensed under GNU GPL v2.
#include "HelpPanel.h"
#include <Richedit.h>

#include <string>

namespace RainSyntax {
namespace {
using Text = std::wstring_view;
enum class Ink { Body, Title, Heading, Bold, Code };

std::wstring FromUtf8(std::string_view text)
{
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) return L"The manual must use UTF-8 encoding.";
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

class MarkdownWriter {
public:
    MarkdownWriter(HWND edit, bool dark) : view(edit), night(dark) {}

    void TextRun(Text value, Ink ink) const
    {
        if (value.empty()) return;
        CHARFORMAT2W font{};
        font.cbSize = sizeof(font);
        font.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_BACKCOLOR;
        font.dwEffects = ink == Ink::Title || ink == Ink::Heading || ink == Ink::Bold ? CFE_BOLD : 0;
        font.yHeight = ink == Ink::Title ? 370 : ink == Ink::Heading ? 275 : 205;
        font.crTextColor = night ? RGB(230, 235, 244) : RGB(37, 45, 58);
        font.dwEffects |= CFE_AUTOBACKCOLOR;
        if (ink == Ink::Title || ink == Ink::Heading)
            font.crTextColor = night ? RGB(164, 207, 255) : RGB(29, 73, 123);
        if (ink == Ink::Code)
        {
            font.dwEffects &= ~CFE_AUTOBACKCOLOR;
            font.crTextColor = night ? RGB(173, 232, 208) : RGB(38, 94, 83);
            font.crBackColor = night ? RGB(43, 55, 66) : RGB(234, 240, 245);
        }
        wcscpy_s(font.szFaceName, ink == Ink::Code ? L"Consolas" : L"Segoe UI");
        SendMessageW(view, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&font));
        const std::wstring terminated(value);
        SendMessageW(view, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(terminated.c_str()));
    }

    void Inline(Text line, Ink base) const
    {
        while (!line.empty())
        {
            const auto at = line.find_first_of(L"`*");
            TextRun(line.substr(0, at), base);
            if (at == Text::npos) break;
            line.remove_prefix(at);
            const Text marker = line.starts_with(L"`") ? L"`" : line.starts_with(L"**") ? L"**" : L"";
            const auto close = marker.empty() ? Text::npos : line.find(marker, marker.size());
            if (close == Text::npos)
            {
                TextRun(line.substr(0, 1), base);
                line.remove_prefix(1);
            }
            else
            {
                TextRun(line.substr(marker.size(), close - marker.size()), marker == L"`" ? Ink::Code : Ink::Bold);
                line.remove_prefix(close + marker.size());
            }
        }
    }

private:
    HWND view;
    bool night;
};
}

void RenderMarkdown(HWND edit, std::string_view markdown, bool dark)
{
    const auto text = FromUtf8(markdown);
    const MarkdownWriter writer(edit, dark);
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
    SendMessageW(edit, EM_SETBKGNDCOLOR, 0, dark ? RGB(30, 34, 42) : RGB(249, 251, 253));
    SetWindowTextW(edit, L"");
    bool fenced = false;
    Text rest = text;
    while (!rest.empty())
    {
        const auto end = rest.find(L'\n');
        auto line = rest.substr(0, end);
        rest.remove_prefix(end == Text::npos ? rest.size() : end + 1);
        if (line.ends_with(L'\r')) line.remove_suffix(1);
        if (line.starts_with(L"```"))
        {
            fenced = !fenced;
            continue;
        }
        if (fenced)
        {
            writer.TextRun(line, Ink::Code);
        }
        else if (line.starts_with(L"# "))
        {
            writer.Inline(line.substr(2), Ink::Title);
        }
        else if (line.starts_with(L"## ") || line.starts_with(L"### "))
        {
            writer.Inline(line.substr(line.find(L' ') + 1), Ink::Heading);
        }
        else
        {
            if (line.starts_with(L"- "))
            {
                writer.TextRun(L"\u2022  ", Ink::Body);
                line.remove_prefix(2);
            }
            writer.Inline(line, Ink::Body);
        }
        writer.TextRun(L"\r\n", Ink::Body);
    }
    SendMessageW(edit, EM_SETSEL, 0, 0);
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, nullptr, TRUE);
}
}

