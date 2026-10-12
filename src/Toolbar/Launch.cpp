#include "pch.h"
#include "Launch.h"
#include "Icons.h"

namespace dm
{
    bool IsShellPath(const std::wstring& path)
    {
        return path.size() >= 6 && _wcsnicmp(path.c_str(), L"shell:", 6) == 0;
    }

    void LaunchItem(HWND owner, const AppItem& item, bool admin)
    {
        const std::wstring file = ExpandPath(item.path);
        std::wstring dir = ExpandPath(item.workingDir);
        if (dir.empty() && !IsShellPath(file))
        {
            std::filesystem::path p(file);
            if (p.is_absolute() && p.has_parent_path())
            {
                dir = p.parent_path().wstring();
            }
        }

        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.fMask = SEE_MASK_ASYNCOK;
        sei.hwnd = owner;
        sei.lpVerb = (admin && !IsShellPath(file)) ? L"runas" : nullptr;
        sei.lpFile = file.c_str();
        sei.lpParameters = item.args.empty() ? nullptr : item.args.c_str();
        sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
    }

    void OpenFileLocation(const std::wstring& path)
    {
        const std::wstring full = ExpandPath(path);
        PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(full.c_str());
        if (pidl)
        {
            SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            ILFree(pidl);
        }
    }

    void CopyToClipboard(HWND owner, const std::wstring& text)
    {
        if (!OpenClipboard(owner))
        {
            return;
        }
        EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            memcpy(GlobalLock(mem), text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem))
            {
                GlobalFree(mem);
            }
        }
        CloseClipboard();
    }

    void ShowRunDialog()
    {
        winrt::com_ptr<IShellDispatch> shell;
        if (SUCCEEDED(CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(shell.put()))))
        {
            shell->FileRun();
        }
    }

    void OpenInNotepad(const std::filesystem::path& file)
    {
        const std::wstring args = L"\"" + file.wstring() + L"\"";
        ShellExecuteW(nullptr, nullptr, L"notepad.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
}
