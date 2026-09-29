// Exercises the native popup without changing Windows appearance, languages or user settings.
#include "../src/displays.h"
#include "../src/settings_ids.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace {
HWND controller{}, panel{}, target{};
void Check(bool ok, const char* label) {
    if (!ok) throw std::runtime_error(label);
    std::printf("PASS: %s\n", label);
}
void Pump(DWORD delay) {
    const auto until = GetTickCount64() + delay;
    do {
        MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    } while (GetTickCount64() < until);
}
bool Wait(const std::function<bool()>& condition) {
    for (int i = 0; i < 60; ++i) { Pump(30); if (condition()) return true; }
    return false;
}
HWND FindPanel() { return FindWindowW(L"#32770", L"LayoutDock Settings"); }
void FocusTarget() {
    SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    const DWORD thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const BOOL attached = AttachThreadInput(GetCurrentThreadId(), thread, TRUE);
    SetForegroundWindow(target);
    if (attached) AttachThreadInput(GetCurrentThreadId(), thread, FALSE);
    Check(Wait([] { return GetForegroundWindow() == target; }), "test window owns focus");
    // Attaching input queues alone does not always grant foreground permission.
    // Use real input in our own window, then restore the cursor used to place the flyout.
    POINT cursor{}, point{20, 30}; GetCursorPos(&cursor); ClientToScreen(target, &point);
    if (WindowFromPoint(point) != target) {
        wchar_t cls[128]{}; GetClassNameW(WindowFromPoint(point), cls, _countof(cls));
        RECT rect{}; GetWindowRect(target, &rect);
        std::wprintf(L"Handoff position: %ld,%ld; test bounds: %ld,%ld,%ld,%ld; covering class: %ls\n",
            point.x, point.y, rect.left, rect.top, rect.right, rect.bottom, cls);
    }
    Check(WindowFromPoint(point) == target, "foreground handoff target is an unobscured test window");
    SetCursorPos(point.x, point.y);
    INPUT input[2]{}; input[0].type = input[1].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    const UINT sent = SendInput(2, input, sizeof(INPUT));
    Pump(40); SetCursorPos(cursor.x, cursor.y);
    Check(sent == 2 && GetForegroundWindow() == target, "test window receives input before foreground handoff");
}
void Open(bool tray = false) {
    FocusTarget();
    DWORD process{}; GetWindowThreadProcessId(controller, &process);
    Check(AllowSetForegroundWindow(process) != FALSE, "settings process receives foreground permission");
    if (tray) PostMessageW(controller, WM_APP + 1, 1, WM_RBUTTONUP);
    else PostMessageW(dock::FindWidget(), WM_RBUTTONUP, 0, 0);
    const bool opened = Wait([] { panel = FindPanel(); return panel && IsWindowVisible(panel) && GetForegroundWindow() == panel; });
    if (!opened) {
        wchar_t cls[128]{}; GetClassNameW(GetForegroundWindow(), cls, _countof(cls));
        std::printf("Open check: panel=%d visible=%d active=%d\n", IsWindow(panel), IsWindowVisible(panel), GetForegroundWindow() == panel);
        std::wprintf(L"Foreground class: %ls\n", cls);
    }
    Check(opened, tray ? "tray right-click opens settings directly" : "widget right-click opens settings directly");
    Pump(180);
}
void Close() { if (IsWindow(panel)) SendMessageW(panel, WM_CLOSE, 0, 0); panel = nullptr; }
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    controller = FindWindowW(dock::ControllerClass, nullptr);
    if (!controller) { std::puts("Run LayoutDock first."); return 1; }
    POINT originalCursor{}; GetCursorPos(&originalCursor); const HWND originalWindow = GetForegroundWindow();
    WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"LayoutDock.FlyoutTest"; RegisterClassW(&wc);
    target = CreateWindowExW(0, wc.lpszClassName, L"LayoutDock — flyout test", WS_OVERLAPPEDWINDOW,
        60, 60, 400, 160, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(target, SW_SHOW); Pump(100);
    int result = 0;
    try {
        if (auto existing = FindPanel()) SendMessageW(existing, WM_CLOSE, 0, 0);
        Open(true);
        Check((GetWindowLongPtrW(panel, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 &&
            !(GetWindowLongPtrW(panel, GWL_EXSTYLE) & WS_EX_APPWINDOW), "flyout has no separate taskbar entry");
        RECT rect{}, client{}; GetWindowRect(panel, &rect); GetClientRect(panel, &client);
        Check(rect.bottom - rect.top == client.bottom, "flyout has no traditional title bar");
        DWM_WINDOW_CORNER_PREFERENCE corner{};
        Check(SUCCEEDED(DwmGetWindowAttribute(panel, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner))) &&
            corner == DWMWCP_ROUND, "native Windows 11 rounded corners requested");
        for (int id : {IDC_MODE, IDC_MONITOR}) {
            SendDlgItemMessageW(panel, id, CB_SHOWDROPDOWN, TRUE, 0);
            // Exercise the refresh explicitly, without waiting on an unrelated desktop app to take focus.
            SendMessageW(panel, WM_TIMER, 1, 0); Pump(60);
            if (!IsWindow(panel) || !SendDlgItemMessageW(panel, id, CB_GETDROPPEDSTATE, 0, 0)) {
                std::printf("Dropdown check: id=%d panel=%d active=%d dropped=%lld\n", id, IsWindow(panel),
                    GetForegroundWindow() == panel, SendDlgItemMessageW(panel, id, CB_GETDROPPEDSTATE, 0, 0));
                wchar_t cls[128]{}; GetClassNameW(GetForegroundWindow(), cls, 128); std::wprintf(L"Foreground class: %s\n", cls);
            }
            Check(IsWindow(panel) && SendDlgItemMessageW(panel, id, CB_GETDROPPEDSTATE, 0, 0),
                "native dropdown stays open across refresh and activation changes");
            SendDlgItemMessageW(panel, id, CB_SHOWDROPDOWN, FALSE, 0);
        }
        SendMessageW(panel, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(panel, IDC_HOTKEY)), TRUE);
        PostMessageW(GetDlgItem(panel, IDC_HOTKEY), WM_KEYDOWN, VK_TAB, 0); Pump(80);
        GUITHREADINFO gui{sizeof(gui)}; GetGUIThreadInfo(GetWindowThreadProcessId(panel, nullptr), &gui);
        if (gui.hwndFocus != GetDlgItem(panel, IDC_RESET_HOTKEY))
            std::printf("Tab check: actual control=%d panel=%d active=%d\n", GetDlgCtrlID(gui.hwndFocus), IsWindow(panel), GetForegroundWindow() == panel);
        Check(gui.hwndFocus == GetDlgItem(panel, IDC_RESET_HOTKEY), "Tab traverses native accessible controls");
        PostMessageW(panel, WM_KEYDOWN, VK_ESCAPE, 0);
        Check(Wait([] { return !FindPanel(); }), "Escape dismisses the flyout");
        Check(IsWindow(controller) && dock::FindWidget(), "dismissing settings leaves widget running");
        for (const auto& display : dock::Displays()) {
            MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(display.monitor, &monitor);
            for (const auto& point : {POINT{monitor.rcMonitor.right - 12, monitor.rcMonitor.bottom - 12},
                POINT{monitor.rcMonitor.left + 12, monitor.rcMonitor.top + 12}}) {
                SetCursorPos(point.x, point.y); Open(); GetWindowRect(panel, &rect);
                Check(MonitorFromWindow(panel, MONITOR_DEFAULTTONULL) == display.monitor &&
                    rect.left >= monitor.rcWork.left && rect.right <= monitor.rcWork.right &&
                    rect.top >= monitor.rcWork.top && rect.bottom <= monitor.rcWork.bottom,
                    "flyout remains on the invoked monitor and inside its work area");
                Close();
            }
        }
        Open();
        GetWindowRect(panel, &rect);
        SetWindowPos(panel, nullptr, 0, 0, rect.right - rect.left, 400, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(panel, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(panel, IDC_EXIT_APP)), TRUE); Pump(80);
        GetWindowRect(GetDlgItem(panel, IDC_EXIT_APP), &rect); MapWindowPoints(nullptr, panel, reinterpret_cast<POINT*>(&rect), 2);
        GetClientRect(panel, &client);
        Check(rect.top >= client.top && rect.bottom <= client.bottom, "short work areas scroll focused controls into view");
        SendMessageW(panel, WM_VSCROLL, SB_TOP, 0); Pump(80);
        GetWindowRect(GetDlgItem(panel, IDCANCEL), &rect); MapWindowPoints(nullptr, panel, reinterpret_cast<POINT*>(&rect), 2);
        Check(rect.top >= 0 && rect.bottom <= client.bottom, "scrolling returns to the header");
        Close();
        SetCursorPos(originalCursor.x, originalCursor.y); Open();
        // Click only the test's own client area, and refuse input if another window covers it.
        POINT point{20, 30}; ClientToScreen(target, &point);
        Check(WindowFromPoint(point) == target, "outside-click target is an unobscured test window");
        SetCursorPos(point.x, point.y);
        INPUT mouse[2]{}; mouse[0].type = mouse[1].type = INPUT_MOUSE;
        mouse[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; mouse[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        Check(SendInput(2, mouse, sizeof(INPUT)) == 2, "outside click delivered to test window");
        Check(Wait([] { return !FindPanel() && GetForegroundWindow() == target; }), "clicking outside dismisses the flyout");
    } catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); result = 1; }
    Close(); DestroyWindow(target); SetCursorPos(originalCursor.x, originalCursor.y);
    if (IsWindow(originalWindow)) SetForegroundWindow(originalWindow);
    return result;
}
