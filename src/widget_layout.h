#pragma once
#include "protocol.h"
#include <algorithm>
#include <cmath>

namespace dock {
struct Grid {
    int count{}, columns = 1, rows = 1;
    float cell = 36, width = 64, height = 36;
    float X(int index) const { return 20.f + (index % columns) * cell; }
    float Y(int index) const { return 4.f + (index / columns) * 32.f; }
    int Hit(float x, float y) const {
        if (x < 20 || y < 4) return -1;
        int column = static_cast<int>((x - 20) / cell), row = static_cast<int>((y - 4) / 32);
        int index = row * columns + column;
        if (column >= columns || row >= rows || index >= count || x - X(index) >= cell - 4 || y - Y(index) >= 28) return -1;
        return index;
    }
};
inline Grid MakeGrid(const std::vector<Layout>& layouts, float maxWidth) {
    Grid result; result.count = static_cast<int>(layouts.size());
    if (layouts.empty()) return result;
    for (const auto& item : layouts) result.cell = std::max(result.cell, 12.f + 7.5f * static_cast<float>(wcslen(item.label)));
    result.columns = std::max(1, std::min(result.count, static_cast<int>(std::floor((maxWidth - 28) / result.cell))));
    result.rows = (result.count + result.columns - 1) / result.columns;
    result.width = 28 + result.columns * result.cell;
    result.height = 4 + result.rows * 32.f;
    return result;
}
}
