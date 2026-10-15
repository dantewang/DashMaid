using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.UI;

namespace DashMaid.Settings;

// Small builders for the layout of the prototype: a page title, groups, and
// rows ("cards") with a label at the left and a control at the right.
public static class Ui
{
    public static readonly FontFamily Mono = new("Consolas");

    public static Brush Res(string key) => (Brush)Application.Current.Resources[key];

    public static ScrollViewer Page(string? title, params UIElement[] content)
    {
        var stack = new StackPanel { Padding = new Thickness(28, 4, 28, 32), MaxWidth = 1000, HorizontalAlignment = HorizontalAlignment.Stretch };
        if (title != null)
        {
            stack.Children.Add(new TextBlock
            {
                Text = title,
                FontSize = 26,
                FontWeight = FontWeights.SemiBold,
                Margin = new Thickness(0, 6, 0, 16),
            });
        }
        foreach (var c in content)
        {
            stack.Children.Add(c);
        }
        return new ScrollViewer { Content = stack };
    }

    public static StackPanel Group(string? header, params UIElement[] rows)
    {
        var g = new StackPanel { Spacing = 3, Margin = new Thickness(0, 0, 0, 20) };
        if (header != null)
        {
            g.Children.Add(new TextBlock { Text = header, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 8, 0, 6) });
        }
        foreach (var r in rows)
        {
            g.Children.Add(r);
        }
        return g;
    }

    public static Border Card(string title, UIElement? control, string? subtitle = null)
    {
        var grid = new Grid { ColumnSpacing = 16 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        text.Children.Add(new TextBlock { Text = title, TextWrapping = TextWrapping.Wrap });
        if (!string.IsNullOrEmpty(subtitle))
        {
            text.Children.Add(new TextBlock
            {
                Text = subtitle,
                FontSize = 12.5,
                Foreground = Res("TextFillColorSecondaryBrush"),
                TextWrapping = TextWrapping.Wrap,
            });
        }
        grid.Children.Add(text);
        if (control is FrameworkElement fe)
        {
            fe.VerticalAlignment = VerticalAlignment.Center;
            Grid.SetColumn(fe, 1);
            grid.Children.Add(fe);
        }
        return Panel(grid, new Thickness(16, 12, 16, 12), 58);
    }

    public static Border Panel(UIElement child, Thickness padding, double minHeight = 0) => new()
    {
        Child = child,
        Padding = padding,
        MinHeight = minHeight,
        CornerRadius = new CornerRadius(7),
        BorderThickness = new Thickness(1),
        Background = Res("CardBackgroundFillColorDefaultBrush"),
        BorderBrush = Res("CardStrokeColorDefaultBrush"),
    };

    public static ComboBox Combo((string value, string label)[] options, string current, Action<string> changed)
    {
        var box = new ComboBox { MinWidth = 160 };
        foreach (var (_, label) in options)
        {
            box.Items.Add(label);
        }
        box.SelectedIndex = Math.Max(0, Array.FindIndex(options, o => o.value == current));
        box.SelectionChanged += (_, _) =>
        {
            if (box.SelectedIndex >= 0)
            {
                changed(options[box.SelectedIndex].value);
            }
        };
        return box;
    }

    public static ToggleSwitch Toggle(bool on, Action<bool> changed, string label)
    {
        var t = new ToggleSwitch { IsOn = on, OnContent = null, OffContent = null, MinWidth = 0 };
        AutomationName(t, label);
        t.Toggled += (_, _) => changed(t.IsOn);
        return t;
    }

    // A labeled field: the label above the control.
    public static StackPanel Field(string label, UIElement control)
    {
        var s = new StackPanel { Spacing = 5 };
        s.Children.Add(new TextBlock { Text = label, FontSize = 12.5, Foreground = Res("TextFillColorSecondaryBrush") });
        s.Children.Add(control);
        return s;
    }

    public static TextBox Text(string value, Action<string> changed, bool mono = false, string? label = null)
    {
        var box = new TextBox { Text = value, HorizontalAlignment = HorizontalAlignment.Stretch };
        if (mono)
        {
            box.FontFamily = Mono;
            box.FontSize = 12.5;
        }
        if (label != null)
        {
            AutomationName(box, label);
        }
        var last = value;
        box.TextChanged += (_, _) =>
        {
            if (box.Text != last)
            {
                last = box.Text;
                changed(box.Text);
            }
        };
        return box;
    }

    public static Button Button(string text, Action click)
    {
        var b = new Button { Content = text };
        b.Click += (_, _) => click();
        return b;
    }

    public static void AutomationName(DependencyObject o, string name) =>
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(o, name);

    // The logo of the prototype (40 x 40 units).
    public static Viewbox Logo(double size)
    {
        var c = new Canvas { Width = 40, Height = 40 };
        void Rect(double x, double y, double w, double h, double r, Color color)
        {
            var rect = new Rectangle { Width = w, Height = h, RadiusX = r, RadiusY = r, Fill = new SolidColorBrush(color) };
            Canvas.SetLeft(rect, x);
            Canvas.SetTop(rect, y);
            c.Children.Add(rect);
        }
        Rect(0, 0, 40, 40, 11, Color.FromArgb(255, 0x1b, 0x1b, 0x1b));
        var dot = new Ellipse { Width = 7.2, Height = 7.2, Fill = new SolidColorBrush(Color.FromArgb(255, 0xd7, 0xdd, 0xe2)) };
        Canvas.SetLeft(dot, 9.5 - 3.6);
        Canvas.SetTop(dot, 20 - 3.6);
        c.Children.Add(dot);
        Rect(15, 15, 2.4, 10, 1.2, Color.FromArgb(255, 0xa3, 0xa3, 0xa3));
        Rect(19.6, 12.5, 9, 15, 3, Color.FromArgb(255, 0xff, 0xff, 0xff));
        Rect(30.8, 15, 2.4, 10, 1.2, Color.FromArgb(255, 0xa3, 0xa3, 0xa3));
        Rect(35, 16.5, 2, 7, 1, Color.FromArgb(255, 0x5a, 0x5a, 0x5a));
        return new Viewbox { Width = size, Height = size, Child = c };
    }
}
