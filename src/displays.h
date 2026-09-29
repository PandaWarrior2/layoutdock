#pragma once
#include "protocol.h"
#include <algorithm>
#include <string>
#include <vector>

namespace dock {
inline std::vector<HWND> Taskbars() {
    std::vector<HWND> result;
    if (HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr)) result.push_back(primary);
    HWND secondary{};
    while ((secondary = FindWindowExW(nullptr, secondary, L"Shell_SecondaryTrayWnd", nullptr))) result.push_back(secondary);
    return result;
}
inline HWND FindWidget() {
    for (HWND bar : Taskbars()) if (HWND view = FindWindowExW(bar, nullptr, WidgetClass, nullptr)) return view;
    return FindWindowW(WidgetClass, nullptr);
}
struct Display {
    HMONITOR monitor{};
    MONITORINFOEXW info{};
    HWND taskbar{};
    std::wstring label;
};
inline std::vector<Display> Displays() {
    std::vector<Display> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto& entries = *reinterpret_cast<std::vector<Display>*>(data);
        Display entry; entry.monitor = monitor; entry.info.cbSize = sizeof(entry.info);
        if (GetMonitorInfoW(monitor, &entry.info)) entries.push_back(std::move(entry));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    const auto bars = Taskbars();
    for (auto& entry : result) {
        for (HWND bar : bars) if (MonitorFromWindow(bar, MONITOR_DEFAULTTONULL) == entry.monitor) { entry.taskbar = bar; break; }
        std::wstring device = entry.info.szDevice;
        auto number = device.find(L"DISPLAY");
        entry.label = L"Monitor " + (number != std::wstring::npos ? device.substr(number + 7) : device);
        entry.label += L" · " + std::to_wstring(entry.info.rcMonitor.right - entry.info.rcMonitor.left) +
            L"×" + std::to_wstring(entry.info.rcMonitor.bottom - entry.info.rcMonitor.top);
        if (entry.info.dwFlags & MONITORINFOF_PRIMARY) entry.label += L" (primary)";
    }
    std::sort(result.begin(), result.end(), [](const Display& a, const Display& b) { return wcscmp(a.info.szDevice, b.info.szDevice) < 0; });
    return result;
}
inline size_t PreferredDisplay(const std::vector<Display>& displays, const std::wstring& device) {
    size_t fallback = 0;
    for (size_t i = 0; i < displays.size(); ++i) {
        if (device == displays[i].info.szDevice) return i;
        if (displays[i].info.dwFlags & MONITORINFOF_PRIMARY) fallback = i;
    }
    return fallback;
}
}
