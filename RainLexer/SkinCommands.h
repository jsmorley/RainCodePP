// Copyright (c) Jeffrey Morley and the Rainmeter Team. Licensed under GNU GPL v2.
#pragma once
#include <Windows.h>

namespace RainLexer {
enum class SkinAction { RefreshSkin, RefreshAll, Restart };
void SendSkinAction(HWND owner, HMODULE module, SkinAction action);
}

