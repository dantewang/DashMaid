#include <windows.h>
#include <algorithm>
#include <cwctype>
#include "TextEdit.h"

namespace dm
{
    namespace
    {
        bool IsLow(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }
        bool IsWordChar(wchar_t c) { return std::iswalnum(c) || c >= 0x2E80; }
    }

    std::wstring TextEdit::Selected() const
    {
        return text_.substr(SelectionStart(), SelectionEnd() - SelectionStart());
    }

    void TextEdit::Clear()
    {
        text_.clear();
        caret_ = anchor_ = 0;
    }

    void TextEdit::DeleteSelection()
    {
        const size_t a = SelectionStart(), b = SelectionEnd();
        text_.erase(a, b - a);
        caret_ = anchor_ = a;
    }

    void TextEdit::Insert(std::wstring_view s)
    {
        DeleteSelection();
        std::wstring clean;
        for (wchar_t c : s)
        {
            if (c == L'\r' || c == L'\n' || c == L'\t')
            {
                clean += L' ';
            }
            else if (c >= 0x20)
            {
                clean += c;
            }
        }
        text_.insert(caret_, clean);
        caret_ += clean.size();
        anchor_ = caret_;
    }

    void TextEdit::SelectAll()
    {
        anchor_ = 0;
        caret_ = text_.size();
    }

    void TextEdit::SetCaret(size_t pos, bool extend)
    {
        caret_ = std::min(pos, text_.size());
        if (caret_ > 0 && caret_ < text_.size() && IsLow(text_[caret_]))
        {
            --caret_;
        }
        if (!extend)
        {
            anchor_ = caret_;
        }
    }

    size_t TextEdit::PrevStop(size_t pos, bool word) const
    {
        if (pos == 0)
        {
            return 0;
        }
        if (!word)
        {
            --pos;
            if (pos > 0 && IsLow(text_[pos]))
            {
                --pos;
            }
            return pos;
        }
        while (pos > 0 && !IsWordChar(text_[pos - 1]))
        {
            --pos;
        }
        while (pos > 0 && IsWordChar(text_[pos - 1]))
        {
            --pos;
        }
        return pos;
    }

    size_t TextEdit::NextStop(size_t pos, bool word) const
    {
        const size_t n = text_.size();
        if (pos >= n)
        {
            return n;
        }
        if (!word)
        {
            ++pos;
            if (pos < n && IsLow(text_[pos]))
            {
                ++pos;
            }
            return pos;
        }
        while (pos < n && IsWordChar(text_[pos]))
        {
            ++pos;
        }
        while (pos < n && !IsWordChar(text_[pos]))
        {
            ++pos;
        }
        return pos;
    }

    bool TextEdit::Key(unsigned vk, bool ctrl, bool shift)
    {
        const size_t before = caret_, beforeAnchor = anchor_;
        const size_t beforeSize = text_.size();
        switch (vk)
        {
        case VK_LEFT:
            if (HasSelection() && !shift)
            {
                SetCaret(SelectionStart(), false);
            }
            else
            {
                SetCaret(PrevStop(caret_, ctrl), shift);
            }
            break;
        case VK_RIGHT:
            if (HasSelection() && !shift)
            {
                SetCaret(SelectionEnd(), false);
            }
            else
            {
                SetCaret(NextStop(caret_, ctrl), shift);
            }
            break;
        case VK_HOME:
            SetCaret(0, shift);
            break;
        case VK_END:
            SetCaret(text_.size(), shift);
            break;
        case VK_BACK:
            if (!HasSelection())
            {
                anchor_ = PrevStop(caret_, ctrl);
            }
            DeleteSelection();
            break;
        case VK_DELETE:
            if (!HasSelection())
            {
                anchor_ = NextStop(caret_, ctrl);
            }
            DeleteSelection();
            break;
        case 'A':
            if (!ctrl)
            {
                return false;
            }
            SelectAll();
            break;
        default:
            return false;
        }
        return caret_ != before || anchor_ != beforeAnchor || text_.size() != beforeSize;
    }
}
