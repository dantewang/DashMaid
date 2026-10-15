using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml.Media.Imaging;

namespace DashMaid.Settings;

// An entry of the Start menu "All apps" list.
public sealed record AppEntry(string Name, string Path, string Display);

// Shell functions: the "All apps" list and icons. The rules are the same as in the toolbar process.
public static partial class Shell
{
    public static bool IsStoreApp(string path) =>
        path.StartsWith(@"shell:AppsFolder\", StringComparison.OrdinalIgnoreCase);

    public static List<AppEntry> AllApps()
    {
        var list = new List<AppEntry>();
        var appsFolder = new Guid("1e87508d-89c2-42f0-8a7e-645a0f50ca58");
        if (SHGetKnownFolderItem(appsFolder, 0, IntPtr.Zero, typeof(IShellItem).GUID, out var folderPtr) != 0)
        {
            return list;
        }
        var folder = Wrap<IShellItem>(folderPtr);
        var enumItems = new Guid("94f60519-2850-4924-aa5a-d15e84868039");
        if (folder.BindToHandler(IntPtr.Zero, enumItems, typeof(IEnumShellItems).GUID, out var enumPtr) != 0)
        {
            Release(folder);
            return list;
        }
        var items = Wrap<IEnumShellItems>(enumPtr);
        while (items.Next(1, out var itemPtr, out var fetched) == 0 && fetched == 1)
        {
            var item = Wrap<IShellItem>(itemPtr);
            var name = DisplayName(item, SIGDN_NORMALDISPLAY);
            var parsing = DisplayName(item, SIGDN_PARENTRELATIVEPARSING);
            Release(item);
            if (string.IsNullOrEmpty(name) || string.IsNullOrEmpty(parsing))
            {
                continue;
            }
            var file = ResolveKnownFolder(parsing);
            list.Add(new AppEntry(name, file ?? @"shell:AppsFolder\" + parsing, file ?? parsing));
        }
        Release(items);
        Release(folder);
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
            SHCreateItemFromParsingName(expanded, IntPtr.Zero, typeof(IShellItemImageFactory).GUID, out var ptr) == 0)
        {
            var factory = Wrap<IShellItemImageFactory>(ptr);
            if (factory.GetImage(new SIZE { cx = size, cy = size }, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, out var hbitmap) == 0)
            {
                bitmap = ToBitmap(hbitmap);
                DeleteObject(hbitmap);
            }
            Release(factory);
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

    // COM interfaces with source-generated marshalling, so that Native AOT
    // can compile them. Interface pointers are IntPtr, and Wrap() makes the
    // managed object.
    static readonly StrategyBasedComWrappers comWrappers = new();

    static T Wrap<T>(IntPtr unknown)
    {
        var o = (T)comWrappers.GetOrCreateObjectForComInstance(unknown, CreateObjectFlags.UniqueInstance);
        Marshal.Release(unknown);
        return o;
    }

    static void Release(object o) => ((ComObject)o).FinalRelease();

    [GeneratedComInterface, Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")]
    internal partial interface IShellItem
    {
        [PreserveSig] int BindToHandler(IntPtr pbc, in Guid bhid, in Guid riid, out IntPtr ppv);
        [PreserveSig] int GetParent(out IntPtr ppsi);
        [PreserveSig] int GetDisplayName(uint sigdnName, out IntPtr ppszName);
        [PreserveSig] int GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
        [PreserveSig] int Compare(IntPtr psi, uint hint, out int piOrder);
    }

    [GeneratedComInterface, Guid("70629033-e363-4a28-a567-0db78006e6d7")]
    internal partial interface IEnumShellItems
    {
        [PreserveSig] int Next(uint celt, out IntPtr rgelt, out uint pceltFetched);
        [PreserveSig] int Skip(uint celt);
        [PreserveSig] int Reset();
        [PreserveSig] int Clone(out IntPtr ppenum);
    }

    [GeneratedComInterface, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b")]
    internal partial interface IShellItemImageFactory
    {
        [PreserveSig] int GetImage(SIZE size, int flags, out IntPtr phbm);
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SIZE { public int cx, cy; }

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

    [LibraryImport("shell32.dll")]
    private static partial int SHGetKnownFolderItem(in Guid rfid, uint flags, IntPtr token, in Guid riid, out IntPtr ppv);

    [LibraryImport("shell32.dll")]
    private static partial int SHGetKnownFolderPath(in Guid rfid, uint flags, IntPtr token, out IntPtr path);

    [LibraryImport("shell32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int SHCreateItemFromParsingName(string path, IntPtr pbc, in Guid riid, out IntPtr ppv);

    [LibraryImport("gdi32.dll", EntryPoint = "GetObjectW")]
    private static partial int GetObject(IntPtr h, int size, out BITMAP bm);

    [LibraryImport("gdi32.dll")]
    private static partial int GetDIBits(IntPtr hdc, IntPtr hbm, uint start, uint lines, [Out] byte[] bits, ref BITMAPINFOHEADER info, uint usage);

    [LibraryImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool DeleteObject(IntPtr h);

    [LibraryImport("user32.dll")]
    private static partial IntPtr GetDC(IntPtr hwnd);

    [LibraryImport("user32.dll")]
    private static partial int ReleaseDC(IntPtr hwnd, IntPtr dc);
}
