// Exercises the actual modeless window and widget; does not change Windows languages or startup.
#include "../src/displays.h"
#include "../src/settings_ids.h"
#include <commctrl.h>
#include <cstdio>
#include <stdexcept>
#include <string>

static HWND controller{}, dialog{};
static LRESULT Query(WPARAM index) { return SendMessageW(controller, dock::Query, index, 0); }
static void Check(bool ok, const char* label) {
    if (!ok) throw std::runtime_error(label);
    std::printf("PASS: %s\n", label);
}
static HWND Settings() {
    HWND result{};
    DWORD process{}; GetWindowThreadProcessId(controller, &process);
    for (HWND candidate = FindWindowW(L"#32770", L"LayoutDock Settings"); candidate;
        candidate = FindWindowExW(nullptr, candidate, L"#32770", L"LayoutDock Settings")) {
        DWORD owner{}; GetWindowThreadProcessId(candidate, &owner);
        if (owner == process) { result = candidate; break; }
    }
    return result;
}
static void EditText(int id, const std::wstring& text) {
    SendDlgItemMessageW(dialog, id, EM_SETSEL, 0, -1);
    for (wchar_t character : text) SendDlgItemMessageW(dialog, id, WM_CHAR, character, 0);
}
static void Edit(int id, int value) { EditText(id, std::to_wstring(value)); }
static void Select(int id, int index) {
    SendDlgItemMessageW(dialog, id, CB_SETCURSEL, index, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(dialog, id)));
}
static RECT WidgetRect() { RECT rect{}; GetWindowRect(dock::FindWidget(), &rect); return rect; }
static std::wstring Text(int id) {
    wchar_t text[128]{};
    SendMessageW(GetDlgItem(dialog, id), WM_GETTEXT, _countof(text), reinterpret_cast<LPARAM>(text));
    return text;
}
static void Open() {
    SendMessageW(controller, dock::Settings, 0, 0);
    // Wait for the controls and initial values before inspecting the visible window.
    for (int attempt = 0; attempt < 20; ++attempt) {
        Sleep(50); dialog = Settings();
        if (dialog && IsWindowVisible(dialog) && !Text(IDC_OFFSET_Y).empty()) break;
    }
    Check(dialog && IsWindowVisible(dialog), "settings window is visible");
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    controller = FindWindowW(dock::ControllerClass, nullptr);
    if (!controller) { std::puts("Run LayoutDock first."); return 1; }
    const LRESULT mode = Query(3), monitor = Query(4), floatingX = Query(5), floatingY = Query(6), x = Query(7), y = Query(8);
    int result = 0;
    try {
        Open();
        const HWND original = dialog;
        SendMessageW(controller, dock::Settings, 0, 0); Sleep(150);
        Check(Settings() == original, "opening settings again reuses the same window");
        Check(SendDlgItemMessageW(dialog, IDC_LAYOUTS, LB_GETCOUNT, 0, 0) == Query(2), "settings list contains every Windows layout");
        Select(IDC_MODE, 0); Edit(IDC_OFFSET_X, 0); Edit(IDC_OFFSET_Y, 0); Sleep(1800);
        Check(GetParent(dock::FindWidget()) != nullptr, "taskbar mode selected in settings");
        const RECT baseline = WidgetRect();
        Edit(IDC_OFFSET_X, -37); Edit(IDC_OFFSET_Y, -3); Sleep(150);
        RECT shifted = WidgetRect();
        const double scale = GetDpiForWindow(dock::FindWidget()) / 96.0;
        Check(Query(7) == -37 && Query(8) == -3, "negative offsets applied immediately");
        Check(abs(shifted.left - baseline.left + static_cast<int>(37 * scale)) <= 1 &&
            abs(shifted.top - baseline.top + static_cast<int>(3 * scale)) <= 1, "widget actually moves left and up");
        Edit(IDC_OFFSET_X, 17); Edit(IDC_OFFSET_Y, 3); Sleep(150); shifted = WidgetRect();
        Check(abs(shifted.left - baseline.left - static_cast<int>(17 * scale)) <= 1 &&
            abs(shifted.top - baseline.top - static_cast<int>(3 * scale)) <= 1, "widget actually moves right and down");
        SendDlgItemMessageW(dialog, IDC_OFFSET_X, EM_SETSEL, 0, -1);
        SendDlgItemMessageW(dialog, IDC_OFFSET_X, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"abc"));
        SendDlgItemMessageW(dialog, IDC_OFFSET_Y, EM_SETSEL, 0, -1);
        SendDlgItemMessageW(dialog, IDC_OFFSET_Y, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"999999999999"));
        Check(Query(7) == 17 && Query(8) == 3, "invalid and overflowing text cannot change offsets");
        Edit(IDC_OFFSET_X, 17); Edit(IDC_OFFSET_Y, 3);
        SendDlgItemMessageW(dialog, IDC_OFFSET_X, WM_KEYDOWN, VK_UP, 0);
        SendDlgItemMessageW(dialog, IDC_OFFSET_X, WM_KEYUP, VK_UP, 0);
        Check(Query(7) == 18, "spinner changes offset by one");
        Edit(IDC_OFFSET_X, 3000); Edit(IDC_OFFSET_Y, -3000); Sleep(150);
        RECT bar{}; GetWindowRect(GetParent(dock::FindWidget()), &bar); shifted = WidgetRect();
        Check(shifted.left >= bar.left && shifted.right <= bar.right && shifted.top >= bar.top && shifted.bottom <= bar.bottom,
            "large offsets keep the widget within taskbar bounds");
        SendDlgItemMessageW(dialog, IDC_RESET_POSITION, BM_CLICK, 0, 0);
        Check(Query(7) == 0 && Query(8) == 0, "reset clears both offsets");
        Select(IDC_MODE, 1); Sleep(150);
        Check(Query(3) == 0 && !GetParent(dock::FindWidget()) && !IsWindowEnabled(GetDlgItem(dialog, IDC_OFFSET_X)),
            "floating mode disables taskbar offsets");
        const auto displays = dock::Displays();
        for (size_t i = 0; i < displays.size(); ++i) {
            Select(IDC_MONITOR, static_cast<int>(i)); Sleep(150);
            Check(Query(4) == reinterpret_cast<LRESULT>(displays[i].monitor) &&
                MonitorFromWindow(dock::FindWidget(), MONITOR_DEFAULTTONULL) == displays[i].monitor,
                "monitor selector moves the actual floating widget");
        }
        Select(IDC_MODE, 0); Edit(IDC_OFFSET_X, -23); Edit(IDC_OFFSET_Y, 2);
        Check(Query(7) == -23 && Query(8) == 2, "nonzero offsets are ready before closing");
        SendMessageW(dialog, WM_CLOSE, 0, 0); dialog = nullptr;
        Check(!Settings() && IsWindow(controller) && dock::FindWidget(), "closing settings keeps the application and widget running");
        Open();
        Check(Text(IDC_OFFSET_X) == L"-23" && Text(IDC_OFFSET_Y) == L"2", "reopening preserves both offsets");
        wchar_t folder[MAX_PATH]{}; GetEnvironmentVariableW(L"LOCALAPPDATA", folder, _countof(folder));
        const std::wstring path = std::wstring(folder) + L"\\LayoutDock\\settings.ini";
        Check(static_cast<int>(GetPrivateProfileIntW(L"Widget", L"Offset", 0, path.c_str())) == -23 &&
            GetPrivateProfileIntW(L"Widget", L"OffsetY", 0, path.c_str()) == 2, "both offsets persisted to disk");
    } catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); result = 1; }
    // Always restore the user's preferences, including the saved floating position.
    if (!dialog) { SendMessageW(controller, dock::Settings, 0, 0); dialog = Settings(); }
    if (dialog) { Edit(IDC_OFFSET_X, static_cast<int>(x)); Edit(IDC_OFFSET_Y, static_cast<int>(y)); }
    SendMessageW(controller, dock::MoveMonitor, static_cast<WPARAM>(monitor), 0);
    SendMessageW(controller, WM_COMMAND, mode ? 201 : 202, 0); Sleep(150);
    SendMessageW(controller, dock::Moved, static_cast<WPARAM>(floatingX), floatingY);
    if (dialog) SendMessageW(dialog, WM_CLOSE, 0, 0);
    return result;
}
