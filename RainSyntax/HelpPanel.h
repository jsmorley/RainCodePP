// Copyright (c) Jeffrey Morley. Licensed under GNU GPL v2.
#pragma once
#include <Windows.h>
#include <string_view>

namespace RainSyntax {
void RenderMarkdown(HWND edit, std::string_view markdown, bool dark);
}

