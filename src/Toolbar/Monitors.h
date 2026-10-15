#pragma once

namespace dm
{
    // The monitor that the screen top mode uses. "path" is the device
    // interface path of the monitor (DisplayConfig "monitorDevicePath"). It
    // stays the same when the display layout changes. An empty path is the
    // primary monitor. If the monitor is not connected, the result is the
    // primary monitor.
    HMONITOR DockMonitor(const std::wstring& path);
}
