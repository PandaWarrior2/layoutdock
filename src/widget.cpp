// Taskbar hosting / DirectComposition pipeline adapted from wndinject/hellodll.cpp.
// This DLL only presents state and forwards clicks. Keyboard handling lives in LayoutDock.exe.
#include "protocol.h"
#include "widget_layout.h"
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;
namespace {
HMODULE module{};
HWND hwnd{}, taskbar{}, controller{};
HANDLE singleton{};
dock::Packet state;
bool docked = true, light = false, dragging = false, ready = false;
bool trackingMouse = false;
POINT dragStart{}, dragOrigin{};
int hovered = -1, pressed = -1;
float scale = 1;
UINT width{}, height{};
dock::Grid grid;
ComPtr<ID3D11Device> d3d;
ComPtr<ID2D1Device> d2d;
ComPtr<ID2D1DeviceContext> context;
ComPtr<IDWriteFactory> textFactory;
ComPtr<IDWriteTextFormat> textFormat;
ComPtr<ID2D1SolidColorBrush> brush;
ComPtr<IDCompositionDevice> composition;
ComPtr<IDCompositionTarget> target;
ComPtr<IDCompositionVisual> visual;
ComPtr<IDCompositionSurface> surface;

void CleanupGraphics() {
    ready = false;
    if (context) context->SetTarget(nullptr);
    surface.Reset(); visual.Reset(); target.Reset(); composition.Reset();
    brush.Reset(); textFormat.Reset(); textFactory.Reset(); context.Reset(); d2d.Reset(); d3d.Reset();
}

bool LightTheme() {
    DWORD value = 0, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
}

void Color(float r, float g, float b, float a = 1) { brush->SetColor(D2D1::ColorF(r, g, b, a)); }
void Rounded(float x, float y, float w, float h, float radius) {
    context->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x * scale, y * scale,
        (x + w) * scale, (y + h) * scale), radius * scale, radius * scale), brush.Get());
}

void Render() {
    if (!ready || !surface) return;
    ComPtr<IDXGISurface> dxgiSurface; POINT offset{};
    HRESULT hr = surface->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurface), &offset);
    if (FAILED(hr)) { ready = false; return; }
    auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    ComPtr<ID2D1Bitmap1> bitmap;
    hr = context->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &props, &bitmap);
    if (SUCCEEDED(hr)) {
        context->SetTarget(bitmap.Get()); context->BeginDraw();
        context->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
        context->Clear(D2D1::ColorF(0, 0, 0, 0));
        context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        if (docked) Color(light ? 0.0f : 1.0f, light ? 0.0f : 1.0f, light ? 0.0f : 1.0f, light ? .06f : .09f);
        else Color(light ? .94f : .10f, light ? .95f : .12f, light ? .97f : .16f, 1);
        Rounded(0, 0, grid.width, grid.height, 10);
        const float neutral = light ? .25f : .72f;
        Color(neutral, neutral, neutral, docked ? .5f : .85f);
        for (int i = 0; i < 3; ++i) {
            context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(10 * scale, (12.f + i * 6) * scale), scale, scale), brush.Get());
        }
        for (int i = 0; i < grid.count; ++i) {
            const float x = grid.X(i), y = grid.Y(i);
            bool active = state.current == i;
            if (active) { Color(.20f, .43f, .85f, 1); Rounded(x, y, grid.cell - 4, 28, 7); }
            else if (hovered == i) {
                Color(light ? 0.f : 1.f, light ? 0.f : 1.f, light ? 0.f : 1.f, .12f);
                Rounded(x, y, grid.cell - 4, 28, 7);
            }
            if (active) Color(1, 1, 1);
            else { float shade = light ? .16f : .91f; Color(shade, shade, shade); }
            auto bounds = D2D1::RectF(x * scale, (y - 1) * scale, (x + grid.cell - 4) * scale, (y + 27) * scale);
            context->DrawTextW(state.layouts[i].label, static_cast<UINT32>(wcslen(state.layouts[i].label)), textFormat.Get(), bounds, brush.Get());
            if (state.previous == i && !active) {
                Color(light ? .18f : .55f, light ? .38f : .73f, light ? .70f : 1.f);
                Rounded(x + (grid.cell - 16) / 2, y + 25, 12, 2, 1);
            }
        }
        hr = context->EndDraw(); context->SetTarget(nullptr);
    }
    HRESULT ended = surface->EndDraw();
    HRESULT committed = composition->Commit();
    if (FAILED(hr) || FAILED(ended) || FAILED(committed)) ready = false;
}

bool InitGraphics() {
    CleanupGraphics();
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
        D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
        D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
    if (FAILED(hr)) return false;
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(d3d.As(&dxgi))) return false;
    ComPtr<ID2D1Factory1> factory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return false;
    if (FAILED(factory->CreateDevice(dxgi.Get(), &d2d))) return false;
    if (FAILED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context))) return false;
    context->SetDpi(96, 96);
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(textFactory.GetAddressOf())))) return false;
    if (FAILED(textFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.f * scale, L"", &textFormat))) return false;
    textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (FAILED(context->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush))) return false;
    if (FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition)))) return false;
    if (FAILED(composition->CreateTargetForHwnd(hwnd, TRUE, &target))) return false;
    if (FAILED(composition->CreateVisual(&visual))) return false;
    if (FAILED(target->SetRoot(visual.Get()))) return false;
    if (FAILED(composition->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
    if (FAILED(visual->SetContent(surface.Get()))) return false;
    ready = true; Render(); return ready;
}

void Position(bool force = false) {
    static bool positioning = false;
    if (!hwnd || !state.monitor || dragging || positioning) return;
    positioning = true;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{positioning};
    HWND desiredBar = reinterpret_cast<HWND>(state.taskbar);
    if (!IsWindow(desiredBar)) desiredBar = nullptr;
    const bool parentChanged = desiredBar != taskbar;
    taskbar = desiredBar;
    HMONITOR monitor = reinterpret_cast<HMONITOR>(state.monitor);
    if (!state.docked && !force && ready) monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO work{sizeof(work)};
    if (!GetMonitorInfoW(monitor, &work)) {
        monitor = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY); GetMonitorInfoW(monitor, &work);
    }
    const UINT dpi = GetDpiForWindow(state.docked && taskbar ? taskbar : hwnd);
    const float nextScale = (dpi ? dpi : 96) / 96.f;
    grid = dock::MakeGrid(state.layouts, (work.rcWork.right - work.rcWork.left) / nextScale - 24);
    const bool geometryKnown = state.trayLeft != dock::UnknownCoordinate && state.appsRight != dock::UnknownCoordinate;
    RECT bar{}; if (taskbar) GetClientRect(taskbar, &bar);
    bool newDocked = state.docked && taskbar && geometryKnown && grid.rows == 1 && bar.right > bar.bottom &&
        state.trayLeft - state.appsRight >= (grid.width + 16) * nextScale;
    const bool changed = docked != newDocked || (newDocked && parentChanged);
    if (changed) {
        docked = newDocked;
        if (docked) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE); SetParent(hwnd, taskbar);
        } else {
            SetParent(hwnd, nullptr); SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        }
    }
    const UINT nextWidth = static_cast<UINT>(std::lround(grid.width * nextScale));
    const UINT nextHeight = static_cast<UINT>(std::lround(grid.height * nextScale));
    const bool resized = nextWidth != width || nextHeight != height;
    scale = nextScale; width = nextWidth; height = nextHeight;
    int x{}, y{};
    if (docked) {
        POINT tray{state.trayLeft, 0};
        ScreenToClient(taskbar, &tray);
        // Manual offsets are relative to the automatic anchor. Keep the whole
        // widget within its parent so a large value cannot make it disappear.
        x = std::clamp(static_cast<int>(std::lround(tray.x - width - 8 * scale + state.offset * scale)),
            0, std::max(0, static_cast<int>(bar.right) - static_cast<int>(width)));
        const int roomY = std::max(0, static_cast<int>(bar.bottom) - static_cast<int>(height));
        y = std::clamp(roomY / 2 + static_cast<int>(std::lround(state.offsetY * scale)), 0, roomY);
    } else {
        RECT current{}; GetWindowRect(hwnd, &current);
        x = force || changed || !ready ? state.floatingX : current.left;
        y = force || changed || !ready ? state.floatingY : current.top;
        if ((x == -1 && y == -1) || state.docked) {
            x = (geometryKnown ? state.trayLeft : work.rcWork.right) - static_cast<int>(width) - 16;
            y = work.rcWork.bottom - static_cast<int>(height) - 16;
        }
        x = std::clamp<int>(x, work.rcWork.left, std::max(work.rcWork.left, work.rcWork.right - static_cast<int>(width)));
        y = std::clamp<int>(y, work.rcWork.top, std::max(work.rcWork.top, work.rcWork.bottom - static_cast<int>(height)));
    }
    SetWindowPos(hwnd, docked ? HWND_TOP : HWND_TOPMOST, x, y, static_cast<int>(width), static_cast<int>(height),
        SWP_NOACTIVATE | SWP_SHOWWINDOW | (changed ? SWP_FRAMECHANGED : 0));
    if (resized || changed || !ready) InitGraphics();
}

int Hit(LPARAM lp) {
    float x = GET_X_LPARAM(lp) / scale, y = GET_Y_LPARAM(lp) / scale;
    return grid.Hit(x, y);
}

LRESULT CALLBACK WindowProc(HWND view, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lp);
        if (reinterpret_cast<HWND>(wp) != controller || !data || data->dwData != dock::PacketId ||
            !data->lpData) return FALSE;
        dock::Packet next;
        if (!dock::Decode(data->lpData, data->cbData, next)) return FALSE;
        // Cancel a pressed button if its identity changed while the mouse was down.
        if (pressed >= 0 && (static_cast<size_t>(pressed) >= next.layouts.size() ||
            static_cast<size_t>(pressed) >= state.layouts.size() || state.layouts[pressed].id != next.layouts[pressed].id)) pressed = -1;
        bool moved = next.monitor != state.monitor || next.floatingX != state.floatingX || next.floatingY != state.floatingY;
        state = std::move(next);
        if (static_cast<size_t>(hovered) >= state.layouts.size()) hovered = -1;
        Position(moved); Render(); return TRUE;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; BeginPaint(view, &ps); EndPaint(view, &ps); return 0; }
    case WM_LBUTTONDOWN:
        if (!docked && GET_X_LPARAM(lp) < 20 * scale) {
            dragging = true; GetCursorPos(&dragStart);
            RECT rect{}; GetWindowRect(view, &rect); dragOrigin = {rect.left, rect.top}; SetCapture(view);
        } else { pressed = Hit(lp); }
        return 0;
    case WM_MOUSEMOVE:
        if (dragging) {
            POINT point{}; GetCursorPos(&point);
            SetWindowPos(view, nullptr, dragOrigin.x + point.x - dragStart.x, dragOrigin.y + point.y - dragStart.y,
                0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
        } else {
            int index = Hit(lp); if (index != hovered) { hovered = index; Render(); }
            if (!trackingMouse) { TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, view, 0}; TrackMouseEvent(&track); trackingMouse = true; }
        }
        return 0;
    case WM_MOUSELEAVE: hovered = -1; pressed = -1; trackingMouse = false; Render(); return 0;
    case WM_LBUTTONUP: {
        int selected = Hit(lp); bool wasDragging = dragging; int wasPressed = pressed;
        dragging = false; pressed = -1;
        if (wasDragging) ReleaseCapture();
        if (wasDragging) {
            RECT rect{}; GetWindowRect(view, &rect);
            state.floatingX = rect.left; state.floatingY = rect.top;
            PostMessageW(controller, dock::Moved, static_cast<WPARAM>(rect.left), rect.top);
        } else if (selected >= 0 && selected == wasPressed) {
            DWORD pid{}; GetWindowThreadProcessId(controller, &pid); AllowSetForegroundWindow(pid);
            PostMessageW(controller, dock::Select, static_cast<WPARAM>(state.layouts[selected].id), 1);
        }
        return 0;
    }
    case WM_CAPTURECHANGED: dragging = false; pressed = -1; return 0;
    case WM_RBUTTONUP: case WM_CONTEXTMENU: {
        DWORD pid{}; GetWindowThreadProcessId(controller, &pid); AllowSetForegroundWindow(pid);
        PostMessageW(controller, dock::Menu, 0, 0); return 0;
    }
    case WM_MBUTTONUP: {
        DWORD pid{}; GetWindowThreadProcessId(controller, &pid); AllowSetForegroundWindow(pid);
        PostMessageW(controller, dock::Toggle, 0, 1); return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT point{}; GetCursorPos(&point); ScreenToClient(view, &point);
            SetCursor(LoadCursorW(nullptr, !docked && point.x < 20 * scale ? IDC_SIZEALL : IDC_HAND)); return TRUE;
        }
        break;
    case WM_DPICHANGED: Position(); Render(); return 0;
    case WM_TIMER:
        if (!IsWindow(controller)) { DestroyWindow(view); return 0; }
        if (!dragging) Position();
        if (const bool current = LightTheme(); current != light) { light = current; Render(); }
        return 0;
    case WM_CLOSE: DestroyWindow(view); return 0;
    case WM_DESTROY: KillTimer(view, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(view, message, wp, lp);
}

DWORD WINAPI Worker(void*) {
    singleton = CreateMutexW(nullptr, FALSE, dock::WidgetMutex);
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (singleton) CloseHandle(singleton);
        FreeLibraryAndExitThread(module, 0);
    }
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    controller = FindWindowW(dock::ControllerClass, nullptr);
    taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (controller && taskbar) {
        WNDCLASSW wc{}; wc.hInstance = module; wc.lpfnWndProc = WindowProc;
        wc.lpszClassName = dock::WidgetClass; wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        if (RegisterClassW(&wc)) {
            light = LightTheme();
            hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOPARENTNOTIFY,
                dock::WidgetClass, L"LayoutDock — Windows keyboard layouts", WS_CHILD,
                0, 0, 1, 1, taskbar, nullptr, module, nullptr);
            if (hwnd) {
                Position(); SetTimer(hwnd, 1, 500, nullptr);
                MSG message{};
                while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            CleanupGraphics(); UnregisterClassW(dock::WidgetClass, module);
        }
    }
    CloseHandle(singleton); singleton = nullptr;
    FreeLibraryAndExitThread(module, 0);
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = instance; DisableThreadLibraryCalls(instance);
        // Do not wait or initialize graphics under the loader lock.
        HANDLE worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (!worker) return FALSE;
        CloseHandle(worker);
    }
    return TRUE;
}
