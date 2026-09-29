#pragma once
#include "protocol.h"
#include "hotkey.h"

namespace dock {
enum class Setting { Mode, Monitor, OffsetX, OffsetY, ResetPosition, Startup, Hotkey, Exit };
struct SettingsState { Packet packet; bool startup{}; Hotkey hotkey; std::wstring hotkeyError; };
using ReadSettings = SettingsState (*)();
using ApplySetting = void (*)(Setting, int64_t);
void ShowSettings(HINSTANCE instance, HWND owner, ReadSettings read, ApplySetting apply);
bool SettingsMessage(MSG& message);
void CloseSettings();
bool RecordingHotkey();
UINT_PTR HotkeyRecordingSession();
void RecordHotkey(Hotkey hotkey); // Called by the keyboard hook; only posts a message.
}
