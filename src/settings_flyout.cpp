#include "settings_flyout.h"
#include "settings_ids.h"
#include "app_icon.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace dock {
namespace {
constexpr int Width = 440, Height = 864;
constexpr UINT_PTR AnimationTimer = 86;
constexpr DWORD_PTR CustomPaint = 1, TrackingMouse = 2;
struct Palette { COLORREF background, card, field, hover, border, text, muted, accent, accentText; } colors;
HWND panel{};
HFONT bodyFont{}, smallFont{}, titleFont{}, sectionFont{};
HICON headerIcon{};
HBRUSH cardBrush{}, fieldBrush{}, backgroundBrush{};
ULONG_PTR graphicsToken{};
UINT dpi = 96;
int scrollOffset{}, pageHeight = Height;
bool dark{}, highContrast{};
POINT finalPosition{};
ULONGLONG animationStart{};

int Px(int value) { return MulDiv(value, static_cast<int>(dpi), 96); }
Gdiplus::Color Color(COLORREF value) { return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value)); }
void Round(HDC dc, RECT rect, COLORREF fill, COLORREF edge, int radius = 7) {
    using namespace Gdiplus;
    Graphics graphics(dc); graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    const REAL x = static_cast<REAL>(rect.left) + .5f, y = static_cast<REAL>(rect.top) + .5f;
    const REAL w = static_cast<REAL>(rect.right - rect.left) - 1, h = static_cast<REAL>(rect.bottom - rect.top) - 1;
    const REAL diameter = static_cast<REAL>(std::min(Px(radius) * 2, static_cast<int>(std::min(w, h))));
    if (w <= 0 || h <= 0 || diameter <= 0) return;
    GraphicsPath path;
    path.AddArc(x, y, diameter, diameter, 180, 90);
    path.AddArc(x + w - diameter, y, diameter, diameter, 270, 90);
    path.AddArc(x + w - diameter, y + h - diameter, diameter, diameter, 0, 90);
    path.AddArc(x, y + h - diameter, diameter, diameter, 90, 90); path.CloseFigure();
    SolidBrush brush(Color(fill)); graphics.FillPath(&brush, &path);
    Pen pen(Color(edge)); graphics.DrawPath(&pen, &path);
}
void Text(HDC dc, const wchar_t* text, RECT rect, HFONT font, COLORREF color, UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX) {
    const auto old = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, flags); SelectObject(dc, old);
}
RECT Area(int x, int y, int w, int h) { return {Px(x), Px(y) - scrollOffset, Px(x + w), Px(y + h) - scrollOffset}; }
void DeleteFonts() {
    for (HFONT font : {bodyFont, smallFont, titleFont, sectionFont}) if (font) DeleteObject(font);
    bodyFont = smallFont = titleFont = sectionFont = nullptr;
}
HFONT Font(int height, int weight) {
    return CreateFontW(-Px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
}
void UpdateFonts() {
    headerIcon = AppIcon(Px(36)); SetAppWindowIcons(panel);
    DeleteFonts(); bodyFont = Font(13, FW_NORMAL); smallFont = Font(12, FW_NORMAL);
    titleFont = Font(21, FW_SEMIBOLD); sectionFont = Font(14, FW_SEMIBOLD);
    for (HWND child = GetWindow(panel, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const int id = GetDlgCtrlID(child);
        const bool useSmall = id == IDC_HOTKEY_HINT || id == IDC_POSITION_HINT || id == IDC_SAVE_STATUS;
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(useSmall ? smallFont : bodyFont), FALSE);
    }
    for (int id : {IDC_MODE, IDC_MONITOR}) {
        SendDlgItemMessageW(panel, id, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Px(26));
        SendDlgItemMessageW(panel, id, CB_SETITEMHEIGHT, 0, Px(30));
    }
    SendDlgItemMessageW(panel, IDC_LAYOUTS, LB_SETITEMHEIGHT, 0, Px(20));
}
void UpdateTheme() {
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    HIGHCONTRASTW contrast{sizeof(contrast)}; SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0; dark = !light;
    colors = dark ? Palette{RGB(32,32,32), RGB(43,43,43), RGB(52,52,52), RGB(62,62,62), RGB(67,67,67),
        RGB(246,246,246), RGB(177,177,177), RGB(115,190,255), RGB(15,30,45)} :
        Palette{RGB(243,243,243), RGB(251,251,251), RGB(255,255,255), RGB(240,240,240), RGB(222,222,222),
        RGB(30,30,30), RGB(102,102,102), RGB(0,95,184), RGB(255,255,255)};
    if (highContrast) colors = {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW),
        GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT),
        GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_HIGHLIGHT), GetSysColor(COLOR_HIGHLIGHTTEXT)};
    for (HBRUSH brush : {cardBrush, fieldBrush, backgroundBrush}) if (brush) DeleteObject(brush);
    cardBrush = CreateSolidBrush(colors.card); fieldBrush = CreateSolidBrush(colors.field);
    backgroundBrush = CreateSolidBrush(colors.background);
    const BOOL useDark = dark;
    DwmSetWindowAttribute(panel, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    const auto corner = highContrast ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    DwmSetWindowAttribute(panel, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DwmSetWindowAttribute(panel, DWMWA_BORDER_COLOR, &colors.border, sizeof(colors.border));
    RedrawWindow(panel, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}
void Place(int id, int x, int y, int w, int h) {
    MoveWindow(GetDlgItem(panel, id), Px(x), Px(y) - scrollOffset, Px(w), Px(h), TRUE);
}
void Layout() {
    RECT client{}; GetClientRect(panel, &client); pageHeight = client.bottom;
    const int maximum = std::max(0, Px(Height) - pageHeight);
    scrollOffset = std::clamp(scrollOffset, 0, maximum);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, 0, Px(Height) - 1,
        static_cast<UINT>(pageHeight), scrollOffset, 0};
    SetScrollInfo(panel, SB_VERT, &info, TRUE);
    Place(IDCANCEL, 388, 16, 32, 32);
    Place(IDC_MODE_LABEL, 36, 106, 90, 30); Place(IDC_MODE, 130, 104, 274, 140);
    Place(IDC_MONITOR_LABEL, 36, 148, 90, 30); Place(IDC_MONITOR, 130, 146, 274, 200);
    Place(IDC_X_LABEL, 36, 233, 110, 20); Place(IDC_Y_LABEL, 162, 233, 110, 20);
    Place(IDC_OFFSET_X, 46, 264, 78, 20); Place(IDC_SPIN_X, 127, 261, 17, 26);
    Place(IDC_OFFSET_Y, 172, 264, 78, 20); Place(IDC_SPIN_Y, 253, 261, 17, 26);
    Place(IDC_RESET_POSITION, 286, 259, 118, 30);
    Place(IDC_POSITION_HINT, 36, 301, 368, 32);
    Place(IDC_HOTKEY, 36, 390, 232, 32); Place(IDC_RESET_HOTKEY, 280, 390, 124, 32);
    Place(IDC_HOTKEY_HINT, 36, 431, 368, 32);
    Place(IDC_STARTUP, 36, 487, 368, 34);
    Place(IDC_LAYOUTS, 36, 580, 368, 60);
    Place(IDC_ABOUT_VERSION, 36, 697, 144, 22);
    Place(IDC_ABOUT_AUTHOR, 192, 697, 212, 22);
    Place(IDC_REPOSITORY, 36, 730, 368, 30);
    Place(IDC_SAVE_STATUS, 22, 785, 396, 28);
    Place(IDC_EXIT_APP, 20, 820, 400, 32);
    InvalidateRect(panel, nullptr, TRUE);
}
void SetScroll(int position) { scrollOffset = position; Layout(); }
void EnsureVisible(HWND control) {
    if (!control || !IsChild(panel, control)) return;
    RECT rect{}; GetWindowRect(control, &rect); MapWindowPoints(nullptr, panel, reinterpret_cast<POINT*>(&rect), 2);
    if (rect.top < Px(8)) SetScroll(scrollOffset + rect.top - Px(8));
    else if (rect.bottom > pageHeight - Px(8)) SetScroll(scrollOffset + rect.bottom - pageHeight + Px(8));
}
void PaintPanel(HDC dc) {
    RECT client{}; GetClientRect(panel, &client); FillRect(dc, &client, backgroundBrush);
    if (headerIcon && !highContrast) {
        const RECT icon = Area(20, 18, 36, 36);
        DrawIconEx(dc, icon.left, icon.top, headerIcon, Px(36), Px(36), 0, nullptr, DI_NORMAL);
    } else {
        Round(dc, Area(20, 18, 36, 36), colors.accent, colors.accent, 9);
        Text(dc, L"LD", Area(20, 18, 36, 36), sectionFont, colors.accentText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    Text(dc, L"LayoutDock", Area(68, 15, 270, 28), titleFont, colors.text);
    Text(dc, L"Keyboard layout settings", Area(69, 44, 270, 17), smallFont, colors.muted);
    for (const auto& card : {Area(20,76,400,114), Area(20,198,400,146), Area(20,352,400,118),
        Area(20,478,400,52), Area(20,538,400,116), Area(20,662,400,112)}) Round(dc, card, colors.card, colors.border, 8);
    for (const auto& field : {std::pair<int,int>{IDC_OFFSET_X, 36}, {IDC_OFFSET_Y, 162}})
        Round(dc, Area(field.second, 259, 110, 30), colors.field,
            GetFocus() == GetDlgItem(panel, field.first) ? colors.accent : colors.border, 5);
    Text(dc, L"Placement", Area(36, 82, 360, 22), sectionFont, colors.text);
    Text(dc, L"Taskbar offset", Area(36, 207, 360, 22), sectionFont, colors.text);
    Text(dc, L"Switch between the last two layouts", Area(36, 360, 368, 22), sectionFont, colors.text);
    Text(dc, L"Windows layouts", Area(36, 548, 200, 22), sectionFont, colors.text);
    Text(dc, L"About", Area(36, 672, 368, 22), sectionFont, colors.text);
    Text(dc, L"System order", Area(233, 548, 171, 22), smallFont, colors.muted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
}
void Chevron(HDC dc, int x, int y, bool up, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, Px(1)), color); const auto old = SelectObject(dc, pen);
    MoveToEx(dc, x - Px(3), y + (up ? Px(2) : -Px(2)), nullptr); LineTo(dc, x, y + (up ? -Px(1) : Px(1)));
    LineTo(dc, x + Px(3), y + (up ? Px(2) : -Px(2))); SelectObject(dc, old); DeleteObject(pen);
}
void PaintControl(HWND child, HDC dc) {
    const int id = GetDlgCtrlID(child); RECT rect{}; GetClientRect(child, &rect);
    const bool enabled = IsWindowEnabled(child) != FALSE, focus = GetFocus() == child;
    POINT cursor{}; GetCursorPos(&cursor); ScreenToClient(child, &cursor);
    const bool hover = PtInRect(&rect, cursor) != FALSE;
    const COLORREF ink = enabled ? colors.text : colors.muted;
    FillRect(dc, &rect, id == IDCANCEL || id == IDC_EXIT_APP ? backgroundBrush : cardBrush);
    if (id == IDC_SPIN_X || id == IDC_SPIN_Y) {
        FillRect(dc, &rect, fieldBrush);
        Chevron(dc, rect.right / 2, rect.bottom / 4, true, ink);
        Chevron(dc, rect.right / 2, rect.bottom * 3 / 4, false, ink); return;
    }
    if (id == IDC_MODE || id == IDC_MONITOR) {
        Round(dc, rect, enabled ? colors.field : colors.card, focus ? colors.accent : colors.border, 5);
        wchar_t label[256]{}; GetWindowTextW(child, label, _countof(label));
        RECT text = rect; text.left += Px(12); text.right -= Px(30);
        Text(dc, label, text, bodyFont, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        Chevron(dc, rect.right - Px(16), rect.bottom / 2, false, ink); return;
    }
    const LRESULT state = SendMessageW(child, BM_GETSTATE, 0, 0);
    if (id == IDC_STARTUP) {
        wchar_t label[128]{}; GetWindowTextW(child, label, _countof(label));
        RECT text = rect; text.right -= Px(62); Text(dc, label, text, bodyFont, ink);
        const bool checked = SendMessageW(child, BM_GETCHECK, 0, 0) == BST_CHECKED;
        RECT track{rect.right - Px(42), (rect.bottom - Px(22)) / 2, rect.right, (rect.bottom + Px(22)) / 2};
        Round(dc, track, checked ? colors.accent : colors.field, checked ? colors.accent : colors.muted, 11);
        const int left = track.left + Px(checked ? 23 : 4);
        RECT thumb{left, track.top + Px(4), left + Px(14), track.top + Px(18)};
        Round(dc, thumb, checked ? colors.accentText : colors.muted, checked ? colors.accentText : colors.muted, 7);
    } else if (id == IDCANCEL) {
        if (hover || (state & BST_PUSHED)) Round(dc, rect, colors.hover, colors.hover, 5);
        const int x = rect.right / 2, y = rect.bottom / 2;
        HPEN pen = CreatePen(PS_SOLID, std::max(1, Px(1)), ink); const auto old = SelectObject(dc, pen);
        MoveToEx(dc, x - Px(4), y - Px(4), nullptr); LineTo(dc, x + Px(5), y + Px(5));
        MoveToEx(dc, x + Px(4), y - Px(4), nullptr); LineTo(dc, x - Px(5), y + Px(5));
        SelectObject(dc, old); DeleteObject(pen);
    } else {
        Round(dc, rect, !enabled ? colors.card : hover || (state & BST_PUSHED) ? colors.hover : colors.field,
            focus ? colors.accent : colors.border, 5);
        wchar_t label[128]{}; GetWindowTextW(child, label, _countof(label));
        Text(dc, label, rect, bodyFont, (id == IDC_HOTKEY || id == IDC_REPOSITORY) && enabled ? colors.accent : ink,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (focus) { RECT outline = rect; InflateRect(&outline, -Px(3), -Px(3)); DrawFocusRect(dc, &outline); }
}
LRESULT CALLBACK ControlProc(HWND child, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR flags) {
    const bool paint = (flags & CustomPaint) != 0;
    if ((message == WM_PAINT || message == WM_PRINTCLIENT) && paint) {
        PAINTSTRUCT ps{}; HDC dc = message == WM_PAINT ? BeginPaint(child, &ps) : reinterpret_cast<HDC>(wp);
        PaintControl(child, dc); if (message == WM_PAINT) EndPaint(child, &ps); return 0;
    }
    if (message == WM_ERASEBKGND && paint) return 1;
    // A hover transition changes the appearance; motion within the same control does not.
    if (message == WM_MOUSEMOVE && paint && !(flags & TrackingMouse)) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, child, 0}; TrackMouseEvent(&tracking);
        SetWindowSubclass(child, ControlProc, 1, flags | TrackingMouse);
        InvalidateRect(child, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE && paint) {
        SetWindowSubclass(child, ControlProc, 1, flags & ~TrackingMouse);
        InvalidateRect(child, nullptr, FALSE);
    }
    const LRESULT result = DefSubclassProc(child, message, wp, lp);
    if (message == WM_SETFOCUS) EnsureVisible(child);
    if (panel && (message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
        const int id = GetDlgCtrlID(child);
        if (id == IDC_OFFSET_X || id == IDC_OFFSET_Y) {
            const RECT field = Area(id == IDC_OFFSET_X ? 36 : 162, 259, 110, 30);
            InvalidateRect(panel, &field, FALSE);
        }
    }
    if (paint && (message == WM_SETFOCUS || message == WM_KILLFOCUS ||
        message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == BM_SETCHECK || message == BM_SETSTATE ||
        message == WM_SETTEXT || message == WM_ENABLE || message == CB_SETCURSEL)) InvalidateRect(child, nullptr, FALSE);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(child, ControlProc, 1);
    return result;
}
void Position(HWND view, bool animate) {
    POINT cursor{}; GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    // Move first so PerMonitorV2 supplies the target DPI before sizing the content.
    SetWindowPos(view, nullptr, cursor.x, cursor.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    dpi = GetDpiForWindow(view); if (!dpi) dpi = 96;
    UpdateFonts(); scrollOffset = 0;
    const int width = Px(Width), height = std::min(Px(Height), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) - Px(16));
    const int x = std::clamp(static_cast<int>(cursor.x) - width + Px(24), static_cast<int>(monitor.rcWork.left) + Px(8),
        std::max(static_cast<int>(monitor.rcWork.left) + Px(8), static_cast<int>(monitor.rcWork.right) - width - Px(8)));
    int y = cursor.y - height - Px(12);
    if (y < monitor.rcWork.top + Px(8)) y = cursor.y + Px(12);
    y = std::clamp(y, static_cast<int>(monitor.rcWork.top) + Px(8), static_cast<int>(monitor.rcWork.bottom) - height - Px(8));
    finalPosition = {x, y}; BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    const bool slide = animate && animations && !highContrast;
    KillTimer(view, AnimationTimer);
    SetWindowPos(view, HWND_TOPMOST, x, y + (slide ? Px(12) : 0), width, height, SWP_NOACTIVATE | SWP_FRAMECHANGED);
    Layout();
    if (slide) { animationStart = GetTickCount64(); SetTimer(view, AnimationTimer, 15, nullptr); }
}
}

void InitializeSettingsFlyout(HWND view) {
    panel = view; dpi = GetDpiForWindow(view); scrollOffset = 0;
    Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&graphicsToken, &input, nullptr);
    SetDialogDpiChangeBehavior(view, DDC_DISABLE_ALL, DDC_DISABLE_ALL);
    for (HWND child = GetWindow(view, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const auto behavior = static_cast<DIALOG_CONTROL_DPI_CHANGE_BEHAVIORS>(DCDC_DISABLE_FONT_UPDATE | DCDC_DISABLE_RELAYOUT);
        SetDialogControlDpiChangeBehavior(child, behavior, behavior);
        const int id = GetDlgCtrlID(child);
        if (id == IDC_OFFSET_X || id == IDC_OFFSET_Y || id == IDC_LAYOUTS) {
            SetWindowLongPtrW(child, GWL_STYLE, GetWindowLongPtrW(child, GWL_STYLE) & ~WS_BORDER);
            SetWindowLongPtrW(child, GWL_EXSTYLE, GetWindowLongPtrW(child, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
            SetWindowPos(child, nullptr, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        const bool custom = id == IDCANCEL || id == IDC_EXIT_APP || id == IDC_HOTKEY || id == IDC_RESET_HOTKEY ||
            id == IDC_RESET_POSITION || id == IDC_STARTUP || id == IDC_MODE || id == IDC_MONITOR || id == IDC_SPIN_X || id == IDC_SPIN_Y ||
            id == IDC_REPOSITORY;
        SetWindowSubclass(child, ControlProc, 1, custom);
    }
    UpdateTheme(); UpdateFonts();
    const MARGINS margins{1, 1, 1, 1}; DwmExtendFrameIntoClientArea(view, &margins);
}
void PresentSettingsFlyout(HWND view) {
    Position(view, !IsWindowVisible(view));
    ShowWindow(view, SW_SHOWNORMAL);
    if (!IsWindowVisible(view)) ShowWindow(view, SW_SHOW);
    SetForegroundWindow(view);
}
void DisposeSettingsFlyout() {
    KillTimer(panel, AnimationTimer); DeleteFonts();
    for (HBRUSH brush : {cardBrush, fieldBrush, backgroundBrush}) if (brush) DeleteObject(brush);
    cardBrush = fieldBrush = backgroundBrush = nullptr;
    if (graphicsToken) Gdiplus::GdiplusShutdown(graphicsToken);
    graphicsToken = 0; headerIcon = nullptr; panel = nullptr;
}
bool SettingsFlyoutMessage(HWND view, UINT message, WPARAM wp, LPARAM lp, INT_PTR& result) {
    result = TRUE;
    if (message == WM_NCCALCSIZE && wp) { SetWindowLongPtrW(view, DWLP_MSGRESULT, 0); return true; }
    if (message == WM_NCHITTEST) {
        const auto hit = DefWindowProcW(view, message, wp, lp);
        SetWindowLongPtrW(view, DWLP_MSGRESULT, hit == HTVSCROLL ? HTVSCROLL : HTCLIENT); return true;
    }
    if (view != panel) return false;
    switch (message) {
    case WM_ERASEBKGND: return true;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(view, &ps); PaintPanel(dc); EndPaint(view, &ps); return true;
    }
    case WM_PRINTCLIENT: PaintPanel(reinterpret_cast<HDC>(wp)); return true;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp)); HDC dc = reinterpret_cast<HDC>(wp);
        const bool footer = id == IDC_SAVE_STATUS;
        const bool muted = id == IDC_POSITION_HINT || id == IDC_HOTKEY_HINT || footer;
        const bool edit = id == IDC_OFFSET_X || id == IDC_OFFSET_Y;
        SetTextColor(dc, muted ? colors.muted : colors.text); SetBkColor(dc, footer ? colors.background : edit ? colors.field : colors.card);
        result = reinterpret_cast<INT_PTR>(footer ? backgroundBrush : edit ? fieldBrush : cardBrush); return true;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item->CtlType != ODT_COMBOBOX) return false;
        const bool selected = (item->itemState & ODS_SELECTED) != 0;
        HBRUSH brush = CreateSolidBrush(selected ? colors.hover : colors.field); FillRect(item->hDC, &item->rcItem, brush); DeleteObject(brush);
        if (item->itemID != static_cast<UINT>(-1)) {
            const LRESULT length = SendMessageW(item->hwndItem, CB_GETLBTEXTLEN, item->itemID, 0);
            if (length >= 0 && length < 4096) {
                std::wstring label(static_cast<size_t>(length) + 1, L'\0');
                SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(label.data()));
                RECT rect = item->rcItem; rect.left += Px(12); rect.right -= Px(8);
                Text(item->hDC, label.c_str(), rect, bodyFont, colors.text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        return true;
    }
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: UpdateTheme(); return true;
    case WM_DPICHANGED: {
        dpi = HIWORD(wp); UpdateFonts(); const auto* rect = reinterpret_cast<RECT*>(lp);
        SetWindowPos(view, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout(); return true;
    }
    case WM_SIZE: Layout(); return true;
    case WM_MOUSEWHEEL: SetScroll(scrollOffset - GET_WHEEL_DELTA_WPARAM(wp) * Px(48) / WHEEL_DELTA); return true;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(view, SB_VERT, &info);
        int next = scrollOffset;
        switch (LOWORD(wp)) {
        case SB_TOP: next = 0; break; case SB_BOTTOM: next = Px(Height); break;
        case SB_LINEUP: next -= Px(32); break; case SB_LINEDOWN: next += Px(32); break;
        case SB_PAGEUP: next -= pageHeight; break; case SB_PAGEDOWN: next += pageHeight; break;
        case SB_THUMBTRACK: next = info.nTrackPos; break;
        }
        SetScroll(next); return true;
    }
    case WM_TIMER:
        if (wp == AnimationTimer) {
            const double t = std::min(1.0, (GetTickCount64() - animationStart) / 140.0);
            SetWindowPos(view, nullptr, finalPosition.x, finalPosition.y + static_cast<int>(Px(12) * std::pow(1.0 - t, 3)),
                0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            if (t >= 1.0) KillTimer(view, AnimationTimer);
            return true;
        }
        return false;
    }
    return false;
}
}
