#pragma once
#include <windows.h>
#include <array>
#include <string>

namespace dock {
inline UINT ModifierForKey(UINT key) {
    switch (key) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return MOD_CONTROL;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return MOD_SHIFT;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return MOD_ALT;
    case VK_LWIN: case VK_RWIN: return MOD_WIN;
    default: return 0;
    }
}
struct Hotkey {
    UINT key = VK_CAPITAL;
    UINT modifiers = 0;
    UINT Pack() const { return key | (modifiers << 16); }
    static Hotkey Unpack(UINT value) { return {value & 0xffff, value >> 16}; }
    bool operator==(const Hotkey& other) const { return key == other.key && modifiers == other.modifiers; }
    bool Default() const { return key == VK_CAPITAL && modifiers == 0; }
    bool AltShift() const { return key == 0 && modifiers == (MOD_ALT | MOD_SHIFT); }
};
inline std::wstring HotkeyError(Hotkey hotkey) {
    if (hotkey.AltShift()) return {};
    if (hotkey.key < VK_BACK || hotkey.key > 0xfe || ModifierForKey(hotkey.key) ||
        hotkey.key == VK_PACKET || hotkey.key == VK_PROCESSKEY || (hotkey.modifiers & ~15u))
        return L"Use a key with modifiers, such as Ctrl + Alt + K.";
    if (hotkey.key == VK_F12 || (hotkey.key == VK_DELETE && (hotkey.modifiers & (MOD_CONTROL | MOD_ALT)) == (MOD_CONTROL | MOD_ALT)) ||
        (hotkey.key == 'L' && (hotkey.modifiers & MOD_WIN)))
        return L"Windows reserves this shortcut. Choose another one.";
    const bool standalone = hotkey.key == VK_CAPITAL || hotkey.key == VK_PAUSE || hotkey.key == VK_SCROLL ||
        (hotkey.key >= VK_F1 && hotkey.key <= VK_F24);
    if (!standalone && !(hotkey.modifiers & (MOD_CONTROL | MOD_ALT | MOD_WIN)))
        return L"Add Ctrl, Alt or Win, or use CapsLock or a function key.";
    return {};
}
inline std::wstring HotkeyName(Hotkey hotkey) {
    if (hotkey.AltShift()) return L"Alt + Shift";
    std::wstring name;
    if (hotkey.modifiers & MOD_CONTROL) name += L"Ctrl + ";
    if (hotkey.modifiers & MOD_ALT) name += L"Alt + ";
    if (hotkey.modifiers & MOD_SHIFT) name += L"Shift + ";
    if (hotkey.modifiers & MOD_WIN) name += L"Win + ";
    if (hotkey.key == VK_CAPITAL) return name + L"CapsLock";
    if (hotkey.key == VK_SPACE) return name + L"Space";
    if (hotkey.key >= VK_F1 && hotkey.key <= VK_F24) return name + L"F" + std::to_wstring(hotkey.key - VK_F1 + 1);
    if ((hotkey.key >= 'A' && hotkey.key <= 'Z') || (hotkey.key >= '0' && hotkey.key <= '9')) return name + static_cast<wchar_t>(hotkey.key);
    wchar_t text[64]{};
    const UINT scan = MapVirtualKeyW(hotkey.key, MAPVK_VK_TO_VSC_EX);
    LONG keyData = static_cast<LONG>((scan & 0xff) << 16);
    if (scan & 0xff00) keyData |= 1 << 24;
    if (GetKeyNameTextW(keyData, text, _countof(text))) return name + text;
    return name + L"Key " + std::to_wstring(hotkey.key);
}

// Swallow matching key-up events even when recording ends or the binding changes
// during a key press. No character data or input history is retained.
class ConsumedKeys {
    std::array<bool, 256> keys{};
public:
    bool Continue(UINT key, bool up) {
        if (key >= keys.size() || !keys[key]) return false;
        if (up) keys[key] = false;
        return true;
    }
    void Down(UINT key) { if (key < keys.size()) keys[key] = true; }
    UINT Modifiers() const {
        UINT result = 0;
        for (UINT key = 0; key < keys.size(); ++key) if (keys[key]) result |= ModifierForKey(key);
        return result;
    }
};

struct AltShiftAction {
    bool consume{}, trigger{}, maskAltUp{};
    UINT replayModifier{};
};
// The first modifier reaches Windows normally (Shift-click, Alt shortcuts, etc.).
// Defer the second until we know whether this is the bare pair or a longer chord.
class AltShiftGesture {
    UINT withheld{};
    bool armed{}, maskAlt{};
public:
    void Cancel() { armed = false; }
    UINT WithheldModifier() const { return ModifierForKey(withheld); }
    AltShiftAction Process(UINT key, bool down, bool up, UINT before, bool enabled) {
        AltShiftAction result;
        const UINT modifier = ModifierForKey(key);
        if (up && modifier == MOD_ALT && maskAlt) {
            maskAlt = false; result.maskAltUp = result.consume = true;
        }
        if (withheld) {
            if (down && key == withheld) { result.consume = true; return result; }
            if (up && (key == withheld || (armed && (modifier == MOD_ALT || modifier == MOD_SHIFT)))) {
                result.trigger = armed && enabled;
                if (result.trigger && ModifierForKey(withheld) == MOD_SHIFT) {
                    // Windows saw only Alt. Mask its release so it cannot open a menu.
                    if (modifier == MOD_ALT) result.maskAltUp = result.consume = true;
                    else maskAlt = true;
                }
                armed = false;
                if (key == withheld) { withheld = 0; result.consume = true; }
                return result;
            }
            if (down || up) {
                result.replayModifier = withheld; result.consume = true;
                withheld = 0; armed = maskAlt = false;
                return result;
            }
        }
        if (enabled && down && (modifier == MOD_ALT || modifier == MOD_SHIFT) &&
            !(before & modifier) && (before | modifier) == (MOD_ALT | MOD_SHIFT)) {
            withheld = key; armed = true; result.consume = true;
        }
        return result;
    }
};
}
