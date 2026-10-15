using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;

namespace DashMaid.Settings;

public partial class App : Application
{
    public const string WindowTitle = "DashMaid 设置";

    MainWindow? window;
    Mutex? single;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        // One settings window at a time. A second start shows the first window.
        single = new Mutex(true, "DashMaid.Settings", out var created);
        if (!created)
        {
            var existing = FindWindow(null, WindowTitle);
            if (existing != IntPtr.Zero)
            {
                ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
                SetForegroundWindow(existing);
            }
            Exit();
            return;
        }
        window = new MainWindow();
        window.Closed += (_, _) => single?.ReleaseMutex();
        window.Activate();
    }

    const int SW_SHOW = 5;
    const int SW_RESTORE = 9;

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr FindWindow(string? cls, string title);

    [DllImport("user32.dll")]
    static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    static extern bool ShowWindow(IntPtr hwnd, int cmd);

    [DllImport("user32.dll")]
    static extern bool IsIconic(IntPtr hwnd);
}
