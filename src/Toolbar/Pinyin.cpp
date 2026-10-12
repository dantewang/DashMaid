#include <windows.h>
#include <cwctype>
#include "Pinyin.h"

namespace dm
{
    wchar_t PinyinInitial(wchar_t c)
    {
        if (c < 0x4E00 || c > 0x9FFF)
        {
            return 0;
        }
        char buf[4]{};
        if (WideCharToMultiByte(936, 0, &c, 1, buf, sizeof(buf), nullptr, nullptr) != 2)
        {
            return 0;
        }
        const int code = (static_cast<unsigned char>(buf[0]) << 8) | static_cast<unsigned char>(buf[1]);
        if (code < 0xB0A1 || code > 0xD7F9)
        {
            return 0;
        }
        // GB2312 level 1 is sorted by pinyin. Each value is the first code of a letter.
        static constexpr int Bounds[] = {
            0xB0A1, 0xB0C5, 0xB2C1, 0xB4EE, 0xB6EA, 0xB7A2, 0xB8C1, 0xB9FE, 0xBBF7, 0xBFA6, 0xC0AC, 0xC2E8,
            0xC4C3, 0xC5B6, 0xC5BE, 0xC6DA, 0xC8BB, 0xC8F6, 0xCBFA, 0xCDDA, 0xCEF4, 0xD1B9, 0xD4D1,
        };
        static constexpr wchar_t Letters[] = L"abcdefghjklmnopqrstwxyz";
        for (int i = static_cast<int>(std::size(Bounds)) - 1; i >= 0; --i)
        {
            if (code >= Bounds[i])
            {
                return Letters[i];
            }
        }
        return 0;
    }

    std::wstring Initials(std::wstring_view name)
    {
        std::wstring out;
        bool wordStart = true;
        wchar_t prev = 0;
        for (wchar_t c : name)
        {
            if (wchar_t p = PinyinInitial(c))
            {
                out += p;
                wordStart = true;
            }
            else if (std::iswalnum(c))
            {
                const bool camel = std::iswupper(c) && std::iswlower(prev);
                if (wordStart || camel)
                {
                    out += static_cast<wchar_t>(std::towlower(c));
                }
                wordStart = false;
            }
            else
            {
                wordStart = true;
            }
            prev = c;
        }
        return out;
    }
}
