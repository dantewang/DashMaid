#include "pch.h"
#include "AppCatalog.h"

namespace dm
{
    namespace
    {
        constexpr ULONGLONG RescanAfterMs = 60'000;

        std::wstring DisplayName(IShellItem* item, SIGDN type)
        {
            PWSTR raw = nullptr;
            if (FAILED(item->GetDisplayName(type, &raw)))
            {
                return {};
            }
            std::wstring s(raw);
            CoTaskMemFree(raw);
            return s;
        }

        // Desktop apps have a parsing name like "{known folder id}\dir\app.exe".
        // Returns the file path, or an empty string for packaged apps.
        std::wstring ResolveKnownFolder(const std::wstring& parsing)
        {
            if (parsing.size() < 40 || parsing[0] != L'{' || parsing[37] != L'}' || parsing[38] != L'\\')
            {
                return {};
            }
            KNOWNFOLDERID id{};
            if (FAILED(CLSIDFromString(parsing.substr(0, 38).c_str(), &id)))
            {
                return {};
            }
            PWSTR raw = nullptr;
            if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &raw)))
            {
                return {};
            }
            std::wstring path = std::wstring(raw) + parsing.substr(38);
            CoTaskMemFree(raw);
            return path;
        }
    }

    AppCatalog::~AppCatalog()
    {
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void AppCatalog::Refresh(HWND notify, UINT message)
    {
        {
            std::lock_guard lock(mutex_);
            if (running_ || (lastScan_ != 0 && GetTickCount64() - lastScan_ < RescanAfterMs))
            {
                return;
            }
            running_ = true;
        }
        if (worker_.joinable())
        {
            worker_.join();
        }
        worker_ = std::thread([this, notify, message] {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            std::vector<CatalogEntry> list = Scan();
            if (SUCCEEDED(hr))
            {
                CoUninitialize();
            }
            {
                std::lock_guard lock(mutex_);
                entries_ = std::move(list);
                lastScan_ = GetTickCount64();
                running_ = false;
            }
            PostMessageW(notify, message, 0, 0);
        });
    }

    std::vector<CatalogEntry> AppCatalog::Entries()
    {
        std::lock_guard lock(mutex_);
        return entries_;
    }

    std::vector<CatalogEntry> AppCatalog::Scan()
    {
        std::vector<CatalogEntry> out;
        winrt::com_ptr<IShellItem> folder;
        if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(folder.put()))))
        {
            return out;
        }
        winrt::com_ptr<IEnumShellItems> items;
        if (FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(items.put()))))
        {
            return out;
        }
        winrt::com_ptr<IShellItem> item;
        while (items->Next(1, item.put(), nullptr) == S_OK)
        {
            CatalogEntry e;
            e.name = DisplayName(item.get(), SIGDN_NORMALDISPLAY);
            const std::wstring parsing = DisplayName(item.get(), SIGDN_PARENTRELATIVEPARSING);
            item = nullptr;
            if (e.name.empty() || parsing.empty())
            {
                continue;
            }
            const std::wstring file = ResolveKnownFolder(parsing);
            e.path = file.empty() ? L"shell:AppsFolder\\" + parsing : file;
            e.display = file.empty() ? parsing : file;
            out.push_back(std::move(e));
        }
        return out;
    }
}
