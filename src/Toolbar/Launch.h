#pragma once

#include "Config.h"

namespace dm
{
    // True for "shell:" paths, for example packaged apps in shell:AppsFolder.
    // These items cannot run as administrator and have no file location.
    bool IsShellPath(const std::wstring& path);

    void LaunchItem(HWND owner, const AppItem& item, bool admin);

    // Runs a command from the run box. Returns false if the shell cannot start it.
    bool RunCommand(HWND owner, const std::wstring& file, const std::wstring& params, bool admin);
    void OpenFileLocation(const std::wstring& path);
    void CopyToClipboard(HWND owner, const std::wstring& text);
    void OpenInNotepad(const std::filesystem::path& file);
}
