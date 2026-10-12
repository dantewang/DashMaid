#include "pch.h"
#include "Config.h"

using namespace winrt::Windows::Data::Json;

namespace dm
{
    namespace
    {
        template <class E>
        struct EnumName
        {
            E value;
            const wchar_t* name;
        };

        constexpr EnumName<PositionMode> ModeNames[] = {
            { PositionMode::Float, L"float" },
            { PositionMode::Dock, L"dock" },
            { PositionMode::Center, L"center" },
            { PositionMode::Cursor, L"cursor" },
        };
        constexpr EnumName<Theme> ThemeNames[] = { { Theme::Dark, L"dark" }, { Theme::Light, L"light" } };
        constexpr EnumName<Material> MaterialNames[] = { { Material::Acrylic, L"acrylic" }, { Material::Solid, L"solid" } };
        constexpr EnumName<CollapsedStyle> StyleNames[] = { { CollapsedStyle::Slice, L"slice" }, { CollapsedStyle::Glow, L"glow" } };

        template <class E, size_t N>
        E Parse(const EnumName<E> (&names)[N], const winrt::hstring& s, E fallback)
        {
            for (auto& n : names)
            {
                if (s == n.name)
                {
                    return n.value;
                }
            }
            return fallback;
        }

        template <class E, size_t N>
        const wchar_t* Name(const EnumName<E> (&names)[N], E v)
        {
            for (auto& n : names)
            {
                if (n.value == v)
                {
                    return n.name;
                }
            }
            return names[0].name;
        }

        bool FileExists(const std::wstring& path)
        {
            wchar_t buf[MAX_PATH * 2];
            ExpandEnvironmentStringsW(path.c_str(), buf, ARRAYSIZE(buf));
            return GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES;
        }

        std::string ReadFile(const std::filesystem::path& p, bool& ok)
        {
            std::ifstream f(p, std::ios::binary);
            ok = f.good();
            if (!ok)
            {
                return {};
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            std::string s = ss.str();
            if (s.size() >= 3 && s.compare(0, 3, "\xEF\xBB\xBF") == 0)
            {
                s.erase(0, 3);
            }
            return s;
        }
    }

    std::filesystem::path Config::FilePath()
    {
        PWSTR raw = nullptr;
        winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &raw));
        std::filesystem::path p(raw);
        CoTaskMemFree(raw);
        return p / L"DashMaid" / L"config.json";
    }

    Config Config::Defaults()
    {
        Config c;
        const AppItem candidates[] = {
            { L"文件资源管理器", L"C:\\Windows\\explorer.exe" },
            { L"终端", L"%LOCALAPPDATA%\\Microsoft\\WindowsApps\\wt.exe" },
            { L"记事本", L"C:\\Windows\\System32\\notepad.exe" },
            { L"命令提示符", L"C:\\Windows\\System32\\cmd.exe" },
            { L"Windows PowerShell", L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" },
            { L"任务管理器", L"C:\\Windows\\System32\\Taskmgr.exe" },
            { L"计算器", L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App" },
            { L"设置", L"shell:AppsFolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel" },
        };
        for (auto& a : candidates)
        {
            if (a.path.starts_with(L"shell:") || FileExists(a.path))
            {
                c.items.push_back(a);
            }
        }
        return c;
    }

    Config Config::Load(std::string& text)
    {
        const auto path = FilePath();
        bool ok = false;
        text = ReadFile(path, ok);
        if (!ok)
        {
            Config c = Defaults();
            text = c.Save();
            return c;
        }

        JsonObject o{ nullptr };
        if (!JsonObject::TryParse(winrt::to_hstring(text), o))
        {
            // Keep the file as it is. The user can correct it.
            return Defaults();
        }

        Config c;
        if (auto pos = o.GetNamedObject(L"position", nullptr))
        {
            c.mode = Parse(ModeNames, pos.GetNamedString(L"mode", L"float"), PositionMode::Float);
            if (pos.HasKey(L"x") && pos.HasKey(L"y"))
            {
                c.x = static_cast<int>(pos.GetNamedNumber(L"x"));
                c.y = static_cast<int>(pos.GetNamedNumber(L"y"));
            }
        }
        c.theme = Parse(ThemeNames, o.GetNamedString(L"theme", L"dark"), Theme::Dark);
        c.material = Parse(MaterialNames, o.GetNamedString(L"material", L"acrylic"), Material::Acrylic);
        c.collapsedStyle = Parse(StyleNames, o.GetNamedString(L"collapsedStyle", L"slice"), CollapsedStyle::Slice);
        if (auto sp = o.GetNamedObject(L"spring", nullptr))
        {
            c.spring.stiffness = static_cast<float>(std::clamp(sp.GetNamedNumber(L"stiffness", 420), 50.0, 2000.0));
            c.spring.damping = static_cast<float>(std::clamp(sp.GetNamedNumber(L"damping", 30), 1.0, 200.0));
        }
        if (auto hk = o.GetNamedObject(L"hotkeys", nullptr))
        {
            c.runHotkey = hk.GetNamedString(L"run", c.runHotkey.c_str());
        }
        for (auto&& v : o.GetNamedArray(L"items", JsonArray()))
        {
            if (v.ValueType() != JsonValueType::Object)
            {
                continue;
            }
            auto io = v.GetObject();
            if (io.GetNamedString(L"type", L"app") != L"app")
            {
                continue;
            }
            AppItem a;
            a.name = io.GetNamedString(L"name", L"");
            a.path = io.GetNamedString(L"path", L"");
            a.args = io.GetNamedString(L"args", L"");
            a.workingDir = io.GetNamedString(L"workingDir", L"");
            a.admin = io.GetNamedBoolean(L"admin", false);
            auto style = io.GetNamedString(L"style", L"default");
            if (style != L"default")
            {
                a.style = Parse(StyleNames, style, CollapsedStyle::Slice);
            }
            if (!a.path.empty())
            {
                c.items.push_back(std::move(a));
            }
        }
        return c;
    }

    std::string Config::Save() const
    {
        JsonObject o;
        o.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));

        JsonObject pos;
        pos.SetNamedValue(L"mode", JsonValue::CreateStringValue(Name(ModeNames, mode)));
        if (x != Unset && y != Unset)
        {
            pos.SetNamedValue(L"x", JsonValue::CreateNumberValue(x));
            pos.SetNamedValue(L"y", JsonValue::CreateNumberValue(y));
        }
        o.SetNamedValue(L"position", pos);
        o.SetNamedValue(L"theme", JsonValue::CreateStringValue(Name(ThemeNames, theme)));
        o.SetNamedValue(L"material", JsonValue::CreateStringValue(Name(MaterialNames, material)));
        o.SetNamedValue(L"collapsedStyle", JsonValue::CreateStringValue(Name(StyleNames, collapsedStyle)));

        JsonObject sp;
        sp.SetNamedValue(L"stiffness", JsonValue::CreateNumberValue(spring.stiffness));
        sp.SetNamedValue(L"damping", JsonValue::CreateNumberValue(spring.damping));
        o.SetNamedValue(L"spring", sp);

        JsonObject hk;
        hk.SetNamedValue(L"run", JsonValue::CreateStringValue(runHotkey));
        o.SetNamedValue(L"hotkeys", hk);

        JsonArray arr;
        for (auto& a : items)
        {
            JsonObject io;
            io.SetNamedValue(L"type", JsonValue::CreateStringValue(L"app"));
            io.SetNamedValue(L"name", JsonValue::CreateStringValue(a.name));
            io.SetNamedValue(L"path", JsonValue::CreateStringValue(a.path));
            io.SetNamedValue(L"args", JsonValue::CreateStringValue(a.args));
            io.SetNamedValue(L"workingDir", JsonValue::CreateStringValue(a.workingDir));
            io.SetNamedValue(L"style", JsonValue::CreateStringValue(a.style ? Name(StyleNames, *a.style) : L"default"));
            io.SetNamedValue(L"admin", JsonValue::CreateBooleanValue(a.admin));
            arr.Append(io);
        }
        o.SetNamedValue(L"items", arr);

        const std::string text = winrt::to_string(o.Stringify());
        const auto path = FilePath();
        std::filesystem::create_directories(path.parent_path());
        auto tmp = path;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        return text;
    }
}
