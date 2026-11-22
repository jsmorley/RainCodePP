// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).
#pragma once
#include <Windows.h>
#include <commctrl.h>
#include <initializer_list>

namespace RainSyntax {
struct ButtonTip {
    int id;
    const wchar_t* text;
};

inline void AddButtonTooltips(HWND dialog, std::initializer_list<ButtonTip> tips)
{
    const HWND tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        dialog, nullptr, nullptr, nullptr);
    if (!tooltip) return;
    RECT width{0, 0, 180, 0};
    MapDialogRect(dialog, &width);
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, width.right);
    for (const auto& tip : tips)
    {
        const HWND button = GetDlgItem(dialog, tip.id);
        if (!button) continue;
        TTTOOLINFOW tool{TTTOOLINFOW_V2_SIZE};
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = dialog;
        tool.uId = reinterpret_cast<UINT_PTR>(button);
        tool.lpszText = const_cast<wchar_t*>(tip.text);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}
}
