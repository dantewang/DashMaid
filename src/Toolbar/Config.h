#pragma once

#include "Spring.h"

namespace dm
{
    enum class PositionMode { Float, Dock, Center, Cursor };
    enum class Theme { Dark, Light };
    enum class Material { Acrylic, Solid };
    enum class CollapsedStyle { Slice, Glow };

    struct AppItem
    {
        std::wstring name;
        std::wstring path;
        std::wstring args;
        std::wstring workingDir;
        std::optional<CollapsedStyle> style;  // Empty: use the global style.
        bool admin = false;
    };

    struct Config
    {
        static constexpr int Unset = INT_MIN;

        PositionMode mode = PositionMode::Float;
        int x = Unset;  // Screen X of the window center, in physical pixels.
        int y = Unset;  // Screen Y of the window top, in physical pixels.
        Theme theme = Theme::Dark;
        Material material = Material::Acrylic;
        CollapsedStyle collapsedStyle = CollapsedStyle::Slice;
        SpringParams spring;
        std::vector<AppItem> items;

        static std::filesystem::path FilePath();

        // Reads the file. If the file does not exist, writes the defaults.
        // Returns the file text in 'text' (UTF-8).
        static Config Load(std::string& text);
        static Config Defaults();

        // Writes the file and returns the text that it wrote (UTF-8).
        std::string Save() const;
    };
}
