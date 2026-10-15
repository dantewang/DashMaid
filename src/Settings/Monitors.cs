using System.Runtime.InteropServices;

namespace DashMaid.Settings;

// The connected monitors, from the DisplayConfig API. The toolbar finds the
// monitor by the same device path (src/Toolbar/Monitors.cpp).
static class Monitors
{
    public record Monitor(string Path, string Name);

    public static List<Monitor> List()
    {
        var result = new List<Monitor>();
        PathInfo[] paths;
        ModeInfo[] modes;
        int r;
        do
        {
            if (GetDisplayConfigBufferSizes(QdcOnlyActivePaths, out var pathCount, out var modeCount) != 0)
            {
                return result;
            }
            paths = new PathInfo[pathCount];
            modes = new ModeInfo[modeCount];
            r = QueryDisplayConfig(QdcOnlyActivePaths, ref pathCount, paths, ref modeCount, modes, IntPtr.Zero);
            Array.Resize(ref paths, (int)pathCount);
        } while (r == ErrorInsufficientBuffer);
        if (r != 0)
        {
            return result;
        }

        foreach (var p in paths)
        {
            var name = new TargetDeviceName();
            name.header.type = GetTargetName;
            name.header.size = (uint)Marshal.SizeOf<TargetDeviceName>();
            name.header.adapterId = p.targetAdapterId;
            name.header.id = p.targetId;
            if (DisplayConfigGetDeviceInfo(ref name) != 0 || string.IsNullOrEmpty(name.monitorDevicePath) ||
                result.Exists(m => string.Equals(m.Path, name.monitorDevicePath, StringComparison.OrdinalIgnoreCase)))
            {
                continue;
            }
            result.Add(new Monitor(name.monitorDevicePath, name.monitorFriendlyDeviceName));
        }

        // Monitors without a name, and monitors with the same name, get a number.
        for (int i = 0; i < result.Count; i++)
        {
            var n = result[i].Name;
            if (string.IsNullOrWhiteSpace(n) || result.FindAll(m => m.Name == n).Count > 1)
            {
                result[i] = result[i] with { Name = $"{(string.IsNullOrWhiteSpace(n) ? "显示器" : n)} {i + 1}" };
            }
        }
        return result;
    }

    const uint QdcOnlyActivePaths = 2;
    const int ErrorInsufficientBuffer = 122;
    const uint GetTargetName = 2;

    [StructLayout(LayoutKind.Sequential)]
    struct Luid
    {
        public uint low;
        public int high;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct PathInfo
    {
        public Luid sourceAdapterId;
        public uint sourceId, sourceModeInfoIdx, sourceStatusFlags;
        public Luid targetAdapterId;
        public uint targetId, targetModeInfoIdx, outputTechnology, rotation, scaling;
        public uint refreshNumerator, refreshDenominator, scanLineOrdering;
        public int targetAvailable;
        public uint targetStatusFlags, flags;
    }

    [StructLayout(LayoutKind.Sequential, Size = 64)]
    struct ModeInfo
    {
        public uint infoType, id;
        public Luid adapterId;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct Header
    {
        public uint type, size;
        public Luid adapterId;
        public uint id;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct TargetDeviceName
    {
        public Header header;
        public uint flags, outputTechnology;
        public ushort edidManufactureId, edidProductCodeId;
        public uint connectorInstance;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string monitorFriendlyDeviceName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string monitorDevicePath;
    }

    [DllImport("user32.dll")]
    static extern int GetDisplayConfigBufferSizes(uint flags, out uint pathCount, out uint modeCount);

    [DllImport("user32.dll")]
    static extern int QueryDisplayConfig(uint flags, ref uint pathCount, [Out] PathInfo[] paths,
        ref uint modeCount, [Out] ModeInfo[] modes, IntPtr topology);

    [DllImport("user32.dll")]
    static extern int DisplayConfigGetDeviceInfo(ref TargetDeviceName request);
}
