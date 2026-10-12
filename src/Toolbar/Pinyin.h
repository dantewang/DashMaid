#pragma once

#include <string>
#include <string_view>

namespace dm
{
    // Lowercase pinyin initial of a common Chinese character (GB2312 level 1).
    // Returns 0 for other characters.
    wchar_t PinyinInitial(wchar_t c);

    // Lowercase initials of a name: the pinyin initial of each Chinese
    // character and the first letter of each Latin word.
    // Example: "记事本" -> "jsb", "Windows PowerShell" -> "wps".
    std::wstring Initials(std::wstring_view name);
}
