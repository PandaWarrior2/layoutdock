// Uses a disposable text field and the actual Windows layouts; never modifies installed languages.
#include "../src/layout_catalog.h"
#include "../src/widget_layout.h"
#include "../src/displays.h"
#include <windowsx.h>
#include <cstdio>
#include <functional>

namespace {
HWND control{}, target{}, edit{};
std::vector<dock::Layout> catalog;
bool failed = false;
void Pump(DWORD milliseconds) {
    ULONGLONG end = GetTickCount64() + milliseconds;
    do {
        MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
bool Wait(const std::function<bool()>& predicate) {
    for (int i = 0; i < 200; ++i) { Pump(20); if (predicate()) return true; }
    return false;
}
void Check(bool ok, const char* label) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label); failed |= !ok;
}
uint64_t Current() { return reinterpret_cast<uintptr_t>(GetKeyboardLayout(GetCurrentThreadId())); }
uint64_t Query(WPARAM field) { return static_cast<uint64_t>(SendMessageW(control, dock::Query, field, 0)); }
bool Selected(uint64_t id) { return Current() == id && Query(0) == id; }
void Select(uint64_t id) { PostMessageW(control, dock::Select, static_cast<WPARAM>(id), 0); }
void Key(WORD vk, bool up = false) {
    if (GetForegroundWindow() != target && !up) { failed = true; return; }
    INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = vk;
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof(input)); Pump(30);
}
HWND View() {
    return dock::FindWidget();
}
void ClickFirst() {
    if (GetForegroundWindow() != target) { Check(false, "test window still foreground before click"); return; }
    HWND view = View();
    Check(view != nullptr, "widget exists"); if (!view) return;
    RECT rect{}; GetWindowRect(view, &rect);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(view, MONITOR_DEFAULTTONEAREST), &monitor);
    float scale = GetDpiForWindow(view) / 96.f;
    auto grid = dock::MakeGrid(catalog, (monitor.rcWork.right - monitor.rcWork.left) / scale - 24);
    Check((rect.right - rect.left) == static_cast<LONG>(std::lround(grid.width * scale)), "widget width matches live catalog");
    Check(SendMessageW(view, WM_MOUSEACTIVATE, 0, MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE, "widget rejects mouse activation");
    POINT previous{}; GetCursorPos(&previous);
    SetCursorPos(rect.left + static_cast<int>((grid.X(0) + (grid.cell - 4) / 2) * scale), rect.top + static_cast<int>(18 * scale));
    INPUT mouse[2]{}; mouse[0].type = mouse[1].type = INPUT_MOUSE;
    mouse[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; mouse[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, mouse, sizeof(INPUT));
    Check(Wait([] { return Selected(catalog[0].id); }), "real widget click selects full HKL");
    Check(Wait([] { return GetForegroundWindow() == target && GetFocus() == edit; }), "widget click preserves text field focus");
    SetCursorPos(previous.x, previous.y);
}
LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) { return DefWindowProcW(hwnd, message, wp, lp); }
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    control = FindWindowW(dock::ControllerClass, nullptr);
    if (!control) { std::puts("FAIL: LayoutDock must be running"); return 1; }
    auto installed = dock::InstalledLayouts();
    Check(Query(2) == installed.size(), "controller count matches Windows");
    std::vector<uint64_t> ordered;
    for (size_t i = 0; i < Query(2); ++i) {
        uint64_t id = static_cast<uint64_t>(SendMessageW(control, dock::QueryLayout, i, 0));
        ordered.push_back(id);
        Check(std::find(installed.begin(), installed.end(), id) != installed.end(), "controller layout exists in Windows");
    }
    catalog = dock::MakeCatalog(ordered);
    Check(ordered == dock::OrderLayouts(installed, dock::WindowsOrder()), "widget order matches Windows switcher settings");
    if (catalog.empty() || failed) return 1;
    HWND original = GetForegroundWindow();
    HKL initial = GetKeyboardLayout(GetWindowThreadProcessId(original, nullptr));
    bool originalDocked = Query(3) != 0;
    auto originalMonitor = static_cast<WPARAM>(Query(4));
    int originalX = static_cast<int>(Query(5)), originalY = static_cast<int>(Query(6));
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LayoutDock.IntegrationTest"; wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    target = CreateWindowExW(0, wc.lpszClassName, L"LayoutDock — Windows layout test", WS_OVERLAPPEDWINDOW,
        900, 500, 520, 180, nullptr, nullptr, wc.hInstance, nullptr);
    edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"LayoutDock test field", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        16, 24, 460, 32, target, nullptr, wc.hInstance, nullptr);
    ShowWindow(target, SW_SHOW);
    DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    BOOL attached = AttachThreadInput(GetCurrentThreadId(), foregroundThread, TRUE);
    SetForegroundWindow(target); SetFocus(edit);
    if (attached) AttachThreadInput(GetCurrentThreadId(), foregroundThread, FALSE);
    Pump(400);
    if (GetForegroundWindow() != target) { std::puts("INTERRUPTED: no input sent"); DestroyWindow(target); return 2; }
    for (const auto& item : catalog) {
        Select(item.id); Check(Wait([&item] { return Selected(item.id); }), "select installed layout in focused edit");
    }
    uint64_t last = catalog.back().id;
    uint64_t previous = catalog.size() > 1 ? catalog[catalog.size() - 2].id : 0;
    Check(Query(1) == previous, "history tracks previous full HKL");
    SHORT capsBefore = GetKeyState(VK_CAPITAL) & 1;
    Key(VK_CAPITAL); Key(VK_CAPITAL, true);
    Check(Wait([&] { return Selected(previous ? previous : last); }), "CapsLock toggles pair or stays on sole layout");
    Check((GetKeyState(VK_CAPITAL) & 1) == capsBefore, "CapsLock does not toggle uppercase state");
    Key(VK_CAPITAL); Pump(150); Key(VK_CAPITAL); Key(VK_CAPITAL); Key(VK_CAPITAL, true);
    Check(Wait([&] { return Selected(last); }), "held/repeated CapsLock switches once");
    Key(VK_LSHIFT); Key(VK_CAPITAL); Key(VK_CAPITAL, true); Key(VK_LSHIFT, true);
    Check((GetKeyState(VK_CAPITAL) & 1) != capsBefore && Current() == last, "Shift+CapsLock retains uppercase behavior");
    Key(VK_LSHIFT); Key(VK_CAPITAL); Key(VK_CAPITAL, true); Key(VK_LSHIFT, true);
    if (GetForegroundWindow() != target) { std::puts("INTERRUPTED: no mouse input sent"); DestroyWindow(target); return 2; }
    SendMessageW(control, WM_COMMAND, 201, 0); Pump(150); ClickFirst();
    if (catalog.size() > 1) {
        PostMessageW(edit, WM_INPUTLANGCHANGEREQUEST, 0, static_cast<LPARAM>(last));
        Check(Wait([&] { return Selected(last) && Query(1) == catalog[0].id; }), "external layout switch updates pair");
    }
    SendMessageW(control, WM_COMMAND, 202, 0);
    Check(Wait([] { return FindWindowW(dock::WidgetClass, nullptr) != nullptr; }), "switch to floating mode");
    ClickFirst();
    for (const auto& display : dock::Displays()) {
        SendMessageW(control, dock::MoveMonitor, reinterpret_cast<WPARAM>(display.monitor), 0);
        Check(Wait([&display] { return MonitorFromWindow(View(), MONITOR_DEFAULTTONULL) == display.monitor; }), "floating widget moves to selected monitor");
        if (display.taskbar) {
            SendMessageW(control, WM_COMMAND, 201, 0);
            Check(Wait([&display] { return GetParent(View()) == display.taskbar; }), "widget embeds in selected monitor taskbar");
            ClickFirst();
            SendMessageW(control, WM_COMMAND, 202, 0);
        }
    }
    SendMessageW(control, dock::MoveMonitor, originalMonitor, 0);
    Wait([&] { return MonitorFromWindow(View(), MONITOR_DEFAULTTONULL) == reinterpret_cast<HMONITOR>(originalMonitor); });
    SendMessageW(control, dock::Moved, static_cast<WPARAM>(originalX), originalY);
    SendMessageW(control, WM_COMMAND, originalDocked ? 201 : 202, 0);
    PostMessageW(edit, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(initial)); Pump(200);
    DestroyWindow(target); if (IsWindow(original)) SetForegroundWindow(original);
    std::puts(failed ? "INTEGRATION FAILED" : "INTEGRATION PASSED");
    return failed ? 1 : 0;
}
