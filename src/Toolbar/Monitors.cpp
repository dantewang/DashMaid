#include "pch.h"
#include "Monitors.h"

namespace dm
{
    namespace
    {
        // The GDI device name (for example \\.\DISPLAY2) of the source that
        // shows the monitor with this device path.
        std::wstring GdiName(const std::wstring& path)
        {
            UINT32 pathCount = 0, modeCount = 0;
            std::vector<DISPLAYCONFIG_PATH_INFO> paths;
            std::vector<DISPLAYCONFIG_MODE_INFO> modes;
            LONG r;
            do
            {
                if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
                {
                    return {};
                }
                paths.resize(pathCount);
                modes.resize(modeCount);
                r = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
            } while (r == ERROR_INSUFFICIENT_BUFFER);
            if (r != ERROR_SUCCESS)
            {
                return {};
            }
            paths.resize(pathCount);

            for (const auto& p : paths)
            {
                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof(target);
                target.header.adapterId = p.targetInfo.adapterId;
                target.header.id = p.targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS ||
                    _wcsicmp(target.monitorDevicePath, path.c_str()) != 0)
                {
                    continue;
                }
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = p.sourceInfo.adapterId;
                source.header.id = p.sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS)
                {
                    return source.viewGdiDeviceName;
                }
            }
            return {};
        }
    }

    HMONITOR DockMonitor(const std::wstring& path)
    {
        const HMONITOR primary = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        if (path.empty())
        {
            return primary;
        }
        struct Search
        {
            std::wstring gdi;
            HMONITOR found = nullptr;
        } search{ GdiName(path) };
        if (search.gdi.empty())
        {
            return primary;
        }
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR mon, HDC, LPRECT, LPARAM data) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(data);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi) && _wcsicmp(mi.szDevice, s.gdi.c_str()) == 0)
            {
                s.found = mon;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found ? search.found : primary;
    }
}
