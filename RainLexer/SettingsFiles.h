// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).
#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace RainLexer {
inline constexpr wchar_t VocabularyFile[] = L"RainCodePP-vocabulary.txt";
inline constexpr wchar_t UninstallerFile[] = L"Uninstall RainCode++.exe";

inline std::filesystem::path ClassicBackup(const std::filesystem::path& config,
    const std::filesystem::path& plugin)
{
    constexpr wchar_t BackupDirectory[] = L"RainCodePP-backup";
    const auto profile = config / BackupDirectory;
    std::error_code error;
    if (std::filesystem::is_directory(profile / L"Original RainLexer", error)) return profile;
    const auto installation = plugin.parent_path() / BackupDirectory;
    if (std::filesystem::is_directory(installation / L"Original RainLexer", error)) return installation;
    return {};
}

inline std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}

