#include "protocol.h"
#include "history.h"
#include "layout_catalog.h"
#include "displays.h"
#include "settings.h"
#include "app_icon.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
HWND window{}, widget{}, lastTarget{}, lastFocus{};
HHOOK keyboardHook{};
HANDLE instanceMutex{};
std::wstring directory, settingsPath, logPath;
std::mutex logMutex;
std::thread injectionThread;
std::thread geometryThread;
HANDLE geometryStop{}, geometryWake{};
std::atomic<HWND> geometryTarget{};
std::mutex geometryMutex;
struct Geometry { HWND bar{}; int left = dock::UnknownCoordinate, right = dock::UnknownCoordinate; } geometry;
std::wstring preferredMonitor;
std::atomic<bool> injecting{false};
LayoutHistory history;
dock::Packet packet;
std::vector<BYTE> sentPacket;
bool trayDirty = true;
bool sentValid = false, trayAdded = false;
dock::Hotkey hotkey, preferredHotkey;
dock::ConsumedKeys consumedKeys;
dock::AltShiftGesture altShift;
UINT_PTR recordingSession{};
UINT recordedModifiers{};
constexpr ULONG_PTR ReplayTag = 0x4c444b52;
std::wstring hotkeyError;
int registeredHotkey{};
HKL pendingLayout{};
HWND pendingTarget{};
ULONGLONG pendingDeadline{}, nextInjection{}, lastLayoutRefresh{};
ULONGLONG lastDisplayRefresh{};
UINT taskbarCreated{};
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT_PTR Timer = 1;

// Windows 11 and taskbar customizers can leave TrayNotifyWnd's HWND bounds stale.
// Query the visible XAML buttons off the input thread so UIA cannot delay CapsLock.
void WatchGeometry() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
    {
        Microsoft::WRL::ComPtr<IUIAutomation2> automation;
        HRESULT hr = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
        if (SUCCEEDED(hr)) {
            automation->put_ConnectionTimeout(500); automation->put_TransactionTimeout(500);
            Microsoft::WRL::ComPtr<IUIAutomationCondition> buttons;
            VARIANT value{}; value.vt = VT_I4; value.lVal = UIA_ButtonControlTypeId;
            automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &buttons);
            Microsoft::WRL::ComPtr<IUIAutomationCacheRequest> cache;
            automation->CreateCacheRequest(&cache);
            if (cache && buttons) {
                cache->AddProperty(UIA_ClassNamePropertyId); cache->AddProperty(UIA_AutomationIdPropertyId);
                cache->AddProperty(UIA_BoundingRectanglePropertyId); cache->AddProperty(UIA_IsOffscreenPropertyId);
                do {
                    int left = INT_MAX, right = dock::UnknownCoordinate;
                    HWND bar = geometryTarget.load();
                    Microsoft::WRL::ComPtr<IUIAutomationElement> root;
                    Microsoft::WRL::ComPtr<IUIAutomationElementArray> elements;
                    if (bar && SUCCEEDED(automation->ElementFromHandle(bar, &root)) && root &&
                        SUCCEEDED(root->FindAllBuildCache(TreeScope_Descendants, buttons.Get(), cache.Get(), &elements)) && elements) {
                        int count{}; elements->get_Length(&count);
                        for (int n = 0; n < count; ++n) {
                            Microsoft::WRL::ComPtr<IUIAutomationElement> element;
                            if (FAILED(elements->GetElement(n, &element)) || !element) continue;
                            BOOL offscreen = TRUE; RECT bounds{}; BSTR name{}, id{};
                            element->get_CachedIsOffscreen(&offscreen); element->get_CachedBoundingRectangle(&bounds);
                            element->get_CachedClassName(&name); element->get_CachedAutomationId(&id);
                            if (!offscreen && bounds.right > bounds.left) {
                                if (name && !wcsncmp(name, L"SystemTray.", 11)) left = std::min(left, static_cast<int>(bounds.left));
                                else if ((name && !wcscmp(name, L"Taskbar.TaskListButtonAutomationPeer")) ||
                                    (id && (!wcscmp(id, L"StartButton") || !wcscmp(id, L"TaskViewButton") || !wcscmp(id, L"SearchButton"))))
                                    right = std::max(right, static_cast<int>(bounds.right));
                            }
                            SysFreeString(name); SysFreeString(id);
                        }
                    }
                    // Respect native taskbar extensions as well as XAML tray buttons.
                    // For example, this machine has a performance panel in a child dialog.
                    for (HWND child = FindWindowExW(bar, nullptr, nullptr, nullptr); bar && child;
                        child = FindWindowExW(bar, child, nullptr, nullptr)) {
                        wchar_t name[64]{}; GetClassNameW(child, name, _countof(name));
                        if (wcscmp(name, L"#32770") || !IsWindowVisible(child)) continue;
                        RECT bounds{}, barBounds{}; GetWindowRect(child, &bounds); GetWindowRect(bar, &barBounds);
                        if (bounds.right > bounds.left && bounds.left >= barBounds.left && bounds.right <= barBounds.right &&
                            bounds.top >= barBounds.top && bounds.bottom <= barBounds.bottom)
                            left = std::min(left, static_cast<int>(bounds.left));
                    }
                    {
                        std::lock_guard<std::mutex> lock(geometryMutex);
                        geometry = {bar, left == INT_MAX ? dock::UnknownCoordinate : left, right};
                    }
                    HANDLE events[] = {geometryStop, geometryWake};
                    if (WaitForMultipleObjects(2, events, FALSE, 1500) == WAIT_OBJECT_0) break;
                } while (true);
            }
        }
    }
    CoUninitialize();
}

void Log(const wchar_t* fmt, ...) {
    std::lock_guard<std::mutex> lock(logMutex);
    FILE* file{};
    if (_wfopen_s(&file, logPath.c_str(), L"a, ccs=UTF-8") || !file) return;
    SYSTEMTIME now{}; GetLocalTime(&now);
    fwprintf(file, L"%02u:%02u:%02u ", now.wHour, now.wMinute, now.wSecond);
    va_list args; va_start(args, fmt); vfwprintf(file, fmt, args); va_end(args);
    fputwc(L'\n', file); fclose(file);
}

void SaveConfig() {
    auto write = [](const wchar_t* key, int value) {
        WritePrivateProfileStringW(L"Widget", key, std::to_wstring(value).c_str(), settingsPath.c_str());
    };
    write(L"Docked", packet.docked); write(L"Offset", packet.offset); write(L"OffsetY", packet.offsetY);
    write(L"FloatingX", packet.floatingX); write(L"FloatingY", packet.floatingY);
    WritePrivateProfileStringW(L"Widget", L"Monitor", preferredMonitor.c_str(), settingsPath.c_str());
    WritePrivateProfileStringW(L"Hotkey", L"Binding", std::to_wstring(preferredHotkey.Pack()).c_str(), settingsPath.c_str());
}

HWND FindWidget() { return dock::FindWidget(); }

void RefreshDisplay() {
    lastDisplayRefresh = GetTickCount64();
    auto displays = dock::Displays();
    if (displays.empty()) return;
    const auto& display = displays[dock::PreferredDisplay(displays, preferredMonitor)];
    packet.monitor = reinterpret_cast<uintptr_t>(display.monitor);
    packet.taskbar = reinterpret_cast<uintptr_t>(display.taskbar);
    if (geometryTarget.exchange(display.taskbar) != display.taskbar) {
        packet.trayLeft = packet.appsRight = dock::UnknownCoordinate;
        if (geometryWake) SetEvent(geometryWake);
    }
}

void MoveToMonitor(HMONITOR monitor) {
    for (const auto& display : dock::Displays()) if (display.monitor == monitor) {
        preferredMonitor = display.info.szDevice;
        packet.floatingX = display.info.rcWork.right - 240;
        packet.floatingY = display.info.rcWork.bottom - 64;
        RefreshDisplay(); SaveConfig(); return;
    }
}

bool InputTarget(HWND target) {
    if (!target || !IsWindow(target) || target == window || target == widget) return false;
    wchar_t name[128]{}; GetClassNameW(target, name, 128);
    return wcscmp(name, L"Shell_TrayWnd") && wcscmp(name, L"Shell_SecondaryTrayWnd") &&
        wcscmp(name, L"Progman") && wcscmp(name, L"WorkerW") && wcscmp(name, dock::WidgetClass);
}

void RefreshLayouts() {
    lastLayoutRefresh = GetTickCount64();
    auto installed = dock::InstalledLayouts();
    if (installed.empty()) return; // Keep a known snapshot during a transient API failure.
    auto ordered = dock::OrderLayouts(installed, dock::WindowsOrder());
    bool changed = ordered.size() != packet.layouts.size();
    if (!changed) for (size_t i = 0; i < ordered.size(); ++i) if (ordered[i] != packet.layouts[i].id) { changed = true; break; }
    if (!changed) return;
    packet.layouts = dock::MakeCatalog(ordered);
    packet.count = static_cast<uint32_t>(packet.layouts.size());
    std::vector<LayoutHistory::Id> available;
    for (uint64_t id : ordered) available.push_back(static_cast<uintptr_t>(id));
    history.SetAvailable(std::move(available));
    packet.current = dock::Index(packet.layouts, history.Current());
    packet.previous = dock::Index(packet.layouts, history.Previous());
    if (pendingLayout && dock::Index(packet.layouts, reinterpret_cast<uintptr_t>(pendingLayout)) < 0) pendingLayout = nullptr;
    trayDirty = true;
    Log(L"Windows layout list refreshed: %zu layouts", packet.layouts.size());
}

const wchar_t* Label(int index) {
    return index >= 0 && static_cast<size_t>(index) < packet.layouts.size() ? packet.layouts[index].label : L"?";
}

HKL ObserveForeground() {
    HWND foreground = GetForegroundWindow();
    if (InputTarget(foreground)) {
        if (lastTarget != foreground) lastFocus = nullptr;
        lastTarget = foreground;
    }
    if (!InputTarget(lastTarget)) return nullptr;
    DWORD thread = GetWindowThreadProcessId(lastTarget, nullptr);
    GUITHREADINFO info{sizeof(info)};
    if (GetGUIThreadInfo(thread, &info) && info.hwndFocus) {
        lastFocus = info.hwndFocus;
        thread = GetWindowThreadProcessId(info.hwndFocus, nullptr);
    }
    HKL actual = GetKeyboardLayout(thread);
    auto id = reinterpret_cast<uintptr_t>(actual);
    if (dock::Index(packet.layouts, id) < 0 && GetTickCount64() - lastLayoutRefresh >= 500) RefreshLayouts();
    history.Observe(id);
    packet.current = dock::Index(packet.layouts, id);
    packet.previous = dock::Index(packet.layouts, history.Previous());
    if (pendingLayout) {
        if (lastTarget != pendingTarget) {
            pendingLayout = nullptr; // The user moved on; never redirect a pending switch.
        } else if (actual == pendingLayout) {
            Log(L"Switch confirmed: %ls", Label(packet.current));
            pendingLayout = nullptr;
        } else if (GetTickCount64() > pendingDeadline) {
            Log(L"Target did not accept layout request (possibly elevated or unsupported window)");
            NOTIFYICONDATAW note{sizeof(note)}; note.hWnd = window; note.uID = 1;
            note.uFlags = NIF_INFO; note.dwInfoFlags = NIIF_WARNING;
            wcscpy_s(note.szInfoTitle, L"LayoutDock");
            wcscpy_s(note.szInfo, L"The window did not accept the layout change. Check whether it is running as administrator.");
            Shell_NotifyIconW(NIM_MODIFY, &note);
            pendingLayout = nullptr;
        }
    }
    return actual;
}

void RequestLayout(HKL target) {
    RefreshLayouts();
    ObserveForeground();
    if (!target || dock::Index(packet.layouts, reinterpret_cast<uintptr_t>(target)) < 0 || !InputTarget(lastTarget)) return;
    HWND recipient = lastTarget;
    GUITHREADINFO info{sizeof(info)};
    const DWORD thread = GetWindowThreadProcessId(lastTarget, nullptr);
    if (GetGUIThreadInfo(thread, &info) && info.hwndFocus) recipient = info.hwndFocus;
    // Post to the focused application only. Do not broadcast to unrelated windows.
    if (PostMessageW(recipient, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(target))) {
        pendingLayout = target; pendingTarget = lastTarget; pendingDeadline = GetTickCount64() + 1500;
    } else {
        Log(L"PostMessage input-language request failed: %lu", GetLastError());
    }
}

void TogglePair() {
    // Re-read the actual foreground layout; a click may have changed it since the last poll.
    ObserveForeground();
    if (pendingLayout) return;
    RequestLayout(reinterpret_cast<HKL>(history.Previous()));
}

void RestoreClickFocus() {
    // Some Windows 11 taskbar hosts clear the foreground even after MA_NOACTIVATE.
    // Restore only the clicked-from app, and never override a new foreground app.
    HWND foreground = GetForegroundWindow();
    if ((InputTarget(foreground) && foreground != lastTarget) || !InputTarget(lastTarget)) return;
    const DWORD targetThread = GetWindowThreadProcessId(lastTarget, nullptr);
    const DWORD thisThread = GetCurrentThreadId();
    BOOL attached = AttachThreadInput(thisThread, targetThread, TRUE);
    SetForegroundWindow(lastTarget);
    if (IsWindow(lastFocus) && GetAncestor(lastFocus, GA_ROOT) == lastTarget) SetFocus(lastFocus);
    if (attached) AttachThreadInput(thisThread, targetThread, FALSE);
}

HICON MakeIcon(int index) {
    if (index < 0 || static_cast<size_t>(index) >= packet.layouts.size())
        return CopyIcon(dock::AppIcon(GetSystemMetrics(SM_CXSMICON)));
    HDC dc = GetDC(nullptr), mem = CreateCompatibleDC(dc);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 32; bi.bmiHeader.biHeight = -32;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels{};
    HBITMAP bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!bitmap) { DeleteDC(mem); ReleaseDC(nullptr, dc); return nullptr; }
    HGDIOBJ old = SelectObject(mem, bitmap);
    RECT rect{0, 0, 32, 32}; HBRUSH background = CreateSolidBrush(RGB(41, 92, 184));
    FillRect(mem, &rect, background); DeleteObject(background);
    HFONT font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT); SetTextColor(mem, RGB(255,255,255));
    std::wstring abbreviation = Label(index);
    abbreviation.resize(std::min<size_t>(3, abbreviation.find(L'-') == std::wstring::npos ? abbreviation.size() : abbreviation.find(L'-')));
    DrawTextW(mem, abbreviation.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();
    for (int i = 0; i < 32 * 32; ++i) pixels[i] |= 0xff000000;
    SelectObject(mem, oldFont); DeleteObject(font); SelectObject(mem, old);
    BYTE maskBits[128]{}; HBITMAP mask = CreateBitmap(32, 32, 1, 1, maskBits);
    ICONINFO ii{}; ii.fIcon = TRUE; ii.hbmColor = bitmap; ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask); DeleteObject(bitmap); DeleteDC(mem); ReleaseDC(nullptr, dc);
    return icon;
}

void UpdateTray(bool force = false) {
    static int shownCurrent = -2, shownPrevious = -2;
    if (!force && !trayDirty && trayAdded && shownCurrent == packet.current && shownPrevious == packet.previous) return;
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = window; data.uID = 1;
    data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE; data.uCallbackMessage = TrayMessage;
    data.hIcon = MakeIcon(packet.current);
    _snwprintf_s(data.szTip, _countof(data.szTip), _TRUNCATE, L"LayoutDock · %ls ↔ %ls\n%ls — switch last two\nRight-click — settings",
        Label(packet.current), Label(packet.previous), dock::HotkeyName(hotkey).c_str());
    BOOL ok = Shell_NotifyIconW(trayAdded ? NIM_MODIFY : NIM_ADD, &data);
    if (data.hIcon) DestroyIcon(data.hIcon);
    if (ok) { trayAdded = true; trayDirty = false; shownCurrent = packet.current; shownPrevious = packet.previous; }
}

void SendState() {
    HWND found = FindWidget();
    if (found != widget) { widget = found; sentValid = false; }
    if (!widget) return;
    auto bytes = dock::Encode(packet);
    if (sentValid && sentPacket == bytes) return;
    COPYDATASTRUCT data{dock::PacketId, static_cast<DWORD>(bytes.size()), bytes.data()};
    DWORD_PTR result{};
    if (SendMessageTimeoutW(widget, WM_COPYDATA, reinterpret_cast<WPARAM>(window),
        reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result) && result == TRUE) {
        sentPacket = std::move(bytes); sentValid = true;
    }
}

// Resolve the export relative to its actual owning module, including forwarded exports.
LPTHREAD_START_ROUTINE RemoteLoadLibrary(DWORD pid) {
    FARPROC local = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner{};
    if (!local || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(local), &owner)) return nullptr;
    wchar_t path[MAX_PATH]{}; GetModuleFileNameW(owner, path, MAX_PATH);
    const wchar_t* base = wcsrchr(path, L'\\'); base = base ? base + 1 : path;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    LPTHREAD_START_ROUTINE remote{};
    if (Module32FirstW(snapshot, &module)) do {
        if (!_wcsicmp(module.szModule, base)) {
            remote = reinterpret_cast<LPTHREAD_START_ROUTINE>(module.modBaseAddr +
                (reinterpret_cast<BYTE*>(local) - reinterpret_cast<BYTE*>(owner)));
            break;
        }
    } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot); return remote;
}

bool ModuleAlreadyLoaded(DWORD pid, const std::wstring& path) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W module{}; module.dwSize = sizeof(module); bool found = false;
    if (Module32FirstW(snapshot, &module)) do {
        if (!_wcsicmp(module.szExePath, path.c_str())) { found = true; break; }
    } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot); return found;
}

void InjectWidget() {
    const std::wstring dll = directory + L"\\LayoutDock.Widget.dll";
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid{}; if (taskbar) GetWindowThreadProcessId(taskbar, &pid);
    if (!pid || GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"Taskbar or widget DLL is unavailable"); return;
    }
    if (ModuleAlreadyLoaded(pid, dll)) return;
    auto entry = RemoteLoadLibrary(pid);
    if (!entry) { Log(L"Could not resolve remote LoadLibraryW"); return; }
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) { Log(L"OpenProcess explorer failed: %lu", GetLastError()); return; }
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    HANDLE thread{};
    if (remote && WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr))
        thread = CreateRemoteThread(process, nullptr, 0, entry, remote, 0, nullptr);
    DWORD waited = thread ? WaitForSingleObject(thread, 8000) : WAIT_FAILED;
    if (waited == WAIT_OBJECT_0) {
        Log(L"Widget DLL load finished in taskbar process %lu", pid);
    } else {
        Log(L"Widget DLL load did not complete: wait=%lu, error=%lu", waited, GetLastError());
    }
    // Never free the path while a timed-out loader thread may still be reading it.
    if (remote && (!thread || waited == WAIT_OBJECT_0)) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    if (thread) CloseHandle(thread);
    CloseHandle(process);
}

bool StartupEnabled() {
    HKEY key{}; DWORD type{}, size{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key)) return false;
    LONG result = RegQueryValueExW(key, L"LayoutDock", nullptr, &type, nullptr, &size);
    RegCloseKey(key); return result == ERROR_SUCCESS;
}

void SetStartup(bool enabled) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr,
        0, KEY_SET_VALUE, nullptr, &key, nullptr)) return;
    if (!enabled) RegDeleteValueW(key, L"LayoutDock");
    else {
        const std::wstring command = L"\"" + directory + L"\\LayoutDock.exe\"";
        RegSetValueExW(key, L"LayoutDock", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
}

void ApplyCommand(int command) {
    switch (command) {
    case 200: TogglePair(); break;
    case 201: packet.docked = 1; break;
    case 202: packet.docked = 0; break;
    case 205: packet.offset = packet.offsetY = 0; break;
    case 206: SetStartup(!StartupEnabled()); break;
    case 207: DestroyWindow(window); return;
    }
    SaveConfig(); SendState();
}

bool SetHotkey(dock::Hotkey next) {
    hotkeyError = dock::HotkeyError(next);
    if (!hotkeyError.empty()) return false;
    if (!(next == hotkey)) {
        // Reserve the replacement before releasing the working shortcut.
        // Alternate IDs also let us ignore queued notifications for an old binding.
        const int newId = next.Default() || next.AltShift() ? 0 : registeredHotkey == 10 ? 11 : 10;
        if (newId && !RegisterHotKey(window, newId, next.modifiers | MOD_NOREPEAT, next.key)) {
            hotkeyError = L"Cannot use " + dock::HotkeyName(next) + L". Shortcut in use or unavailable; previous shortcut kept.";
            return false;
        }
        if (registeredHotkey) UnregisterHotKey(window, registeredHotkey);
        registeredHotkey = newId; hotkey = next;
        altShift.Cancel();
    }
    preferredHotkey = next; trayDirty = true;
    return true;
}

void ApplySetting(dock::Setting setting, int64_t value) {
    switch (setting) {
    case dock::Setting::Mode: packet.docked = value ? 1 : 0; break;
    case dock::Setting::Monitor: MoveToMonitor(reinterpret_cast<HMONITOR>(value)); break;
    case dock::Setting::OffsetX: packet.offset = static_cast<int>(std::clamp<int64_t>(value, -3000, 3000)); break;
    case dock::Setting::OffsetY: packet.offsetY = static_cast<int>(std::clamp<int64_t>(value, -3000, 3000)); break;
    case dock::Setting::ResetPosition: packet.offset = packet.offsetY = 0; break;
    case dock::Setting::Startup: SetStartup(value != 0); break;
    case dock::Setting::Hotkey:
        if (!SetHotkey(dock::Hotkey::Unpack(static_cast<UINT>(value)))) return;
        break;
    case dock::Setting::Exit: PostMessageW(window, dock::Stop, 0, 0); return;
    }
    SaveConfig(); SendState();
}

void ShowSettings() {
    RefreshLayouts(); RefreshDisplay(); ObserveForeground();
    dock::ShowSettings(GetModuleHandleW(nullptr), window,
        [] { return dock::SettingsState{packet, StartupEnabled(), hotkey, hotkeyError}; }, ApplySetting);
}

UINT HeldModifiers() {
    UINT result = consumedKeys.Modifiers();
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) result |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) result |= MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) result |= MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) result |= MOD_WIN;
    return result;
}

INPUT ReplayKey(UINT vk, bool up, UINT scan = 0, bool extended = false) {
    INPUT input{}; input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vk); input.ki.wScan = static_cast<WORD>(scan);
    input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    input.ki.dwExtraInfo = ReplayTag;
    return input;
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (key->dwExtraInfo == ReplayTag) return CallNextHookEx(keyboardHook, code, wp, lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        const bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
        const UINT_PTR session = dock::HotkeyRecordingSession();
        if (session && session != recordingSession) { recordingSession = session; recordedModifiers = HeldModifiers(); }
        if (session && up && recordedModifiers == (MOD_ALT | MOD_SHIFT) &&
            HeldModifiers() == recordedModifiers &&
            (dock::ModifierForKey(key->vkCode) == MOD_ALT || dock::ModifierForKey(key->vkCode) == MOD_SHIFT))
            dock::RecordHotkey({0, MOD_ALT | MOD_SHIFT});
        if ((down || up) && consumedKeys.Continue(key->vkCode, up)) return 1;
        if (down && session) {
            consumedKeys.Down(key->vkCode);
            recordedModifiers |= dock::ModifierForKey(key->vkCode);
            if (!dock::ModifierForKey(key->vkCode)) dock::RecordHotkey({key->vkCode, HeldModifiers()});
            return 1;
        }
        const auto action = altShift.Process(key->vkCode, down, up, HeldModifiers() | altShift.WithheldModifier(),
            hotkey.AltShift() && !session);
        if (action.trigger) PostMessageW(window, dock::Toggle, 0, 0);
        if (action.replayModifier) {
            INPUT replay[] = {ReplayKey(action.replayModifier, false, 0, action.replayModifier == VK_RMENU),
                ReplayKey(key->vkCode, up, key->scanCode, (key->flags & LLKHF_EXTENDED) != 0)};
            if (SendInput(_countof(replay), replay, sizeof(INPUT)) == _countof(replay)) return 1;
            return CallNextHookEx(keyboardHook, code, wp, lp);
        }
        if (action.maskAltUp) {
            // 0xE8 is unassigned; this mask prevents Alt's menu activation without
            // changing Ctrl/Shift state or producing a character in the target.
            INPUT replay[] = {ReplayKey(0xe8, false), ReplayKey(0xe8, true), ReplayKey(key->vkCode, true,
                key->scanCode, (key->flags & LLKHF_EXTENDED) != 0)};
            if (SendInput(_countof(replay), replay, sizeof(INPUT)) == _countof(replay)) return 1;
            return CallNextHookEx(keyboardHook, code, wp, lp);
        }
        if (action.consume) return 1;
        // CapsLock uses the hook so switching never changes uppercase state.
        // All other bindings use WM_HOTKEY with system conflict detection.
        if (down && hotkey.key == VK_CAPITAL && key->vkCode == hotkey.key && HeldModifiers() == hotkey.modifiers) {
            consumedKeys.Down(key->vkCode); PostMessageW(window, dock::Toggle, 0, 0); return 1;
        }
    }
    return CallNextHookEx(keyboardHook, code, wp, lp);
}

void Tick() {
    const ULONGLONG now = GetTickCount64();
    if (now - lastLayoutRefresh >= 3000) RefreshLayouts();
    if (now - lastDisplayRefresh >= 1500) RefreshDisplay();
    {
        std::lock_guard<std::mutex> lock(geometryMutex);
        if (reinterpret_cast<uintptr_t>(geometry.bar) == packet.taskbar) {
            packet.trayLeft = geometry.left; packet.appsRight = geometry.right;
        }
    }
    ObserveForeground(); UpdateTray(); SendState();
    if (!widget && !injecting && now >= nextInjection) {
        if (injectionThread.joinable()) injectionThread.join();
        injecting = true; nextInjection = now + 15000;
        injectionThread = std::thread([] { InjectWidget(); injecting = false; });
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreated && msg == taskbarCreated) {
        trayAdded = false; widget = nullptr; sentValid = false; nextInjection = 0; return 0;
    }
    switch (msg) {
    case WM_TIMER: Tick(); return 0;
    case WM_SETTINGCHANGE: case WM_INPUTLANGCHANGE: lastLayoutRefresh = 0; return 0;
    case WM_DISPLAYCHANGE: lastDisplayRefresh = 0; return 0;
    case dock::MoveMonitor:
        MoveToMonitor(reinterpret_cast<HMONITOR>(wp)); SendState(); return 0;
    case dock::Select:
        if (lp == 1) RestoreClickFocus();
        RequestLayout(reinterpret_cast<HKL>(wp));
        return 0;
    case dock::Toggle:
        if (lp == 1) RestoreClickFocus();
        TogglePair(); return 0;
    case WM_COMMAND: ApplyCommand(LOWORD(wp)); return 0;
    case WM_HOTKEY:
        if (registeredHotkey && wp == static_cast<WPARAM>(registeredHotkey) &&
            HIWORD(lp) == hotkey.key && (LOWORD(lp) & 15u) == hotkey.modifiers && !dock::RecordingHotkey()) TogglePair();
        return 0;
    case dock::Settings: ShowSettings(); return 0;
    case dock::Menu: ShowSettings(); return 0;
    case dock::Moved:
        packet.floatingX = static_cast<int>(wp); packet.floatingY = static_cast<int>(lp);
        for (const auto& display : dock::Displays()) if (display.monitor == MonitorFromWindow(widget, MONITOR_DEFAULTTONEAREST)) {
            preferredMonitor = display.info.szDevice; break;
        }
        RefreshDisplay(); SaveConfig(); return 0;
    case dock::Query:
        if (wp == 0) return packet.current >= 0 ? static_cast<LRESULT>(packet.layouts[packet.current].id) : 0;
        if (wp == 1) return static_cast<LRESULT>(history.Previous());
        if (wp == 2) return static_cast<LRESULT>(packet.layouts.size());
        if (wp == 3) return packet.docked;
        if (wp == 4) return static_cast<LRESULT>(packet.monitor);
        if (wp == 5) return packet.floatingX;
        if (wp == 6) return packet.floatingY;
        if (wp == 7) return packet.offset;
        if (wp == 8) return packet.offsetY;
        if (wp == 9) return hotkey.Pack();
        if (wp == 10) return 0; // Legacy popup-menu query; settings now open directly.
        return 0;
    case dock::QueryLayout:
        return wp < packet.layouts.size() ? static_cast<LRESULT>(packet.layouts[wp].id) : 0;
    case TrayMessage:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ShowSettings();
        else if (lp == WM_LBUTTONUP) TogglePair();
        return 0;
    case dock::Stop: case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: {
        KillTimer(hwnd, Timer);
        dock::CloseSettings();
        if (registeredHotkey) { UnregisterHotKey(hwnd, registeredHotkey); registeredHotkey = 0; }
        if (keyboardHook) { UnhookWindowsHookEx(keyboardHook); keyboardHook = nullptr; }
        HWND view = FindWidget(); if (view) PostMessageW(view, WM_CLOSE, 0, 0);
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd = hwnd; data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data); PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR args, int) {
    const bool showSettings = args && !_wcsicmp(args, L"--settings");
    if (args && !_wcsicmp(args, L"--stop")) {
        HWND existing = FindWindowW(dock::ControllerClass, nullptr);
        if (existing) PostMessageW(existing, dock::Stop, 0, 0);
        return 0;
    }
    instanceMutex = CreateMutexW(nullptr, FALSE, dock::InstanceMutex);
    if (!instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (showSettings) {
            HWND existing = FindWindowW(dock::ControllerClass, nullptr);
            DWORD pid{}; if (existing) GetWindowThreadProcessId(existing, &pid);
            if (pid) { AllowSetForegroundWindow(pid); PostMessageW(existing, dock::Settings, 0, 0); }
        }
        if (instanceMutex) CloseHandle(instanceMutex);
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
    directory = path; directory.resize(directory.find_last_of(L'\\'));
    wchar_t local[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local);
    std::wstring dataDirectory = std::wstring(local) + L"\\LayoutDock";
    CreateDirectoryW(dataDirectory.c_str(), nullptr);
    settingsPath = dataDirectory + L"\\settings.ini"; logPath = dataDirectory + L"\\LayoutDock.log";
    preferredHotkey = dock::Hotkey::Unpack(GetPrivateProfileIntW(L"Hotkey", L"Binding", VK_CAPITAL, settingsPath.c_str()));
    packet.docked = GetPrivateProfileIntW(L"Widget", L"Docked", 1, settingsPath.c_str()) ? 1 : 0;
    packet.offset = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Widget", L"Offset", 0, settingsPath.c_str())), -3000, 3000);
    packet.offsetY = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Widget", L"OffsetY", 0, settingsPath.c_str())), -3000, 3000);
    packet.floatingX = static_cast<int>(GetPrivateProfileIntW(L"Widget", L"FloatingX", -1, settingsPath.c_str()));
    packet.floatingY = static_cast<int>(GetPrivateProfileIntW(L"Widget", L"FloatingY", -1, settingsPath.c_str()));
    wchar_t savedMonitor[CCHDEVICENAME]{};
    GetPrivateProfileStringW(L"Widget", L"Monitor", L"", savedMonitor, _countof(savedMonitor), settingsPath.c_str());
    preferredMonitor = savedMonitor;
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = WindowProc; wc.lpszClassName = dock::ControllerClass;
    wc.hIcon = dock::AppIcon(GetSystemMetrics(SM_CXICON));
    if (!RegisterClassW(&wc)) { CloseHandle(instanceMutex); return 1; }
    window = CreateWindowExW(WS_EX_TOOLWINDOW, dock::ControllerClass, L"LayoutDock", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(instanceMutex); return 1; }
    dock::SetAppWindowIcons(window);
    if (!SetHotkey(preferredHotkey)) {
        hotkeyError = L"Saved shortcut unavailable. Using CapsLock for now.";
        Log(L"Saved hotkey unavailable; using CapsLock for this session");
    }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    geometryStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    geometryWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    RefreshDisplay();
    if (geometryStop && geometryWake) geometryThread = std::thread(WatchGeometry);
    RefreshLayouts(); ObserveForeground(); UpdateTray(true);
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, instance, 0);
    if (!keyboardHook) {
        Log(L"Keyboard hook failed: %lu", GetLastError());
        MessageBoxW(nullptr, L"Could not enable keyboard shortcuts. See the LayoutDock log for details.", L"LayoutDock", MB_ICONERROR);
        DestroyWindow(window);
    } else {
        Log(L"Started. Layout switching hotkey: %ls", dock::HotkeyName(hotkey).c_str());
        SetTimer(window, Timer, 100, nullptr); Tick();
        if (showSettings) ShowSettings();
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!dock::SettingsMessage(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (injectionThread.joinable()) injectionThread.join();
    if (geometryStop) SetEvent(geometryStop);
    if (geometryThread.joinable()) geometryThread.join();
    if (geometryStop) CloseHandle(geometryStop);
    if (geometryWake) CloseHandle(geometryWake);
    Log(L"Stopped"); CloseHandle(instanceMutex); return 0;
}
