# LayoutDock

Version **1.0** · Author: **Moonl1ght** · [GitHub repository](https://github.com/PandaWarrior2/layoutdock)

A native keyboard layout switcher for Windows 11. Keep your installed layouts visible in the taskbar and switch between the last two with a customizable keyboard shortcut.

LayoutDock reads layouts directly from Windows, follows the system switcher order, and updates the list without a restart. The active layout has a filled background; the previous layout is underlined. Layouts of the same language get distinct labels, such as `EN-US` and `EN-GB`.

## Requirements

- Windows 11, x64, with the Explorer desktop shell.
- At least two installed keyboard layouts to use the last-two-layouts shortcut.
- No administrator privileges or separate application runtime are required.

The interface is in English. Language and region names are requested from Windows in English; keyboard variant names come from the installed layout metadata.

## Getting started

Build the application using the instructions below, then run `build\LayoutDock.exe`. Keep `LayoutDock.Widget.dll` in the same directory as the executable. To distribute the application, copy these two files together.

By default, the widget docks near the notification area of the primary taskbar. Right-click the widget or its tray icon to open settings. Changes are applied and saved automatically.

| Action | Result |
| --- | --- |
| Click a layout | Activate that layout and update the last-used pair |
| Press **CapsLock** | Switch between the last two layouts, using the default shortcut |
| Press **Shift + CapsLock** | Toggle capitalization when CapsLock is the configured shortcut |
| Middle-click the widget | Switch between the last two layouts |
| Left-click the tray icon | Switch between the last two layouts |
| Right-click the widget or tray icon | Open settings |

For example, after selecting `RU → EN → IS`, the shortcut toggles `IS ↔ EN`. With only one installed layout, it has no effect. Windows may place the tray icon in the hidden-icons area.

Use **Quit LayoutDock** in settings to exit. Closing the settings panel leaves the widget running. A second launch reuses the existing application instance.

```powershell
# Open settings for the running instance, or start with settings open.
.\build\LayoutDock.exe --settings

# Stop the running instance.
Start-Process -FilePath .\build\LayoutDock.exe -ArgumentList --stop -WindowStyle Hidden -Wait
```

## Settings

### Placement

Choose **Dock in taskbar** or **Floating widget**, then select a monitor. The monitor list includes resolution and marks the primary display. In floating mode, drag the dots on the left to move the widget. Its position and selected monitor are saved.

If the selected monitor is disconnected, LayoutDock uses the primary display and keeps the preference for when the monitor returns. If there is no taskbar or not enough room, the widget temporarily floats above the selected display's work area.

### Taskbar offset

**Horizontal** and **Vertical** adjust the docked position relative to automatic placement. Negative values move left or up; positive values move right or down. Values range from −3000 to 3000 in logical pixels, scaled for the display's DPI, and the resulting position stays within the taskbar bounds.

The arrow buttons move by one unit. **Reset** clears both offsets. Invalid text does not move the widget and is replaced with the last valid value when the field loses focus. Floating mode disables these fields while retaining their values.

### Keyboard shortcut

Click the current shortcut under **Switch between the last two layouts**, then press and release a new combination. Supported shortcuts include CapsLock, function keys, **Alt + Shift** without a third key, and combinations of Ctrl, Alt, Shift or Win with another key. Ordinary typing keys require Ctrl, Alt or Win. Windows reserves some combinations, including F12.

Press Esc, click the shortcut again, or move focus away to cancel recording. **Reset** restores CapsLock. If a shortcut is already registered or unavailable, the previous shortcut remains active. When another shortcut is configured, CapsLock resumes its normal capitalization behavior.

When **Alt + Shift** is configured, releasing the pair switches the last two layouts instead of cycling through the Windows list. Either press order works. An additional key passes the full combination through to Windows or the application, including Alt + Shift + Tab. Holding Ctrl or Win prevents the pair from triggering. LayoutDock does not modify the Windows language shortcut setting; normal system behavior returns when LayoutDock exits.

If a saved shortcut is unavailable at startup, LayoutDock uses CapsLock for that session and displays a message in settings. The saved preference is retained for the next launch. Avoid running another layout-switching utility with the same shortcut.

### Appearance and startup

The settings panel follows the Windows app theme and high-contrast settings, uses native rounded corners, and respects the system animation preference. It opens near the cursor, stays inside the display's work area, and scrolls when necessary, including during keyboard navigation. Click outside, press Esc, or use the close button to dismiss it. While recording a shortcut, the first Esc cancels recording.

**Start with Windows** is off by default. Enabling it creates the `LayoutDock` value under:

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
```

If you move the application after enabling startup, turn this setting off and on again from its new location.

### About

The **About** section shows the application version and author. Click the repository link to open the project on GitHub in your default browser.

## Build from source

The project uses **GNU Make** on Windows and C++17. Run commands from the repository root. `make` and `mingw32-make` are both supported names for GNU Make; Microsoft's `nmake` is not supported.

Stop LayoutDock before rebuilding or cleaning a running copy: Windows locks the executable and the DLL loaded by Explorer.

### MinGW-w64

Install an x64 MinGW-w64 toolchain with `g++` and `windres`, plus GNU Make, and add their binary directories to `PATH`. The current build has been verified with GCC 15.2 and GNU Make 4.4.1.

```powershell
make -j4
make test
make run
```

The application is written to `build\LayoutDock.exe` and `build\LayoutDock.Widget.dll`. Compiler runtimes are linked statically; no MinGW runtime DLLs need to be distributed.

### MSVC

Install Visual Studio Build Tools with the **Desktop development with C++** workload and a Windows SDK. Open an **x64 Native Tools Command Prompt**, make GNU Make available on `PATH`, and run:

```text
make TOOLCHAIN=msvc -j4
make TOOLCHAIN=msvc test
```

MSVC outputs go to `build\msvc\` so the two toolchains do not share object files or binaries. This configuration uses the static CRT (`/MT`).

### Make targets

| Target | Purpose |
| --- | --- |
| `make` | Build the application and widget DLL |
| `make tests` | Build every test executable |
| `make test` | Build and run unit tests |
| `make test-ui` | Run the isolated settings rendering test |
| `make run` | Build and start LayoutDock |
| `make stop` | Request a clean application shutdown |
| `make clean` | Remove the generated `build` directory for both toolchains |
| `make help` | Show available targets |

Add `TOOLCHAIN=msvc` to use the MSVC output directory for any build or test target. `make clean` removes both directories. Build outputs, compiled reference binaries, local diagnostic captures and IDE caches are excluded by `.gitignore`.

## Tests and diagnostics

`make test` checks layout history, shortcut validation and serialization, modifier handling, catalog ordering, distinct labels, monitor fallback, widget geometry and the IPC format. Catalog changes use synthetic data and do not install or remove Windows languages.

`make test-ui` opens a temporary settings window backed by an in-memory model. It checks that mouse movement within a control and refreshes of unchanged values do not cause redundant repaints. It also exercises changed values, dropdowns, the startup switch, Tab navigation and scrolling without changing saved settings or requiring a running LayoutDock instance.

For integration tests, build all test executables and start the matching application build first:

```powershell
make tests
make run
.\build\settings_test.exe
.\build\flyout_test.exe
.\build\hotkey_integration_test.exe
```

Use `build\msvc\` instead when testing the MSVC build. Interactive tests temporarily take focus; avoid switching windows or typing while they run.

| Test | Coverage |
| --- | --- |
| `settings_test.exe` | Reopening settings, placement modes, monitors, offsets, spinners, input validation, reset and persistence; restores the original settings |
| `flyout_test.exe` | Opening from the tray and widget, native corner preference, dropdowns, Tab, Esc, placement on each monitor, scrolling and outside-click dismissal |
| `hotkey_integration_test.exe` | Shortcut recording, cancellation, conflicts, real switching, repeat suppression, CapsLock behavior, Alt + Shift, restart persistence and reset; restores the original shortcut |
| `integration_test.exe` | Windows layout enumeration, real CapsLock and Shift + CapsLock, widget clicks, focus preservation and external layout changes; requires the default CapsLock shortcut |

Additional PowerShell tools are available:

```powershell
.\tools\verify-lifecycle.ps1
.\tools\verify-monitor.ps1
.\tools\inspect-taskbar.ps1 -Capture .\tools\taskbar.png
.\tools\capture-settings.ps1 -Capture .\tools\settings.png
```

The lifecycle check verifies single-instance behavior, widget recreation, DLL unloading and restart. The monitor check requires a secondary taskbar and restores the original monitor. The capture tool requires settings to be open. `verify-menu.ps1` is a compatibility wrapper for the flyout test. Pass `-Executable .\build\msvc\LayoutDock.exe` to the lifecycle and monitor scripts when testing MSVC.

## Data and architecture

Settings and logs are stored in `%LOCALAPPDATA%\LayoutDock\`:

- `settings.ini`: placement, offsets, selected monitor and shortcut.
- `LayoutDock.log`: lifecycle and layout-switching diagnostics. Typed text and application titles are not logged.

| Source | Responsibility |
| --- | --- |
| `src/controller.cpp` | Application process, layout history, global shortcut, settings, persistence and widget recovery |
| `src/settings.cpp`, `src/settings.rc` | Settings behavior and native keyboard-accessible controls |
| `src/settings_flyout.cpp` | Win32/GDI+ presentation, theme, DPI, scrolling, animation and buffered rendering |
| `src/widget.cpp` | Widget DLL running in Explorer, rendered with Direct2D, DirectWrite and DirectComposition |
| `src/hotkey.h`, `src/history.h` | Shortcut handling and last-two-layout history |
| `src/layout_catalog.h` | Full HKL enumeration, Windows switcher order and distinct layout labels |
| `src/displays.h`, `src/widget_layout.h` | Monitor selection and widget placement |
| `src/protocol.h` | Window messages and versioned `WM_COPYDATA` state packets |

The controller requests layout changes from the focused window and confirms them against its actual HKL. Changes made through Windows update the same history. Layouts refresh every three seconds and after relevant Windows notifications. A separate UI Automation thread measures taskbar contents without blocking keyboard input.

The `wndinject/` directory contains the original injection and rendering reference, not a dependency of the application build.

The application icon and its editable SVG source are in [`assets/`](assets/README.md). The icon is embedded during the normal build; no image tooling is required.

## Known limitations

- Taskbar docking loads a DLL into Explorer and is not an official taskbar extension API. Major Windows updates or taskbar customizers may require changes. Floating mode is available as a fallback.
- The widget does not remove Windows language indicators or reserve taskbar space. It positions itself around the visible contents without resizing the original taskbar windows.
- Elevated applications or applications with custom input handling may reject a layout change. Unconfirmed changes are not shown as successful.
- Layouts come from `GetKeyboardLayoutList`. LayoutDock does not install or remove layouts, and IME profiles sharing the same HKL are not shown as separate buttons.
- Windows CTF ordering metadata is an internal format read without modification. If unavailable, LayoutDock uses the user language order, retaining all loaded layouts.
- The controller supports widget recovery. Tests cover recreating the widget; they do not forcibly restart the user's Explorer process or physically disconnect monitors.
