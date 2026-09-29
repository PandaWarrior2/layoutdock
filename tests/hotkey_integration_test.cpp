// Actual keyboard input is sent only while a LayoutDock test/settings window is foreground.
#include "../src/protocol.h"
#include "../src/hotkey.h"
#include "../src/settings_ids.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <vector>

namespace {
HWND controller{}, dialog{}, target{}, edit{};
int longerChordCount{};
LRESULT CALLBACK TestWindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_HOTKEY && wp == 124) { ++longerChordCount; return 0; }
    return DefWindowProcW(hwnd, message, wp, lp);
}
std::vector<WORD> held;
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
    for (int attempt = 0; attempt < 100; ++attempt) { Pump(30); if (condition()) return true; }
    return false;
}
UINT Binding() { return static_cast<UINT>(SendMessageW(controller, dock::Query, 9, 0)); }
std::wstring Text(int id) {
    wchar_t text[256]{};
    SendMessageW(GetDlgItem(dialog, id), WM_GETTEXT, _countof(text), reinterpret_cast<LPARAM>(text));
    return text;
}
void Focus(HWND view) {
    const DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const BOOL attached = AttachThreadInput(GetCurrentThreadId(), foregroundThread, TRUE);
    SetForegroundWindow(view);
    if (view == target) SetFocus(edit);
    if (attached) AttachThreadInput(GetCurrentThreadId(), foregroundThread, FALSE);
    const bool active = Wait([=] { return GetForegroundWindow() == view; });
    if (!active) std::printf("Foreground check: expected window valid=%d, actual thread=%lu, expected thread=%lu\n",
        IsWindow(view), GetWindowThreadProcessId(GetForegroundWindow(), nullptr), GetWindowThreadProcessId(view, nullptr));
    Check(active, "owned window is foreground before keyboard input");
}
void Key(HWND expected, WORD key, bool up = false) {
    if (!up && GetForegroundWindow() != expected) throw std::runtime_error("interrupted: focus changed; no further keys sent");
    INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key;
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (SendInput(1, &input, sizeof(input)) != 1) throw std::runtime_error("SendInput failed");
    if (up) {
        held.erase(std::remove(held.begin(), held.end(), key), held.end());
    } else if (std::find(held.begin(), held.end(), key) == held.end()) held.push_back(key);
    Pump(40);
}
void Stroke(HWND expected, dock::Hotkey hotkey, bool repeat = false) {
    std::vector<WORD> modifiers;
    if (hotkey.modifiers & MOD_CONTROL) modifiers.push_back(VK_LCONTROL);
    if (hotkey.modifiers & MOD_ALT) modifiers.push_back(VK_LMENU);
    if (hotkey.modifiers & MOD_SHIFT) modifiers.push_back(VK_LSHIFT);
    if (hotkey.modifiers & MOD_WIN) modifiers.push_back(VK_LWIN);
    for (WORD key : modifiers) Key(expected, key);
    if (hotkey.key) {
        Key(expected, static_cast<WORD>(hotkey.key));
        if (repeat) { Key(expected, static_cast<WORD>(hotkey.key)); Key(expected, static_cast<WORD>(hotkey.key)); }
        Key(expected, static_cast<WORD>(hotkey.key), true);
    }
    for (auto i = modifiers.rbegin(); i != modifiers.rend(); ++i) Key(expected, *i, true);
}
void Open() {
    // Match the widget's foreground handoff; an unrelated background test process cannot activate a flyout.
    if (GetForegroundWindow() != dialog) Focus(target);
    DWORD controllerProcess{}; GetWindowThreadProcessId(controller, &controllerProcess);
    AllowSetForegroundWindow(controllerProcess);
    SendMessageW(controller, dock::Settings, 0, 0);
    Check(Wait([] {
        dialog = FindWindowW(L"#32770", L"LayoutDock Settings");
        return dialog && IsWindowVisible(dialog) && !Text(IDC_HOTKEY).empty();
    }), "hotkey control is visible in settings");
    Focus(dialog);
}
void Record(dock::Hotkey hotkey) {
    Open();
    SendDlgItemMessageW(dialog, IDC_HOTKEY, BM_CLICK, 0, 0); Pump(80);
    Check(Text(IDC_HOTKEY).find(L"Press") != std::wstring::npos, "recording starts by clicking the hotkey");
    Stroke(dialog, hotkey); Pump(80);
}
void Close() { if (IsWindow(dialog)) SendMessageW(dialog, WM_CLOSE, 0, 0); dialog = nullptr; }
void Restart(const std::wstring& executable) {
    Close();
    DWORD pid{}; GetWindowThreadProcessId(controller, &pid);
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    SendMessageW(controller, dock::Stop, 0, 0);
    const DWORD waited = process ? WaitForSingleObject(process, 5000) : WAIT_FAILED;
    if (process) CloseHandle(process);
    Check(waited == WAIT_OBJECT_0, "controller exits cleanly for persistence check");
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION info{};
    std::wstring command = L"\"" + executable + L"\"";
    Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info), "controller restarts");
    CloseHandle(info.hThread); CloseHandle(info.hProcess);
    Check(Wait([] { controller = FindWindowW(dock::ControllerClass, nullptr); return controller && Binding(); }), "controller responds after restart");
}
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    controller = FindWindowW(dock::ControllerClass, nullptr);
    if (!controller) { std::puts("Run LayoutDock first."); return 1; }
    const UINT original = Binding();
    const HWND originalWindow = GetForegroundWindow();
    wchar_t local[MAX_PATH]{}, binary[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local, _countof(local));
    const std::wstring settings = std::wstring(local) + L"\\LayoutDock\\settings.ini";
    GetModuleFileNameW(nullptr, binary, _countof(binary));
    const std::wstring path(binary);
    const std::wstring executable = path.substr(0, path.find_last_of(L'\\')) + L"\\LayoutDock.exe";
    const HKL initialLayout = GetKeyboardLayout(GetCurrentThreadId());
    const SHORT initialCaps = GetKeyState(VK_CAPITAL) & 1;
    WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = TestWindowProc;
    wc.lpszClassName = L"LayoutDock.HotkeyTest"; RegisterClassW(&wc);
    target = CreateWindowExW(0, wc.lpszClassName, L"LayoutDock — shortcut test", WS_OVERLAPPEDWINDOW,
        800, 500, 460, 150, nullptr, nullptr, wc.hInstance, nullptr);
    edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 16, 16, 400, 28, target, nullptr, wc.hInstance, nullptr);
    ShowWindow(target, SW_SHOW);
    int result = 0;
    try {
        const dock::Hotkey next{VK_F9, MOD_CONTROL | MOD_ALT};
        Record(next);
        Check(Binding() == next.Pack() && Text(IDC_HOTKEY) == dock::HotkeyName(next), "captured modifiers and key apply immediately");
        Check(!(GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000), "recording leaves no stuck modifiers");
        Record({'A', 0});
        Check(Binding() == next.Pack() && Text(IDC_SAVE_STATUS).find(L"Add") != std::wstring::npos, "unsafe bare letter leaves previous hotkey intact");
        Record({VK_ESCAPE, 0});
        Check(Binding() == next.Pack() && IsWindow(dialog), "Escape cancels recording without closing settings");
        const dock::Hotkey occupied{VK_F10, MOD_CONTROL | MOD_ALT | MOD_SHIFT};
        Check(RegisterHotKey(nullptr, 123, occupied.modifiers | MOD_NOREPEAT, occupied.key), "test reserves a conflicting shortcut");
        Record(occupied);
        Check(Binding() == next.Pack() && Text(IDC_SAVE_STATUS).find(L"in use") != std::wstring::npos, "conflict reports an error and retains previous binding");
        UnregisterHotKey(nullptr, 123);
        Close(); Focus(target); Pump(200);
        const auto first = SendMessageW(controller, dock::QueryLayout, 0, 0);
        const auto second = SendMessageW(controller, dock::QueryLayout, 1, 0);
        Check(first && second, "two layouts available for real switching test");
        auto Select = [&](LRESULT id) {
            PostMessageW(controller, dock::Select, static_cast<WPARAM>(id), 0);
            Check(Wait([&] { return reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == id; }), "test edit accepts layout");
        };
        Select(first); Select(second); Pump(150);
        Stroke(target, next);
        Check(Wait([&] { return reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == first; }), "custom hotkey switches to the previous layout");
        Stroke(target, next, true);
        Check(Wait([&] { return reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == second; }), "held custom hotkey switches only once");
        const SHORT caps = GetKeyState(VK_CAPITAL) & 1;
        Stroke(target, {});
        Check((GetKeyState(VK_CAPITAL) & 1) != caps && reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == second,
            "CapsLock returns to uppercase behavior with a custom hotkey");
        Stroke(target, {});
        Restart(executable);
        Check(Binding() == next.Pack(), "custom hotkey survives process restart");
        Open(); Check(Text(IDC_HOTKEY) == dock::HotkeyName(next), "settings display restored custom binding");
        const dock::Hotkey pair{0, MOD_ALT | MOD_SHIFT};
        Record(pair);
        Check(Binding() == pair.Pack() && Text(IDC_HOTKEY) == L"Alt + Shift", "Alt+Shift records on release without a third key");
        Close(); Focus(target); Select(first); Select(second); Pump(120);
        Stroke(target, pair); Pump(250);
        Check(reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == first, "Alt+Shift switches once instead of Windows cycling");
        Key(target, VK_LSHIFT); Key(target, VK_LMENU); Key(target, VK_LMENU); Key(target, VK_LSHIFT, true); Key(target, VK_LMENU, true); Pump(250);
        Check(reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == second, "Shift-first order and opposite release order switch once");
        Key(target, VK_LMENU); Key(target, VK_RSHIFT); Key(target, VK_LMENU, true); Key(target, VK_RSHIFT, true); Pump(250);
        GUITHREADINFO gui{sizeof(gui)}; GetGUIThreadInfo(GetCurrentThreadId(), &gui);
        Check(reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == first && !(gui.flags & GUI_INMENUMODE),
            "Alt-first release with right Shift does not activate a menu");
        Check(!(GetAsyncKeyState(VK_MENU) & 0x8000) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000), "bare pair leaves no stuck modifiers");
        // A real third-key shortcut must still reach the application with both modifiers.
        Check(RegisterHotKey(target, 124, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F8), "test reserves a longer Alt+Shift shortcut");
        Key(target, VK_LMENU); Key(target, VK_LSHIFT); Key(target, VK_F8); Key(target, VK_F8, true);
        Key(target, VK_LSHIFT, true); Key(target, VK_LMENU, true); Pump(200);
        Check(longerChordCount == 1 && reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == first,
            "third-key chord reaches its registered handler without toggling the pair");
        UnregisterHotKey(target, 124);
        Restart(executable); Check(Binding() == pair.Pack(), "Alt+Shift persists across restart");
        Open();
        SendDlgItemMessageW(dialog, IDC_RESET_HOTKEY, BM_CLICK, 0, 0); Pump(100);
        Check(Binding() == dock::Hotkey{}.Pack(), "reset restores CapsLock");
        Close(); Focus(target); Select(first); Select(second); Pump(100);
        Stroke(target, {});
        Check(Wait([&] { return reinterpret_cast<LRESULT>(GetKeyboardLayout(GetCurrentThreadId())) == first; }) &&
            (GetKeyState(VK_CAPITAL) & 1) == caps, "default CapsLock switches without changing uppercase state");
    } catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); result = 1; }
    UnregisterHotKey(nullptr, 123);
    for (WORD key : held) { INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = key; input.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &input, sizeof(input)); }
    held.clear(); Close();
    // Restore the saved binding without sending more keys if the user interrupted the test.
    if (Binding() != original) {
        DWORD pid{}; GetWindowThreadProcessId(controller, &pid); HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        SendMessageW(controller, dock::Stop, 0, 0);
        if (process) { WaitForSingleObject(process, 5000); CloseHandle(process); }
        WritePrivateProfileStringW(L"Hotkey", L"Binding", std::to_wstring(original).c_str(), settings.c_str());
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION info{}; std::wstring command = L"\"" + executable + L"\"";
        if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &info)) {
            CloseHandle(info.hThread); CloseHandle(info.hProcess);
        }
    }
    if (GetForegroundWindow() == target && (GetKeyState(VK_CAPITAL) & 1) != initialCaps) {
        INPUT input[4]{}; for (auto& item : input) item.type = INPUT_KEYBOARD;
        input[0].ki.wVk = VK_LSHIFT; input[1].ki.wVk = input[2].ki.wVk = VK_CAPITAL;
        input[2].ki.dwFlags = input[3].ki.dwFlags = KEYEVENTF_KEYUP; input[3].ki.wVk = VK_LSHIFT;
        SendInput(4, input, sizeof(INPUT)); Pump(100);
    }
    PostMessageW(edit, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(initialLayout)); Pump(100);
    DestroyWindow(target); if (IsWindow(originalWindow)) SetForegroundWindow(originalWindow);
    return result;
}
