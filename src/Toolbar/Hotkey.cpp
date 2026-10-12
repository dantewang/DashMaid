#include <windows.h>
#include <cwctype>
#include <string>
#include "Hotkey.h"

namespace dm
{
    namespace
    {
        bool Equal(std::wstring_view a, std::wstring_view b)
        {
            return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
        }

        unsigned KeyCode(std::wstring_view k)
        {
            if (k.size() == 1)
            {
                const wchar_t c = static_cast<wchar_t>(std::towupper(k[0]));
                if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
                {
                    return c;
                }
                switch (c)
                {
                case L'`': return VK_OEM_3;
                case L'-': return VK_OEM_MINUS;
                case L'=': return VK_OEM_PLUS;
                case L'[': return VK_OEM_4;
                case L']': return VK_OEM_6;
                case L';': return VK_OEM_1;
                case L'\'': return VK_OEM_7;
                case L',': return VK_OEM_COMMA;
                case L'.': return VK_OEM_PERIOD;
                case L'/': return VK_OEM_2;
                case L'\\': return VK_OEM_5;
                default: return 0;
                }
            }
            if (Equal(k, L"Space")) return VK_SPACE;
            if (Equal(k, L"Enter")) return VK_RETURN;
            if (Equal(k, L"Tab")) return VK_TAB;
            if ((k[0] == L'F' || k[0] == L'f') && k.size() <= 3)
            {
                const int n = _wtoi(std::wstring(k.substr(1)).c_str());
                if (n >= 1 && n <= 24)
                {
                    return VK_F1 + n - 1;
                }
            }
            return 0;
        }
    }

    Hotkey ParseHotkey(std::wstring_view text)
    {
        Hotkey hk;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find(L'+', start);
            // A "+" at the end is the key itself, for example "Ctrl++".
            if (end == start && end + 1 == text.size())
            {
                end = std::wstring_view::npos;
            }
            const std::wstring_view part = text.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
            if (Equal(part, L"Ctrl")) hk.modifiers |= MOD_CONTROL;
            else if (Equal(part, L"Alt")) hk.modifiers |= MOD_ALT;
            else if (Equal(part, L"Shift")) hk.modifiers |= MOD_SHIFT;
            else if (Equal(part, L"Win")) hk.modifiers |= MOD_WIN;
            else if (!part.empty()) hk.vk = KeyCode(part);
            if (end == std::wstring_view::npos)
            {
                break;
            }
            start = end + 1;
        }
        return hk;
    }
}
