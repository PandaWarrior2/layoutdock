#include "../src/history.h"
#include <cstdio>
#include <cstdlib>
static void Check(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
int main() {
    LayoutHistory h;
    Check(!h.Current() && !h.Previous(), "empty list");
    h.SetAvailable({1, 2, 3});
    h.Observe(2);
    Check(h.Current() == 2 && h.Previous() == 1, "initial foreground");
    h.Observe(3);
    Check(h.Current() == 3 && h.Previous() == 2, "third language replaces pair");
    h.Observe(h.Previous());
    Check(h.Current() == 2 && h.Previous() == 3, "toggle returns to previous");
    h.Observe(h.Previous());
    Check(h.Current() == 3 && h.Previous() == 2, "repeated toggle stays in pair");
    Check(!h.Observe(3) && h.Previous() == 2, "polling does not overwrite previous");
    Check(!h.Observe(99) && h.Current() == 3, "unknown handle ignored");
    h.SetAvailable({1, 3});
    Check(h.Current() == 3 && h.Previous() == 1, "removed layout pruned");
    h.SetAvailable({3});
    Check(!h.Previous(), "one layout has no previous");
    h.SetAvailable({});
    Check(!h.Current() && !h.Previous(), "all layouts removed");
    std::puts("PASS: layout history (9 scenarios)");
}
