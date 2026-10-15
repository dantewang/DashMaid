#pragma once

#include "Spring.h"

namespace dm
{
    enum class PositionMode { Float, Dock, Center, Cursor };
    enum class DockStyle { Float, Flush };  // Screen top: a gap above the toolbar, or no gap and square top corners.
    enum class Theme { Dark, Light };
    enum class Material { Acrylic, Solid };
    enum class CollapsedStyle { Slice, Glow };
    enum class WidgetSource { Clock, Cpu, Memory, Network, Command };

    // An app shortcut or a widget. App fields and widget fields share one type
    // because the toolbar keeps all items in one ordered list.
    struct Item
    {
        std::wstring name;
        std::wstring path;
        std::wstring args;
        std::wstring workingDir;
        std::optional<CollapsedStyle> style;  // Empty: use the global style.
        bool admin = false;

        // Widget fields. "name" is the display name of a command widget.
        std::optional<WidgetSource> widget;  // Empty: the item is an app.
        std::wstring command;                // Command line of a command widget.
        int interval = 60;                   // Seconds between two runs of the command.
        std::wstring click;                  // Target to open on click. Empty: the default of the source.
        std::optional<Rgb> color;            // Text color of a command widget.

        bool IsWidget() const { return widget.has_value(); }
    };

    struct Config
    {
        static constexpr int Unset = INT_MIN;

        PositionMode mode = PositionMode::Float;
        int x = Unset;  // Screen X of the window center, in physical pixels.
        int y = Unset;  // Screen Y of the toolbar top, in physical pixels.
        DockStyle dockStyle = DockStyle::Float;
        bool appBar = true;  // Screen top: keep maximized windows below the toolbar.
        Theme theme = Theme::Dark;
        Material material = Material::Solid;
        CollapsedStyle collapsedStyle = CollapsedStyle::Slice;
        bool widgetsCollapse = false;  // False: widgets always show their text.
        SpringParams spring;
        std::wstring runHotkey = L"Ctrl+Alt+R";  // Empty: no hotkey.
        std::wstring toggleHotkey = L"Alt+`";     // Shows and hides the toolbar in the center and cursor modes.
        std::vector<Item> items;

        static std::filesystem::path FilePath();

        // Reads the file. If the file does not exist, writes the defaults.
        // Returns the file text in "text" (UTF-8). Returns nothing if the file
        // exists but cannot be read or is not valid JSON.
        static std::optional<Config> Load(std::string& text);
        static Config Defaults();

        // Writes the file and returns the text that it wrote (UTF-8).
        std::string Save() const;
    };
}
