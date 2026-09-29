#pragma once
#include <windows.h>
namespace dock {
void InitializeSettingsFlyout(HWND view);
void PresentSettingsFlyout(HWND view);
void DisposeSettingsFlyout();
bool SettingsFlyoutMessage(HWND view, UINT message, WPARAM wp, LPARAM lp, INT_PTR& result);
}
