#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace dm
{
    struct Candidate
    {
        std::wstring name;
        std::wstring path;      // Path to start. Can be a "shell:" path.
        std::wstring display;   // Path to show.
        bool onToolbar = false;
        std::wstring initials;  // Set by PrepareCandidate.
        std::wstring lowerName; // Set by PrepareCandidate.
        std::wstring lowerPath; // Set by PrepareCandidate.
    };

    struct Match
    {
        size_t index = 0;
        int score = 0;
        int highlightStart = -1;  // Range in the name, or -1.
        int highlightLength = 0;
    };

    void PrepareCandidate(Candidate& c);

    // Returns the best matches, best first. 'query' can have any case.
    std::vector<Match> Rank(const std::vector<Candidate>& candidates, std::wstring_view query, size_t maxCount);

    struct CommandLine
    {
        std::wstring file;
        std::wstring params;
    };

    // Splits text like the Windows Run dialog does. A quoted first part is
    // the file. If the full text is an existing path, the full text is the
    // file. In other cases, the text before the first space is the file.
    CommandLine ParseCommand(std::wstring_view text, const std::function<bool(const std::wstring&)>& exists);

    std::wstring Trim(std::wstring_view s);
    std::wstring ToLower(std::wstring_view s);
}
