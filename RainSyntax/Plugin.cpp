// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).
#include <Windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <Richedit.h>
#include "PluginInterface.h"
#include "menuCmdID.h"
#include "resource.h"
#include "Syntax.h"
#include "SettingsFiles.h"
#include "Palette.h"
#include "RestartSession.h"
#include "SkinCommands.h"
#include "HelpPanel.h"
#include "Version.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {
using namespace RainSyntax;
NppData npp{};
std::wstring settingsPath;
std::filesystem::path configPath;
bool automaticCompletion = true;
bool caseCorrection = true;
bool typoCorrection = true;
bool editing = false;
bool ready = false;
UINT_PTR backgroundTimer = 0;
std::optional<Palette> livePalette;
HWND panel = nullptr;
HWND restartWindow = nullptr;
HWND helpPanel = nullptr, helpText = nullptr;
enum Menu { CompleteTerm, Automatic, Capitalization, Typos, SettingsSidebar, SettingsApp, HelpItem, AboutPlugin };
constexpr wchar_t SettingsSection[] = L"Typing";
constexpr wchar_t CompletionKey[] = L"AutomaticCompletion";
constexpr wchar_t CorrectionKey[] = L"CorrectCapitalization";
constexpr wchar_t TypoKey[] = L"CorrectObviousTypos";

struct CorrectedTerm {
    LRESULT start, end;
    std::string canonical;
};
std::unordered_map<LRESULT, std::vector<CorrectedTerm>> capitalization;

LRESULT Editor(HWND view, UINT message, WPARAM w = 0, LPARAM l = 0)
{
    return SendMessage(view, message, w, l);
}

HWND ActiveView()
{
    int view = -1;
    SendMessage(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&view));
    return view == 0 ? npp._scintillaMainHandle : view == 1 ? npp._scintillaSecondHandle : nullptr;
}

LRESULT ActiveBuffer()
{
    return SendMessage(npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
}

void TrackEdits(const SCNotification& change)
{
    if (!(change.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) return;
    if (change.nmhdr.hwndFrom != ActiveView()) return;
    const auto found = capitalization.find(ActiveBuffer());
    if (found == capitalization.end()) return;
    auto& marks = found->second;
    const auto pos = change.position;
    const auto size = change.length;
    if (change.modificationType & SC_MOD_INSERTTEXT)
    {
        for (auto& mark : marks)
        {
            if (pos < mark.start || (pos == mark.start && mark.start != mark.end))
            {
                mark.start += size;
                mark.end += size;
            }
            else if (pos < mark.end || (pos == mark.start && mark.start == mark.end)) mark.end += size;
        }
    }
    else
    {
        std::erase_if(marks, [&](const auto& mark) {
            return pos <= mark.start && pos + size >= mark.end &&
                (pos < mark.start || pos + size > mark.end);
        });
        const auto move = [&](LRESULT offset) {
            return offset <= pos ? offset : std::max<LRESULT>(pos, offset - size);
        };
        for (auto& mark : marks)
        {
            mark.start = move(mark.start);
            mark.end = move(mark.end);
        }
    }
}

bool IsRainmeter(HWND view)
{
    if (!view || (view != npp._scintillaMainHandle && view != npp._scintillaSecondHandle)) return false;
    const auto length = Editor(view, SCI_GETLEXERLANGUAGE);
    if (length != sizeof(LexerName) - 1) return false;
    char language[sizeof(LexerName)]{};
    Editor(view, SCI_GETLEXERLANGUAGE, 0, reinterpret_cast<LPARAM>(language));
    return std::strcmp(language, LexerName) == 0;
}

bool ReloadLivePalette()
{
    if (configPath.empty()) return false;
    const auto path = configPath / LivePaletteFile;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
    bool loaded = false;
    try
    {
        livePalette = ReadPalette(path);
        loaded = true;
    }
    catch (const std::exception&) {}
    if (SUCCEEDED(initialized)) CoUninitialize();
    return loaded;
}

void ApplyLiveStyles(HWND view, LRESULT baseBackground)
{
    const auto baseForeground = Editor(view, SCI_STYLEGETFORE, STYLE_DEFAULT);
    const auto baseSize = Editor(view, SCI_STYLEGETSIZE, STYLE_DEFAULT);
    std::string baseFont(static_cast<size_t>(Editor(view, SCI_STYLEGETFONT, STYLE_DEFAULT)) + 1, '\0');
    Editor(view, SCI_STYLEGETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>(baseFont.data()));
    for (int i = 0; i < StyleCount; ++i)
    {
        const auto& style = (*livePalette)[i];
        const auto id = ScintillaStyle(i);
        const unsigned colors = style.colorStyle.empty() ? 3 : _wtoi(style.colorStyle.c_str());
        const auto font = style.family.empty() ? baseFont : Utf8(style.family);
        const int size = _wtoi(style.size.c_str());
        Editor(view, SCI_STYLESETFORE, id, (colors & 1) ? style.foreground : baseForeground);
        Editor(view, SCI_STYLESETBACK, id, (colors & 2) ? style.background : baseBackground);
        Editor(view, SCI_STYLESETFONT, id, reinterpret_cast<LPARAM>(font.c_str()));
        Editor(view, SCI_STYLESETSIZE, id, size > 0 ? size : baseSize);
        Editor(view, SCI_STYLESETBOLD, id, !!(style.emphasis & 1));
        Editor(view, SCI_STYLESETITALIC, id, !!(style.emphasis & 2));
        Editor(view, SCI_STYLESETUNDERLINE, id, !!(style.emphasis & 4));
    }
}

void CALLBACK SyncPageBackground(HWND, UINT, UINT_PTR timer, DWORD)
{
    KillTimer(nullptr, timer);
    backgroundTimer = 0;
    struct Canvas {
        LRESULT base = -1;
        LRESULT applied = -1;
    };
    static Canvas canvases[2];
    const HWND views[] = {npp._scintillaMainHandle, npp._scintillaSecondHandle};
    for (size_t i = 0; i < std::size(views); ++i)
    {
        const auto view = views[i];
        auto& canvas = canvases[i];
        if (!IsRainmeter(view)) { canvas = {}; continue; }
        const auto current = Editor(view, SCI_STYLEGETBACK, STYLE_DEFAULT);
        if (current != canvas.applied) canvas.base = current;
        if (livePalette) ApplyLiveStyles(view, canvas.base);
        const auto background = Editor(view, SCI_STYLEGETBACK, Default);
        if (current != background)
            Editor(view, SCI_STYLESETBACK, STYLE_DEFAULT, background);
        canvas.applied = background;
    }
}

void QueuePageBackground()
{
    // Notepad++ announces an external lexer before it applies the language's styles.
    // Let that finish before reading style 0 and setting Scintilla's canvas color.
    if (!backgroundTimer)
        backgroundTimer = SetTimer(nullptr, 0, USER_TIMER_MINIMUM, SyncPageBackground);
}

struct Line {
    std::string text;
    LRESULT start = 0;
};
Line ReadLine(HWND view, LRESULT line)
{
    Line result;
    result.start = Editor(view, SCI_POSITIONFROMLINE, line);
    const auto end = Editor(view, SCI_GETLINEENDPOSITION, line);
    result.text.resize(static_cast<size_t>(Editor(view, SCI_LINELENGTH, line)) + 1);
    Editor(view, SCI_GETLINE, line, reinterpret_cast<LPARAM>(result.text.data()));
    result.text.resize(static_cast<size_t>(end - result.start));
    return result;
}

bool InVariables(HWND view, LRESULT line)
{
    while (--line >= 0)
    {
        // A nonzero state comes from the lexer. Fall back to scanning when styling is pending.
        const auto state = Editor(view, SCI_GETLINESTATE, line);
        if (state != 0) return state == 1;
        const auto current = ReadLine(view, line);
        const auto name = SectionName(current.text);
        if (!name.empty()) return name.size() == 9 && _strnicmp(name.data(), "variables", 9) == 0;
    }
    return false;
}

bool CanEdit(HWND view)
{
    return IsRainmeter(view) && !Editor(view, SCI_GETREADONLY) &&
        Editor(view, SCI_GETSELECTIONS) == 1 && Editor(view, SCI_GETSELECTIONEMPTY) &&
        !Editor(view, SCI_AUTOCACTIVE);
}

struct CompletionPopup {
    LRESULT ignoreCase = 0;
    bool active = false;
};
std::array<CompletionPopup, 2> completionPopups{};

CompletionPopup& PopupFor(HWND view)
{
    return completionPopups[view == npp._scintillaSecondHandle ? 1 : 0];
}

void RestorePopup(HWND view)
{
    if (view != npp._scintillaMainHandle && view != npp._scintillaSecondHandle) return;
    auto& popup = PopupFor(view);
    if (popup.active)
    {
        popup.active = false;
        Editor(view, SCI_AUTOCSETIGNORECASE, popup.ignoreCase);
    }
}

void Complete(HWND view, bool automatic)
{
    if (!view) return;
    auto& popup = PopupFor(view);
    if (popup.active)
    {
        // Refresh our list so subsequent characters narrow the visible candidates.
        if (Editor(view, SCI_AUTOCACTIVE)) Editor(view, SCI_AUTOCCANCEL);
        RestorePopup(view);
    }
    if (!CanEdit(view)) return;
    const auto position = Editor(view, SCI_GETCURRENTPOS);
    const auto line = Editor(view, SCI_LINEFROMPOSITION, position);
    const auto current = ReadLine(view, line);
    const auto caret = static_cast<size_t>(position - current.start);
    if (caret > current.text.size()) return;
    const auto terms = TermsAt(current.text, caret, InVariables(view, line));
    if (terms.candidates.empty() || terms.end != caret ||
        (automatic && (caret - terms.start < 2 || (!terms.canonical.empty() && terms.candidates.size() == 1)))) return;
    const auto separator = static_cast<char>(Editor(view, SCI_AUTOCGETSEPARATOR));
    const auto typeSeparator = static_cast<char>(Editor(view, SCI_AUTOCGETTYPESEPARATOR));
    std::string suggestions;
    for (const auto& candidate : terms.candidates)
    {
        if (candidate.find(separator) != std::string::npos || candidate.find(typeSeparator) != std::string::npos) continue;
        if (!suggestions.empty()) suggestions += separator;
        suggestions += candidate;
    }
    if (suggestions.empty()) return;
    popup.ignoreCase = Editor(view, SCI_AUTOCGETIGNORECASE);
    popup.active = true;
    Editor(view, SCI_AUTOCSETIGNORECASE, TRUE);
    Editor(view, SCI_AUTOCSHOW, caret - terms.start, reinterpret_cast<LPARAM>(suggestions.c_str()));
    if (!Editor(view, SCI_AUTOCACTIVE)) RestorePopup(view);
}

void Correct(HWND view, char delimiter)
{
    if (!CanEdit(view)) return;
    const auto position = Editor(view, SCI_GETCURRENTPOS);
    if (position <= 0) return;
    const bool enter = delimiter == '\r' || delimiter == '\n';
    const auto line = Editor(view, SCI_LINEFROMPOSITION, position) - (enter ? 1 : 0);
    if (line < 0) return;
    const auto current = ReadLine(view, line);
    const auto caret = enter ? current.text.size() : static_cast<size_t>(position - current.start - 1);
    if (caret > current.text.size() || (!enter && (caret == current.text.size() || current.text[caret] != delimiter))) return;
    const auto terms = TermsAt(current.text, caret, InVariables(view, line));
    const std::string_view replacement = terms.canonical.empty() && typoCorrection ? terms.typo :
        caseCorrection ? terms.canonical : std::string_view();
    if (replacement.empty() || terms.end != caret || caret <= terms.start ||
        current.text.compare(terms.start, caret - terms.start, replacement) == 0) return;

    const auto start = current.start + static_cast<LRESULT>(terms.start);
    auto* marks = terms.canonical.empty() ? nullptr : &capitalization[ActiveBuffer()];
    if (marks && std::any_of(marks->begin(), marks->end(), [&](const auto& mark) {
        return mark.start == start && mark.canonical == terms.canonical;
    })) return;
    editing = true;
    Editor(view, SCI_BEGINUNDOACTION);
    Editor(view, SCI_SETTARGETRANGE, start, current.start + caret);
    const auto written = Editor(view, SCI_REPLACETARGET, replacement.size(), reinterpret_cast<LPARAM>(replacement.data()));
    Editor(view, SCI_ENDUNDOACTION);
    editing = false;
    if (marks && written > 0) marks->push_back({start, start + written, terms.canonical});
}

void ManualCompletion() { Complete(ActiveView(), false); }
void ToggleCompletion();
void ToggleCorrection();
void ToggleTypos();
void TogglePanel();
void OpenSettings();
void OpenBackup();
void Uninstall();
void OpenHelp();
HMODULE PluginModule();
void About()
{
    MessageBoxW(npp._nppHandle, RAINSYNTAX_TITLE L"\nCopyright \u00A9 2026 Jeffrey Morley\n\n"
        L"Rainmeter 5 syntax highlighting and typing assistance for Notepad++.\n"
        L"Based on RainLexer by the Rainmeter Team.", RAINSYNTAX_TITLE, MB_OK | MB_ICONINFORMATION);
}
ShortcutKey completionShortcut{true, true, false, VK_SPACE};
FuncItem functions[] = {
    {L"Complete Rainmeter term", ManualCompletion, 0, false, &completionShortcut},
    {L"Automatic completion", ToggleCompletion, 0, false, nullptr},
    {L"Correct capitalization", ToggleCorrection, 0, false, nullptr},
    {L"Correct obvious typos", ToggleTypos, 0, false, nullptr},
    {L"Settings sidebar", TogglePanel, 0, false, nullptr},
    {L"Edit vocabulary and palettes...", OpenSettings, 0, false, nullptr},
    {L"Help", OpenHelp, 0, false, nullptr},
    {L"About RainSyntax", About, 0, false, nullptr}
};

void UpdateCheck(size_t item, bool enabled)
{
    SendMessage(npp._nppHandle, NPPM_SETMENUITEMCHECK, functions[item]._cmdID, enabled);
}

void SyncPanel(HWND window)
{
    if (!window) return;
    CheckDlgButton(window, IDC_COMPLETION, automaticCompletion ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_CORRECTION, caseCorrection ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_TYPOS, typoCorrection ? BST_CHECKED : BST_UNCHECKED);
}

void Toggle(size_t item, bool& setting, const wchar_t* key)
{
    setting = !setting;
    UpdateCheck(item, setting);
    SyncPanel(panel);
    if (!settingsPath.empty())
        WritePrivateProfileStringW(SettingsSection, key, setting ? L"1" : L"0", settingsPath.c_str());
}
void ToggleCompletion() { Toggle(Automatic, automaticCompletion, CompletionKey); }
void ToggleCorrection() { Toggle(Capitalization, caseCorrection, CorrectionKey); }
void ToggleTypos() { Toggle(Typos, typoCorrection, TypoKey); }

INT_PTR CALLBACK PanelDialog(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        SyncPanel(window);
        const HWND link = GetDlgItem(window, IDC_STYLE);
        RECT bounds{};
        if (GetWindowRect(link, &bounds))
        {
            const long width = bounds.right - bounds.left;
            SIZE ideal{};
            if (SendMessageW(link, LM_GETIDEALSIZE, width, reinterpret_cast<LPARAM>(&ideal)) &&
                ideal.cx > 0 && ideal.cx < width)
            {
                POINT origin{bounds.left, bounds.top};
                ScreenToClient(window, &origin);
                const long offset = (width - ideal.cx) / 2;
                SetWindowPos(link, nullptr, origin.x + offset, origin.y, width - offset,
                    bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return TRUE;
    }
    case WM_NOTIFY:
    {
        const auto* notification = reinterpret_cast<const NMHDR*>(lParam);
        if (notification->idFrom == IDC_STYLE &&
            (notification->code == NM_CLICK || notification->code == NM_RETURN))
        {
            SendMessage(npp._nppHandle, NPPM_MENUCOMMAND, 0, IDM_LANGSTYLE_CONFIG_DLG);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_COMPLETION: ToggleCompletion(); return TRUE;
        case IDC_CORRECTION: ToggleCorrection(); return TRUE;
        case IDC_TYPOS: ToggleTypos(); return TRUE;
        case IDC_EDIT_SETTINGS: OpenSettings(); return TRUE;
        case IDC_UNINSTALL: Uninstall(); return TRUE;
        case IDC_BACKUP: OpenBackup(); return TRUE;
        case IDC_REFRESH_SKIN: SendSkinAction(npp._nppHandle, PluginModule(), SkinAction::RefreshSkin); return TRUE;
        case IDC_REFRESH_ALL: SendSkinAction(npp._nppHandle, PluginModule(), SkinAction::RefreshAll); return TRUE;
        case IDC_RESTART_RAINMETER: SendSkinAction(npp._nppHandle, PluginModule(), SkinAction::Restart); return TRUE;
        case IDC_ABOUT: About(); return TRUE;
        case IDC_SHOW_MANUAL: OpenHelp(); return TRUE;
        }
        break;
    case WM_NCDESTROY:
        panel = nullptr;
        break;
    }
    return FALSE;
}

// Layout of Notepad++'s public DockedWidgetData interface.
struct DockPanel {
    HWND window;
    const wchar_t* name;
    int dialogId;
    UINT flags;
    HICON tabIcon;
    const wchar_t* details;
    RECT floatingBounds;
    int previousContainer;
    const wchar_t* moduleName;
};

HMODULE PluginModule()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&PluginModule), &module);
    return module;
}

LRESULT CALLBACK RestartSessionWindow(HWND window, UINT message, WPARAM sender, LPARAM data)
{
    if (message == ReloadLivePaletteMessage)
    {
        if (!ReloadLivePalette()) return FALSE;
        QueuePageBackground();
        return TRUE;
    }
    if (message != SaveRestartSessionMessage) return DefWindowProcW(window, message, sender, data);
    DWORD settingsPid = 0;
    if (!GetWindowThreadProcessId(reinterpret_cast<HWND>(sender), &settingsPid) || configPath.empty()) return FALSE;
    try
    {
        const auto path = RestartSessionPath(configPath, GetCurrentProcessId(), settingsPid);
        DeleteFileW(path.c_str());
        SendMessageW(npp._nppHandle, NPPM_SAVECURRENTSESSION, 0, reinterpret_cast<LPARAM>(path.c_str()));
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    }
    catch (const std::exception&) { return FALSE; }
}

void RemoveLoadedRestartSession()
{
    if (configPath.empty()) return;
    int count = 0;
    const auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return;
    for (int i = 1; i + 1 < count; ++i)
    {
        if (wcscmp(args[i], L"-openSession") != 0) continue;
        const auto path = std::filesystem::path(args[i + 1]);
        if (path != (configPath / path.filename()).lexically_normal() || path.extension() != L".xml") break;
        const auto stem = path.stem().wstring();
        const std::wstring_view name(stem);
        if (!name.starts_with(RestartSessionPrefix)) break;
        const auto ids = name.substr(std::size(RestartSessionPrefix) - 1);
        const auto separator = ids.find(L'-');
        if (separator > 0 && separator < ids.size() - 1 &&
            ids.substr(0, separator).find_first_not_of(L"0123456789") == std::wstring_view::npos &&
            ids.substr(separator + 1).find_first_not_of(L"0123456789") == std::wstring_view::npos)
            DeleteFileW(path.c_str());
        break;
    }
    LocalFree(args);
}

std::filesystem::path PluginDirectory()
{
    wchar_t path[32768]{};
    GetModuleFileNameW(PluginModule(), path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(path).parent_path();
}

void RefreshHelp()
{
    if (!helpText) return;
    constexpr UINT IsNotepadDark = NPPMSG + 107;
    const bool dark = SendMessageW(npp._nppHandle, IsNotepadDark, 0, 0) != 0;
    const auto manual = ReadTextFile(PluginDirectory() / L"manual.md");
    RenderMarkdown(helpText, manual.empty()
        ? "# RainSyntax Help\n\nThe installed manual.md could not be opened. Run RainSyntax setup to restore it."
        : manual, dark);
}

INT_PTR CALLBACK HelpDialog(HWND window, UINT message, WPARAM, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        helpText = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_READONLY,
            0, 0, 0, 0, window, nullptr, PluginModule(), nullptr);
        return TRUE;
    case WM_SIZE:
        if (helpText)
        {
            MoveWindow(helpText, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
            RECT margin{12, 10, LOWORD(lParam) - 12, HIWORD(lParam) - 8};
            SendMessageW(helpText, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&margin));
        }
        return TRUE;
    case WM_NCDESTROY:
        helpPanel = helpText = nullptr;
        break;
    }
    return FALSE;
}

void OpenHelp()
{
    constexpr wchar_t title[] = L"RainSyntax Help";
    if (!helpPanel)
    {
        static const HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (richEdit)
            helpPanel = CreateDialogParamW(PluginModule(), MAKEINTRESOURCEW(IDD_HELP),
                npp._nppHandle, HelpDialog, 0);
        if (!helpPanel || !helpText)
        {
            if (helpPanel) DestroyWindow(helpPanel);
            MessageBoxW(npp._nppHandle, L"Could not open the help panel.", title, MB_OK | MB_ICONERROR);
            return;
        }
        constexpr UINT DockRight = 1U << 28;
        static constexpr wchar_t dockTitle[] = L"RainSyntax Help";
        DockPanel data{helpPanel, dockTitle, HelpItem, DockRight, nullptr, nullptr, {}, 0, L"RainSyntax.dll"};
        SendMessage(npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));
    }
    RefreshHelp();
    SendMessage(npp._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(helpPanel));
    SetFocus(helpText);
}

void OpenBackup()
{
    if (const auto path = ClassicBackup(configPath, PluginDirectory()); !path.empty())
    {
        ShellExecuteW(npp._nppHandle, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    MessageBoxW(npp._nppHandle, L"Setup did not find any original RainLexer files to back up.",
        RAINSYNTAX_TITLE, MB_OK | MB_ICONINFORMATION);
}

void Uninstall()
{
    const auto path = PluginDirectory() / UninstallerFile;
    const auto result = ShellExecuteW(npp._nppHandle, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) == ERROR_FILE_NOT_FOUND)
        MessageBoxW(npp._nppHandle, L"The uninstaller is missing. Run RainSyntax setup to restore it.",
            RAINSYNTAX_TITLE, MB_OK | MB_ICONERROR);
}

void OpenSettings()
{
    const auto directory = PluginDirectory();
    const auto program = directory / L"RainSyntaxSettings.exe";
    if (!configPath.empty())
    {
        auto parameters = L"--config \"" + (configPath / L".").wstring() + L"\" --owner " +
            std::to_wstring(GetCurrentProcessId());
        // Public message added after the headers bundled with the original RainLexer.
        constexpr UINT GetNotepadSettingsPath = NPPMSG + 119;
        std::array<wchar_t, 32768> activeSettings{};
        if (SendMessage(npp._nppHandle, GetNotepadSettingsPath, activeSettings.size(),
            reinterpret_cast<LPARAM>(activeSettings.data())))
        {
            parameters += L" --npp-settings \"" + (std::filesystem::path(activeSettings.data()) / L".").wstring() + L"\"";
        }
        const auto result = ShellExecuteW(npp._nppHandle, L"open", program.c_str(), parameters.c_str(), directory.c_str(), SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) > 32) return;
    }
    MessageBoxW(npp._nppHandle, L"Could not open RainSyntax Settings. Check the plugin installation.",
        RAINSYNTAX_TITLE, MB_OK | MB_ICONERROR);
}

void TogglePanel()
{
    if (panel)
    {
        SendMessage(npp._nppHandle, IsWindowVisible(panel) ? NPPM_DMMHIDE : NPPM_DMMSHOW,
            0, reinterpret_cast<LPARAM>(panel));
        return;
    }
    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LINK_CLASS};
    InitCommonControlsEx(&controls);
    panel = CreateDialogParamW(PluginModule(), MAKEINTRESOURCEW(IDD_SETTINGS), npp._nppHandle, PanelDialog, 0);
    if (!panel) return;
    constexpr UINT DockRight = 1U << 28;
    DockPanel data{panel, L"RainSyntax", SettingsSidebar, DockRight, nullptr, nullptr, {}, 0, L"RainSyntax.dll"};
    SendMessage(npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));
    SendMessage(npp._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(panel));
}
}

extern "C" void setInfo(NppData data)
{
    npp = data;
    std::array<wchar_t, 32768> path{};
    if (!SendMessage(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, path.size(), reinterpret_cast<LPARAM>(path.data()))) return;
    configPath = path.data();
    settingsPath = (configPath / L"RainSyntax-settings.ini").wstring();
    ConfigureVocabulary(ReadTextFile(configPath / VocabularyFile));
}
extern "C" const wchar_t* getName() { return L"RainSyntax"; }
extern "C" BOOL isUnicode() { return TRUE; }
extern "C" FuncItem* getFuncsArray(int* count)
{
    *count = static_cast<int>(std::size(functions));
    return functions;
}
extern "C" LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" void beNotified(SCNotification* notification)
{
    switch (notification->nmhdr.code)
    {
    case NPPN_READY:
    {
        if (!settingsPath.empty())
        {
            automaticCompletion = GetPrivateProfileIntW(SettingsSection, CompletionKey, 1, settingsPath.c_str()) != 0;
            caseCorrection = GetPrivateProfileIntW(SettingsSection, CorrectionKey, 1, settingsPath.c_str()) != 0;
            typoCorrection = GetPrivateProfileIntW(SettingsSection, TypoKey, 1, settingsPath.c_str()) != 0;
        }
        UpdateCheck(Automatic, automaticCompletion);
        UpdateCheck(Capitalization, caseCorrection);
        UpdateCheck(Typos, typoCorrection);
        SyncPanel(panel);
        ready = true;
        const auto module = PluginModule();
        WNDCLASSW bridge{};
        bridge.lpfnWndProc = RestartSessionWindow;
        bridge.hInstance = module;
        bridge.lpszClassName = RestartWindowClass;
        if (RegisterClassW(&bridge))
            restartWindow = CreateWindowExW(0, RestartWindowClass, nullptr, 0, 0, 0, 0, 0,
                HWND_MESSAGE, nullptr, module, nullptr);
        RemoveLoadedRestartSession();
        ReloadLivePalette();
        QueuePageBackground();
        break;
    }
    case NPPN_EXTERNALLEXERBUFFER:
    case NPPN_BUFFERACTIVATED:
    case NPPN_LANGCHANGED:
    case NPPN_WORDSTYLESUPDATED:
        if (ready) QueuePageBackground();
        break;
    case SCN_CHARADDED:
        if (ready && !editing && notification->characterSource == SC_CHARACTERSOURCE_DIRECT_INPUT &&
            notification->ch > 0 && notification->ch < 128)
        {
            const HWND view = static_cast<HWND>(notification->nmhdr.hwndFrom);
            const auto ch = static_cast<char>(notification->ch);
            if ((caseCorrection || typoCorrection) && std::strchr("= ]#\"\t\r\n(|,", ch)) Correct(view, ch);
            if (automaticCompletion && (std::strchr("!#$:@_", ch) ||
                (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')))
                Complete(view, true);
        }
        break;
    case SCN_MODIFIED:
        if (ready && !capitalization.empty()) TrackEdits(*notification);
        break;
    case SCN_AUTOCCANCELLED:
    case SCN_AUTOCCOMPLETED:
        RestorePopup(static_cast<HWND>(notification->nmhdr.hwndFrom));
        break;
    case NPPN_FILECLOSED:
        capitalization.erase(static_cast<LRESULT>(notification->nmhdr.idFrom));
        break;
    case NPPN_DARKMODECHANGED:
        RefreshHelp();
        if (ready) QueuePageBackground();
        break;
    case NPPN_SHUTDOWN:
        ready = false;
        if (restartWindow) DestroyWindow(restartWindow);
        if (backgroundTimer)
        {
            KillTimer(nullptr, backgroundTimer);
            backgroundTimer = 0;
        }
        break;
    default:
        break;
    }
}

