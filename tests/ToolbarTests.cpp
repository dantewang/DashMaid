// Tests for the parts of the toolbar that have no UI.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>
#include "../src/Toolbar/Hotkey.h"
#include "../src/Toolbar/Paint.h"
#include "../src/Toolbar/Pinyin.h"
#include "../src/Toolbar/Search.h"
#include "../src/Toolbar/TextEdit.h"

namespace
{
    int failures = 0;

    void Check(bool ok, const char* what, int line)
    {
        if (!ok)
        {
            ++failures;
            std::printf("FAIL line %d: %s\n", line, what);
        }
    }
}

#define CHECK(x) Check((x), #x, __LINE__)

using namespace dm;

int main()
{
    // CSS timing functions.
    CHECK(std::fabs(CubicBezier(0, 0, 1, 1, 0.3f) - 0.3f) < 1e-3f);
    CHECK(std::fabs(Ease(0.5f) - 0.8024f) < 2e-3f);
    CHECK(std::fabs(EaseInOut(0.5f) - 0.5f) < 1e-3f);
    CHECK(CubicBezier(0.3f, 1.6f, 0.5f, 1, 0.5f) > 1.f);  // Overshoot.

    // CSS shadow list interpolation.
    {
        const ShadowList a = { { 0, 2, 6, 0, { 0, 0, 0, 0.45f } }, { 0, -2, 4, 0, { 0, 0, 0, 0.22f }, true } };
        const ShadowList b = { { 0, 0, 14, 2, { 1, 1, 1, 0.35f } } };
        const ShadowList m = Interpolate(a, b, 0.5f);
        CHECK(m.size() == 2);
        CHECK(std::fabs(m[0].blur - 10) < 1e-4f && std::fabs(m[0].spread - 1) < 1e-4f);
        CHECK(std::fabs(m[0].color.a - 0.4f) < 1e-4f);
        CHECK(std::fabs(m[0].color.r - 0.4375f) < 1e-3f);  // Premultiplied: 0.175 / 0.4.
        CHECK(m[1].inset && std::fabs(m[1].color.a - 0.11f) < 1e-4f && std::fabs(m[1].y + 1) < 1e-4f);
    }

    // CSS transition with the reversing rule.
    {
        Transition t(1.f, 0, 0, 1, 1, 0.f);
        t.Set(1.f, 0.0);
        CHECK(std::fabs(t.Value(0.5) - 0.5f) < 1e-3f);
        t.Set(0.f, 0.5);  // Reverse at the middle: half the time.
        CHECK(std::fabs(t.Value(0.75) - 0.25f) < 1e-3f);
        CHECK(t.Value(1.0) == 0.f && !t.Running(1.0));
    }

    // Pinyin initials.
    CHECK(Initials(L"记事本") == L"jsb");
    CHECK(Initials(L"计算器") == L"jsq");
    CHECK(Initials(L"文件资源管理器") == L"wjzyglq");
    CHECK(Initials(L"Windows PowerShell") == L"wps");
    CHECK(Initials(L"7-Zip File Manager") == L"7zfm");
    CHECK(PinyinInitial(L'A') == 0);

    // Ranking.
    std::vector<Candidate> c = {
        { L"记事本", L"C:\\Windows\\notepad.exe" },
        { L"Notepad++", L"C:\\Program Files\\Notepad++\\notepad++.exe" },
        { L"计算器", L"shell:AppsFolder\\calc" },
        { L"Windows PowerShell", L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" },
    };
    for (auto& x : c)
    {
        PrepareCandidate(x);
    }
    auto r = Rank(c, L"jsb", 5);
    CHECK(!r.empty() && r[0].index == 0);
    r = Rank(c, L"note", 5);
    CHECK(!r.empty() && r[0].index == 1 && r[0].highlightStart == 0 && r[0].highlightLength == 4);
    r = Rank(c, L"shell", 5);
    CHECK(!r.empty() && r[0].index == 3 && r[0].highlightStart == 13);
    r = Rank(c, L"js", 5);
    CHECK(r.size() == 2);
    CHECK(Rank(c, L"   ", 5).empty());
    CHECK(Rank(c, L"zzzz", 5).empty());

    // Command parsing.
    auto none = [](const std::wstring&) { return false; };
    auto all = [](const std::wstring&) { return true; };
    auto cl = ParseCommand(L"  notepad  a.txt ", none);
    CHECK(cl.file == L"notepad" && cl.params == L"a.txt");
    cl = ParseCommand(L"\"C:\\Program Files\\x.exe\" -v", none);
    CHECK(cl.file == L"C:\\Program Files\\x.exe" && cl.params == L"-v");
    cl = ParseCommand(L"C:\\Program Files\\x.exe", all);
    CHECK(cl.file == L"C:\\Program Files\\x.exe" && cl.params.empty());
    cl = ParseCommand(L"https://example.com", none);
    CHECK(cl.file == L"https://example.com");

    // Text editing.
    TextEdit t;
    t.Insert(L"hello world");
    CHECK(t.Caret() == 11);
    CHECK(t.Key(VK_BACK, true, false) && t.Text() == L"hello ");
    t.Key(VK_HOME, false, false);
    t.Key(VK_RIGHT, false, true);
    t.Key(VK_RIGHT, false, true);
    CHECK(t.Selected() == L"he");
    t.Insert(L"J");
    CHECK(t.Text() == L"Jllo ");
    t.SelectAll();
    t.Insert(L"a\r\nb");
    CHECK(t.Text() == L"a  b");
    t.Key(VK_LEFT, false, false);
    CHECK(t.Key(VK_DELETE, false, false) && t.Text() == L"a  ");
    CHECK(!t.Key('A', false, false));

    // Hotkeys.
    auto hk = ParseHotkey(L"Ctrl+Alt+Space");
    CHECK(hk.modifiers == (MOD_CONTROL | MOD_ALT) && hk.vk == VK_SPACE);
    hk = ParseHotkey(L"Alt+`");
    CHECK(hk.modifiers == MOD_ALT && hk.vk == VK_OEM_3);
    hk = ParseHotkey(L"Win+Shift+F12");
    CHECK(hk.modifiers == (MOD_WIN | MOD_SHIFT) && hk.vk == VK_F12);
    CHECK(!ParseHotkey(L"Space").Valid());

    std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
