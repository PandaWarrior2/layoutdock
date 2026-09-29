// Exercise the real settings UI with an in-memory model: no hooks, registry writes or widget injection.
#include "../src/settings.h"
#include "../src/settings_ids.h"
#include "../src/displays.h"
#include <commctrl.h>
#include <cstdio>

namespace {
HWND panel{};
dock::SettingsState state;
int paints{}, failures{};

void Check(bool ok, const char* label) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
dock::SettingsState Read() { return state; }
void Apply(dock::Setting setting, int64_t value) {
    if (setting == dock::Setting::Startup) state.startup = value != 0;
}
LRESULT CALLBACK Observe(HWND view, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_PAINT) ++paints;
    return DefSubclassProc(view, message, wp, lp);
}
BOOL CALLBACK FindPanel(HWND view, LPARAM) {
    wchar_t title[64]{}; GetWindowTextW(view, title, _countof(title));
    if (wcscmp(title, L"LayoutDock Settings") == 0) panel = view;
    return TRUE;
}
void FlushPaints() {
    UpdateWindow(panel);
    for (HWND child = GetWindow(panel, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) UpdateWindow(child);
}
std::wstring Text(int id) {
    wchar_t text[256]{}; GetDlgItemTextW(panel, id, text, _countof(text)); return text;
}
void Refresh() { SendMessageW(panel, WM_TIMER, 1, 0); FlushPaints(); }
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HWND previous = GetForegroundWindow();
    const auto displays = dock::Displays();
    if (!displays.empty()) state.packet.monitor = reinterpret_cast<uint64_t>(displays.front().monitor);
    dock::ShowSettings(GetModuleHandleW(nullptr), nullptr, Read, Apply);
    EnumThreadWindows(GetCurrentThreadId(), FindPanel, 0);
    if (!panel) { std::puts("FAIL: settings dialog creation"); return 1; }
    // Drive refreshes and mouse messages synchronously so no desktop input or wall-clock timer can race the assertions.
    KillTimer(panel, 1); KillTimer(panel, 86);
    SetWindowSubclass(panel, Observe, 2, 0);
    for (HWND child = GetWindow(panel, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SetWindowSubclass(child, Observe, 2, 0);
    RedrawWindow(panel, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);

    paints = 0;
    for (int i = 0; i < 5; ++i) Refresh();
    std::printf("Unchanged refresh paint count: %d\n", paints);
    Check(paints == 0, "unchanged settings do not repaint on timer refresh");

    for (int id : {IDCANCEL, IDC_EXIT_APP, IDC_HOTKEY, IDC_RESET_HOTKEY, IDC_RESET_POSITION,
                   IDC_STARTUP, IDC_MODE, IDC_MONITOR, IDC_SPIN_X, IDC_SPIN_Y, IDC_REPOSITORY}) {
        const HWND child = GetDlgItem(panel, id);
        SendMessageW(child, WM_MOUSELEAVE, 0, 0); FlushPaints();
        paints = 0;
        SendMessageW(child, WM_MOUSEMOVE, 0, MAKELPARAM(4, 4)); FlushPaints();
        Check(paints > 0, "mouse entry updates the control");
        paints = 0;
        for (int step = 0; step < 50; ++step) {
            SendMessageW(child, WM_MOUSEMOVE, 0, MAKELPARAM(4 + step % 4, 4)); FlushPaints();
        }
        std::printf("Control %d, repeated mouse-move paint count: %d\n", id, paints);
        Check(paints == 0, "motion inside a control does not repaint it");
        paints = 0;
        SendMessageW(child, WM_MOUSELEAVE, 0, 0); FlushPaints();
        Check(paints > 0, "mouse leave updates the control");
    }

    state.packet.docked = 0;
    state.hotkey = {0, MOD_ALT | MOD_SHIFT};
    state.startup = true;
    state.hotkeyError = L"Test status";
    paints = 0; Refresh();
    Check(paints > 0 && SendDlgItemMessageW(panel, IDC_MODE, CB_GETCURSEL, 0, 0) == 1 &&
        !IsWindowEnabled(GetDlgItem(panel, IDC_OFFSET_X)), "changed mode still refreshes and disables offsets");
    Check(Text(IDC_HOTKEY) == L"Alt + Shift" && Text(IDC_SAVE_STATUS) == state.hotkeyError,
        "changed hotkey and status remain visible");
    Check(IsDlgButtonChecked(panel, IDC_STARTUP) == BST_CHECKED, "changed startup state remains visible");
    SendDlgItemMessageW(panel, IDC_STARTUP, BM_CLICK, 0, 0); FlushPaints();
    Check(!state.startup && IsDlgButtonChecked(panel, IDC_STARTUP) == BST_UNCHECKED,
        "native checkbox interaction still updates the in-memory model");

    for (int id : {IDC_MODE, IDC_MONITOR}) {
        SendDlgItemMessageW(panel, id, CB_SHOWDROPDOWN, TRUE, 0); Refresh();
        Check(SendDlgItemMessageW(panel, id, CB_GETDROPPEDSTATE, 0, 0) != 0,
            "native dropdown remains open during refresh");
        SendDlgItemMessageW(panel, id, CB_SHOWDROPDOWN, FALSE, 0);
    }
    SendMessageW(panel, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(panel, IDC_HOTKEY)), TRUE);
    SendMessageW(panel, WM_NEXTDLGCTL, FALSE, FALSE);
    Check(GetFocus() == GetDlgItem(panel, IDC_RESET_HOTKEY), "keyboard navigation still traverses controls");
    RECT rect{}; GetWindowRect(panel, &rect);
    SetWindowPos(panel, nullptr, 0, 0, rect.right - rect.left, 400, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageW(panel, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(panel, IDC_EXIT_APP)), TRUE);
    RECT client{}; GetClientRect(panel, &client);
    GetWindowRect(GetDlgItem(panel, IDC_EXIT_APP), &rect);
    MapWindowPoints(nullptr, panel, reinterpret_cast<POINT*>(&rect), 2);
    Check(rect.top >= 0 && rect.bottom <= client.bottom, "scrolling keeps the focused button visible");
    dock::CloseSettings();
    if (IsWindow(previous)) SetForegroundWindow(previous);
    return failures ? 1 : 0;
}
