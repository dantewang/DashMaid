using System.Text.Json.Nodes;
using Microsoft.UI.Input;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;
using Windows.System;
using Windows.UI.Core;

namespace DashMaid.Settings;

// The pages of the settings window. Each page is built again from the
// configuration when it shows, and when another program changes the file.
public sealed class Pages
{
    readonly ConfigStore store;
    readonly Window window;
    int selected;  // Index of the selected item on the 项目 page.

    public Action<string>? Navigate;

    public Pages(ConfigStore store, Window window)
    {
        this.store = store;
        this.window = window;
    }

    public UIElement Build(string tag)
    {
        if (!store.Valid)
        {
            return Ui.Page(null, new InfoBar { IsOpen = true, IsClosable = false, Severity = InfoBarSeverity.Error, Title = "无法读取配置文件" });
        }
        return tag switch
        {
            "general" => General(),
            "items" => Items(),
            "widgets" => Widgets(),
            "look" => Look(),
            "keys" => Keys(),
            _ => About(),
        };
    }

    void Rebuild(string tag) => Navigate?.Invoke(tag);

    // ------------------------------------------------------------ 常规

    UIElement General()
    {
        var pos = store.Root["position"];
        var mode = ConfigStore.Str(pos, "mode", "float");
        var rows = new List<UIElement>
        {
            Ui.Card("位置", Ui.Combo(
                [("float", "浮动"), ("dock", "屏幕顶部"), ("center", "呼出到屏幕中央"), ("cursor", "呼出到鼠标旁")], mode,
                v => { store.Change(r => store.Object("position")["mode"] = v); Rebuild("general"); })),
        };
        if (mode == "dock")
        {
            rows.Add(Ui.Card("顶部样式", Ui.Combo([("float", "悬浮"), ("flush", "贴边")], ConfigStore.Str(pos, "dockStyle", "float"),
                v => store.Change(r => store.Object("position")["dockStyle"] = v))));
            rows.Add(Ui.Card("保留屏幕空间", Ui.Toggle(ConfigStore.Bool(pos, "appBar", true),
                v => store.Change(r => store.Object("position")["appBar"] = v), "保留屏幕空间")));
        }
        return Ui.Page("常规", Ui.Group(null, [.. rows]));
    }

    // ------------------------------------------------------------ 项目

    static readonly (string value, string label)[] Sources =
        [("clock", "时钟"), ("cpu", "CPU"), ("memory", "内存"), ("network", "网络"), ("command", "命令")];

    static string SourceName(string source) =>
        Array.Find(Sources, s => s.value == source).label ?? source;

    static bool IsWidget(JsonNode? item) => ConfigStore.Str(item, "type", "app") == "widget";

    static string ItemName(JsonNode? item)
    {
        if (!IsWidget(item))
        {
            return ConfigStore.Str(item, "name");
        }
        var source = ConfigStore.Str(item, "source", "clock");
        var name = ConfigStore.Str(item, "name");
        return source == "command" && name.Length > 0 ? name : SourceName(source);
    }

    static UIElement ItemIcon(JsonNode? item, double size)
    {
        if (!IsWidget(item))
        {
            var bitmap = Shell.Icon(ConfigStore.Str(item, "path"), size > 32 ? 64 : 32);
            if (bitmap != null)
            {
                return new Image { Source = bitmap, Width = size, Height = size };
            }
        }
        return new FontIcon { Glyph = IsWidget(item) ? "" : "", FontSize = size * 0.75, Width = size, Height = size };
    }

    UIElement Items()
    {
        var items = store.Items;
        selected = items.Count == 0 ? -1 : Math.Clamp(selected, 0, items.Count - 1);

        var list = new ListView { SelectionMode = ListViewSelectionMode.Single, MaxHeight = 520 };
        for (int i = 0; i < items.Count; i++)
        {
            var item = items[i];
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Padding = new Thickness(0, 4, 0, 4) };
            row.Children.Add(new Border { Width = 28, Height = 28, Child = ItemIcon(item, 24) });
            var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            text.Children.Add(new TextBlock { Text = ItemName(item), TextTrimming = TextTrimming.CharacterEllipsis });
            var sub = IsWidget(item) ? "小组件" : "应用" + (ConfigStore.Bool(item, "admin", false) ? " · 管理员" : "");
            text.Children.Add(new TextBlock { Text = sub, FontSize = 12, Foreground = Ui.Res("TextFillColorSecondaryBrush") });
            row.Children.Add(text);
            list.Items.Add(new ListViewItem { Content = row });
        }
        list.SelectedIndex = selected;
        list.SelectionChanged += (_, _) =>
        {
            if (list.SelectedIndex >= 0 && list.SelectedIndex != selected)
            {
                selected = list.SelectedIndex;
                Rebuild("items");
            }
        };

        var tools = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, Padding = new Thickness(6) };
        tools.Resources["ButtonPadding"] = new Thickness(8, 3, 8, 4);
        tools.Children.Add(Ui.Button("＋ 应用", () => _ = PickApp(add: true)));
        tools.Children.Add(Ui.Button("＋ 小组件", AddWidget));
        var up = Ui.Button("↑", () => MoveItem(-1));
        Ui.AutomationName(up, "上移");
        var down = Ui.Button("↓", () => MoveItem(1));
        Ui.AutomationName(down, "下移");
        tools.Children.Add(up);
        tools.Children.Add(down);
        tools.Children.Add(Ui.Button("删除", DeleteItem));

        var left = new StackPanel();
        left.Children.Add(tools);
        left.Children.Add(new Border { Height = 1, Background = Ui.Res("CardStrokeColorDefaultBrush") });
        left.Children.Add(list);

        var editor = new StackPanel { Spacing = 14 };
        if (selected < 0)
        {
            editor.Children.Add(new TextBlock { Text = "没有项目", Foreground = Ui.Res("TextFillColorSecondaryBrush") });
        }
        else if (IsWidget(items[selected]))
        {
            WidgetEditor(editor, items[selected]!.AsObject());
        }
        else
        {
            AppEditor(editor, items[selected]!.AsObject());
        }

        var grid = new Grid { ColumnSpacing = 16 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(300) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var leftPanel = Ui.Panel(left, new Thickness(0));
        leftPanel.VerticalAlignment = VerticalAlignment.Top;
        grid.Children.Add(leftPanel);
        var right = Ui.Panel(editor, new Thickness(18));
        right.VerticalAlignment = VerticalAlignment.Top;
        Grid.SetColumn(right, 1);
        grid.Children.Add(right);
        return Ui.Page("项目", grid);
    }

    void SetField(JsonObject item, string name, JsonNode? value) => store.Change(_ => item[name] = value);

    void AppEditor(StackPanel editor, JsonObject item)
    {
        var path = ConfigStore.Str(item, "path");
        var store_ = Shell.IsStoreApp(path);

        var icon = new Border { Width = 56, Height = 56, HorizontalAlignment = HorizontalAlignment.Left, Child = ItemIcon(item, 56) };
        editor.Children.Add(icon);
        editor.Children.Add(Ui.Field("名称", Ui.Text(ConfigStore.Str(item, "name"), v => SetField(item, "name", v), label: "名称")));

        var target = Ui.Text(path, v => SetField(item, "path", v), mono: true, label: "目标");
        target.IsReadOnly = store_;
        target.LostFocus += (_, _) => icon.Child = ItemIcon(item, 56);
        var targetRow = new Grid { ColumnSpacing = 6 };
        targetRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        targetRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        targetRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        targetRow.Children.Add(target);
        var pick = Ui.Button("从开始菜单选择…", () => _ = PickApp(add: false));
        Grid.SetColumn(pick, 1);
        targetRow.Children.Add(pick);
        var browse = Ui.Button("浏览…", () => _ = Browse(item));
        Grid.SetColumn(browse, 2);
        targetRow.Children.Add(browse);
        editor.Children.Add(Ui.Field("目标", targetRow));

        var args = Ui.Text(ConfigStore.Str(item, "args"), v => SetField(item, "args", v), mono: true, label: "启动参数");
        args.IsEnabled = !store_;
        editor.Children.Add(Ui.Field("启动参数", args));
        var cwd = Ui.Text(ConfigStore.Str(item, "workingDir"), v => SetField(item, "workingDir", v), mono: true, label: "起始位置");
        cwd.IsEnabled = !store_;
        editor.Children.Add(Ui.Field("起始位置", cwd));

        var global = ConfigStore.Str(store.Root, "collapsedStyle", "slice") == "glow" ? "主题色光晕" : "图标局部";
        editor.Children.Add(Ui.Field("收起样式", Ui.Combo(
            [("default", $"默认（{global}）"), ("slice", "图标局部"), ("glow", "主题色光晕")],
            ConfigStore.Str(item, "style", "default"), v => SetField(item, "style", v))));

        var admin = Ui.Toggle(ConfigStore.Bool(item, "admin", false), v => { SetField(item, "admin", v); Rebuild("items"); }, "总是以管理员身份运行");
        admin.IsEnabled = !store_;
        editor.Children.Add(Ui.Card("总是以管理员身份运行", admin));
    }

    void WidgetEditor(StackPanel editor, JsonObject item)
    {
        var source = ConfigStore.Str(item, "source", "clock");
        editor.Children.Add(Ui.Field("数据来源", Ui.Combo(Sources, source, v =>
        {
            store.Change(_ =>
            {
                item["source"] = v;
                item["click"] = "";
                if (v == "command")
                {
                    item["name"] ??= "命令";
                    item["command"] ??= "";
                    item["interval"] ??= 60;
                }
            });
            Rebuild("items");
        })));
        if (source == "command")
        {
            editor.Children.Add(Ui.Field("名称", Ui.Text(ConfigStore.Str(item, "name"), v => SetField(item, "name", v), label: "名称")));
            editor.Children.Add(Ui.Field("命令", Ui.Text(ConfigStore.Str(item, "command"), v => SetField(item, "command", v), mono: true, label: "命令")));
            var interval = (int)ConfigStore.Num(item, "interval", 60);
            var options = new List<(string, string)> { ("1", "1 秒"), ("5", "5 秒"), ("30", "30 秒"), ("60", "60 秒") };
            if (!options.Exists(o => o.Item1 == interval.ToString()))
            {
                options.Add((interval.ToString(), $"{interval} 秒"));
            }
            editor.Children.Add(Ui.Field("刷新间隔", Ui.Combo([.. options], interval.ToString(),
                v => SetField(item, "interval", int.Parse(v)))));
        }
        editor.Children.Add(Ui.Field("单击时打开", Ui.Text(ConfigStore.Str(item, "click"), v => SetField(item, "click", v), mono: true, label: "单击时打开")));
    }

    void MoveItem(int delta)
    {
        var items = store.Items;
        int j = selected + delta;
        if (selected < 0 || j < 0 || j >= items.Count)
        {
            return;
        }
        store.Change(_ =>
        {
            var node = items[selected];
            items.RemoveAt(selected);
            items.Insert(j, node);
        });
        selected = j;
        Rebuild("items");
    }

    void DeleteItem()
    {
        var items = store.Items;
        if (selected < 0 || selected >= items.Count)
        {
            return;
        }
        store.Change(_ => items.RemoveAt(selected));
        Rebuild("items");
    }

    void InsertItem(JsonObject item)
    {
        var items = store.Items;
        int at = selected < 0 ? items.Count : selected + 1;
        store.Change(_ => items.Insert(at, item));
        selected = at;
        Rebuild("items");
    }

    void AddWidget()
    {
        var present = new HashSet<string>();
        foreach (var i in store.Items)
        {
            if (IsWidget(i))
            {
                present.Add(ConfigStore.Str(i, "source"));
            }
        }
        var source = Array.Find(["memory", "network", "cpu", "clock"], s => !present.Contains(s)) ?? "command";
        InsertItem(NewWidget(source));
    }

    static JsonObject NewWidget(string source)
    {
        var o = new JsonObject { ["type"] = "widget", ["source"] = source, ["click"] = "" };
        if (source == "command")
        {
            o["name"] = "命令";
            o["command"] = "";
            o["interval"] = 60;
        }
        return o;
    }

    static JsonObject NewApp(string name, string path) => new()
    {
        ["type"] = "app",
        ["name"] = name,
        ["path"] = path,
        ["args"] = "",
        ["workingDir"] = "",
        ["style"] = "default",
        ["admin"] = false,
    };

    // The Start menu "All apps" list. 'add': add a new item, else change the selected item.
    async Task PickApp(bool add)
    {
        var search = new TextBox { PlaceholderText = "搜索" };
        Ui.AutomationName(search, "搜索应用");
        var list = new ListView { Height = 380, SelectionMode = ListViewSelectionMode.Single, IsItemClickEnabled = true };
        var dialog = new ContentDialog
        {
            XamlRoot = window.Content.XamlRoot,
            Title = add ? "添加应用" : "选择应用",
            CloseButtonText = "取消",
            Content = new StackPanel { Spacing = 10, Width = 520, Children = { search, list } },
        };

        List<AppEntry> apps = [];
        AppEntry? chosen = null;
        void Fill()
        {
            var q = search.Text.Trim();
            list.Items.Clear();
            foreach (var a in apps)
            {
                if (q.Length > 0 && !a.Name.Contains(q, StringComparison.CurrentCultureIgnoreCase) &&
                    !a.Display.Contains(q, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Padding = new Thickness(0, 2, 0, 2) };
                var image = new Image { Width = 24, Height = 24 };
                row.Children.Add(image);
                row.Children.Add(new TextBlock { Text = a.Name, VerticalAlignment = VerticalAlignment.Center });
                var lvi = new ListViewItem { Content = row, Tag = a };
                // Icons load when the row shows, so that a long list opens fast.
                lvi.Loaded += (_, _) => image.Source ??= Shell.Icon(a.Path, 32);
                list.Items.Add(lvi);
            }
        }
        search.TextChanged += (_, _) => Fill();
        list.ItemClick += (_, e) =>
        {
            if (e.ClickedItem is StackPanel sp && sp.Parent is ListViewItem lvi && lvi.Tag is AppEntry a)
            {
                chosen = a;
                dialog.Hide();
            }
        };
        dialog.Opened += async (_, _) =>
        {
            apps = await Task.Run(Shell.AllApps);
            Fill();
            search.Focus(FocusState.Programmatic);
        };
        await dialog.ShowAsync();
        if (chosen == null)
        {
            return;
        }
        if (add || selected < 0)
        {
            InsertItem(NewApp(chosen.Name, chosen.Path));
            return;
        }
        var item = store.Items[selected]!.AsObject();
        store.Change(_ =>
        {
            item["name"] = chosen.Name;
            item["path"] = chosen.Path;
            item["args"] = "";
            item["workingDir"] = "";
        });
        Rebuild("items");
    }

    async Task Browse(JsonObject item)
    {
        var picker = new Microsoft.Windows.Storage.Pickers.FileOpenPicker(window.AppWindow.Id);
        foreach (var ext in new[] { ".exe", ".lnk", ".bat", ".cmd" })
        {
            picker.FileTypeFilter.Add(ext);
        }
        var file = await picker.PickSingleFileAsync();
        if (file == null)
        {
            return;
        }
        store.Change(_ =>
        {
            item["name"] = Path.GetFileNameWithoutExtension(file.Path);
            item["path"] = file.Path;
            item["args"] = "";
            item["workingDir"] = "";
        });
        Rebuild("items");
    }

    // ------------------------------------------------------------ 小组件

    UIElement Widgets()
    {
        var present = new HashSet<string>();
        foreach (var i in store.Items)
        {
            if (IsWidget(i))
            {
                present.Add(ConfigStore.Str(i, "source"));
            }
        }
        var display = Ui.Group(null, Ui.Card("收起方式", Ui.Combo([("text", "始终显示"), ("collapse", "可收起")],
            ConfigStore.Str(store.Root, "widgetDisplay", "text"), v => store.Change(r => r["widgetDisplay"] = v))));

        var builtIn = new List<UIElement>();
        foreach (var (value, label) in Sources[..4])
        {
            UIElement control = present.Contains(value)
                ? new TextBlock { Text = "已添加", Foreground = Ui.Res("TextFillColorSecondaryBrush") }
                : Ui.Button("添加", () =>
                {
                    store.Change(_ => store.Items.Add(NewWidget(value)));
                    Rebuild("widgets");
                });
            builtIn.Add(Ui.Card(label, control));
        }

        var commands = new List<UIElement>();
        for (int i = 0; i < store.Items.Count; i++)
        {
            var item = store.Items[i];
            if (!IsWidget(item) || ConfigStore.Str(item, "source") != "command")
            {
                continue;
            }
            int index = i;
            var sub = $"{ConfigStore.Str(item, "command")} · 每 {(int)ConfigStore.Num(item, "interval", 60)} 秒";
            commands.Add(Ui.Card(ItemName(item), Ui.Button("编辑", () => { selected = index; Navigate?.Invoke("items"); }), sub));
        }
        commands.Add(Ui.Card("命令", Ui.Button("添加", () =>
        {
            store.Change(_ => store.Items.Add(NewWidget("command")));
            selected = store.Items.Count - 1;
            Navigate?.Invoke("items");
        })));

        return Ui.Page("小组件", display, Ui.Group("内置", [.. builtIn]), Ui.Group("命令", [.. commands]));
    }

    // ------------------------------------------------------------ 外观

    const int DefaultStiffness = 420;
    const int DefaultDamping = 30;

    UIElement Look()
    {
        var spring = store.Root["spring"];
        var colors = Ui.Group(null,
            Ui.Card("主题", Ui.Combo([("dark", "深色"), ("light", "浅色")], ConfigStore.Str(store.Root, "theme", "dark"),
                v => store.Change(r => r["theme"] = v))),
            Ui.Card("背景材质", Ui.Combo([("solid", "纯色"), ("acrylic", "亚克力")], ConfigStore.Str(store.Root, "material", "solid"),
                v => store.Change(r => r["material"] = v))));
        var style = Ui.Group("应用收起样式",
            Ui.Card("收起样式", Ui.Combo([("slice", "图标局部"), ("glow", "主题色光晕")],
                ConfigStore.Str(store.Root, "collapsedStyle", "slice"), v => store.Change(r => r["collapsedStyle"] = v))));

        UIElement SpringRow(string label, string key, int min, int max, int step, int fallback)
        {
            var value = (int)Math.Round(ConfigStore.Num(spring, key, fallback));
            var text = new TextBlock { Text = value.ToString(), MinWidth = 32, VerticalAlignment = VerticalAlignment.Center };
            var slider = new Slider { Minimum = min, Maximum = max, StepFrequency = step, Value = value, Width = 220 };
            Ui.AutomationName(slider, label);
            slider.ValueChanged += (_, e) =>
            {
                text.Text = ((int)e.NewValue).ToString();
                store.Change(_ => store.Object("spring")[key] = (int)e.NewValue);
            };
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12, Children = { text, slider } };
            return Ui.Card(label, row);
        }
        var motion = Ui.Group("动画",
            SpringRow("弹性", "stiffness", 150, 900, 10, DefaultStiffness),
            SpringRow("阻尼", "damping", 10, 60, 1, DefaultDamping),
            Ui.Card("", Ui.Button("恢复默认", () =>
            {
                store.Change(_ =>
                {
                    var s = store.Object("spring");
                    s["stiffness"] = DefaultStiffness;
                    s["damping"] = DefaultDamping;
                });
                Rebuild("look");
            })));
        return Ui.Page("外观", colors, style, motion);
    }

    // ------------------------------------------------------------ 快捷键

    UIElement Keys()
    {
        var hotkeys = store.Root["hotkeys"];
        return Ui.Page("快捷键", Ui.Group(null,
            HotkeyRow("显示 / 隐藏", "toggle", ConfigStore.Str(hotkeys, "toggle", "Alt+`")),
            HotkeyRow("运行", "run", ConfigStore.Str(hotkeys, "run", "Ctrl+Alt+R"))));
    }

    UIElement HotkeyRow(string label, string key, string value)
    {
        var keys = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 3, VerticalAlignment = VerticalAlignment.Center };
        void ShowKeys(string text)
        {
            keys.Children.Clear();
            if (text.Length == 0)
            {
                return;
            }
            foreach (var part in SplitHotkey(text))
            {
                keys.Children.Add(new Border
                {
                    Padding = new Thickness(8, 2, 8, 2),
                    CornerRadius = new CornerRadius(5),
                    BorderThickness = new Thickness(1, 1, 1, 2),
                    BorderBrush = Ui.Res("CardStrokeColorDefaultBrush"),
                    Background = Ui.Res("ControlFillColorDefaultBrush"),
                    Child = new TextBlock { Text = part, FontFamily = Ui.Mono, FontSize = 12 },
                });
            }
        }
        ShowKeys(value);

        var button = new Button { Content = "更改" };
        bool recording = false;
        button.Click += (_, _) =>
        {
            recording = true;
            keys.Children.Clear();
            keys.Children.Add(new TextBlock { Text = "请按下组合键", Foreground = Ui.Res("AccentTextFillColorPrimaryBrush") });
        };
        void Stop(string text)
        {
            recording = false;
            ShowKeys(text);
        }
        button.LostFocus += (_, _) =>
        {
            if (recording)
            {
                Stop(ConfigStore.Str(store.Root["hotkeys"], key, value));
            }
        };
        button.PreviewKeyDown += (_, e) =>
        {
            if (!recording)
            {
                return;
            }
            e.Handled = true;
            if (e.Key == VirtualKey.Escape)
            {
                Stop(ConfigStore.Str(store.Root["hotkeys"], key, value));
                return;
            }
            var name = KeyName(e.Key);
            var mods = Modifiers();
            if (name == null || mods.Count == 0)
            {
                return;  // Wait for a key with at least one modifier.
            }
            var text = string.Join("+", mods) + "+" + name;
            store.Change(_ => store.Object("hotkeys")[key] = text);
            Stop(text);
        };
        // Alt opens nothing while the window records a key.
        button.PreviewKeyUp += (_, e) => e.Handled = recording;

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Children = { keys, button } };
        return Ui.Card(label, row);
    }

    static IEnumerable<string> SplitHotkey(string text)
    {
        // "Ctrl++" ends with the "+" key.
        if (text.EndsWith("++"))
        {
            foreach (var p in text[..^2].Split('+', StringSplitOptions.RemoveEmptyEntries))
            {
                yield return p;
            }
            yield return "+";
            yield break;
        }
        foreach (var p in text.Split('+', StringSplitOptions.RemoveEmptyEntries))
        {
            yield return p;
        }
    }

    static List<string> Modifiers()
    {
        static bool Down(VirtualKey k) =>
            InputKeyboardSource.GetKeyStateForCurrentThread(k).HasFlag(CoreVirtualKeyStates.Down);
        var list = new List<string>();
        if (Down(VirtualKey.Control)) list.Add("Ctrl");
        if (Down(VirtualKey.Menu)) list.Add("Alt");
        if (Down(VirtualKey.Shift)) list.Add("Shift");
        if (Down(VirtualKey.LeftWindows) || Down(VirtualKey.RightWindows)) list.Add("Win");
        return list;
    }

    // The key names that the toolbar process reads (Hotkey.cpp).
    static string? KeyName(VirtualKey key)
    {
        int k = (int)key;
        if (k >= 'A' && k <= 'Z' || k >= '0' && k <= '9')
        {
            return ((char)k).ToString();
        }
        if (key >= VirtualKey.F1 && key <= VirtualKey.F24)
        {
            return "F" + (key - VirtualKey.F1 + 1);
        }
        return key switch
        {
            VirtualKey.Space => "Space",
            VirtualKey.Enter => "Enter",
            VirtualKey.Tab => "Tab",
            (VirtualKey)0xC0 => "`",
            (VirtualKey)0xBD => "-",
            (VirtualKey)0xBB => "=",
            (VirtualKey)0xDB => "[",
            (VirtualKey)0xDD => "]",
            (VirtualKey)0xBA => ";",
            (VirtualKey)0xDE => "'",
            (VirtualKey)0xBC => ",",
            (VirtualKey)0xBE => ".",
            (VirtualKey)0xBF => "/",
            (VirtualKey)0xDC => "\\",
            _ => null,
        };
    }

    // ------------------------------------------------------------ 关于

    static UIElement About()
    {
        var head = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 16, Margin = new Thickness(0, 10, 0, 20) };
        head.Children.Add(Ui.Logo(56));
        var name = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        name.Children.Add(new TextBlock { Text = "DashMaid", FontSize = 26, FontWeight = FontWeights.Bold });
        var version = typeof(Pages).Assembly.GetName().Version;
        name.Children.Add(new TextBlock
        {
            Text = $"版本 {version?.Major}.{version?.Minor}.{version?.Build}",
            Foreground = Ui.Res("TextFillColorSecondaryBrush"),
        });
        head.Children.Add(name);

        var grid = new Grid { ColumnSpacing = 18, RowSpacing = 8 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        void Add(int row, string label, Inline value)
        {
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            var l = new TextBlock { Text = label, Foreground = Ui.Res("TextFillColorSecondaryBrush") };
            Grid.SetRow(l, row);
            grid.Children.Add(l);
            var v = new TextBlock();
            v.Inlines.Add(value);
            Grid.SetRow(v, row);
            Grid.SetColumn(v, 1);
            grid.Children.Add(v);
        }
        Hyperlink Link(string text, string uri)
        {
            var h = new Hyperlink { NavigateUri = new Uri(uri) };
            h.Inlines.Add(new Run { Text = text });
            return h;
        }
        Add(0, "设计来源", Link("Segmented Navigation", "https://www.framer.com/marketplace/components/segmented-navigation/"));
        Add(1, "作者", new Run { Text = "Till Janek" });
        Add(2, "X", Link("@tilljanek", "https://x.com/tilljanek"));

        return Ui.Page(null, head, Ui.Panel(grid, new Thickness(16, 14, 16, 14)));
    }
}
