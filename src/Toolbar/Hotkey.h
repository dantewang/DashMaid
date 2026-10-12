#pragma once

#include <string_view>

namespace dm
{
    struct Hotkey
    {
        unsigned modifiers = 0;  // MOD_* flags for RegisterHotKey.
        unsigned vk = 0;
        bool Valid() const { return vk != 0 && modifiers != 0; }
    };

    // Parses text like "Ctrl+Alt+Space" or "Alt+`".
    Hotkey ParseHotkey(std::wstring_view text);
}
