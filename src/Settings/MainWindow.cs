using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DashMaid.Settings;

public sealed partial class MainWindow : Window
{
    static readonly (string tag, string label, string glyph)[] Nav =
    [
        ("general", "常规", ""),
        ("items", "项目", ""),
        ("widgets", "小组件", ""),
        ("look", "外观", ""),
        ("keys", "快捷键", ""),
        ("about", "关于", ""),
    ];

    readonly ConfigStore store;
    readonly Pages pages;
    readonly NavigationView nav;
    string current = "items";

    public MainWindow()
    {
        Title = App.WindowTitle;
        SystemBackdrop = new MicaBackdrop();
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "DashMaid.ico"));
        ExtendsContentIntoTitleBar = true;

        store = new ConfigStore(DispatcherQueue);
        pages = new Pages(store, this);
        prebuildTimer = DispatcherQueue.CreateTimer();
        prebuildTimer.Interval = TimeSpan.FromSeconds(1);
        prebuildTimer.IsRepeating = false;
        prebuildTimer.Tick += (_, _) => Prebuild();

        var titleBar = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 10,
            Height = 40,
            Padding = new Thickness(14, 0, 0, 0),
        };
        titleBar.Children.Add(new ContentControl { Content = Ui.Logo(16), VerticalAlignment = VerticalAlignment.Center });
        titleBar.Children.Add(new TextBlock
        {
            Text = App.WindowTitle,
            FontSize = 12,
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = Ui.Res("TextFillColorSecondaryBrush"),
        });

        nav = new NavigationView
        {
            PaneDisplayMode = NavigationViewPaneDisplayMode.Left,
            IsSettingsVisible = false,
            IsBackButtonVisible = NavigationViewBackButtonVisible.Collapsed,
            IsPaneToggleButtonVisible = false,
            OpenPaneLength = 216,
        };
        foreach (var (tag, label, glyph) in Nav)
        {
            nav.MenuItems.Add(new NavigationViewItem { Content = label, Tag = tag, Icon = new FontIcon { Glyph = glyph } });
        }
        nav.SelectionChanged += (_, e) =>
        {
            if (e.SelectedItem is NavigationViewItem item && item.Tag is string tag)
            {
                Show(tag);
            }
        };

        var root = new Grid();
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.Children.Add(titleBar);
        Grid.SetRow(nav, 1);
        root.Children.Add(nav);
        Content = root;
        SetTitleBar(titleBar);

        store.Reloaded += () =>
        {
            cache.Clear();
            Show(current);
            Prebuild();
        };
        // A change can show on other pages, for example the default collapsed
        // style in the item editor. Those pages are built again later.
        store.Changed += () =>
        {
            foreach (var tag in cache.Keys.Where(t => t != current).ToList())
            {
                cache.Remove(tag);
            }
            prebuildTimer.Stop();
            prebuildTimer.Start();
        };
        pages.Navigate = tag =>
        {
            cache.Remove(tag);
            Select(tag);
        };
        var args = Environment.GetCommandLineArgs();
        var page = Array.IndexOf(args, "--page") is var i && i >= 0 && i + 1 < args.Length ? args[i + 1] : "items";
        Select(Array.Exists(Nav, n => n.tag == page) ? page : "items");

        var dpi = GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this)) / 96.0;
        AppWindow.Resize(new Windows.Graphics.SizeInt32((int)(1080 * dpi), (int)(740 * dpi)));
        Activated += (_, _) => Prebuild();
        Closed += (_, _) => store.Dispose();
    }

    void Select(string tag)
    {
        foreach (var o in nav.MenuItems)
        {
            if (o is NavigationViewItem item && (string)item.Tag == tag)
            {
                if (ReferenceEquals(nav.SelectedItem, item))
                {
                    Show(tag);
                }
                nav.SelectedItem = item;
                return;
            }
        }
    }

    // Built pages, so that a page shows at once when the user selects it again.
    readonly Dictionary<string, UIElement> cache = [];
    bool prebuilding;
    readonly Microsoft.UI.Dispatching.DispatcherQueueTimer prebuildTimer;

    void Show(string tag)
    {
        current = tag;
        if (!cache.TryGetValue(tag, out var page))
        {
            page = pages.Build(tag);
            cache[tag] = page;
        }
        nav.Content = page;
    }

    // Builds the other pages when the window is idle, one page at a time.
    void Prebuild()
    {
        if (prebuilding)
        {
            return;
        }
        var tag = Array.Find(Nav, n => n.tag != current && !cache.ContainsKey(n.tag)).tag;
        if (tag == null)
        {
            return;
        }
        prebuilding = true;
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
        {
            prebuilding = false;
            if (tag != current && !cache.ContainsKey(tag))
            {
                cache[tag] = pages.Build(tag);
            }
            Prebuild();
        });
    }

    [DllImport("user32.dll")]
    static extern uint GetDpiForWindow(IntPtr hwnd);
}
