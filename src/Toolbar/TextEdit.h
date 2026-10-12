#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace dm
{
    // Single-line text with a caret and a selection.
    class TextEdit
    {
    public:
        const std::wstring& Text() const { return text_; }
        size_t Caret() const { return caret_; }
        size_t SelectionStart() const { return std::min(caret_, anchor_); }
        size_t SelectionEnd() const { return std::max(caret_, anchor_); }
        bool HasSelection() const { return caret_ != anchor_; }
        std::wstring Selected() const;

        void Clear();
        void Insert(std::wstring_view s);  // Replaces the selection.
        void DeleteSelection();
        void SelectAll();
        void SetCaret(size_t pos, bool extend);

        // Handles navigation and delete keys. Returns true if the text,
        // the caret, or the selection changed.
        bool Key(unsigned vk, bool ctrl, bool shift);

    private:
        size_t PrevStop(size_t pos, bool word) const;
        size_t NextStop(size_t pos, bool word) const;

        std::wstring text_;
        size_t caret_ = 0;
        size_t anchor_ = 0;
    };
}
