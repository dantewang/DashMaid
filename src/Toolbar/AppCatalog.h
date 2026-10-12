#pragma once

namespace dm
{
    struct CatalogEntry
    {
        std::wstring name;
        std::wstring path;     // Path to start.
        std::wstring display;  // Path to show.
    };

    // Lists the apps in the Start menu "All apps" list (shell:AppsFolder).
    // The list is built on a worker thread.
    class AppCatalog
    {
    public:
        ~AppCatalog();

        // Starts a new scan unless a scan runs or the last scan is recent.
        // Posts 'message' to 'notify' when the scan is complete.
        void Refresh(HWND notify, UINT message);

        // Returns the result of the last complete scan.
        std::vector<CatalogEntry> Entries();

    private:
        static std::vector<CatalogEntry> Scan();

        std::mutex mutex_;
        std::thread worker_;
        bool running_ = false;
        ULONGLONG lastScan_ = 0;
        std::vector<CatalogEntry> entries_;
    };
}
