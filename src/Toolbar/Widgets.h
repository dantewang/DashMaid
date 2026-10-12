#pragma once

#include "Config.h"

namespace dm
{
    struct WidgetText
    {
        std::wstring brief;   // Shown when the widget is not expanded.
        std::wstring detail;  // Shown when the widget is expanded.
        bool operator==(const WidgetText&) const = default;
    };

    // Runs a command again and again on a worker thread and keeps the last output.
    class CommandRunner
    {
    public:
        CommandRunner(std::wstring command, int intervalSeconds, HWND notify, UINT message);
        ~CommandRunner();
        CommandRunner(const CommandRunner&) = delete;
        CommandRunner& operator=(const CommandRunner&) = delete;

        // Returns false until the first run is complete.
        bool Output(std::wstring& text);

    private:
        void Loop();
        std::wstring RunOnce();

        std::wstring command_;
        DWORD intervalMs_;
        HWND notify_;
        UINT message_;
        HANDLE stop_;
        std::mutex mutex_;
        std::wstring output_;
        bool ready_ = false;
        std::thread thread_;
    };

    // Reads the system values and makes the text of each widget.
    class Widgets
    {
    public:
        ~Widgets();

        // Starts again with the widgets in 'items'. 'message' goes to 'notify'
        // when a command widget has new output.
        void Configure(const std::vector<Item>& items, HWND notify, UINT message);
        bool Empty() const { return slots_.empty(); }

        // Reads new values. 'sample' false reads only the command output.
        // Returns the item indexes whose text changed.
        std::vector<int> Update(bool sample);
        const WidgetText& Text(int itemIndex) const;

        // The default click target of a source.
        static std::wstring DefaultClick(WidgetSource source);

    private:
        struct Slot
        {
            int index = 0;
            WidgetSource source = WidgetSource::Clock;
            std::wstring name;
            WidgetText text;
            std::unique_ptr<CommandRunner> runner;
        };

        void SampleCpu();
        void SampleNetwork();
        WidgetText Format(const Slot& s);

        std::vector<Slot> slots_;
        bool needCpu_ = false;
        bool needNetwork_ = false;

        // CPU.
        PDH_HQUERY pdh_ = nullptr;
        PDH_HCOUNTER utility_ = nullptr;
        PDH_HCOUNTER performance_ = nullptr;
        ULONGLONG lastIdle_ = 0, lastTotal_ = 0;
        double cpuPercent_ = 0;
        double cpuGhz_ = 0;
        double maxMhz_ = 0;
        DWORD threads_ = 0;

        // Network.
        ULONG64 lastIn_ = 0, lastOut_ = 0;
        ULONGLONG lastNetTick_ = 0;
        double downBps_ = 0, upBps_ = 0;
    };
}
