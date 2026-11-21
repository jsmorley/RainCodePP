// Copyright (c) Jeffrey Morley and the Rainmeter Team. Licensed under GNU GPL v2.
#include "SkinCommands.h"
#include "Notepad_plus_msgs.h"
#include "Version.h"

#include <array>
#include <filesystem>
#include <string>

namespace RainLexer {
namespace {
constexpr UINT QueryRainmeter = WM_APP + 1000;
constexpr ULONG_PTR QuerySkinsPath = 4101;
constexpr ULONG_PTR QuerySkinWindow = 5101;
constexpr UINT ReplyTimeout = 1500;

struct SkinsPath {
    HWND tray;
    std::wstring path;
};

LRESULT CALLBACK ReadSkinsPath(HWND window, UINT message, WPARAM sender, LPARAM data)
{
    if (message != WM_COPYDATA) return DefWindowProcW(window, message, sender, data);
    auto* request = reinterpret_cast<SkinsPath*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    const auto* reply = reinterpret_cast<COPYDATASTRUCT*>(data);
    if (!request || reinterpret_cast<HWND>(sender) != request->tray || !reply ||
        reply->dwData != QuerySkinsPath || !reply->lpData || reply->cbData < sizeof(wchar_t) ||
        reply->cbData % sizeof(wchar_t) != 0 || reply->cbData > 32768 * sizeof(wchar_t)) return FALSE;

    const auto* path = static_cast<const wchar_t*>(reply->lpData);
    const auto count = reply->cbData / sizeof(wchar_t);
    if (path[count - 1] != L'\0') return FALSE;
    request->path.assign(path, wcsnlen_s(path, count));
    return TRUE;
}

std::filesystem::path ConfigFolder(std::filesystem::path file, std::filesystem::path root)
{
    constexpr wchar_t IniExtension[] = L".ini";
    if (_wcsicmp(file.extension().c_str(), IniExtension) != 0 || root.empty()) return {};
    file = file.lexically_normal().parent_path();
    root = root.lexically_normal();
    if (!root.has_filename()) root = root.parent_path();
    auto part = file.begin();
    for (const auto& component : root)
    {
        if (part == file.end() || _wcsicmp(component.c_str(), part->c_str()) != 0) return {};
        ++part;
    }
    std::filesystem::path folder;
    for (; part != file.end(); ++part) folder /= *part;
    return folder;
}

bool CopyData(HWND target, HWND sender, ULONG_PTR type, const std::wstring& value, DWORD_PTR& result)
{
    COPYDATASTRUCT data{type, static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)),
        const_cast<wchar_t*>(value.c_str())};
    return SendMessageTimeoutW(target, WM_COPYDATA, reinterpret_cast<WPARAM>(sender),
        reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, ReplyTimeout, &result) != 0;
}

HWND FindActiveSkin(HWND owner, HMODULE module, HWND controller, const wchar_t*& error)
{
    std::array<wchar_t, 32768> path{};
    if (!SendMessageW(owner, NPPM_GETFULLCURRENTPATH, path.size(), reinterpret_cast<LPARAM>(path.data())) ||
        _wcsicmp(std::filesystem::path(path.data()).extension().c_str(), L".ini") != 0)
    {
        error = L"Open a Rainmeter skin INI file to refresh that skin.";
        return nullptr;
    }
    const HWND tray = FindWindowW(L"RainmeterTrayClass", nullptr);
    DWORD controllerPid = 0, trayPid = 0;
    GetWindowThreadProcessId(controller, &controllerPid);
    if (!tray || !GetWindowThreadProcessId(tray, &trayPid) || trayPid != controllerPid)
    {
        error = L"Could not locate the Rainmeter tray window.";
        return nullptr;
    }
    const HWND receiver = CreateWindowExW(0, L"STATIC", nullptr, WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, module, nullptr);
    if (!receiver) return nullptr;
    SkinsPath request{tray, {}};
    SetWindowLongPtrW(receiver, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&request));
    SetWindowLongPtrW(receiver, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ReadSkinsPath));
    DWORD_PTR result = 0;
    SendMessageTimeoutW(tray, QueryRainmeter, QuerySkinsPath, reinterpret_cast<LPARAM>(receiver),
        SMTO_ABORTIFHUNG, ReplyTimeout, &result);
    DestroyWindow(receiver);
    if (request.path.empty())
    {
        error = L"Rainmeter did not provide its Skins folder.";
        return nullptr;
    }
    const auto folder = ConfigFolder(path.data(), request.path);
    if (folder.empty())
    {
        error = L"The current INI file is outside Rainmeter's Skins folder.";
        return nullptr;
    }
    if (!CopyData(tray, owner, QuerySkinWindow, folder.wstring(), result)) return nullptr;
    const auto skin = reinterpret_cast<HWND>(result);
    DWORD skinPid = 0;
    if (!skin || !GetWindowThreadProcessId(skin, &skinPid) || skinPid != controllerPid)
    {
        error = L"Rainmeter has no active skin for this INI file's folder.";
        return nullptr;
    }
    return skin;
}
}

void SendSkinAction(HWND owner, HMODULE module, SkinAction action)
{
    const HWND controller = FindWindowW(L"DummyRainWClass", L"Rainmeter control window");
    const wchar_t* error = L"Rainmeter did not respond to the request.";
    if (!controller) error = L"Rainmeter is not running.";
    else
    {
        HWND target = controller;
        const wchar_t* command = action == SkinAction::Restart ? L"!Restart" : L"!Refresh *";
        if (action == SkinAction::RefreshSkin)
        {
            target = FindActiveSkin(owner, module, controller, error);
            command = L"!Refresh";
        }
        DWORD_PTR result = 0;
        if (target && CopyData(target, owner, 1, command, result)) return;
    }
    MessageBoxW(owner, error, RAINLEXER_TITLE, MB_ICONINFORMATION | MB_OK);
}
}

