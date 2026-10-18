#pragma once
#include "platform.hpp"
#include <functional>
namespace splitter {
// Explicit user action only. Uses the unchanged, signed vendor installer for install AND removal.
void runDriverSetup(const std::function<void(const std::wstring&)>& progress);
void runRenameHelper(HWND owner, const std::string& source);
void openLink(HWND owner, const wchar_t* url);
}
