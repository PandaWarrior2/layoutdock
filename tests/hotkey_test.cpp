#include "../src/hotkey.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool ok, const char* text) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", text);
    if (!ok) std::exit(1);
}
int main() {
    using dock::Hotkey;
    Check(Hotkey::Unpack(Hotkey{}.Pack()).Default(), "missing setting defaults to CapsLock");
    const Hotkey combination{'K', MOD_CONTROL | MOD_ALT};
    Check(Hotkey::Unpack(combination.Pack()) == combination && dock::HotkeyError(combination).empty(), "modifier combination round-trips");
    Check(dock::HotkeyName(combination) == L"Ctrl + Alt + K", "label uses stable key names across layouts");
    Check(dock::HotkeyError({'A', 0}).size() && dock::HotkeyError({'A', MOD_SHIFT}).size(), "normal typing is not accepted as a global binding");
    Check(dock::HotkeyError({VK_LCONTROL, 0}).size(), "a modifier alone is not a complete binding");
    Check(dock::HotkeyError({VK_F12, 0}).size() && dock::HotkeyError({'L', MOD_WIN}).size() &&
        dock::HotkeyError({VK_DELETE, MOD_CONTROL | MOD_ALT}).size(), "reserved system bindings rejected");
    Check(dock::HotkeyError({VK_F8, 0}).empty() && dock::HotkeyError({VK_CAPITAL, MOD_SHIFT}).empty(), "function key and modified CapsLock supported");
    Check(dock::HotkeyError(Hotkey::Unpack(0xffffffff)).size(), "invalid persisted binding rejected");
    const Hotkey pair{0, MOD_ALT | MOD_SHIFT};
    Check(dock::HotkeyError(pair).empty() && Hotkey::Unpack(pair.Pack()).AltShift() && dock::HotkeyName(pair) == L"Alt + Shift",
        "modifier-only Alt+Shift is a distinct persisted binding");
    dock::AltShiftGesture gesture;
    Check(!gesture.Process(VK_LSHIFT, true, false, 0, true).consume, "first Shift remains available for ordinary input");
    Check(gesture.Process(VK_LMENU, true, false, MOD_SHIFT, true).consume, "second modifier is withheld from Windows");
    Check(!gesture.Process(VK_LMENU, true, false, MOD_ALT | MOD_SHIFT, true).trigger, "repeated modifier does not trigger");
    auto action = gesture.Process(VK_LMENU, false, true, MOD_ALT | MOD_SHIFT, true);
    Check(action.consume && action.trigger && !action.maskAltUp, "Shift then Alt triggers once on release");
    Check(!gesture.Process(VK_LSHIFT, false, true, MOD_SHIFT, true).trigger, "final release does not trigger again");
    gesture.Process(VK_LMENU, true, false, 0, true);
    gesture.Process(VK_RSHIFT, true, false, MOD_ALT, true);
    action = gesture.Process(VK_LMENU, false, true, MOD_ALT | MOD_SHIFT, true);
    Check(action.trigger && action.maskAltUp && action.consume, "Alt-first release is masked to prevent opening menus");
    Check(gesture.Process(VK_RSHIFT, false, true, MOD_SHIFT, true).consume, "withheld Shift release is balanced");
    gesture.Process(VK_RSHIFT, true, false, MOD_ALT, true);
    action = gesture.Process('X', true, false, MOD_ALT | MOD_SHIFT, true);
    Check(action.replayModifier == VK_RSHIFT && !action.trigger, "third key restores normal Alt+Shift chord");
    Check(!gesture.Process(VK_RSHIFT, false, true, MOD_ALT | MOD_SHIFT, true).consume, "replayed modifier release reaches Windows");
    Check(!gesture.Process(VK_LSHIFT, true, false, MOD_ALT | MOD_CONTROL, true).consume, "extra Ctrl prevents bare-pair interception");
    Check(!gesture.Process(VK_LSHIFT, true, false, MOD_ALT, false).consume, "other configured hotkeys leave system Alt+Shift alone");
    dock::ConsumedKeys keys;
    keys.Down(VK_CAPITAL);
    Check(keys.Continue(VK_CAPITAL, false) && keys.Continue(VK_CAPITAL, false), "held trigger suppresses auto-repeat");
    Check(keys.Continue(VK_CAPITAL, true) && !keys.Continue(VK_CAPITAL, true), "key-up is consumed exactly once after binding changes");
    keys.Down(VK_LCONTROL); keys.Down(VK_RCONTROL); keys.Down(VK_LWIN);
    Check(keys.Modifiers() == (MOD_CONTROL | MOD_WIN), "recording tracks suppressed modifiers");
    keys.Continue(VK_LCONTROL, true);
    Check(keys.Modifiers() == (MOD_CONTROL | MOD_WIN), "releasing one Ctrl preserves the other Ctrl");
    keys.Continue(VK_RCONTROL, true); keys.Continue(VK_LWIN, true);
    Check(!keys.Modifiers() && !keys.Continue('A', false), "cancelled recording releases swallowed modifiers without affecting other keys");
}
