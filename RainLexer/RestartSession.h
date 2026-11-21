#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

namespace RainLexer {
inline constexpr wchar_t RestartWindowClass[] = L"RainCodePP.RestartSession";
inline constexpr UINT SaveRestartSessionMessage = WM_APP + 1;
inline constexpr UINT ReloadLivePaletteMessage = WM_APP + 2;
inline constexpr wchar_t RestartSessionPrefix[] = L"RainCodePP-restart-";

inline std::filesystem::path RestartSessionPath(const std::filesystem::path& config, DWORD editor, DWORD settings)
{
    return (config / (RestartSessionPrefix + std::to_wstring(editor) + L"-" +
        std::to_wstring(settings) + L".xml")).lexically_normal();
}
}
