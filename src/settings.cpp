#include "settings.h"
#include "settings_ids.h"
#include "settings_flyout.h"
#include "app_info.h"
#include "displays.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

namespace dock {
namespace {
HWND dialog{};
ReadSettings readSettings{};
ApplySetting applySetting{};
bool refreshing{};
bool recording{}, recordPending{};
UINT_PTR recordSession{};
constexpr UINT RecordedHotkey = WM_APP + 80;
constexpr UINT CheckActivation = WM_APP + 81;
std::vector<Display> monitors;
std::vector<Layout> layouts;
constexpr int OffsetLimit = 3000;

void SetTextIfChanged(int id, const wchar_t* text) {
    const HWND control = GetDlgItem(dialog, id);
    std::wstring current(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    GetWindowTextW(control, current.data(), static_cast<int>(current.size()));
    if (wcscmp(current.c_str(), text)) SetWindowTextW(control, text);
}

void SelectIfChanged(int id, int index) {
    if (!SendDlgItemMessageW(dialog, id, CB_GETDROPPEDSTATE, 0, 0) &&
        SendDlgItemMessageW(dialog, id, CB_GETCURSEL, 0, 0) != index)
        SendDlgItemMessageW(dialog, id, CB_SETCURSEL, index, 0);
}

bool ReadOffset(int control, int& value) {
    wchar_t text[32]{};
    GetDlgItemTextW(dialog, control, text, _countof(text));
    if (!text[0]) return false;
    wchar_t* end{}; errno = 0;
    const long parsed = wcstol(text, &end, 10);
    if (errno == ERANGE || end == text || *end || parsed < -OffsetLimit || parsed > OffsetLimit) return false;
    value = static_cast<int>(parsed); return true;
}

void UpdateStatus() {
    int x{}, y{};
    const bool valid = ReadOffset(IDC_OFFSET_X, x) && ReadOffset(IDC_OFFSET_Y, y);
    const auto state = readSettings();
    SetTextIfChanged(IDC_SAVE_STATUS, !valid ? L"Offset: enter a whole number from −3000 to 3000." :
        !state.hotkeyError.empty() ? state.hotkeyError.c_str() : L"Changes are saved automatically.");
}

void Refresh(bool force = false) {
    if (!dialog) return;
    const auto state = readSettings();
    refreshing = true;
    SelectIfChanged(IDC_MODE, state.packet.docked ? 0 : 1);
    auto currentMonitors = Displays();
    bool changed = currentMonitors.size() != monitors.size();
    for (size_t i = 0; !changed && i < monitors.size(); ++i)
        changed = currentMonitors[i].monitor != monitors[i].monitor || currentMonitors[i].label != monitors[i].label;
    if (changed) {
        monitors = std::move(currentMonitors);
        SendDlgItemMessageW(dialog, IDC_MONITOR, CB_RESETCONTENT, 0, 0);
        for (const auto& display : monitors)
            SendDlgItemMessageW(dialog, IDC_MONITOR, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.label.c_str()));
    }
    int selected = -1;
    for (size_t i = 0; i < monitors.size(); ++i)
        if (reinterpret_cast<uintptr_t>(monitors[i].monitor) == state.packet.monitor) selected = static_cast<int>(i);
    SelectIfChanged(IDC_MONITOR, selected);
    const HWND focus = GetFocus();
    for (const auto& field : {std::pair<int, int>{IDC_OFFSET_X, state.packet.offset}, {IDC_OFFSET_Y, state.packet.offsetY}}) {
        HWND edit = GetDlgItem(dialog, field.first);
        if (force || focus != edit) {
            int value{};
            if (!ReadOffset(field.first, value) || value != field.second)
                SetDlgItemInt(dialog, field.first, field.second, TRUE);
        }
        EnableWindow(edit, state.packet.docked);
    }
    for (int id : {IDC_SPIN_X, IDC_SPIN_Y, IDC_RESET_POSITION}) EnableWindow(GetDlgItem(dialog, id), state.packet.docked);
    SetTextIfChanged(IDC_POSITION_HINT, state.packet.docked ?
        L"Negative: left / up. Positive: right / down.\nOffsets stay within the taskbar bounds." :
        L"Drag the dots on the left to move the widget.\nOffsets are kept for taskbar mode.");
    const UINT checked = state.startup ? BST_CHECKED : BST_UNCHECKED;
    if (IsDlgButtonChecked(dialog, IDC_STARTUP) != checked) CheckDlgButton(dialog, IDC_STARTUP, checked);
    SetTextIfChanged(IDC_HOTKEY, recording || recordPending ? L"Press a shortcut…" : HotkeyName(state.hotkey).c_str());
    SetTextIfChanged(IDC_HOTKEY_HINT, recording || recordPending ?
        L"Press and release your shortcut. Alt + Shift works too.\nPress Esc to cancel." : state.hotkey.Default() ?
        L"Click the shortcut to change it.\nShift + CapsLock toggles capitalization." :
        state.hotkey.AltShift() ? L"Click the shortcut to change it.\nAlt + Shift overrides Windows layout switching." :
        L"Click the shortcut to change it.\nCapsLock toggles capitalization as usual.");
    changed = layouts.size() != state.packet.layouts.size();
    for (size_t i = 0; !changed && i < layouts.size(); ++i)
        changed = layouts[i].id != state.packet.layouts[i].id || wcscmp(layouts[i].name, state.packet.layouts[i].name);
    if (changed) {
        layouts = state.packet.layouts;
        SendDlgItemMessageW(dialog, IDC_LAYOUTS, LB_RESETCONTENT, 0, 0);
        for (const auto& entry : layouts) {
            const std::wstring label = std::wstring(entry.label) + L" · " + entry.name;
            SendDlgItemMessageW(dialog, IDC_LAYOUTS, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
    }
    refreshing = false;
    UpdateStatus();
}

INT_PTR CALLBACK DialogProc(HWND view, UINT message, WPARAM wp, LPARAM lp) {
    INT_PTR result{};
    if (SettingsFlyoutMessage(view, message, wp, lp, result)) return result;
    switch (message) {
    case WM_INITDIALOG: {
        dialog = view;
        refreshing = true;
        SetDlgItemTextW(view, IDC_ABOUT_VERSION, (std::wstring(L"Version ") + AppVersion).c_str());
        SetDlgItemTextW(view, IDC_ABOUT_AUTHOR, (std::wstring(L"Author: ") + AppAuthor).c_str());
        SetDlgItemTextW(view, IDC_REPOSITORY, RepositoryUrl);
        SendDlgItemMessageW(view, IDC_MODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dock in taskbar"));
        SendDlgItemMessageW(view, IDC_MODE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Floating widget"));
        for (const auto& pair : {std::pair<int, int>{IDC_SPIN_X, IDC_OFFSET_X}, {IDC_SPIN_Y, IDC_OFFSET_Y}}) {
            SendDlgItemMessageW(view, pair.first, UDM_SETBUDDY, reinterpret_cast<WPARAM>(GetDlgItem(view, pair.second)), 0);
            SendDlgItemMessageW(view, pair.first, UDM_SETRANGE32, static_cast<WPARAM>(-OffsetLimit), OffsetLimit);
            SendDlgItemMessageW(view, pair.second, EM_SETLIMITTEXT, 12, 0);
        }
        InitializeSettingsFlyout(view);
        Refresh(true);
        SetTimer(view, 1, 1000, nullptr);
        return TRUE;
    }
    case WM_TIMER: if (wp == 1) Refresh(); return TRUE;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            if (recording || recordPending) { recording = recordPending = false; ++recordSession; Refresh(); }
            // Native dropdowns and owned windows are part of the flyout. Check after activation settles.
            PostMessageW(view, CheckActivation, 0, 0);
        }
        return FALSE;
    case CheckActivation: {
        HWND active = GetForegroundWindow();
        if (active != view && !IsChild(view, active) && GetAncestor(active, GA_ROOTOWNER) != GetAncestor(view, GA_ROOTOWNER))
            DestroyWindow(view);
        return TRUE;
    }
    case RecordedHotkey:
        if (static_cast<UINT_PTR>(lp) == recordSession && recordPending) {
            recording = recordPending = false;
            if (wp) applySetting(Setting::Hotkey, static_cast<int64_t>(wp));
            Refresh();
        }
        return TRUE;
    case WM_NOTIFY: {
        const auto* notification = reinterpret_cast<NMHDR*>(lp);
        if (notification && notification->code == UDN_DELTAPOS) {
            const auto* change = reinterpret_cast<NMUPDOWN*>(lp);
            const int id = notification->idFrom == IDC_SPIN_X ? IDC_OFFSET_X : IDC_OFFSET_Y;
            int value{};
            if (!ReadOffset(id, value)) value = 0;
            SetDlgItemInt(view, id, std::clamp(value + change->iDelta, -OffsetLimit, OffsetLimit), TRUE);
            SetWindowLongPtrW(view, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        if (refreshing) return TRUE;
        const int id = LOWORD(wp), event = HIWORD(wp);
        if (id == IDC_HOTKEY && event == BN_KILLFOCUS && (recording || recordPending)) {
            recording = recordPending = false; ++recordSession; Refresh(); return TRUE;
        }
        if ((id == IDC_OFFSET_X || id == IDC_OFFSET_Y) && event == EN_CHANGE) {
            int value{};
            if (ReadOffset(id, value)) applySetting(id == IDC_OFFSET_X ? Setting::OffsetX : Setting::OffsetY, value);
            UpdateStatus(); return TRUE;
        }
        if ((id == IDC_OFFSET_X || id == IDC_OFFSET_Y) && event == EN_KILLFOCUS) { Refresh(); return TRUE; }
        if (id == IDC_MODE && event == CBN_SELCHANGE) {
            applySetting(Setting::Mode, SendDlgItemMessageW(view, id, CB_GETCURSEL, 0, 0) == 0);
        } else if (id == IDC_MONITOR && event == CBN_SELCHANGE) {
            const LRESULT index = SendDlgItemMessageW(view, id, CB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<size_t>(index) < monitors.size())
                applySetting(Setting::Monitor, reinterpret_cast<int64_t>(monitors[index].monitor));
        } else if (id == IDC_RESET_POSITION && event == BN_CLICKED) {
            applySetting(Setting::ResetPosition, 0); Refresh(true); return TRUE;
        } else if (id == IDC_HOTKEY && event == BN_CLICKED) {
            recording = !recording; recordPending = false; ++recordSession;
            SetFocus(GetDlgItem(view, IDC_HOTKEY));
        } else if (id == IDC_RESET_HOTKEY && event == BN_CLICKED) {
            recording = recordPending = false; ++recordSession;
            applySetting(Setting::Hotkey, Hotkey{}.Pack());
        } else if (id == IDC_STARTUP && event == BN_CLICKED) {
            applySetting(Setting::Startup, IsDlgButtonChecked(view, id) == BST_CHECKED);
        } else if (id == IDC_REPOSITORY && event == BN_CLICKED) {
            const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(view, L"open", RepositoryUrl,
                nullptr, nullptr, SW_SHOWNORMAL));
            if (opened <= 32) {
                const std::wstring error = std::wstring(L"Couldn't open the repository. Open this link in your browser:\n\n") + RepositoryUrl;
                MessageBoxW(IsWindow(view) ? view : nullptr, error.c_str(), L"LayoutDock", MB_OK | MB_ICONERROR);
            }
            return TRUE;
        } else if (id == IDC_EXIT_APP && event == BN_CLICKED) {
            applySetting(Setting::Exit, 0); return TRUE;
        } else if (id == IDCANCEL) {
            if (recording || recordPending) { recording = recordPending = false; ++recordSession; Refresh(); }
            else DestroyWindow(view);
            return TRUE;
        }
        else return FALSE;
        Refresh(); return TRUE;
    }
    case WM_CLOSE: DestroyWindow(view); return TRUE;
    case WM_DESTROY:
        recording = recordPending = false; ++recordSession;
        DisposeSettingsFlyout();
        KillTimer(view, 1); dialog = nullptr; monitors.clear(); layouts.clear(); return TRUE;
    }
    return FALSE;
}
}

void ShowSettings(HINSTANCE instance, HWND owner, ReadSettings read, ApplySetting apply) {
    readSettings = read; applySetting = apply;
    if (!dialog) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_UPDOWN_CLASS}; InitCommonControlsEx(&controls);
        CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, DialogProc, 0);
    } else Refresh();
    if (dialog) PresentSettingsFlyout(dialog);
}
bool SettingsMessage(MSG& message) { return dialog && IsDialogMessageW(dialog, &message); }
void CloseSettings() { if (dialog) DestroyWindow(dialog); }
bool RecordingHotkey() {
    return recording && dialog && GetForegroundWindow() == dialog && GetFocus() == GetDlgItem(dialog, IDC_HOTKEY);
}
UINT_PTR HotkeyRecordingSession() { return RecordingHotkey() ? recordSession : 0; }
void RecordHotkey(Hotkey hotkey) {
    if (!RecordingHotkey()) return;
    recording = false; recordPending = true;
    PostMessageW(dialog, RecordedHotkey, hotkey.key == VK_ESCAPE ? 0 : hotkey.Pack(), static_cast<LPARAM>(recordSession));
}
}
