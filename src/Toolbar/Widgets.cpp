#include "pch.h"
#include "Widgets.h"
#include "Strings.h"

namespace dm
{
    namespace
    {
        constexpr DWORD CommandTimeoutMs = 30000;
        constexpr size_t CommandOutputLimit = 64 * 1024;
        constexpr size_t BriefLimit = 40;
        constexpr size_t DetailLineLimit = 80;
        constexpr size_t DetailLines = 3;

        ULONGLONG ToU64(FILETIME f)
        {
            return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
        }

        std::wstring Printf(const wchar_t* format, ...)
        {
            wchar_t buf[128];
            va_list args;
            va_start(args, format);
            _vsnwprintf_s(buf, _TRUNCATE, format, args);
            va_end(args);
            return buf;
        }

        // Decimal units, like the Windows network flyout.
        std::wstring Rate(double bytesPerSecond)
        {
            if (bytesPerSecond >= 1e6)
            {
                return Printf(L"%.1f MB", bytesPerSecond / 1e6);
            }
            return Printf(L"%.0f KB", bytesPerSecond / 1e3);
        }

        // The manifest sets UTF-8 as the code page of this process, so CP_OEMCP
        // is UTF-8 here. Console programs write in the OEM code page of the system.
        UINT SystemOemCodePage()
        {
            wchar_t buf[16]{};
            DWORD size = sizeof(buf);
            if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage", L"OEMCP",
                             RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
            {
                if (const UINT cp = static_cast<UINT>(_wtoi(buf)); cp != 0 && IsValidCodePage(cp))
                {
                    return cp;
                }
            }
            return 437;
        }

        std::wstring Decode(const std::string& bytes)
        {
            if (bytes.empty())
            {
                return {};
            }
            const int n = static_cast<int>(bytes.size());
            UINT cp = CP_UTF8;
            int len = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, bytes.data(), n, nullptr, 0);
            if (len == 0)
            {
                cp = SystemOemCodePage();
                len = MultiByteToWideChar(cp, 0, bytes.data(), n, nullptr, 0);
            }
            std::wstring s(static_cast<size_t>(len), L'\0');
            MultiByteToWideChar(cp, 0, bytes.data(), n, s.data(), len);
            if (!s.empty() && s[0] == 0xFEFF)
            {
                s.erase(0, 1);
            }
            return s;
        }

        std::wstring Clip(std::wstring s, size_t limit)
        {
            if (s.size() > limit)
            {
                s.resize(limit - 1);
                s += L'…';
            }
            return s;
        }

        std::vector<std::wstring> Lines(const std::wstring& text)
        {
            std::vector<std::wstring> lines;
            std::wistringstream in(text);
            std::wstring line;
            while (std::getline(in, line))
            {
                const auto b = line.find_first_not_of(L" \t\r");
                if (b == std::wstring::npos)
                {
                    continue;
                }
                const auto e = line.find_last_not_of(L" \t\r");
                lines.push_back(line.substr(b, e - b + 1));
            }
            return lines;
        }

        std::wstring ProfileDir()
        {
            PWSTR raw = nullptr;
            std::wstring dir;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &raw)))
            {
                dir = raw;
            }
            CoTaskMemFree(raw);
            return dir;
        }

        // Gives this process a console without a window, one time. The commands
        // inherit it, so no console window shows and the code page is UTF-8.
        // AllocConsoleWithOptions exists on Windows 11 24H2 and later.
        bool AttachHiddenUtf8Console()
        {
            static std::once_flag once;
            static bool ok = false;
            std::call_once(once, [] {
                struct Options
                {
                    int mode;  // 2: ALLOC_CONSOLE_MODE_NO_WINDOW
                    BOOL useShowWindow;
                    WORD showWindow;
                };
                using AllocFn = HRESULT(WINAPI*)(Options*, int*);
                auto alloc = reinterpret_cast<AllocFn>(
                    GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AllocConsoleWithOptions"));
                if (!alloc)
                {
                    return;
                }
                Options options{ 2, FALSE, 0 };
                int result = 0;
                if (SUCCEEDED(alloc(&options, &result)) && result != 0)
                {
                    SetConsoleOutputCP(CP_UTF8);
                    SetConsoleCP(CP_UTF8);
                    ok = true;
                }
            });
            return ok;
        }

        double BaseMhz()
        {
            DWORD mhz = 0, size = sizeof(mhz);
            RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz",
                         RRF_RT_REG_DWORD, nullptr, &mhz, &size);
            return mhz;
        }
    }

    // ---------------------------------------------------------------- CommandRunner

    CommandRunner::CommandRunner(std::wstring command, int intervalSeconds, HWND notify, UINT message)
        : command_(std::move(command)),
          intervalMs_(static_cast<DWORD>(std::max(1, intervalSeconds)) * 1000),
          notify_(notify),
          message_(message),
          stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        thread_ = std::thread([this] { Loop(); });
    }

    CommandRunner::~CommandRunner()
    {
        SetEvent(stop_);
        if (thread_.joinable())
        {
            thread_.join();
        }
        CloseHandle(stop_);
    }

    bool CommandRunner::Output(std::wstring& text)
    {
        std::lock_guard lock(mutex_);
        text = output_;
        return ready_;
    }

    void CommandRunner::Loop()
    {
        for (;;)
        {
            std::wstring text = RunOnce();
            if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0)
            {
                return;
            }
            {
                std::lock_guard lock(mutex_);
                output_ = std::move(text);
                ready_ = true;
            }
            PostMessageW(notify_, message_, 0, 0);
            if (WaitForSingleObject(stop_, intervalMs_) == WAIT_OBJECT_0)
            {
                return;
            }
        }
    }

    // Runs the command through cmd.exe, so that pipes and built-in commands work.
    // A job object stops the command and its child processes at the end.
    std::wstring CommandRunner::RunOnce()
    {
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE readPipe = nullptr, writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        {
            return {};
        }
        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

        wchar_t comspec[MAX_PATH];
        if (!GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH))
        {
            wcscpy_s(comspec, L"cmd.exe");
        }
        std::wstring cmdline = L"\"" + std::wstring(comspec) + L"\" /d /s /c \"" + command_ + L"\"";
        const DWORD consoleFlag = AttachHiddenUtf8Console() ? 0 : CREATE_NO_WINDOW;
        const std::wstring dir = ProfileDir();

        STARTUPINFOW si{ sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nul;
        si.hStdOutput = writePipe;
        si.hStdError = nul;
        PROCESS_INFORMATION pi{};

        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

        const BOOL started = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                                            consoleFlag | CREATE_SUSPENDED, nullptr,
                                            dir.empty() ? nullptr : dir.c_str(), &si, &pi);
        CloseHandle(writePipe);
        if (nul != INVALID_HANDLE_VALUE)
        {
            CloseHandle(nul);
        }

        std::string bytes;
        auto drain = [&] {
            DWORD avail = 0;
            while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
            {
                char buf[4096];
                DWORD got = 0;
                if (!ReadFile(readPipe, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0)
                {
                    break;
                }
                if (bytes.size() < CommandOutputLimit)
                {
                    bytes.append(buf, got);
                }
            }
        };

        if (started)
        {
            AssignProcessToJobObject(job, pi.hProcess);
            ResumeThread(pi.hThread);
            const ULONGLONG deadline = GetTickCount64() + CommandTimeoutMs;
            const HANDLE waits[2] = { pi.hProcess, stop_ };
            for (;;)
            {
                drain();
                const DWORD r = WaitForMultipleObjects(2, waits, FALSE, 50);
                if (r == WAIT_OBJECT_0)
                {
                    drain();
                    break;
                }
                if (r != WAIT_TIMEOUT || GetTickCount64() > deadline)
                {
                    break;
                }
            }
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        CloseHandle(job);
        CloseHandle(readPipe);
        return Decode(bytes);
    }

    // ---------------------------------------------------------------- Widgets

    Widgets::~Widgets()
    {
        slots_.clear();
        if (pdh_)
        {
            PdhCloseQuery(pdh_);
        }
    }

    std::wstring Widgets::DefaultClick(WidgetSource source)
    {
        switch (source)
        {
        case WidgetSource::Clock:
            return L"ms-clock:";
        case WidgetSource::Cpu:
        case WidgetSource::Memory:
            return L"taskmgr.exe";
        case WidgetSource::Network:
            return L"ms-settings:network";
        default:
            return {};
        }
    }

    void Widgets::Configure(const std::vector<Item>& items, HWND notify, UINT message)
    {
        slots_.clear();
        needCpu_ = needNetwork_ = false;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            const Item& it = items[i];
            if (!it.widget)
            {
                continue;
            }
            Slot s;
            s.index = i;
            s.source = *it.widget;
            s.name = it.name;
            if (s.source == WidgetSource::Command && !it.command.empty())
            {
                s.runner = std::make_unique<CommandRunner>(it.command, it.interval, notify, message);
            }
            needCpu_ |= s.source == WidgetSource::Cpu;
            needNetwork_ |= s.source == WidgetSource::Network;
            slots_.push_back(std::move(s));
        }

        if (needCpu_ && !pdh_ && PdhOpenQueryW(nullptr, 0, &pdh_) == ERROR_SUCCESS)
        {
            PdhAddEnglishCounterW(pdh_, L"\\Processor Information(_Total)\\% Processor Utility", 0, &utility_);
            PdhAddEnglishCounterW(pdh_, L"\\Processor Information(_Total)\\% Processor Performance", 0, &performance_);
            PdhCollectQueryData(pdh_);
            maxMhz_ = BaseMhz();
            threads_ = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        }
    }

    // Task Manager shows "% Processor Utility". If the counter is not
    // available, the system times give a similar value.
    void Widgets::SampleCpu()
    {
        bool fromPdh = false;
        if (pdh_ && utility_ && PdhCollectQueryData(pdh_) == ERROR_SUCCESS)
        {
            PDH_FMT_COUNTERVALUE v{};
            if (PdhGetFormattedCounterValue(utility_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) == ERROR_SUCCESS &&
                v.CStatus == PDH_CSTATUS_VALID_DATA)
            {
                cpuPercent_ = std::clamp(v.doubleValue, 0.0, 100.0);
                fromPdh = true;
            }
            if (performance_ &&
                PdhGetFormattedCounterValue(performance_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) == ERROR_SUCCESS &&
                v.CStatus == PDH_CSTATUS_VALID_DATA && maxMhz_ > 0)
            {
                cpuGhz_ = maxMhz_ * v.doubleValue / 100.0 / 1000.0;
            }
        }
        if (!fromPdh)
        {
            FILETIME idle, kernel, user;
            if (GetSystemTimes(&idle, &kernel, &user))
            {
                const ULONGLONG i = ToU64(idle), t = ToU64(kernel) + ToU64(user);
                if (lastTotal_ && t > lastTotal_)
                {
                    cpuPercent_ = 100.0 * (1.0 - static_cast<double>(i - lastIdle_) / static_cast<double>(t - lastTotal_));
                    cpuPercent_ = std::clamp(cpuPercent_, 0.0, 100.0);
                }
                lastIdle_ = i;
                lastTotal_ = t;
            }
        }
    }

    // Adds the traffic of the physical interfaces that are up.
    void Widgets::SampleNetwork()
    {
        PMIB_IF_TABLE2 table = nullptr;
        if (GetIfTable2(&table) != NO_ERROR)
        {
            return;
        }
        ULONG64 in = 0, out = 0;
        for (ULONG i = 0; i < table->NumEntries; ++i)
        {
            const MIB_IF_ROW2& row = table->Table[i];
            if (row.InterfaceAndOperStatusFlags.HardwareInterface && !row.InterfaceAndOperStatusFlags.FilterInterface &&
                row.OperStatus == IfOperStatusUp && row.Type != IF_TYPE_SOFTWARE_LOOPBACK)
            {
                in += row.InOctets;
                out += row.OutOctets;
            }
        }
        FreeMibTable(table);

        const ULONGLONG now = GetTickCount64();
        if (lastNetTick_ && now > lastNetTick_ && in >= lastIn_ && out >= lastOut_)
        {
            const double sec = (now - lastNetTick_) / 1000.0;
            downBps_ = (in - lastIn_) / sec;
            upBps_ = (out - lastOut_) / sec;
        }
        lastIn_ = in;
        lastOut_ = out;
        lastNetTick_ = now;
    }

    WidgetText Widgets::Format(const Slot& s)
    {
        using namespace strings;
        switch (s.source)
        {
        case WidgetSource::Clock:
        {
            SYSTEMTIME t;
            GetLocalTime(&t);
            return { Printf(L"%02u:%02u", t.wHour, t.wMinute),
                     Printf(L"%u月%u日 周%c %02u:%02u:%02u", t.wMonth, t.wDay, Weekdays[t.wDayOfWeek], t.wHour, t.wMinute, t.wSecond) };
        }
        case WidgetSource::Cpu:
        {
            const std::wstring brief = Printf(L"CPU %.0f%%", cpuPercent_);
            std::wstring detail = brief + Printf(L" · %u %s", threads_, Threads);
            if (cpuGhz_ > 0)
            {
                detail += Printf(L" · %.1f GHz", cpuGhz_);
            }
            return { brief, detail };
        }
        case WidgetSource::Memory:
        {
            MEMORYSTATUSEX m{ sizeof(m) };
            GlobalMemoryStatusEx(&m);
            constexpr double gb = 1024.0 * 1024.0 * 1024.0;
            return { Printf(L"%s %u%%", Memory, m.dwMemoryLoad),
                     Printf(L"%s %.1f / %.1f GB", Memory, (m.ullTotalPhys - m.ullAvailPhys) / gb, m.ullTotalPhys / gb) };
        }
        case WidgetSource::Network:
            return { L"↓ " + Rate(downBps_), L"↓ " + Rate(downBps_) + L"/s · ↑ " + Rate(upBps_) + L"/s" };
        case WidgetSource::Command:
        {
            std::wstring output;
            const bool ready = s.runner && s.runner->Output(output);
            const auto lines = Lines(output);
            if (!ready || lines.empty())
            {
                const std::wstring label = s.name.empty() ? std::wstring(L"…") : s.name;
                return { Clip(label, BriefLimit), label };
            }
            std::wstring detail;
            for (size_t i = 0; i < std::min(lines.size(), DetailLines); ++i)
            {
                detail += (i ? L" · " : L"") + Clip(lines[i], DetailLineLimit);
            }
            return { Clip(lines[0], BriefLimit), detail };
        }
        }
        return {};
    }

    std::vector<int> Widgets::Update(bool sample)
    {
        if (sample)
        {
            if (needCpu_)
            {
                SampleCpu();
            }
            if (needNetwork_)
            {
                SampleNetwork();
            }
        }
        std::vector<int> changed;
        for (auto& s : slots_)
        {
            if (!sample && s.source != WidgetSource::Command)
            {
                continue;
            }
            WidgetText t = Format(s);
            if (!(t == s.text))
            {
                s.text = std::move(t);
                changed.push_back(s.index);
            }
        }
        return changed;
    }

    const WidgetText& Widgets::Text(int itemIndex) const
    {
        static const WidgetText empty;
        for (auto& s : slots_)
        {
            if (s.index == itemIndex)
            {
                return s.text;
            }
        }
        return empty;
    }
}
