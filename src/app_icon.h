#pragma once
#include <windows.h>
#include <map>
#include "resource_ids.h"

namespace dock {
// UI-thread cache owns the handles. LR_SHARED would reuse the first size even at a different DPI.
inline HICON AppIcon(int size) {
    struct Cache {
        std::map<int, HICON> icons;
        ~Cache() { for (const auto& entry : icons) if (entry.second) DestroyIcon(entry.second); }
    };
    static Cache cache;
    auto& icon = cache.icons[size];
    if (!icon) icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_LAYOUTDOCK),
        IMAGE_ICON, size, size, 0));
    return icon;
}
inline void SetAppWindowIcons(HWND view) {
    UINT dpi = GetDpiForWindow(view); if (!dpi) dpi = 96;
    SendMessageW(view, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(AppIcon(GetSystemMetricsForDpi(SM_CXICON, dpi))));
    SendMessageW(view, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(AppIcon(GetSystemMetricsForDpi(SM_CXSMICON, dpi))));
}
}
