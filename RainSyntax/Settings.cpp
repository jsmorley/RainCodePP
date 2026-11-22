// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE-GPL-2.0).
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dlgs.h>
#include <shellapi.h>
#include <shlobj.h>

#include "ButtonTooltips.h"
#include "Palette.h"
#include "SettingsFiles.h"
#include "RestartSession.h"
#include "SettingsResource.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace RainSyntax;
using Path = std::filesystem::path;
constexpr wchar_t DefaultPreset[] = L"Clear Skies";
constexpr UINT_PTR RestartTimer = 1;
constexpr UINT_PTR OwnerTimer = 2;
constexpr UINT_PTR PresetShiftTimer = 3;
constexpr UINT RemovePresetMessage = WM_APP + 1;
constexpr wchar_t HiddenPresetsSection[] = L"HiddenPresets";
constexpr wchar_t SettingsWindowSection[] = L"SettingsWindow";
constexpr wchar_t PositionKey[] = L"Position";
constexpr wchar_t SettingsInstanceMutex[] = L"Local\\RainSyntax.Settings";
constexpr wchar_t SettingsInstanceWindow[] = L"RainSyntax.Settings.Window";
constexpr std::string_view PreviewLines[] = {
    "[Rainmeter]",
    "; Rainmeter syntax preview",
    "[Variables]",
    "colorBar=235,170,0,255",
    "[MeterCaption]",
    "Meter=String",
    "FontColor=FF8020C8",
    "Text=https://example.com/[#Title] [&Clock:X]",
    "X=(max(180, [$SkinW]) / 2)r",
    "Y=10R",
    "IfMatch=^(?:[A-Z]\\d{2,4}|none)$",
    "LeftMouseUpAction=[!SetOption Label Text [#Title]]",
};

std::wstring WindowText(HWND window)
{
    std::wstring value(GetWindowTextLengthW(window) + 1, L'\0');
    value.resize(GetWindowTextW(window, value.data(), static_cast<int>(value.size())));
    return value;
}

BOOL CALLBACK ActivateSettingsWindow(HWND window, LPARAM)
{
    if (!GetPropW(window, SettingsInstanceWindow)) return TRUE;
    if (IsIconic(window)) ShowWindowAsync(window, SW_RESTORE);
    const auto popup = GetLastActivePopup(window);
    const auto target = IsWindowVisible(popup) ? popup : window;
    if (!SetForegroundWindow(target)) FlashWindow(target, TRUE);
    return FALSE;
}

std::wstring ColorCode(COLORREF color)
{
    wchar_t text[7]{};
    swprintf_s(text, L"%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return text;
}

void SetForeground(StyleSettings& style, COLORREF foreground)
{
    style.foreground = foreground;
    const auto flags = ColorFlags(style);
    if (!(flags & 1)) style.colorStyle = std::to_wstring(flags | 1);
}

UINT_PTR CALLBACK PaletteFontDialog(HWND dialog, UINT message, WPARAM, LPARAM)
{
    if (message == WM_INITDIALOG)
    {
        SetWindowTextW(dialog, L"Font for this entire palette");
        // Color has a separate palette control, and syntax styles have no strikeout flag.
        for (const int id : {cmb4, stc4, chx1})
            ShowWindow(GetDlgItem(dialog, id), SW_HIDE);
    }
    return 0;
}

struct ColorPicker {
    const wchar_t* title;
    COLORREF color;
    std::array<COLORREF, 16>& customColors;
    std::optional<COLORREF> preview;
    HWND window = nullptr;
    HWND visualPicker = nullptr;
    bool updating = false;

    void SyncColor(std::optional<COLORREF> value, int editedField = 0)
    {
        preview = value;
        updating = true;
        if (editedField != IDC_HEX_COLOR)
        {
            const auto hex = value ? L"#" + ColorCode(*value) : std::wstring{};
            SetDlgItemTextW(window, IDC_HEX_COLOR, hex.c_str());
        }
        if (!editedField || editedField == IDC_HEX_COLOR)
        {
            SetDlgItemTextW(window, IDC_RED_COLOR, value ? std::to_wstring(GetRValue(*value)).c_str() : L"");
            SetDlgItemTextW(window, IDC_GREEN_COLOR, value ? std::to_wstring(GetGValue(*value)).c_str() : L"");
            SetDlgItemTextW(window, IDC_BLUE_COLOR, value ? std::to_wstring(GetBValue(*value)).c_str() : L"");
        }
        updating = false;
        EnableWindow(GetDlgItem(window, IDOK), preview.has_value());
        InvalidateRect(GetDlgItem(window, IDC_COLOR_SWATCH), nullptr, FALSE);
    }

    std::optional<COLORREF> ReadHex() const
    {
        const auto input = WindowText(GetDlgItem(window, IDC_HEX_COLOR));
        std::wstring_view hex = input;
        while (!hex.empty() && iswspace(hex.front())) hex.remove_prefix(1);
        while (!hex.empty() && iswspace(hex.back())) hex.remove_suffix(1);
        if (hex.starts_with(L"#")) hex.remove_prefix(1);
        return ParseHexColor(hex);
    }

    std::optional<COLORREF> ReadRgb() const
    {
        const auto readChannel = [&](int field) -> std::optional<int> {
            const auto text = WindowText(GetDlgItem(window, field));
            if (text.empty() || text.size() > 3 || text.find_first_not_of(L"0123456789") != std::wstring::npos)
                return std::nullopt;
            const auto number = _wtoi(text.c_str());
            if (number > 255) return std::nullopt;
            return number;
        };
        const auto red = readChannel(IDC_RED_COLOR);
        const auto green = readChannel(IDC_GREEN_COLOR);
        const auto blue = readChannel(IDC_BLUE_COLOR);
        if (!red || !green || !blue) return std::nullopt;
        return RGB(*red, *green, *blue);
    }

    void Browse()
    {
        CHOOSECOLORW choose{sizeof(choose)};
        choose.hwndOwner = window;
        choose.rgbResult = preview.value_or(color);
        choose.lpCustColors = customColors.data();
        choose.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ENABLEHOOK;
        choose.lCustData = reinterpret_cast<LPARAM>(this);
        choose.lpfnHook = VisualPickerHook;
        if (ChooseColorW(&choose)) SyncColor(choose.rgbResult);
        visualPicker = nullptr;
    }

    void Close() const
    {
        if (visualPicker) SendMessageW(visualPicker, WM_CLOSE, 0, 0);
        if (window) EndDialog(window, IDCANCEL);
    }

    static UINT_PTR CALLBACK VisualPickerHook(HWND window, UINT message, WPARAM, LPARAM lParam)
    {
        if (message == WM_INITDIALOG)
        {
            const auto& options = *reinterpret_cast<const CHOOSECOLORW*>(lParam);
            reinterpret_cast<ColorPicker*>(options.lCustData)->visualPicker = window;
        }
        return 0;
    }

    static INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_INITDIALOG)
        {
            auto& picker = *reinterpret_cast<ColorPicker*>(lParam);
            picker.window = window;
            SetWindowLongPtrW(window, DWLP_USER, lParam);
            SetWindowTextW(window, picker.title);
            AddButtonTooltips(window, {
                {IDC_VISUAL_COLOR, L"Open the visual color picker."},
                {IDOK, L"Use this color in the palette you are editing."},
                {IDCANCEL, L"Close the picker and discard this color selection."},
            });
            SendDlgItemMessageW(window, IDC_HEX_COLOR, EM_SETLIMITTEXT, 64, 0);
            for (const int field : {IDC_RED_COLOR, IDC_GREEN_COLOR, IDC_BLUE_COLOR})
                SendDlgItemMessageW(window, field, EM_SETLIMITTEXT, 3, 0);
            picker.SyncColor(picker.color);
            SendDlgItemMessageW(window, IDC_HEX_COLOR, EM_SETSEL, 0, -1);
            return TRUE;
        }
        auto* picker = reinterpret_cast<ColorPicker*>(GetWindowLongPtrW(window, DWLP_USER));
        if (!picker) return FALSE;
        if (message == WM_DRAWITEM && wParam == IDC_COLOR_SWATCH)
        {
            const auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            const auto brush = picker->preview ? CreateSolidBrush(*picker->preview) : nullptr;
            FillRect(draw.hDC, &draw.rcItem, brush ? brush : GetSysColorBrush(COLOR_BTNFACE));
            if (brush) DeleteObject(brush);
            return TRUE;
        }
        if (message != WM_COMMAND) return FALSE;
        switch (LOWORD(wParam))
        {
        case IDC_HEX_COLOR:
        case IDC_RED_COLOR:
        case IDC_GREEN_COLOR:
        case IDC_BLUE_COLOR:
            if (HIWORD(wParam) == EN_CHANGE && !picker->updating)
            {
                const auto field = LOWORD(wParam);
                picker->SyncColor(field == IDC_HEX_COLOR ? picker->ReadHex() : picker->ReadRgb(), field);
            }
            return TRUE;
        case IDC_VISUAL_COLOR:
            picker->Browse();
            return TRUE;
        case IDOK:
            if (picker->preview)
            {
                picker->color = *picker->preview;
                EndDialog(window, IDOK);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(window, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
        }
    }
};

void CommitFile(const Path& temp, const Path& target)
{
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Could not replace the file. Check its permissions and try again.");
    }
}

Path StagingPath(const Path& target)
{
    return target.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
}

void SavePalette(const Path& model, const Path& target, const Palette& palette)
{
    const auto xml = OpenXml(model);
    const auto writeStyle = [&](size_t i, IXMLDOMElement* element) {
        const auto& style = palette[i];
        const auto set = [&](const wchar_t* name, const std::wstring& value) {
            if (FAILED(element->setAttribute(_bstr_t(name), _variant_t(value.c_str()))))
                throw std::runtime_error("Could not update the palette attributes.");
        };
        set(L"fgColor", ColorCode(style.foreground));
        set(L"bgColor", ColorCode(style.background));
        set(L"fontName", style.family);
        set(L"fontSize", style.size);
        set(L"fontStyle", std::to_wstring(style.emphasis));
        if (!style.colorStyle.empty()) set(L"colorStyle", style.colorStyle);
        else element->removeAttribute(_bstr_t(L"colorStyle"));
    };
    const auto seen = ForStyles(xml.Get(), writeStyle);
    ComPtr<IXMLDOMNode> lexer;
    for (size_t i = Red; i < palette.size(); ++i)
    {
        if (seen[i]) continue;
        if (!lexer)
            xml->selectSingleNode(_bstr_t(L"/NotepadPlus/LexerStyles/LexerType[@name='Rainmeter5']"), &lexer);
        ComPtr<IXMLDOMElement> element;
        if (!lexer || FAILED(xml->createElement(_bstr_t(L"WordsStyle"), &element)) ||
            FAILED(element->setAttribute(_bstr_t(L"name"), _variant_t(palette[i].name.c_str()))) ||
            FAILED(element->setAttribute(_bstr_t(L"styleID"), _variant_t(std::to_wstring(ScintillaStyle(static_cast<int>(i))).c_str()))))
            throw std::runtime_error("Could not create a syntax style.");
        writeStyle(i, element.Get());
        ComPtr<IXMLDOMNode> appended;
        if (FAILED(lexer->appendChild(element.Get(), &appended)))
            throw std::runtime_error("Could not add the syntax style to the palette.");
    }
    const auto temp = StagingPath(target);
    if (FAILED(xml->save(_variant_t(temp.c_str()))))
    {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Could not save the palette XML.");
    }
    CommitFile(temp, target);
}

void SetNotepadMode(const Path& configFile, bool dark)
{
    const wchar_t* enabled = dark ? L"yes" : L"no";
    const auto document = OpenXml(configFile);
    ComPtr<IXMLDOMNode> node;
    document->selectSingleNode(_bstr_t(L"/NotepadPlus/GUIConfigs/GUIConfig[@name='DarkMode']"), &node);
    ComPtr<IXMLDOMElement> mode;
    if (!node || FAILED(node.As(&mode)))
        throw std::runtime_error("Notepad++ has not saved its light and dark mode preferences yet.");
    if (Attribute(mode.Get(), L"enable") == enabled && Attribute(mode.Get(), L"enableWindowsMode") != L"yes")
        return;
    if (FAILED(mode->setAttribute(_bstr_t(L"enable"), _variant_t(enabled))) ||
        FAILED(mode->setAttribute(_bstr_t(L"enableWindowsMode"), _variant_t(L"no"))))
        throw std::runtime_error("Could not set Notepad++ light or dark mode.");
    const auto temp = StagingPath(configFile);
    if (FAILED(document->save(_variant_t(temp.c_str()))))
    {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Could not save Notepad++ light or dark mode.");
    }
    CommitFile(temp, configFile);
}

struct NotepadWindows {
    const Path& program;
    std::vector<HWND> windows;
    std::vector<HANDLE> processes;
    explicit NotepadWindows(const Path& executable) : program(executable) {}

    static BOOL CALLBACK Find(HWND window, LPARAM data)
    {
        auto& found = *reinterpret_cast<NotepadWindows*>(data);
        wchar_t name[16]{};
        if (!GetClassNameW(window, name, static_cast<int>(std::size(name))) ||
            wcscmp(name, L"Notepad++") != 0) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!process) return TRUE;
        wchar_t path[32768]{};
        DWORD size = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(process, 0, path, &size) &&
            CompareStringOrdinal(path, -1, found.program.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            found.windows.push_back(window);
            found.processes.push_back(process);
        }
        else CloseHandle(process);
        return TRUE;
    }
};

HWND FindEditorBridge(DWORD pid)
{
    HWND bridge = nullptr;
    while ((bridge = FindWindowExW(HWND_MESSAGE, bridge, RestartWindowClass, nullptr)))
    {
        DWORD owner = 0;
        GetWindowThreadProcessId(bridge, &owner);
        if (owner == pid) return bridge;
    }
    return nullptr;
}

EditorMode ReadEditorMode(DWORD pid)
{
    if (!pid) return EditorMode::Unknown;
    const auto bridge = FindEditorBridge(pid);
    DWORD_PTR result = 0;
    if (!bridge || !SendMessageTimeoutW(bridge, QueryEditorModeMessage, 0, 0, SMTO_ABORTIFHUNG, 1000, &result))
        return EditorMode::Unknown;
    return static_cast<EditorMode>(result);
}

constexpr const wchar_t* StyleDescriptions[] = {
    L"Ordinary text and the editor page. Syntax styles inherit this background unless they have an override.",
    L"Comment lines beginning with a semicolon.",
    L"Section headers such as [Rainmeter].",
    L"Recognized option names such as FontColor.",
    L"The = between an option and its value.",
    L"An unrecognized fixed value or a standalone bang outside an action value.",
    L"A recognized value such as String in Meter=String.",
    L"A Rainmeter command such as !Refresh.",
    L"A Rainmeter-supplied #variable#, such as #CURRENTCONFIG#.",
    L"A skin's own #variable# or variable definition.",
    L"Numbers in options, formulas, and bang arguments.",
    L"An option retained for compatibility with older skins.",
    L"An option value retained for older skins.",
    L"A Rainmeter command retained for older skins.",
    L"A numeric character escape such as [\\x20].",
    L"A pipe that separates items in a list.",
    L"A bracketed section reference such as [&Clock].",
    L"A $ variable such as [$SkinW].",
    L"The brackets that enclose a bang.",
    L"Text passed as an argument to a bang.",
    L"Quotes and quoted text in expressions.",
    L"Formula operators and separators such as + or *.",
    L"A function name such as max in a formula.",
    L"A section selector or modifier such as TimeStamp.",
    L"An environment variable such as %APPDATA%.",
    L"A reference escaped so Rainmeter leaves it as text.",
    L"A placeholder in a format string, such as %Y.",
    L"A directive such as @Include.",
    L"A named section, measure, or other referenced item.",
    L"The optional 0x prefix on a hexadecimal color.",
    L"The red component of an RGB or RGBA color.",
    L"The green component of an RGB or RGBA color.",
    L"The blue component of an RGB or RGBA color.",
    L"The optional alpha component that sets opacity.",
    L"Lowercase r positions X or Y from the previous meter's left or top edge.",
    L"Uppercase R positions X or Y from the previous meter's right or bottom edge.",
    L"The complete pattern in a regex-valued option.",
    L"A link value or literal URL. Embedded variables use their own styles."
};
static_assert(std::size(StyleDescriptions) == StyleCount);

class StyleTooltips {
public:
    void Attach(HWND combo)
    {
        COMBOBOXINFO info{sizeof(info)};
        if (!GetComboBoxInfo(combo, &info) || !info.hwndList) return;
        list = info.hwndList;
        tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW,
            nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, combo, nullptr, nullptr, nullptr);
        if (!tooltip) return;
        auto tool = Tool();
        tool.uFlags = TTF_TRACK;
        tool.lpszText = const_cast<wchar_t*>(StyleDescriptions[Default]);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        SetWindowSubclass(list, ListProc, 1, reinterpret_cast<DWORD_PTR>(this));
    }

    void Hide()
    {
        if (list) KillTimer(list, HoverTimer);
        item = -1;
        if (tooltip)
        {
            auto tool = Tool();
            SendMessageW(tooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
        }
    }

private:
    static constexpr UINT_PTR HoverTimer = 0x5243;
    HWND list = nullptr, tooltip = nullptr;
    int item = -1;
    POINT position{};

    TOOLINFOW Tool() const
    {
        TOOLINFOW tool{sizeof(tool)};
        tool.hwnd = list;
        tool.uId = 1;
        return tool;
    }

    void Hover(LPARAM coordinates)
    {
        const auto hit = SendMessageW(list, LB_ITEMFROMPOINT, 0, coordinates);
        const POINT point{GET_X_LPARAM(coordinates), GET_Y_LPARAM(coordinates)};
        RECT row{};
        const int index = LOWORD(hit);
        if (HIWORD(hit) || index >= static_cast<int>(std::size(StyleDescriptions)) ||
            SendMessageW(list, LB_GETITEMRECT, index, reinterpret_cast<LPARAM>(&row)) == LB_ERR ||
            !PtInRect(&row, point))
        {
            Hide();
            return;
        }
        if (item == index) return;
        Hide();
        item = index;
        position = {point.x + 12, row.bottom};
        ClientToScreen(list, &position);
        TRACKMOUSEEVENT leave{sizeof(leave), TME_LEAVE, list, 0};
        TrackMouseEvent(&leave);
        SetTimer(list, HoverTimer, 400, nullptr);
    }

    void Show()
    {
        KillTimer(list, HoverTimer);
        if (item < 0 || !IsWindowVisible(list)) return;
        auto tool = Tool();
        tool.lpszText = const_cast<wchar_t*>(StyleDescriptions[item]);
        SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(position.x, position.y));
        SendMessageW(tooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
    }

    static LRESULT CALLBACK ListProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR id, DWORD_PTR data)
    {
        auto& self = *reinterpret_cast<StyleTooltips*>(data);
        switch (message)
        {
        case WM_MOUSEMOVE: self.Hover(lParam); break;
        case WM_MOUSELEAVE:
        case WM_MOUSEWHEEL:
        case WM_VSCROLL: self.Hide(); break;
        case WM_TIMER:
            if (wParam == HoverTimer) { self.Show(); return 0; }
            break;
        case WM_NCDESTROY:
            self.Hide();
            RemoveWindowSubclass(hwnd, ListProc, id);
            self.list = self.tooltip = nullptr;
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }
};

struct EditorFont {
    std::wstring family = L"Consolas";
    int points = 10;
    int zoom = 0;

    LOGFONTW Resolve(const StyleSettings& style, UINT dpi, bool preview) const
    {
        const int customSize = _wtoi(style.size.c_str());
        const int baseSize = customSize > 0 ? customSize : points;
        const auto zoomedSize = static_cast<long long>(baseSize) + zoom;
        const int size = preview ? static_cast<int>(std::clamp(zoomedSize, 2LL, 32767LL)) :
            std::clamp(baseSize, 5, 72);
        LOGFONTW font{};
        font.lfHeight = -MulDiv(size, dpi, 72);
        font.lfWeight = style.emphasis & 1 ? FW_BOLD : FW_NORMAL;
        font.lfItalic = (style.emphasis & 2) != 0;
        font.lfUnderline = (style.emphasis & 4) != 0;
        wcsncpy_s(font.lfFaceName, style.family.empty() ? family.c_str() : style.family.c_str(), _TRUNCATE);
        return font;
    }
};

struct Editor {
    Path config, plugin, nppSettings;
    EditorFont editorFont;
    HWND window = nullptr;
    std::optional<POINT> windowPosition;
    Palette palette, activePalette;
    std::vector<std::string> words;
    size_t group = 0, style = 0;
    bool wordsDirty = false, updating = false;
    std::array<COLORREF, 16> customColors{};
    ColorPicker* activeColorPicker = nullptr;
    std::vector<std::wstring> presetNames;
    HWND presetList = nullptr;
    int presetPressed = -1;
    bool presetShift = false;
    std::optional<Palette> presetPreview;
    StyleTooltips styleTooltips;
    std::optional<Palette> pendingPalette;
    std::vector<HANDLE> closing;
    std::vector<Path> restartSessions;
    std::optional<std::filesystem::file_time_type> nativeSessionBefore;
    HANDLE ownerProcess = nullptr;
    DWORD ownerId = 0;
    ULONGLONG restartDeadline = 0;

    Editor(Path settings, Path home, Path notepadSettings, DWORD owner, EditorFont font) :
        config(std::move(settings)), plugin(std::move(home)), nppSettings(std::move(notepadSettings)),
        editorFont(std::move(font)), ownerId(owner) {}
    ~Editor()
    {
        for (HANDLE process : closing) CloseHandle(process);
        if (ownerProcess) CloseHandle(ownerProcess);
    }
    HWND Control(int id) const { return GetDlgItem(window, id); }
    void Status(const wchar_t* text) const { SetDlgItemTextW(window, IDC_STATUS, text); }

    Path Preset(std::wstring_view name) const
    {
        return plugin / L"Presets" / (L"RainSyntax-" + std::wstring(name) + L".xml");
    }
    Path ActivePalette() const
    {
        const auto saved = config / LivePaletteFile;
        return std::filesystem::exists(saved) ? saved : config / StyleFile;
    }
    Path Notepad() const { return plugin.parent_path().parent_path() / L"notepad++.exe"; }

    void TrackWindowPosition()
    {
        RECT bounds{};
        if (!IsIconic(window) && GetWindowRect(window, &bounds))
            windowPosition = POINT{bounds.left, bounds.top};
    }

    void RestoreWindowPosition()
    {
        const auto settings = config / SettingsFile;
        wchar_t saved[64]{};
        GetPrivateProfileStringW(SettingsWindowSection, PositionKey, L"", saved,
            static_cast<DWORD>(std::size(saved)), settings.c_str());
        POINT position{};
        wchar_t extra{};
        if (swscanf_s(saved, L"%ld,%ld%lc", &position.x, &position.y, &extra, 1U) == 2)
        {
            RECT bounds{};
            MONITORINFO monitor{sizeof(monitor)};
            if (GetWindowRect(window, &bounds) &&
                GetMonitorInfoW(MonitorFromPoint(position, MONITOR_DEFAULTTONEAREST), &monitor))
            {
                const auto& work = monitor.rcWork;
                const auto right = std::max(work.left, work.right - (bounds.right - bounds.left));
                const auto bottom = std::max(work.top, work.bottom - (bounds.bottom - bounds.top));
                SetWindowPos(window, nullptr, std::clamp(position.x, work.left, right),
                    std::clamp(position.y, work.top, bottom), 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        TrackWindowPosition();
    }

    void SaveWindowPosition() const
    {
        if (!windowPosition) return;
        const auto settings = config / SettingsFile;
        const auto value = std::to_wstring(windowPosition->x) + L"," + std::to_wstring(windowPosition->y);
        WritePrivateProfileStringW(SettingsWindowSection, PositionKey, value.c_str(), settings.c_str());
    }

    void Init()
    {
        if (!ownerId)
        {
            const auto program = Notepad();
            NotepadWindows targets(program);
            EnumWindows(NotepadWindows::Find, reinterpret_cast<LPARAM>(&targets));
            if (targets.processes.size() == 1) ownerId = GetProcessId(targets.processes.front());
            for (HANDLE process : targets.processes) CloseHandle(process);
        }
        if (ownerId)
        {
            ownerProcess = OpenProcess(SYNCHRONIZE, FALSE, ownerId);
            if (!ownerProcess || !SetTimer(window, OwnerTimer, 250, nullptr))
            {
                EndDialog(window, 0);
                return;
            }
        }
        words = ReadVocabulary(ReadTextFile(config / VocabularyFile));
        for (const auto& entry : VocabularyGroups())
            SendMessageW(Control(IDC_GROUP), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.label));
        SendMessageW(Control(IDC_GROUP), CB_SETCURSEL, 0, 0);
        ShowTerms();
        SendMessageW(Control(IDC_FIND), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Find a term"));
        if (std::filesystem::exists(ActivePalette())) palette = ReadPalette(ActivePalette());
        else palette = ReadPalette(Preset(DefaultPreset));
        activePalette = palette;
        std::istringstream manifest(ReadTextFile(plugin / L"Presets" / L"Palettes.txt"));
        const auto settings = config / SettingsFile;
        std::string entry;
        SendMessageW(Control(IDC_PRESET), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current palette"));
        while (std::getline(manifest, entry))
        {
            if (entry.ends_with('\r')) entry.pop_back();
            const auto split = entry.find('|');
            if (split == std::string::npos || split + 1 == entry.size()) continue;
            auto name = Wide(std::string_view(entry).substr(split + 1));
            if (GetPrivateProfileIntW(HiddenPresetsSection, name.c_str(), 0, settings.c_str())) continue;
            const auto label = name + L"  (" + Wide(std::string_view(entry).substr(0, split)) + L")";
            SendMessageW(Control(IDC_PRESET), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            presetNames.push_back(std::move(name));
        }
        SendMessageW(Control(IDC_PRESET), CB_SETCURSEL, 0, 0);
        COMBOBOXINFO presetInfo{sizeof(presetInfo)};
        if (GetComboBoxInfo(Control(IDC_PRESET), &presetInfo) &&
            SetWindowSubclass(presetInfo.hwndList, PresetListProc, 1, reinterpret_cast<DWORD_PTR>(this)))
            presetList = presetInfo.hwndList;
        for (const auto& item : palette)
            SendMessageW(Control(IDC_STYLE_ID), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.name.c_str()));
        SendMessageW(Control(IDC_STYLE_ID), CB_SETCURSEL, 0, 0);
        styleTooltips.Attach(Control(IDC_STYLE_ID));
        AddButtonTooltips(window, {
            {IDC_FIND_NEXT, L"Find the next matching term in the selected category."},
            {IDC_RESET_TERMS, L"Replace this category's edits with the built-in vocabulary."},
            {IDC_SAVE_TERMS, L"Save vocabulary edits for the next time Notepad++ starts."},
            {IDC_USE_PRESET, L"Copy the previewed preset into the palette you can edit or apply."},
            {IDC_FOREGROUND, L"Choose the text color for the selected syntax style."},
            {IDC_BACKGROUND, L"Choose the background color for the selected syntax style."},
            {IDC_ALL_FOREGROUNDS, L"Set foreground color for this palette"},
            {IDC_ALL_BACKGROUNDS, L"Set background color for this palette"},
            {IDC_FONT, L"Choose the font, size, and emphasis for the selected syntax style."},
            {IDC_PALETTE_FONT, L"Set the font, size, and emphasis for this palette"},
            {IDC_APPLY, L"Save the palette and apply it to Rainmeter tabs in Notepad++."},
            {IDC_ACTIVE, L"Reload the palette currently in use by Notepad++."},
            {IDC_IMPORT, L"Load a RainSyntax palette XML file for editing."},
            {IDC_EXPORT, L"Export the edited palette to a separate XML file."},
            {IDC_BACKUP, L"Open the folder containing any original RainLexer files saved by setup."},
            {IDC_UNINSTALL, L"Start the RainSyntax uninstaller."},
            {IDCANCEL, L"Close RainSyntax Settings."},
        });
        UpdateStyleFields();
        RestoreWindowPosition();
    }

    void ShowTerms()
    {
        std::wistringstream input(Wide(words[group]));
        std::vector<std::wstring> entries;
        for (std::wstring entry; std::getline(input, entry);)
        {
            if (entry.ends_with(L'\r')) entry.pop_back();
            if (!entry.empty()) entries.push_back(std::move(entry));
        }
        std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
        std::wstring text;
        for (const auto& entry : entries)
        {
            text += L"\r\n";
            text += entry;
        }
        updating = true;
        const auto control = Control(IDC_TERMS);
        SetWindowTextW(control, text.c_str());
        SendMessageW(control, EM_SETSEL, 0, 0);
        SendMessageW(control, EM_SCROLLCARET, 0, 0);
        updating = false;
    }

    void RememberTerms() { words[group] = Utf8(WindowText(Control(IDC_TERMS))); }

    void SaveTerms()
    {
        RememberTerms();
        const auto content = WriteVocabulary(words);
        const auto target = config / VocabularyFile;
        const auto temp = StagingPath(target);
        {
            std::ofstream file(temp, std::ios::binary);
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            file.close();
            if (!file)
            {
                DeleteFileW(temp.c_str());
                throw std::runtime_error("Could not write the vocabulary file.");
            }
        }
        CommitFile(temp, target);
        words = ReadVocabulary(content);
        ShowTerms();
        wordsDirty = false;
        Status(L"Vocabulary saved. Restart Notepad++ to use it.");
    }

    void FindNext()
    {
        auto query = WindowText(Control(IDC_FIND));
        auto text = WindowText(Control(IDC_TERMS));
        if (query.empty()) return;
        for (auto& c : query) c = static_cast<wchar_t>(towlower(c));
        for (auto& c : text) c = static_cast<wchar_t>(towlower(c));
        DWORD end = 0;
        SendMessageW(Control(IDC_TERMS), EM_GETSEL, 0, reinterpret_cast<LPARAM>(&end));
        auto found = text.find(query, end);
        if (found == std::wstring::npos) found = text.find(query);
        if (found == std::wstring::npos) { Status(L"No matching term in this category."); return; }
        SetFocus(Control(IDC_TERMS));
        SendMessageW(Control(IDC_TERMS), EM_SETSEL, found, found + query.size());
        SendMessageW(Control(IDC_TERMS), EM_SCROLLCARET, 0, 0);
        Status(L"");
    }

    void DrawColorButton(const DRAWITEMSTRUCT& draw) const
    {
        const auto& shown = presetPreview ? *presetPreview : palette;
        const auto& item = shown[style];
        const auto dc = draw.hDC;
        const int saved = SaveDC(dc);
        const auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));

        SetDCBrushColor(dc, item.background);
        FillRect(dc, &draw.rcItem, brush);
        SetDCBrushColor(dc, GetSysColor(draw.itemState & (ODS_FOCUS | ODS_SELECTED) ? COLOR_HIGHLIGHT : COLOR_3DSHADOW));
        FrameRect(dc, &draw.rcItem, brush);

        // Keep the button label readable at the control's fixed height.
        auto font = editorFont.Resolve(item, GetDpiForWindow(window), false);
        font.lfHeight = -MulDiv(9, GetDpiForWindow(window), 72);
        const HFONT labelFont = CreateFontIndirectW(&font);
        if (labelFont) SelectObject(dc, labelFont);
        SetTextColor(dc, item.foreground);
        SetBkMode(dc, TRANSPARENT);
        RECT label = draw.rcItem;
        InflateRect(&label, -5, -2);
        if (draw.itemState & ODS_SELECTED) OffsetRect(&label, 1, 1);
        const auto caption = WindowText(draw.hwndItem);
        DrawTextW(dc, caption.c_str(), -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if ((draw.itemState & ODS_FOCUS) && !(draw.itemState & ODS_NOFOCUSRECT))
        {
            RECT focus = draw.rcItem;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(dc, &focus);
        }
        RestoreDC(dc, saved);
        if (labelFont) DeleteObject(labelFont);
    }

    void UpdateStyleFields()
    {
        const auto& shown = presetPreview ? *presetPreview : palette;
        const auto& item = shown[style];
        SetDlgItemTextW(window, IDC_FOREGROUND, (L"Foreground #" + ColorCode(item.foreground)).c_str());
        const auto label = style != Default && !(ColorFlags(item) & 2) ? L"Default #" : L"Background #";
        SetDlgItemTextW(window, IDC_BACKGROUND, (label + ColorCode(item.background)).c_str());
        UpdatePaletteActions();
        InvalidateRect(Control(IDC_FOREGROUND), nullptr, TRUE);
        InvalidateRect(Control(IDC_BACKGROUND), nullptr, TRUE);
        InvalidateRect(Control(IDC_PREVIEW), nullptr, TRUE);
    }

    bool PaletteDirty() const { return palette != activePalette; }

    void UpdatePaletteActions() const
    {
        const bool editingPalette = !presetPreview && !pendingPalette;
        for (const int id : {IDC_FOREGROUND, IDC_BACKGROUND, IDC_FONT,
                IDC_EXPORT, IDC_ALL_FOREGROUNDS, IDC_ALL_BACKGROUNDS, IDC_PALETTE_FONT})
            EnableWindow(Control(id), editingPalette);
        EnableWindow(Control(IDC_USE_PRESET), presetPreview.has_value() && !pendingPalette);
        EnableWindow(Control(IDC_APPLY), !presetPreview);
        if (!pendingPalette) SetDlgItemTextW(window, IDC_APPLY, PaletteDirty() ? L"Apply *" : L"Apply");
    }

    void ShowCurrent()
    {
        presetPreview.reset();
        SendMessageW(Control(IDC_PRESET), CB_SETCURSEL, 0, 0);
        UpdateStyleFields();
    }

    void PreviewPreset()
    {
        const auto index = SendMessageW(Control(IDC_PRESET), CB_GETCURSEL, 0, 0);
        if (index <= 0 || static_cast<size_t>(index) > presetNames.size())
        {
            ShowCurrent();
            Status(L"");
            return;
        }
        presetPreview = ReadPalette(Preset(presetNames[static_cast<size_t>(index - 1)]));
        UpdateStyleFields();
        Status(L"Choose Use preset to apply these colors and optionally switch Notepad++ mode.");
    }

    void RefreshPresetShift()
    {
        if (!presetList) return;
        const bool pressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (presetShift == pressed) return;
        presetShift = pressed;
        InvalidateRect(presetList, nullptr, FALSE);
    }

    int PresetAt(HWND list, LPARAM coordinates) const
    {
        const auto hit = SendMessageW(list, LB_ITEMFROMPOINT, 0, coordinates);
        const int row = LOWORD(hit);
        if (HIWORD(hit) || row <= 0 || static_cast<size_t>(row) > presetNames.size()) return -1;
        RECT bounds{};
        if (SendMessageW(list, LB_GETITEMRECT, row, reinterpret_cast<LPARAM>(&bounds)) == LB_ERR ||
            !PtInRect(&bounds, POINT{GET_X_LPARAM(coordinates), GET_Y_LPARAM(coordinates)})) return -1;
        return row;
    }

    static LRESULT CALLBACK PresetListProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR id, DWORD_PTR data)
    {
        auto& self = *reinterpret_cast<Editor*>(data);
        if (message == WM_LBUTTONDOWN)
        {
            self.presetPressed = (wParam & MK_SHIFT) ? self.PresetAt(list, lParam) : -1;
            if (self.presetPressed > 0) return 0;
        }
        if (message == WM_LBUTTONUP && self.presetPressed > 0)
        {
            const auto row = std::exchange(self.presetPressed, -1);
            if ((wParam & MK_SHIFT) && self.PresetAt(list, lParam) == row)
                PostMessageW(self.window, RemovePresetMessage, row, 0);
            return 0;
        }
        if (message == WM_NCDESTROY)
        {
            self.presetList = nullptr;
            KillTimer(self.window, PresetShiftTimer);
            RemoveWindowSubclass(list, PresetListProc, id);
        }
        return DefSubclassProc(list, message, wParam, lParam);
    }

    void DrawPreset(const DRAWITEMSTRUCT& draw) const
    {
        const bool selected = (draw.itemState & ODS_SELECTED) != 0;
        const bool enabled = (draw.itemState & (ODS_DISABLED | ODS_GRAYED)) == 0;
        const auto dc = draw.hDC;
        const auto saved = SaveDC(dc);
        FillRect(dc, &draw.rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        if (draw.itemID != static_cast<UINT>(-1))
        {
            const auto length = SendMessageW(draw.hwndItem, CB_GETLBTEXTLEN, draw.itemID, 0);
            if (length >= 0)
            {
                std::wstring label(static_cast<size_t>(length) + 1, L'\0');
                SendMessageW(draw.hwndItem, CB_GETLBTEXT, draw.itemID, reinterpret_cast<LPARAM>(label.data()));
                label.resize(static_cast<size_t>(length));
                if (selected && presetShift && draw.itemID > 0 && !(draw.itemState & ODS_COMBOBOXEDIT))
                    label += L" (delete)";
                RECT bounds = draw.rcItem;
                InflateRect(&bounds, -3, 0);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, GetSysColor(!enabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
                DrawTextW(dc, label.c_str(), -1, &bounds, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
        }
        if ((draw.itemState & ODS_FOCUS) && !(draw.itemState & ODS_NOFOCUSRECT))
            DrawFocusRect(dc, &draw.rcItem);
        RestoreDC(dc, saved);
    }

    void RemovePreset(size_t row)
    {
        if (!row || row > presetNames.size()) return;
        const auto combo = Control(IDC_PRESET);
        SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
        const auto settings = config / SettingsFile;
        if (!WritePrivateProfileStringW(HiddenPresetsSection, presetNames[row - 1].c_str(), L"1", settings.c_str()))
            throw std::runtime_error("Could not save the preset list.");
        SendMessageW(combo, CB_DELETESTRING, row, 0);
        presetNames.erase(presetNames.begin() + (row - 1));
        ShowCurrent();
        Status(L"Preset removed from your list.");
    }

    void DrawPreview(const DRAWITEMSTRUCT& draw) const
    {
        const auto& colors = presetPreview ? *presetPreview : palette;
        HBRUSH background = CreateSolidBrush(colors[Default].background);
        FillRect(draw.hDC, &draw.rcItem, background);
        DeleteObject(background);
        const int saved = SaveDC(draw.hDC);
        IntersectClipRect(draw.hDC, draw.rcItem.left, draw.rcItem.top, draw.rcItem.right, draw.rcItem.bottom);
        SetBkMode(draw.hDC, OPAQUE);
        SetTextAlign(draw.hDC, TA_LEFT | TA_BASELINE);
        std::array<HFONT, StyleCount> fonts{};
        std::array<TEXTMETRICW, StyleCount> metrics{};
        const auto original = GetCurrentObject(draw.hDC, OBJ_FONT);
        const auto dpi = GetDpiForWindow(window);
        const int margin = MulDiv(9, dpi, 96);
        int y = draw.rcItem.top + margin;
        bool variables = false;
        for (const auto line : PreviewLines)
        {
            int x = draw.rcItem.left + margin;
            if (const auto section = SectionName(line); !section.empty())
                variables = section == "Variables";
            const auto runs = StyleLine(line, variables);
            struct Run {
                size_t start, end;
                int style;
            };
            std::vector<Run> segments;
            LONG ascent = 0, descent = 0;
            for (size_t i = 0; i < line.size();)
            {
                size_t end = i + 1;
                while (end < line.size() && runs[end] == runs[i]) ++end;
                const auto styleId = runs[i];
                const auto& item = colors[styleId];
                if (!fonts[styleId])
                {
                    const auto font = editorFont.Resolve(item, dpi, true);
                    fonts[styleId] = CreateFontIndirectW(&font);
                    SelectObject(draw.hDC, fonts[styleId] ? fonts[styleId] : original);
                    GetTextMetricsW(draw.hDC, &metrics[styleId]);
                }
                ascent = std::max(ascent, metrics[styleId].tmAscent);
                descent = std::max(descent, metrics[styleId].tmDescent);
                segments.push_back({i, end, styleId});
                i = end;
            }
            for (const auto& segment : segments)
            {
                const auto& item = colors[segment.style];
                const auto font = fonts[segment.style];
                SelectObject(draw.hDC, font ? font : original);
                const auto value = Wide(line.substr(segment.start, segment.end - segment.start));
                const auto count = static_cast<int>(value.size());
                SetTextColor(draw.hDC, item.foreground);
                SetBkColor(draw.hDC, item.background);
                TextOutW(draw.hDC, x, y + ascent, value.data(), count);
                SIZE measured{};
                GetTextExtentPoint32W(draw.hDC, value.data(), count, &measured);
                x += measured.cx;
            }
            y += ascent + descent;
        }
        SelectObject(draw.hDC, original);
        for (auto font : fonts) if (font) DeleteObject(font);
        RestoreDC(draw.hDC, saved);
    }

    void PickColor(bool foreground, bool entirePalette = false)
    {
        const auto index = entirePalette ? static_cast<size_t>(Default) : style;
        auto& item = palette[index];
        const auto title = foreground
            ? (entirePalette ? L"Foreground color for this entire palette" : L"Foreground color")
            : (entirePalette ? L"Background color for this entire palette" : L"Background color");
        ColorPicker picker{title, foreground ? item.foreground : item.background, customColors};
        activeColorPicker = &picker;
        const auto result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_COLOR_PICKER),
            window, ColorPicker::Dialog, reinterpret_cast<LPARAM>(&picker));
        activeColorPicker = nullptr;
        if (result == IDOK)
        {
            if (foreground)
            {
                if (entirePalette)
                {
                    for (auto& entry : palette) SetForeground(entry, picker.color);
                }
                else SetForeground(item, picker.color);
            }
            else SetBackground(palette, index, picker.color, entirePalette);
            ShowCurrent();
        }
    }

    void PickFont(bool entirePalette = false)
    {
        const auto index = entirePalette ? static_cast<size_t>(Default) : style;
        auto& item = palette[index];
        auto font = editorFont.Resolve(item, GetDpiForWindow(window), false);
        CHOOSEFONTW choose{sizeof(choose)};
        choose.hwndOwner = window;
        choose.lpLogFont = &font;
        choose.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_LIMITSIZE | CF_EFFECTS;
        choose.nSizeMin = 5;
        choose.nSizeMax = 72;
        choose.rgbColors = item.foreground;
        if (entirePalette)
        {
            choose.Flags |= CF_ENABLEHOOK;
            choose.lpfnHook = PaletteFontDialog;
        }
        if (!ChooseFontW(&choose)) return;
        const auto size = std::to_wstring(choose.iPointSize / 10);
        const unsigned emphasis = (font.lfWeight >= FW_BOLD ? 1 : 0) |
            (font.lfItalic ? 2 : 0) | (font.lfUnderline ? 4 : 0);
        const auto applyFont = [&](StyleSettings& target) {
            target.family = font.lfFaceName;
            target.size = size;
            target.emphasis = emphasis;
        };
        if (entirePalette)
        {
            for (auto& entry : palette) applyFont(entry);
        }
        else
        {
            applyFont(item);
            if (choose.rgbColors != item.foreground) SetForeground(item, choose.rgbColors);
        }
        ShowCurrent();
    }

    bool ReplacePalette() const
    {
        return !PaletteDirty() || MessageBoxW(window, L"Replace the palette you are editing?",
            L"RainSyntax Settings", MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
    }

    void Load(const Path& path, bool active)
    {
        if (!ReplacePalette()) return;
        palette = ReadPalette(path);
        if (active) activePalette = palette;
        ShowCurrent();
        Status(active ? L"Loaded the active palette." : L"Palette loaded. Edit or apply it when ready.");
    }

    Path Browse(bool save) const
    {
        std::array<wchar_t, 32768> filename{};
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = window;
        dialog.lpstrFile = filename.data();
        dialog.nMaxFile = static_cast<DWORD>(filename.size());
        dialog.lpstrFilter = L"RainSyntax palette (*.xml)\0*.xml\0\0";
        dialog.lpstrDefExt = L"xml";
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
            (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return {};
        return filename.data();
    }

    void CancelRestart()
    {
        KillTimer(window, RestartTimer);
        for (size_t i = 0; i < closing.size(); ++i)
        {
            if (i < restartSessions.size())
            {
                if (WaitForSingleObject(closing[i], 0) == WAIT_OBJECT_0)
                    ReopenNotepad(restartSessions[i]);
                else DeleteFileW(restartSessions[i].c_str());
            }
            CloseHandle(closing[i]);
        }
        closing.clear();
        restartSessions.clear();
        pendingPalette.reset();
        EnableDialogEdits(true);
    }

    void EnableDialogEdits(bool enabled) const
    {
        constexpr int edits[] = {IDC_GROUP, IDC_TERMS, IDC_RESET_TERMS, IDC_SAVE_TERMS,
            IDC_STYLE_ID, IDC_PRESET, IDC_IMPORT, IDC_FIND, IDC_FIND_NEXT, IDC_ACTIVE, IDC_BACKUP, IDC_UNINSTALL};
        for (const int id : edits) EnableWindow(Control(id), enabled);
        UpdatePaletteActions();
    }

    void FinishRestart()
    {
        const auto replacement = std::move(*pendingPalette);
        auto sessions = std::move(restartSessions);
        restartSessions.clear();
        const auto nativeSessionAfter = NativeSessionTime();
        if (sessions.size() == 1 && nativeSessionAfter && nativeSessionAfter != nativeSessionBefore)
        {
            // Let Notepad++ use its own session if it wrote it at shutdown. It
            // also contains untitled tabs and backup data in snapshot mode.
            DeleteFileW(sessions.front().c_str());
            sessions.clear();
        }
        CancelRestart();
        const auto reopen = [&] {
            if (sessions.empty()) return ReopenNotepad();
            bool opened = true;
            for (const auto& session : sessions)
                if (!ReopenNotepad(session)) opened = false;
            return opened;
        };
        try
        {
            SaveActivePalette(replacement);
            try { SetNotepadMode(nppSettings / L"config.xml", IsDark(replacement[Default].background)); }
            catch (const std::exception& error)
            {
                const auto message = L"The syntax palette was saved. " + Wide(error.what());
                MessageBoxW(window, message.c_str(), L"RainSyntax Settings", MB_OK | MB_ICONWARNING);
            }
        }
        catch (...)
        {
            reopen();
            throw;
        }
        if (reopen())
        {
            if (ownerProcess) EndDialog(window, 0);
            else Status(L"Palette applied and Notepad++ reopened.");
        }
        else
        {
            KillTimer(window, OwnerTimer);
            Status(L"Palette applied. Could not reopen Notepad++ automatically.");
        }
    }

    bool ReopenNotepad(const Path& session = {}) const
    {
        const auto executable = Notepad();
        std::wstring args = L"-multiInst";
        if (!session.empty()) args += L" -nosession -openSession \"" + session.wstring() + L"\"";
        return reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", executable.c_str(),
            args.c_str(), executable.parent_path().c_str(), SW_SHOWNORMAL)) > 32;
    }

    std::optional<std::filesystem::file_time_type> NativeSessionTime() const
    {
        std::error_code error;
        const auto timestamp = std::filesystem::last_write_time(nppSettings / L"session.xml", error);
        if (error) return std::nullopt;
        return timestamp;
    }

    bool SaveRestartSessions()
    {
        for (const HANDLE process : closing)
        {
            const DWORD pid = GetProcessId(process);
            const auto session = RestartSessionPath(config, pid, GetCurrentProcessId());
            const HWND bridge = FindEditorBridge(pid);
            DWORD_PTR saved = FALSE;
            if (!bridge || !SendMessageTimeoutW(bridge, SaveRestartSessionMessage, reinterpret_cast<WPARAM>(window), 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &saved) || !saved ||
                GetFileAttributesW(session.c_str()) == INVALID_FILE_ATTRIBUTES)
                return false;
            restartSessions.push_back(session);
        }
        return true;
    }

    void CheckRestart()
    {
        if (!pendingPalette) return;
        if (std::all_of(closing.begin(), closing.end(), [](HANDLE process) {
            return WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
        }))
        {
            FinishRestart();
        }
        else if (GetTickCount64() >= restartDeadline)
        {
            CancelRestart();
            Status(L"Notepad++ is still open. Apply again when you are ready to restart.");
        }
    }

    void CheckOwner()
    {
        if (!pendingPalette && ownerProcess && WaitForSingleObject(ownerProcess, 0) == WAIT_OBJECT_0)
        {
            if (activeColorPicker) activeColorPicker->Close();
            EndDialog(window, 0);
        }
    }

    void SaveActivePalette(const Palette& replacement)
    {
        const auto model = ActivePalette();
        SavePalette(std::filesystem::exists(model) ? model : Preset(DefaultPreset),
            config / LivePaletteFile, replacement);
        activePalette = replacement;
        UpdatePaletteActions();
    }

    void ApplyLive()
    {
        SaveActivePalette(palette);
        const auto program = Notepad();
        NotepadWindows targets(program);
        EnumWindows(NotepadWindows::Find, reinterpret_cast<LPARAM>(&targets));
        bool notified = false;
        for (const auto process : targets.processes)
        {
            if (const auto bridge = FindEditorBridge(GetProcessId(process)))
            {
                DWORD_PTR loaded = FALSE;
                if (SendMessageTimeoutW(bridge, ReloadLivePaletteMessage, 0, 0,
                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &loaded) && loaded) notified = true;
            }
            CloseHandle(process);
        }
        Status(notified ? L"Palette applied to Rainmeter tabs." : L"Palette saved for the next Notepad++ launch.");
    }

    void Apply()
    {
        if (presetPreview) return;
        if (pendingPalette)
        {
            CancelRestart();
            Status(L"Restart canceled. The palette was left as it was.");
            return;
        }
        const auto currentMode = ReadEditorMode(ownerId);
        const bool dark = IsDark(palette[Default].background);
        const auto paletteMode = dark ? EditorMode::Dark : EditorMode::Light;
        if (currentMode == EditorMode::Unknown || currentMode == paletteMode)
        {
            ApplyLive();
            return;
        }
        const auto prompt = dark ?
            L"Switch Notepad++ to dark mode and restart?\n\nChoose No to apply the palette and keep the current mode." :
            L"Switch Notepad++ to light mode and restart?\n\nChoose No to apply the palette and keep the current mode.";
        const auto choice = MessageBoxW(window, prompt, L"Match Notepad++ mode",
            MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (choice == IDNO) ApplyLive();
        if (choice != IDYES) return;
        const auto program = Notepad();
        NotepadWindows targets(program);
        EnumWindows(NotepadWindows::Find, reinterpret_cast<LPARAM>(&targets));
        closing = std::move(targets.processes);
        pendingPalette = palette;
        if (closing.empty()) { FinishRestart(); return; }
        nativeSessionBefore = NativeSessionTime();
        if (!SaveRestartSessions())
        {
            CancelRestart();
            Status(L"Could not save the open tabs. Notepad++ is still open.");
            return;
        }
        restartDeadline = GetTickCount64() + 60000;
        SetDlgItemTextW(window, IDC_APPLY, L"Cancel restart");
        EnableDialogEdits(false);
        Status(L"Waiting for Notepad++ to close. Save open files if prompted.");
        if (!SetTimer(window, RestartTimer, 200, nullptr))
        {
            CancelRestart();
            throw std::runtime_error("Could not start the Notepad++ restart.");
        }
        for (HWND target : targets.windows) PostMessageW(target, WM_CLOSE, 0, 0);
    }

    void Close()
    {
        if ((wordsDirty || PaletteDirty()) && MessageBoxW(window,
            L"Close settings with unapplied vocabulary or palette changes?", L"RainSyntax Settings",
            MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        if (pendingPalette) CancelRestart();
        EndDialog(window, 0);
    }

    INT_PTR Command(WORD id, WORD event)
    {
        switch (id)
        {
        case IDC_PRESET:
            if (event == CBN_SELCHANGE) PreviewPreset();
            if (event == CBN_DROPDOWN && presetList)
            {
                RefreshPresetShift();
                SetTimer(window, PresetShiftTimer, 60, nullptr);
            }
            if (event == CBN_CLOSEUP)
            {
                KillTimer(window, PresetShiftTimer);
                presetShift = false;
                presetPressed = -1;
            }
            break;
        case IDC_USE_PRESET:
            if (presetPreview && ReplacePalette())
            {
                palette = *presetPreview;
                ShowCurrent();
                Status(L"Preset ready. Choose Apply to use it.");
            }
            break;
        case IDC_GROUP:
            if (event == CBN_SELCHANGE)
            {
                RememberTerms();
                group = static_cast<size_t>(SendMessageW(Control(id), CB_GETCURSEL, 0, 0));
                ShowTerms();
            }
            break;
        case IDC_TERMS:
            if (event == EN_CHANGE && !updating) wordsDirty = true;
            break;
        case IDC_FIND_NEXT: FindNext(); break;
        case IDC_RESET_TERMS:
            if (MessageBoxW(window, L"Restore the built-in terms for this category?",
                L"RainSyntax Settings", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
            {
                words[group] = ReadVocabulary({})[group];
                wordsDirty = true;
                ShowTerms();
            }
            break;
        case IDC_SAVE_TERMS: SaveTerms(); break;
        case IDC_STYLE_ID:
            if (event == CBN_CLOSEUP) styleTooltips.Hide();
            if (event == CBN_SELCHANGE)
            {
                style = static_cast<size_t>(SendMessageW(Control(id), CB_GETCURSEL, 0, 0));
                UpdateStyleFields();
            }
            break;
        case IDC_FOREGROUND: PickColor(true); break;
        case IDC_BACKGROUND: PickColor(false); break;
        case IDC_ALL_FOREGROUNDS: PickColor(true, true); break;
        case IDC_ALL_BACKGROUNDS: PickColor(false, true); break;
        case IDC_FONT: PickFont(); break;
        case IDC_PALETTE_FONT: PickFont(true); break;
        case IDC_ACTIVE: Load(ActivePalette(), true); break;
        case IDC_IMPORT:
            if (const auto path = Browse(false); !path.empty()) Load(path, false);
            break;
        case IDC_EXPORT:
            if (const auto path = Browse(true); !path.empty())
            {
                std::error_code error;
                if (std::filesystem::equivalent(path, config / StyleFile, error) ||
                    std::filesystem::equivalent(path, config / LivePaletteFile, error))
                {
                    Status(L"Use Apply to Notepad++ to replace the active palette.");
                    break;
                }
                SavePalette(Preset(DefaultPreset), path, palette);
                Status(PaletteDirty() ? L"Palette exported. Choose Apply to update Notepad++." : L"Palette exported.");
            }
            break;
        case IDC_APPLY: Apply(); break;
        case IDC_BACKUP:
            if (const auto folder = ClassicBackup(config, plugin); !folder.empty())
                ShellExecuteW(window, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else Status(L"Setup did not find any original RainLexer files to back up.");
            break;
        case IDC_UNINSTALL:
        {
            const auto path = plugin / UninstallerFile;
            const auto result = ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) == ERROR_FILE_NOT_FOUND)
                Status(L"The uninstaller is missing. Run setup to restore it.");
            break;
        }
        case IDCANCEL: Close(); break;
        default: return FALSE;
        }
        return TRUE;
    }
};

INT_PTR CALLBACK Dialog(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_MEASUREITEM && wParam == IDC_PRESET)
    {
        RECT bounds{0, 0, 0, 10};
        MapDialogRect(window, &bounds);
        reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = std::max(1L, bounds.bottom);
        return TRUE;
    }
    auto* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(window, DWLP_USER));
    try
    {
        if (message == WM_INITDIALOG)
        {
            editor = reinterpret_cast<Editor*>(lParam);
            editor->window = window;
            SetWindowLongPtrW(window, DWLP_USER, lParam);
            SetPropW(window, SettingsInstanceWindow, reinterpret_cast<HANDLE>(1));
            if (HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1)))
                SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
            editor->Init();
            return TRUE;
        }
        if (!editor) return FALSE;
        if (message == WM_WINDOWPOSCHANGED && editor->windowPosition) editor->TrackWindowPosition();
        if (message == WM_DESTROY)
        {
            RemovePropW(window, SettingsInstanceWindow);
            editor->SaveWindowPosition();
        }
        if (message == WM_COMMAND) return editor->Command(LOWORD(wParam), HIWORD(wParam));
        if (message == WM_CLOSE) { editor->Close(); return TRUE; }
        if (message == WM_TIMER && wParam == RestartTimer) { editor->CheckRestart(); return TRUE; }
        if (message == WM_TIMER && wParam == OwnerTimer) { editor->CheckOwner(); return TRUE; }
        if (message == WM_TIMER && wParam == PresetShiftTimer) { editor->RefreshPresetShift(); return TRUE; }
        if (message == RemovePresetMessage) { editor->RemovePreset(static_cast<size_t>(wParam)); return TRUE; }
        if (message == WM_DRAWITEM && wParam == IDC_PRESET)
        {
            editor->DrawPreset(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        if (message == WM_DRAWITEM && (wParam == IDC_FOREGROUND || wParam == IDC_BACKGROUND))
        {
            editor->DrawColorButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        if (message == WM_DRAWITEM && wParam == IDC_PREVIEW)
        {
            editor->DrawPreview(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
    }
    catch (const std::exception& e)
    {
        const auto error = Wide(e.what());
        MessageBoxW(window, error.c_str(), L"RainSyntax Settings", MB_OK | MB_ICONERROR);
        if (message == WM_INITDIALOG) EndDialog(window, 1);
        return TRUE;
    }
    return FALSE;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    const auto instanceLock = CreateMutexExW(nullptr, SettingsInstanceMutex, 0, SYNCHRONIZE);
    if (!instanceLock) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        EnumWindows(ActivateSettingsWindow, 0);
        CloseHandle(instanceLock);
        return 0;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
        CloseHandle(instanceLock);
        return 1;
    }
    wchar_t executable[32768]{};
    GetModuleFileNameW(instance, executable, static_cast<DWORD>(std::size(executable)));
    const auto plugin = Path(executable).parent_path();
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    int result = 1;
    Path config;
    Path nppSettings;
    DWORD owner = 0;
    EditorFont editorFont;
    if (arguments && count >= 3 && count % 2 != 0)
    {
        for (int i = 1; i + 1 < count; i += 2)
        {
            const std::wstring_view name(arguments[i]);
            if (name == L"--config") config = arguments[i + 1];
            else if (name == L"--npp-settings") nppSettings = arguments[i + 1];
            else if (name == L"--owner") owner = wcstoul(arguments[i + 1], nullptr, 10);
            else if (name == L"--editor-font") editorFont.family = arguments[i + 1];
            else if (name == L"--editor-size")
            {
                const int points = _wtoi(arguments[i + 1]);
                if (points > 0) editorFont.points = points;
            }
            else if (name == L"--editor-zoom") editorFont.zoom = _wtoi(arguments[i + 1]);
        }
    }
    else if (count == 1)
    {
        const auto home = plugin.parent_path().parent_path();
        if (std::filesystem::exists(home / L"doLocalConf.xml")) config = home / L"plugins" / L"config";
        else
        {
            wchar_t appData[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
                config = Path(appData) / L"Notepad++" / L"plugins" / L"config";
        }
    }
    if (!config.empty())
    {
        if (nppSettings.empty())
        {
            auto directory = config.lexically_normal();
            if (!directory.has_filename()) directory = directory.parent_path();
            nppSettings = directory.parent_path().parent_path();
        }
        Editor editor(config, plugin, nppSettings, owner, std::move(editorFont));
        result = static_cast<int>(DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_EDITOR), nullptr,
            Dialog, reinterpret_cast<LPARAM>(&editor)));
    }
    else
    {
        MessageBoxW(nullptr, L"Open this app from the RainSyntax settings sidebar in Notepad++.",
            L"RainSyntax Settings", MB_OK | MB_ICONINFORMATION);
    }
    if (arguments) LocalFree(arguments);
    CoUninitialize();
    CloseHandle(instanceLock);
    return result;
}

