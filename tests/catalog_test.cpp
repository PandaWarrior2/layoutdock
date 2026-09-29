#include "../src/layout_catalog.h"
#include "../src/widget_layout.h"
#include "../src/history.h"
#include "../src/displays.h"
#include <cstdio>
#include <cstdlib>
#include <set>

static void Check(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
    std::printf("PASS: %s\n", label);
}
int main() {
    auto live = dock::InstalledLayouts();
    auto catalog = dock::MakeCatalog(live);
    Check(!live.empty() && catalog.size() == live.size(), "live Windows list has one entry per HKL");
    for (const auto& entry : catalog) std::wprintf(L"  %016llX  %ls  %ls\n", entry.id, entry.label, entry.name);
    // Synthetic snapshots only: never install/remove languages in the user's Windows account.
    auto variants = dock::MakeCatalog({0x04090409, 0x08090809, 0xfffffffff0010409, 0x04070407, 0x04110411});
    std::set<std::wstring> labels;
    for (const auto& entry : variants) { labels.insert(entry.label); Check(entry.name[0] != 0, "layout has a readable full name"); }
    Check(labels.size() == variants.size(), "same-language layouts have distinct labels");
    Check(dock::Index(variants, 0x04090409) != dock::Index(variants, 0xfffffffff0010409), "same-language layouts retain full HKL identity");
    std::vector<dock::LanguageOrder> switchOrder{{0x0409, {0x04090409}}, {0x0419, {0x04190419}}, {0x040f, {0x040f040f}}, {0x0407, {0x04070407}}};
    auto ordered = dock::OrderLayouts({0x04090409, 0x040f040f, 0x04190419, 0x04070407}, switchOrder);
    Check(ordered == std::vector<uint64_t>({0x04090409, 0x04190419, 0x040f040f, 0x04070407}), "switcher order wins over HKL load order");
    Check(dock::OrderLayouts({0x04090409, 0x04070407, 0x04110411}, switchOrder) ==
        std::vector<uint64_t>({0x04090409, 0x04070407, 0x04110411}), "stale order entries ignored and unknown language retained");
    std::vector<dock::LanguageOrder> sameLanguage{{0x0409, {0xf0010409, 0x04090409}}};
    Check(dock::OrderLayouts({0x04090409, 0xfffffffff0010409}, sameLanguage) ==
        std::vector<uint64_t>({0xfffffffff0010409, 0x04090409}), "profile ordering supports sign-extended HKLs");
    Check(dock::OrderLayouts(live, {}).size() == live.size(), "missing Windows sort metadata retains all layouts");
    auto displays = dock::Displays();
    Check(!displays.empty(), "monitors enumerated");
    for (size_t i = 0; i < displays.size(); ++i) Check(dock::PreferredDisplay(displays, displays[i].info.szDevice) == i, "monitor selection uses persisted device name");
    Check((displays[dock::PreferredDisplay(displays, L"disconnected")].info.dwFlags & MONITORINFOF_PRIMARY) != 0, "disconnected monitor falls back to primary");
    dock::Packet negative, restored; negative.trayLeft = -335; negative.appsRight = -1438;
    negative.offset = 37; negative.offsetY = -3;
    auto negativeBytes = dock::Encode(negative);
    Check(dock::Decode(negativeBytes.data(), negativeBytes.size(), restored) && restored.trayLeft == -335 &&
        restored.appsRight == -1438 && restored.trayLeft != dock::UnknownCoordinate, "left monitor coordinates survive IPC");
    Check(restored.offset == 37 && restored.offsetY == -3, "both signed taskbar offsets survive IPC");
    negative.version = 3; negativeBytes = dock::Encode(negative);
    Check(!dock::Decode(negativeBytes.data(), negativeBytes.size(), restored), "previous IPC version rejected after offset extension");
    LayoutHistory history; history.SetAvailable({1, 2, 3}); history.Observe(2); history.Observe(3);
    history.SetAvailable({1, 2, 3, 4});
    Check(history.Current() == 3 && history.Previous() == 2, "adding a layout preserves the active pair");
    history.SetAvailable({1, 3, 4});
    Check(history.Current() == 3 && history.Previous() == 1, "removing previous layout selects a remaining fallback");
    for (size_t count : {size_t(0), size_t(1), size_t(2), size_t(5)}) {
        dock::Packet source, decoded; source.layouts.assign(variants.begin(), variants.begin() + count);
        source.current = count ? static_cast<int>(count - 1) : -1; source.previous = count > 1 ? 0 : -1;
        auto bytes = dock::Encode(source);
        Check(dock::Decode(bytes.data(), bytes.size(), decoded) && decoded.layouts.size() == count && decoded.current == source.current,
            "variable-size state round-trips including zero/one/many layouts");
        if (count) Check(!dock::Decode(bytes.data(), bytes.size() - 1, decoded), "truncated packet rejected");
    }
    dock::Packet source, decoded; source.layouts = variants; source.current = 0;
    source.layouts[1].id = source.layouts[0].id;
    auto bytes = dock::Encode(source);
    Check(!dock::Decode(bytes.data(), bytes.size(), decoded), "duplicate layout IDs rejected");
    source.layouts = variants; source.current = 99; bytes = dock::Encode(source);
    Check(!dock::Decode(bytes.data(), bytes.size(), decoded), "invalid selected index rejected");
    source.current = 0; std::fill(std::begin(source.layouts[0].label), std::end(source.layouts[0].label), L'X');
    bytes = dock::Encode(source);
    Check(!dock::Decode(bytes.data(), bytes.size(), decoded), "unterminated label rejected");
    auto grid = dock::MakeGrid(variants, 220);
    Check(grid.width <= 220 && grid.rows > 1, "long labels wrap within monitor width");
    for (int i = 0; i < grid.count; ++i) Check(grid.Hit(grid.X(i) + 2, grid.Y(i) + 2) == i, "wrapped button hit testing");
    Check(grid.Hit(1, 10) == -1 && grid.Hit(grid.width, 10) == -1, "padding does not select a layout");
    auto shortList = dock::MakeCatalog({0x04090409});
    Check(dock::MakeGrid(shortList, 1000).width < dock::MakeGrid(variants, 1000).width, "widget shrinks after layouts are removed");
    std::puts("CATALOG AND PROTOCOL PASSED");
}
