#pragma once

#include "Config.h"

namespace dm
{
    // True for "shell:" paths, for example packaged apps in shell:AppsFolder.
    // These items cannot run as administrator and have no file location.
    bool IsShellPath(const std::wstring& path);

    void LaunchItem(HWND owner, const AppItem& item, bool admin);
    void OpenFileLocation(const std::wstring& path);
    void CopyToClipboard(HWND owner, const std::wstring& text);
    void ShowRunDialog();
    void OpenInNotepad(const std::filesystem::path& file);
}
