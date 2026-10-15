using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml.Media.Imaging;

namespace DashMaid.Settings;

// An entry of the Start menu "All apps" list.
public sealed record AppEntry(string Name, string Path, string Display);

// Shell functions: the "All apps" list and icons. The rules are the same as in the toolbar process.
public static class Shell
{
    public static bool IsStoreApp(string path) =>
        path.StartsWith(@"shell:AppsFolder\", StringComparison.OrdinalIgnoreCase);

    public static List<AppEntry> AllApps()
    {
        var list = new List<AppEntry>();
        var appsFolder = new Guid("1e87508d-89c2-42f0-8a7e-645a0f50ca58");
        if (SHGetKnownFolderItem(appsFolder, 0, IntPtr.Zero, typeof(IShellItem).GUID, out var folderObj) != 0)
        {
            return list;
        }
        var folder = (IShellItem)folderObj;
        var enumItems = new Guid("94f60519-2850-4924-aa5a-d15e84868039");
        if (folder.BindToHandler(IntPtr.Zero, enumItems, typeof(IEnumShellItems).GUID, out var enumObj) != 0)
        {
            return list;
        }
        var items = (IEnumShellItems)enumObj;
        while (items.Next(1, out var item, out var fetched) == 0 && fetched == 1)
        {
            var name = DisplayName(item, SIGDN_NORMALDISPLAY);
            var parsing = DisplayName(item, SIGDN_PARENTRELATIVEPARSING);
            Marshal.ReleaseComObject(item);
            if (string.IsNullOrEmpty(name) || string.IsNullOrEmpty(parsing))
            {
                continue;
            }
            var file = ResolveKnownFolder(parsing);
            list.Add(new AppEntry(name, file ?? @"shell:AppsFolder\" + parsing, file ?? parsing));
        }
        list.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.CurrentCultureIgnoreCase));
        return list;
    }

    // "{known folder id}\rest" becomes a file path.
    static string? ResolveKnownFolder(string parsing)
    {
        if (parsing.Length < 40 || parsing[0] != '{' || parsing[37] != '}' || parsing[38] != '\\')
        {
            return null;
        }
        if (!Guid.TryParse(parsing[..38], out var id))
        {
            return null;
        }
        if (SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, IntPtr.Zero, out var raw) != 0)
        {
            return null;
        }
        var path = Marshal.PtrToStringUni(raw) + parsing[38..];
        Marshal.FreeCoTaskMem(raw);
        return path;
    }

    static string DisplayName(IShellItem item, uint sigdn)
    {
        if (item.GetDisplayName(sigdn, out var raw) != 0)
        {
            return "";
        }
        var s = Marshal.PtrToStringUni(raw) ?? "";
        Marshal.FreeCoTaskMem(raw);
        return s;
    }

    static readonly Dictionary<string, WriteableBitmap?> icons = new(StringComparer.OrdinalIgnoreCase);

    // The icon of a file, a folder, or a "shell:AppsFolder\..." app. The result is cached.
    public static WriteableBitmap? Icon(string path, int size = 64)
    {
        var key = path + "|" + size;
        if (icons.TryGetValue(key, out var cached))
        {
            return cached;
        }
        WriteableBitmap? bitmap = null;
        var expanded = Environment.ExpandEnvironmentVariables(path.Trim().Trim('"'));
        if (expanded.Length > 0 &&
            SHCreateItemFromParsingName(expanded, IntPtr.Zero, typeof(IShellItemImageFactory).GUID, out var obj) == 0)
        {
            var factory = (IShellItemImageFactory)obj;
            if (factory.GetImage(new SIZE { cx = size, cy = size }, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, out var hbitmap) == 0)
            {
                bitmap = ToBitmap(hbitmap);
                DeleteObject(hbitmap);
            }
            Marshal.ReleaseComObject(factory);
        }
        icons[key] = bitmap;
        return bitmap;
    }

    static WriteableBitmap? ToBitmap(IntPtr hbitmap)
    {
        if (GetObject(hbitmap, Marshal.SizeOf<BITMAP>(), out var bm) == 0 || bm.bmWidth <= 0 || bm.bmHeight == 0)
        {
            return null;
        }
        int w = bm.bmWidth, h = Math.Abs(bm.bmHeight);
        var info = new BITMAPINFOHEADER
        {
            biSize = Marshal.SizeOf<BITMAPINFOHEADER>(),
            biWidth = w,
            biHeight = -h,  // Top-down rows.
            biPlanes = 1,
            biBitCount = 32,
        };
        var pixels = new byte[w * h * 4];
        var dc = GetDC(IntPtr.Zero);
        var rows = GetDIBits(dc, hbitmap, 0, (uint)h, pixels, ref info, 0);
        ReleaseDC(IntPtr.Zero, dc);
        if (rows == 0)
        {
            return null;
        }
        // XAML needs premultiplied alpha. Some icons come with straight alpha.
        bool straight = false;
        for (int i = 0; i < pixels.Length && !straight; i += 4)
        {
            byte a = pixels[i + 3];
            straight = pixels[i] > a || pixels[i + 1] > a || pixels[i + 2] > a;
        }
        if (straight)
        {
            for (int i = 0; i < pixels.Length; i += 4)
            {
                int a = pixels[i + 3];
                pixels[i] = (byte)(pixels[i] * a / 255);
                pixels[i + 1] = (byte)(pixels[i + 1] * a / 255);
                pixels[i + 2] = (byte)(pixels[i + 2] * a / 255);
            }
        }
        var bitmap = new WriteableBitmap(w, h);
        using (var stream = bitmap.PixelBuffer.AsStream())
        {
            stream.Write(pixels, 0, pixels.Length);
        }
        bitmap.Invalidate();
        return bitmap;
    }

    const uint SIGDN_NORMALDISPLAY = 0;
    const uint SIGDN_PARENTRELATIVEPARSING = 0x80018001;
    const uint KF_FLAG_DONT_VERIFY = 0x00004000;
    const int SIIGBF_BIGGERSIZEOK = 0x1;
    const int SIIGBF_ICONONLY = 0x4;

    [ComImport, Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellItem
    {
        [PreserveSig] int BindToHandler(IntPtr pbc, [MarshalAs(UnmanagedType.LPStruct)] Guid bhid,
                                        [MarshalAs(UnmanagedType.LPStruct)] Guid riid, [MarshalAs(UnmanagedType.Interface)] out object ppv);
        [PreserveSig] int GetParent(out IShellItem ppsi);
        [PreserveSig] int GetDisplayName(uint sigdnName, out IntPtr ppszName);
        [PreserveSig] int GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
        [PreserveSig] int Compare(IShellItem psi, uint hint, out int piOrder);
    }

    [ComImport, Guid("70629033-e363-4a28-a567-0db78006e6d7"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IEnumShellItems
    {
        [PreserveSig] int Next(uint celt, out IShellItem rgelt, out uint pceltFetched);
        [PreserveSig] int Skip(uint celt);
        [PreserveSig] int Reset();
        [PreserveSig] int Clone(out IEnumShellItems ppenum);
    }

    [ComImport, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellItemImageFactory
    {
        [PreserveSig] int GetImage(SIZE size, int flags, out IntPtr phbm);
    }

    [StructLayout(LayoutKind.Sequential)]
    struct SIZE { public int cx, cy; }

    [StructLayout(LayoutKind.Sequential)]
    struct BITMAP
    {
        public int bmType, bmWidth, bmHeight, bmWidthBytes;
        public ushort bmPlanes, bmBitsPixel;
        public IntPtr bmBits;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct BITMAPINFOHEADER
    {
        public int biSize, biWidth, biHeight;
        public ushort biPlanes, biBitCount;
        public uint biCompression, biSizeImage;
        public int biXPelsPerMeter, biYPelsPerMeter;
        public uint biClrUsed, biClrImportant;
    }

    [DllImport("shell32.dll")]
    static extern int SHGetKnownFolderItem([MarshalAs(UnmanagedType.LPStruct)] Guid rfid, uint flags, IntPtr token,
                                           [MarshalAs(UnmanagedType.LPStruct)] Guid riid, [MarshalAs(UnmanagedType.Interface)] out object ppv);

    [DllImport("shell32.dll")]
    static extern int SHGetKnownFolderPath([MarshalAs(UnmanagedType.LPStruct)] Guid rfid, uint flags, IntPtr token, out IntPtr path);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    static extern int SHCreateItemFromParsingName(string path, IntPtr pbc, [MarshalAs(UnmanagedType.LPStruct)] Guid riid,
                                                  [MarshalAs(UnmanagedType.Interface)] out object ppv);

    [DllImport("gdi32.dll")]
    static extern int GetObject(IntPtr h, int size, out BITMAP bm);

    [DllImport("gdi32.dll")]
    static extern int GetDIBits(IntPtr hdc, IntPtr hbm, uint start, uint lines, byte[] bits, ref BITMAPINFOHEADER info, uint usage);

    [DllImport("gdi32.dll")]
    static extern bool DeleteObject(IntPtr h);

    [DllImport("user32.dll")]
    static extern IntPtr GetDC(IntPtr hwnd);

    [DllImport("user32.dll")]
    static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
}
