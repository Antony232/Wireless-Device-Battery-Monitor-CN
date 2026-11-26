#pragma once
// Manual "check for updates": reads the newest tag from the project's GitHub releases page.
//
// This is the only place where the app talks to the network, and it only happens when the user picks
// "Check for updates..." from the tray menu: one HEAD request, no version or device data is sent.
#include <windows.h>

#include <string>

namespace update {

// The version this exe was built as, taken from its own version resource, e.g. "1.0.2".
std::wstring CurrentVersion();

// Starts one background check and returns at once. When it finishes it posts `msg` to `notify`:
//   wParam = 1 newer version available, 0 already newest, -1 the check failed
//   lParam = new std::wstring(tag without the leading "v"), never null; the receiver owns it
// Returns false when a check is already running.
bool CheckAsync(HWND notify, UINT msg);

// Opens the releases page in the default browser, so the user downloads the installer themselves.
void OpenDownloadPage();

// True when dotted version `a` ("1.0.3") is newer than `b` ("1.0.2").
bool IsNewer(const std::wstring& a, const std::wstring& b);

}  // namespace update
