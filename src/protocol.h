#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <vector>

namespace dock {
inline constexpr wchar_t ControllerClass[] = L"LayoutDock.Controller.v1";
inline constexpr wchar_t WidgetClass[] = L"LayoutDock.Widget.v1";
inline constexpr wchar_t InstanceMutex[] = L"Local\\LayoutDock.Controller.v1";
inline constexpr wchar_t WidgetMutex[] = L"Local\\LayoutDock.Widget.v1";
inline constexpr UINT Select = WM_APP + 40; // wParam: full HKL, lParam: mouse origin
inline constexpr UINT Settings = WM_APP + 41;
inline constexpr UINT Toggle = WM_APP + 42;
inline constexpr UINT Moved = WM_APP + 43;
inline constexpr UINT Stop = WM_APP + 44;
inline constexpr UINT Query = WM_APP + 45; // 0=current HKL, 1=previous HKL, 2=count, 3=docked
inline constexpr UINT QueryLayout = WM_APP + 46; // index -> full HKL
inline constexpr UINT MoveMonitor = WM_APP + 47; // wParam: HMONITOR
inline constexpr UINT Menu = WM_APP + 48;
inline constexpr ULONG_PTR PacketId = 0x4C445634;
inline constexpr size_t MaxPacketBytes = 1024 * 1024;
inline constexpr int32_t UnknownCoordinate = INT32_MIN;

struct Layout {
    uint64_t id{};
    wchar_t label[16]{};
    wchar_t name[160]{};
};
struct Header {
    uint32_t version = 4;
    int32_t current = -1;
    int32_t previous = -1;
    uint32_t count = 0;
    int32_t docked = 1;
    int32_t offset = 0;
    int32_t floatingX = -1;
    int32_t floatingY = -1;
    int32_t trayLeft = UnknownCoordinate;
    int32_t appsRight = UnknownCoordinate;
    int32_t offsetY = 0;
    uint32_t reserved = 0;
    uint64_t monitor{};
    uint64_t taskbar{};
};
static_assert(sizeof(Header) == 64);
static_assert(sizeof(Layout) == 360);
struct Packet : Header { std::vector<Layout> layouts; };

inline int Index(const std::vector<Layout>& layouts, uint64_t id) {
    for (size_t i = 0; i < layouts.size(); ++i) if (layouts[i].id == id) return static_cast<int>(i);
    return -1;
}
inline std::vector<BYTE> Encode(const Packet& packet) {
    Header header = packet;
    header.count = static_cast<uint32_t>(packet.layouts.size());
    std::vector<BYTE> bytes(sizeof(Header) + packet.layouts.size() * sizeof(Layout));
    memcpy(bytes.data(), &header, sizeof(header));
    if (!packet.layouts.empty()) memcpy(bytes.data() + sizeof(header), packet.layouts.data(), packet.layouts.size() * sizeof(Layout));
    return bytes;
}
inline bool Decode(const void* data, size_t size, Packet& packet) {
    if (!data || size < sizeof(Header) || size > MaxPacketBytes) return false;
    Header header; memcpy(&header, data, sizeof(header));
    if (header.version != 4 || header.count != (size - sizeof(Header)) / sizeof(Layout) ||
        (size - sizeof(Header)) % sizeof(Layout) || header.current < -1 || header.previous < -1 ||
        header.current >= static_cast<int>(header.count) || header.previous >= static_cast<int>(header.count)) return false;
    Packet next; static_cast<Header&>(next) = header;
    next.layouts.resize(header.count);
    if (header.count) memcpy(next.layouts.data(), static_cast<const BYTE*>(data) + sizeof(Header), header.count * sizeof(Layout));
    for (size_t i = 0; i < next.layouts.size(); ++i) {
        const auto& entry = next.layouts[i];
        if (!entry.id || !entry.label[0] || !wmemchr(entry.label, 0, _countof(entry.label)) ||
            !wmemchr(entry.name, 0, _countof(entry.name))) return false;
        for (size_t j = 0; j < i; ++j) if (next.layouts[j].id == entry.id) return false;
    }
    packet = std::move(next); return true;
}
}
