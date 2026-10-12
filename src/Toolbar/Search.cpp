#include <windows.h>
#include <algorithm>
#include <cwctype>
#include "Pinyin.h"
#include "Search.h"

namespace dm
{
    std::wstring Trim(std::wstring_view s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::iswspace(s[a]))
        {
            ++a;
        }
        while (b > a && std::iswspace(s[b - 1]))
        {
            --b;
        }
        return std::wstring(s.substr(a, b - a));
    }

    std::wstring ToLower(std::wstring_view s)
    {
        std::wstring out(s);
        if (!out.empty())
        {
            CharLowerBuffW(out.data(), static_cast<DWORD>(out.size()));
        }
        return out;
    }

    void PrepareCandidate(Candidate& c)
    {
        c.initials = Initials(c.name);
        c.lowerName = ToLower(c.name);
        c.lowerPath = ToLower(c.display.empty() ? c.path : c.display);
    }

    namespace
    {
        int Score(const Candidate& c, const std::wstring& q, Match& m)
        {
            int score = 0;
            const size_t pos = c.lowerName.find(q);
            if (pos == 0)
            {
                score = 100;
            }
            else if (pos != std::wstring::npos)
            {
                const wchar_t before = c.name[pos - 1];
                const bool wordStart = !std::iswalnum(before) || (std::iswupper(c.name[pos]) && std::iswlower(before));
                score = wordStart ? 85 : 60;
            }
            if (pos != std::wstring::npos)
            {
                m.highlightStart = static_cast<int>(pos);
                m.highlightLength = static_cast<int>(q.size());
            }
            if (c.initials.starts_with(q))
            {
                score = std::max(score, 75);
            }
            else if (q.size() >= 2 && c.initials.find(q) != std::wstring::npos)
            {
                score = std::max(score, 45);
            }
            if (score == 0 && q.size() >= 2 && c.lowerPath.find(q) != std::wstring::npos)
            {
                score = 20;
            }
            if (score > 0 && c.onToolbar)
            {
                score += 10;
            }
            return score;
        }
    }

    std::vector<Match> Rank(const std::vector<Candidate>& candidates, std::wstring_view query, size_t maxCount)
    {
        const std::wstring q = ToLower(Trim(query));
        std::vector<Match> out;
        if (q.empty())
        {
            return out;
        }
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            Match m;
            m.index = i;
            m.score = Score(candidates[i], q, m);
            if (m.score > 0)
            {
                out.push_back(m);
            }
        }
        std::stable_sort(out.begin(), out.end(), [&](const Match& a, const Match& b) {
            if (a.score != b.score)
            {
                return a.score > b.score;
            }
            return candidates[a.index].name.size() < candidates[b.index].name.size();
        });
        if (out.size() > maxCount)
        {
            out.resize(maxCount);
        }
        return out;
    }

    CommandLine ParseCommand(std::wstring_view text, const std::function<bool(const std::wstring&)>& exists)
    {
        const std::wstring t = Trim(text);
        CommandLine cl;
        if (t.empty())
        {
            return cl;
        }
        if (t[0] == L'"')
        {
            const size_t close = t.find(L'"', 1);
            if (close == std::wstring::npos)
            {
                cl.file = t.substr(1);
                return cl;
            }
            cl.file = t.substr(1, close - 1);
            cl.params = Trim(std::wstring_view(t).substr(close + 1));
            return cl;
        }
        if (exists(t))
        {
            cl.file = t;
            return cl;
        }
        const size_t space = t.find_first_of(L" \t");
        if (space == std::wstring::npos)
        {
            cl.file = t;
            return cl;
        }
        cl.file = t.substr(0, space);
        cl.params = Trim(std::wstring_view(t).substr(space + 1));
        return cl;
    }
}
