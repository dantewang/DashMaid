#include "pch.h"
#include "App.h"
#include "Hotkey.h"
#include "Icons.h"
#include "Launch.h"
#include "Metrics.h"
#include "Strings.h"

using namespace dm::metrics;

namespace dm
{
    namespace
    {
        constexpr wchar_t WindowClass[] = L"DashMaid.Toolbar";
        constexpr UINT TrayMessage = WM_APP + 1;
        constexpr UINT CatalogMessage = WM_APP + 2;
        constexpr UINT WidgetMessage = WM_APP + 3;
        constexpr UINT TrayId = 1;
        constexpr UINT_PTR MenuTimer = 1;
        constexpr UINT_PTR WidgetTimer = 2;
        constexpr UINT_PTR ConfigRetryTimer = 3;
        constexpr int HotkeyRun = 1;
        constexpr int HotkeyToggle = 2;
        constexpr UINT IconSourcePx = 128;
        constexpr UINT ResultIconPx = 64;
        constexpr size_t EmptyQueryRows = 5;

        bool PathExists(const std::wstring& path)
        {
            return GetFileAttributesW(ExpandPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
        }

        std::wstring DisplayPath(const std::wstring& path)
        {
            constexpr std::wstring_view apps = L"shell:AppsFolder\\";
            if (path.size() > apps.size() && _wcsnicmp(path.c_str(), apps.data(), apps.size()) == 0)
            {
                return path.substr(apps.size());
            }
            return ExpandPath(path);
        }

        std::wstring ReadClipboard(HWND owner)
        {
            std::wstring text;
            if (!OpenClipboard(owner))
            {
                return text;
            }
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
            {
                if (auto* p = static_cast<const wchar_t*>(GlobalLock(h)))
                {
                    text = p;
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            return text;
        }

        enum Command : UINT
        {
            CmdOpen = 1,
            CmdRunAsAdmin,
            CmdOpenLocation,
            CmdCopyPath,
            CmdRemove,
            CmdSettings,
            CmdExit,
        };
    }

    bool App::Initialize(HINSTANCE instance)
    {
        instance_ = instance;
        QueryPerformanceFrequency(&frequency_);
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic_.put())));

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = WindowClass;
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
                                WindowClass, strings::AppName, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!hwnd_)
        {
            return false;
        }

        scale_ = GetDpiForWindow(hwnd_) / 96.f;
        scene_.Initialize(hwnd_, scale_);

        auto loaded = Config::Load(configText_);
        configBroken_ = !loaded;
        config_ = loaded ? std::move(*loaded) : Config::Defaults();
        ApplyConfig();

        const auto dir = Config::FilePath().parent_path();
        configWatch_ = FindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);

        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        AddTrayIcon();
        UpdateVisibility();
        Kick();
        return true;
    }

    int App::Run()
    {
        MSG msg;
        for (;;)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    RemoveTrayIcon();
                    if (configWatch_ != INVALID_HANDLE_VALUE)
                    {
                        FindCloseChangeNotification(configWatch_);
                    }
                    return static_cast<int>(msg.wParam);
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            if (animating_)
            {
                Frame();
                if (animating_)
                {
                    DCompositionWaitForCompositorClock(0, nullptr, 100);
                }
                continue;
            }

            const bool watching = configWatch_ != INVALID_HANDLE_VALUE;
            const DWORD r = MsgWaitForMultipleObjectsEx(watching ? 1 : 0, watching ? &configWatch_ : nullptr, INFINITE,
                                                       QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (watching && r == WAIT_OBJECT_0)
            {
                FindNextChangeNotification(configWatch_);
                OnConfigFileChanged();
            }
        }
    }

    void App::ApplyConfig()
    {
        scene_.SetAppearance(config_.theme, config_.material, config_.collapsedStyle, config_.spring);
        scene_.SetWidgetsCollapse(config_.widgetsCollapse);
        LoadItems();
        if (config_.x == Config::Unset || config_.y == Config::Unset ||
            !MonitorFromPoint({ config_.x, config_.y + 1 }, MONITOR_DEFAULTTONULL))
        {
            ResetAnchor();
        }
        else
        {
            anchor_ = { config_.x, config_.y };
        }
        PlaceWindow();
        RegisterHotkeys();
        UpdateVisibility();
        if (runOpen_)
        {
            BuildCandidates();
            UpdateResults();
        }
    }

    void App::RegisterHotkeys()
    {
        UnregisterHotKey(hwnd_, HotkeyRun);
        const Hotkey hk = ParseHotkey(config_.runHotkey);
        if (hk.Valid())
        {
            RegisterHotKey(hwnd_, HotkeyRun, hk.modifiers | MOD_NOREPEAT, hk.vk);
        }
        // The toggle hotkey has no function in the other modes, so it stays free for other apps.
        UnregisterHotKey(hwnd_, HotkeyToggle);
        const Hotkey toggle = ParseHotkey(config_.toggleHotkey);
        if (HiddenMode() && toggle.Valid())
        {
            RegisterHotKey(hwnd_, HotkeyToggle, toggle.modifiers | MOD_NOREPEAT, toggle.vk);
        }
    }

    void App::LoadItems()
    {
        std::vector<SceneItem> items;
        items.reserve(config_.items.size());
        for (auto& a : config_.items)
        {
            if (a.IsWidget())
            {
                items.push_back({ nullptr, {}, std::nullopt, true, a.color });
                continue;
            }
            IconData icon = LoadAppIcon(wic_.get(), a.path, IconSourcePx);
            items.push_back({ icon.bitmap, icon.main, a.style, false, std::nullopt });
        }
        scene_.SetItems(std::move(items));
        widgets_.Configure(config_.items, hwnd_, WidgetMessage);
        ApplyWidgetText(widgets_.Update(true));
        KillTimer(hwnd_, WidgetTimer);
        if (!widgets_.Empty())
        {
            ArmWidgetTimer();
        }
        hover_ = -1;
        regionDirty_ = true;
    }

    void App::ResetAnchor()
    {
        HMONITOR mon = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        anchor_.x = (mi.rcWork.left + mi.rcWork.right) / 2;
        anchor_.y = mi.rcWork.top + static_cast<LONG>(std::lround(FloatDefaultTop * scale_));
    }

    void App::PlaceWindow()
    {
        const int w = static_cast<int>(std::ceil(scene_.WindowWidth() * scale_));
        const int h = static_cast<int>(std::ceil(scene_.WindowHeight() * scale_));
        scene_.SetWindowWidth(w / scale_);

        // The anchor is the top of the toolbar. The window starts higher by the shadow margin.
        const int margin = static_cast<int>(std::lround(Scene::TopMargin() * scale_));
        int x = anchor_.x - w / 2;
        int y = anchor_.y;
        if (config_.mode == PositionMode::Dock)
        {
            HMONITOR mon = MonitorFromPoint(anchor_, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(mon, &mi);
            x = (mi.rcMonitor.left + mi.rcMonitor.right) / 2 - w / 2;
            y = mi.rcMonitor.top + static_cast<int>(std::lround(DockTopOffset * scale_));
        }
        else if (HiddenMode())
        {
            SummonPosition(x, y);
        }
        y -= margin;
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        regionDirty_ = true;
        Kick();
    }

    void App::MoveWindowToAnchor()
    {
        RECT rc;
        GetWindowRect(hwnd_, &rc);
        const int w = rc.right - rc.left;
        const int margin = static_cast<int>(std::lround(Scene::TopMargin() * scale_));
        SetWindowPos(hwnd_, HWND_TOPMOST, anchor_.x - w / 2, anchor_.y - margin, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
    }

    void App::SaveConfig()
    {
        if (configBroken_)
        {
            return;
        }
        configText_ = config_.Save();
    }

    void App::OnConfigFileChanged()
    {
        std::string text;
        auto next = Config::Load(text);
        if (!next)
        {
            // The file is not valid now. Keep the current settings, and do not
            // write the file, so that the changes of the user stay. Try again later
            // because another program can still hold the file.
            configBroken_ = true;
            SetTimer(hwnd_, ConfigRetryTimer, 1000, nullptr);
            return;
        }
        configBroken_ = false;
        KillTimer(hwnd_, ConfigRetryTimer);
        if (text == configText_)
        {
            return;
        }
        configText_ = std::move(text);
        config_ = std::move(*next);
        ApplyConfig();
    }

    void App::Kick()
    {
        if (!animating_)
        {
            animating_ = true;
            QueryPerformanceCounter(&lastTick_);
        }
    }

    void App::Frame()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - lastTick_.QuadPart) / static_cast<float>(frequency_.QuadPart);
        lastTick_ = now;
        dt = std::clamp(dt, 0.f, 0.05f);

        animating_ = scene_.Step(dt);
        scene_.Apply();
        UpdateRegion();

        // The window keeps the run box height until the close animation ends.
        if (!animating_ && shrinkPending_ && !scene_.RunVisible())
        {
            shrinkPending_ = false;
            PlaceWindow();
        }
        if (!animating_ && hidePending_ && !scene_.VisibleNow())
        {
            hidePending_ = false;
            ShowWindow(hwnd_, SW_HIDE);
        }
    }

    // The window is wider than the toolbar at rest. Outside the toolbar,
    // the window must not take mouse input, so at rest the window region
    // is the rectangle of the toolbar and its visible shadow. The region is a
    // rectangle, so the rounded corners keep their antialiasing. While the
    // toolbar is open or moves, the region is the full window, so the shadows
    // of the expanded items show.
    void App::UpdateRegion()
    {
        const bool full = hover_ >= 0 || menuOpen_ || pressing_ || animating_ || runOpen_ || scene_.RunVisible();
        if (full == fullRegion_ && !regionDirty_)
        {
            return;
        }
        if (full)
        {
            SetWindowRgn(hwnd_, nullptr, FALSE);
        }
        else
        {
            const RECT r = scene_.RestRegion();
            SetWindowRgn(hwnd_, CreateRectRgn(r.left, r.top, r.right, r.bottom), FALSE);
        }
        fullRegion_ = full;
        regionDirty_ = false;
    }

    void App::RefreshHoverFromCursor()
    {
        POINT c;
        GetCursorPos(&c);
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        int h = -1;
        bool orb = false;
        if (PtInRect(&wr, c))
        {
            ScreenToClient(hwnd_, &c);
            h = scene_.HitTestItem(c);
            orb = scene_.HitTestOrb(c);
        }
        hover_ = h;
        scene_.SetHover(h);
        scene_.SetOrbHover(orb);
        Kick();
    }

    void App::OnMouseMove(POINT p)
    {
        if (!tracking_)
        {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd_, 0 };
            TrackMouseEvent(&tme);
            tracking_ = true;
        }

        if (runOpen_)
        {
            OnRunMouseMove(p);
            return;
        }

        if (pressing_)
        {
            POINT c;
            GetCursorPos(&c);
            const float dx = static_cast<float>(c.x - pressCursor_.x), dy = static_cast<float>(c.y - pressCursor_.y);
            if (!dragging_ && config_.mode == PositionMode::Float &&
                std::hypot(dx, dy) > DragThreshold * scale_)
            {
                dragging_ = true;
                scene_.SetPressed(-1);
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            }
            if (dragging_)
            {
                anchor_ = { pressAnchor_.x + (c.x - pressCursor_.x), pressAnchor_.y + (c.y - pressCursor_.y) };
                MoveWindowToAnchor();
                return;
            }
        }

        const int h = scene_.HitTestItem(p);
        const bool orb = scene_.HitTestOrb(p);
        if (h != hover_)
        {
            hover_ = h;
            scene_.SetHover(h);
            Kick();
        }
        scene_.SetOrbHover(orb);
        Kick();
    }

    void App::OnMouseLeave()
    {
        tracking_ = false;
        if (runOpen_)
        {
            scene_.SetOrbHover(false);
            Kick();
            return;
        }
        if (menuOpen_ || pressing_)
        {
            return;
        }
        hover_ = -1;
        scene_.SetHover(-1);
        scene_.SetOrbHover(false);
        Kick();
    }

    void App::OnLeftDown(POINT p)
    {
        if (runOpen_)
        {
            OnRunLeftDown(p);
            return;
        }
        SetCapture(hwnd_);
        pressing_ = true;
        dragging_ = false;
        pressItem_ = scene_.HitTestItem(p);
        pressOrb_ = pressItem_ < 0 && scene_.HitTestOrb(p);
        GetCursorPos(&pressCursor_);
        pressAnchor_ = anchor_;
        scene_.SetPressed(pressItem_);
        Kick();
    }

    void App::EndPress()
    {
        const bool wasDragging = dragging_;
        pressing_ = false;
        dragging_ = false;
        scene_.SetPressed(-1);
        Kick();
        if (wasDragging)
        {
            config_.x = anchor_.x;
            config_.y = anchor_.y;
            SaveConfig();
            RefreshHoverFromCursor();
        }
    }

    void App::OnLeftUp(POINT p)
    {
        if (runOpen_)
        {
            OnRunLeftUp(p);
            return;
        }
        if (!pressing_)
        {
            return;
        }
        const bool wasDragging = dragging_;
        const int item = pressItem_;
        const bool orb = pressOrb_;
        EndPress();
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }
        if (wasDragging)
        {
            return;
        }
        if (item >= 0 && item == scene_.HitTestItem(p) && item < static_cast<int>(config_.items.size()))
        {
            OpenItem(item, false);
        }
        else if (orb && scene_.HitTestOrb(p))
        {
            OpenRun();
        }
    }

    void App::OnRightUp(POINT p)
    {
        if (runOpen_)
        {
            return;
        }
        POINT screen = p;
        ClientToScreen(hwnd_, &screen);
        const int item = scene_.HitTestItem(p);
        if (item >= 0)
        {
            ShowItemMenu(item, screen);
        }
        else
        {
            ShowAppMenu(screen);
        }
    }

    void App::OpenRun()
    {
        if (runOpen_)
        {
            SetForegroundWindow(hwnd_);
            return;
        }
        if (HiddenMode() && !summoned_)
        {
            Summon();
        }
        runOpen_ = true;
        shrinkPending_ = false;
        hover_ = -1;
        scene_.SetHover(-1);
        scene_.SetPinned(-1);
        edit_.Clear();
        pendingHigh_ = 0;

        catalog_.Refresh(hwnd_, CatalogMessage);
        BuildCandidates();
        scene_.SetRunOpen(true);
        PushInput(true);

        PlaceWindow();
        Activate();
        UpdateIme();
        Kick();
    }

    // In the center and cursor modes, the toolbar hides when the run box closes.
    void App::CloseRun(bool restoreFocus)
    {
        if (!runOpen_)
        {
            return;
        }
        runOpen_ = false;
        selectingText_ = false;
        pressResult_ = -1;
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }
        scene_.SetRunOpen(false);
        shrinkPending_ = true;
        if (summoned_)
        {
            Dismiss(restoreFocus);
            return;
        }
        Deactivate(restoreFocus);
        RefreshHoverFromCursor();
        Kick();
    }

    // The window takes the keyboard focus while the run box is open or while a
    // hotkey shows the toolbar. At other times, clicks do not activate it.
    void App::Activate()
    {
        if (!previousForeground_)
        {
            HWND fg = GetForegroundWindow();
            previousForeground_ = fg != hwnd_ ? fg : nullptr;
        }
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
    }

    void App::Deactivate(bool restoreFocus)
    {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
        if (restoreFocus && GetForegroundWindow() == hwnd_ && previousForeground_ && IsWindow(previousForeground_))
        {
            SetForegroundWindow(previousForeground_);
        }
        previousForeground_ = nullptr;
    }

    bool App::HiddenMode() const
    {
        return config_.mode == PositionMode::Center || config_.mode == PositionMode::Cursor;
    }

    // Persistent modes show the toolbar at all times. Hidden modes show it
    // only after the hotkey.
    void App::UpdateVisibility()
    {
        if (!HiddenMode())
        {
            summoned_ = false;
            hidePending_ = false;
            scene_.SnapVisible(true);
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        }
        else if (!summoned_)
        {
            scene_.SnapVisible(false);
            ShowWindow(hwnd_, SW_HIDE);
        }
        Kick();
    }

    void App::Summon()
    {
        if (summoned_)
        {
            return;
        }
        summoned_ = true;
        hidePending_ = false;
        GetCursorPos(&summonCursor_);
        hover_ = -1;
        scene_.SetHover(-1);
        scene_.SetVisible(true, config_.mode == PositionMode::Cursor);
        PlaceWindow();
        UpdateRegion();
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        Activate();
        Kick();
    }

    void App::Dismiss(bool restoreFocus)
    {
        if (!summoned_)
        {
            return;
        }
        if (runOpen_)
        {
            // CloseRun calls Dismiss again after it closes the run box.
            CloseRun(restoreFocus);
            return;
        }
        summoned_ = false;
        Deactivate(restoreFocus);
        hover_ = -1;
        scene_.SetHover(-1);
        scene_.SetOrbHover(false);
        scene_.SetVisible(false, config_.mode == PositionMode::Cursor);
        hidePending_ = true;
        Kick();
    }

    // Center mode: the toolbar center is at 42% of the work area height, like
    // a dialog. Cursor mode: the orb is just below and right of the pointer.
    // Both modes use the monitor of the pointer and keep the run results in
    // the work area.
    void App::SummonPosition(int& x, int& y)
    {
        HMONITOR mon = MonitorFromPoint(summonCursor_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        const RECT wa = mi.rcWork;
        const RECT track = scene_.RestTrackRect();
        const int trackW = track.right - track.left;
        const int margin = static_cast<int>(std::lround(8 * scale_));
        const int fullH = static_cast<int>(std::ceil(Scene::RunContentHeight() * scale_));

        int trackLeft, top;
        if (config_.mode == PositionMode::Center)
        {
            trackLeft = (wa.left + wa.right) / 2 - trackW / 2;
            top = wa.top + static_cast<int>((wa.bottom - wa.top) * 0.42f) - static_cast<int>(std::lround(BarHeight * scale_ / 2));
        }
        else
        {
            trackLeft = summonCursor_.x - static_cast<int>(std::lround(24 * scale_));
            top = summonCursor_.y + static_cast<int>(std::lround(18 * scale_));
        }
        const int left = wa.left + margin, right = wa.right - margin, topEdge = wa.top + margin, bottom = wa.bottom - margin;
        trackLeft = std::clamp(trackLeft, left, std::max(left, right - trackW));
        top = std::clamp(top, topEdge, std::max(topEdge, bottom - fullH));
        x = trackLeft - track.left;
        y = top;
    }

    // The candidates are the toolbar apps and the apps in the Start menu.
    void App::BuildCandidates()
    {
        candidates_.clear();
        sources_.clear();
        std::unordered_set<std::wstring> seen;

        for (int i = 0; i < static_cast<int>(config_.items.size()); ++i)
        {
            const Item& a = config_.items[i];
            if (a.IsWidget())
            {
                continue;
            }
            Candidate c;
            c.name = a.name;
            c.path = a.path;
            c.display = DisplayPath(a.path);
            c.onToolbar = true;
            PrepareCandidate(c);
            seen.insert(ToLower(ExpandPath(a.path)));
            candidates_.push_back(std::move(c));
            sources_.push_back({ i, {} });
        }

        for (auto& e : catalog_.Entries())
        {
            if (!seen.insert(ToLower(e.path)).second)
            {
                continue;
            }
            Candidate c;
            c.name = e.name;
            c.path = e.path;
            c.display = e.display;
            PrepareCandidate(c);
            candidates_.push_back(std::move(c));
            sources_.push_back({ -1, e });
        }
    }

    winrt::com_ptr<IWICBitmapSource> App::ResultIcon(const std::wstring& path)
    {
        if (auto it = resultIcons_.find(path); it != resultIcons_.end())
        {
            return it->second;
        }
        auto bmp = LoadAppIcon(wic_.get(), path, ResultIconPx).bitmap;
        resultIcons_.emplace(path, bmp);
        return bmp;
    }

    void App::UpdateResults()
    {
        const std::wstring query = Trim(edit_.Text());
        std::vector<ResultRow> rows;
        targets_.clear();

        auto addSource = [&](size_t index, int hlStart, int hlLength) {
            const Candidate& c = candidates_[index];
            rows.push_back({ ResultIcon(c.path), c.name, c.display, hlStart, hlLength, false });
            targets_.push_back({ static_cast<int>(index), false });
        };
        auto addCommand = [&] {
            rows.push_back({ nullptr, strings::RunPrefix + query, L"", -1, 0, true });
            targets_.push_back({ -1, true });
        };

        if (query.empty())
        {
            for (size_t i = 0; i < sources_.size() && rows.size() < EmptyQueryRows; ++i)
            {
                if (sources_[i].item >= 0)
                {
                    addSource(i, -1, 0);
                }
            }
        }
        else
        {
            const auto matches = Rank(candidates_, query, ResultsMaxRows - 1);
            // Text that looks like a path, or a weak match, runs as a command first.
            const bool pathLike = query.find_first_of(L"\\/:.\"%") != std::wstring::npos;
            const bool commandFirst = matches.empty() || pathLike || matches[0].score < 60;
            if (commandFirst)
            {
                addCommand();
            }
            for (auto& m : matches)
            {
                addSource(m.index, m.highlightStart, m.highlightLength);
            }
            if (!commandFirst)
            {
                addCommand();
            }
        }

        selected_ = 0;
        scene_.SetResults(std::move(rows), selected_);
        Kick();
    }

    void App::PushInput(bool textChanged)
    {
        scene_.SetInput(edit_.Text(), edit_.Caret(), edit_.SelectionStart(), edit_.SelectionEnd());
        if (textChanged)
        {
            UpdateResults();
        }
        UpdateIme();
        Kick();
    }

    // The system IME window shows the composition next to the caret.
    void App::UpdateIme()
    {
        HIMC imc = ImmGetContext(hwnd_);
        if (!imc)
        {
            return;
        }
        const POINT caret = scene_.CaretClientPx();
        COMPOSITIONFORM cf{ CFS_POINT, caret, {} };
        ImmSetCompositionWindow(imc, &cf);
        CANDIDATEFORM cand{ 0, CFS_CANDIDATEPOS, { caret.x, caret.y + scene_.CaretHeightPx() }, {} };
        ImmSetCandidateWindow(imc, &cand);
        ImmReleaseContext(hwnd_, imc);
    }

    void App::RunSelected(bool admin)
    {
        if (selected_ < 0 || selected_ >= static_cast<int>(targets_.size()))
        {
            return;
        }
        const RunTarget t = targets_[selected_];
        if (t.command)
        {
            const CommandLine cmd = ParseCommand(Trim(edit_.Text()), PathExists);
            if (cmd.file.empty() || !RunCommand(hwnd_, cmd.file, cmd.params, admin))
            {
                return;
            }
            CloseRun(false);
            return;
        }

        const RunSource& src = sources_[t.source];
        Item item;
        if (src.item >= 0)
        {
            item = config_.items[src.item];
        }
        else
        {
            item.name = src.entry.name;
            item.path = src.entry.path;
        }
        CloseRun(false);
        LaunchItem(hwnd_, item, admin || item.admin);
    }

    void App::MoveSelection(int delta)
    {
        const int n = static_cast<int>(targets_.size());
        if (n == 0)
        {
            return;
        }
        selected_ = (selected_ + delta + n) % n;
        scene_.SetSelected(selected_);
        Kick();
    }

    bool App::OnRunKey(UINT vk)
    {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        switch (vk)
        {
        case VK_ESCAPE:
            CloseRun(true);
            return true;
        case VK_RETURN:
            RunSelected(ctrl && shift);
            return true;
        case VK_UP:
            MoveSelection(-1);
            return true;
        case VK_DOWN:
            MoveSelection(1);
            return true;
        case VK_TAB:
            MoveSelection(shift ? -1 : 1);
            return true;
        default:
            break;
        }

        if (ctrl && (vk == 'C' || vk == 'X'))
        {
            if (edit_.HasSelection())
            {
                CopyToClipboard(hwnd_, edit_.Selected());
                if (vk == 'X')
                {
                    edit_.DeleteSelection();
                    PushInput(true);
                }
            }
            return true;
        }
        if (ctrl && vk == 'V')
        {
            edit_.Insert(ReadClipboard(hwnd_));
            PushInput(true);
            return true;
        }

        const std::wstring before = edit_.Text();
        if (edit_.Key(vk, ctrl, shift))
        {
            PushInput(edit_.Text() != before);
            return true;
        }
        return false;
    }

    void App::OnRunChar(wchar_t ch)
    {
        if (IS_HIGH_SURROGATE(ch))
        {
            pendingHigh_ = ch;
            return;
        }
        std::wstring s;
        if (IS_LOW_SURROGATE(ch))
        {
            if (!pendingHigh_)
            {
                return;
            }
            s = { pendingHigh_, ch };
        }
        else if (ch >= 0x20 && ch != 0x7F)
        {
            s = ch;
        }
        pendingHigh_ = 0;
        if (!s.empty())
        {
            edit_.Insert(s);
            PushInput(true);
        }
    }

    void App::OnRunMouseMove(POINT p)
    {
        if (selectingText_)
        {
            const int pos = scene_.HitTestInput(p);
            if (pos >= 0)
            {
                edit_.SetCaret(static_cast<size_t>(pos), true);
                PushInput(false);
            }
            return;
        }
        const int r = scene_.HitTestResult(p);
        if (r >= 0 && r != selected_)
        {
            selected_ = r;
            scene_.SetSelected(r);
        }
        scene_.SetOrbHover(scene_.HitTestOrb(p));
        Kick();
    }

    void App::OnRunLeftDown(POINT p)
    {
        pressResult_ = -1;
        pressOrb_ = false;
        if (const int pos = scene_.HitTestInput(p); pos >= 0)
        {
            SetCapture(hwnd_);
            selectingText_ = true;
            edit_.SetCaret(static_cast<size_t>(pos), GetKeyState(VK_SHIFT) < 0);
            PushInput(false);
            return;
        }
        if (const int r = scene_.HitTestResult(p); r >= 0)
        {
            SetCapture(hwnd_);
            pressResult_ = r;
            return;
        }
        if (scene_.HitTestOrb(p))
        {
            SetCapture(hwnd_);
            pressOrb_ = true;
            return;
        }
        if (!scene_.HitTestTrack(p))
        {
            CloseRun(true);
        }
    }

    void App::OnRunLeftUp(POINT p)
    {
        const int r = pressResult_;
        const bool orb = pressOrb_;
        selectingText_ = false;
        pressResult_ = -1;
        pressOrb_ = false;
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }
        if (r >= 0 && r == scene_.HitTestResult(p))
        {
            selected_ = r;
            RunSelected(GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0);
        }
        else if (orb && scene_.HitTestOrb(p))
        {
            CloseRun(true);
        }
    }

    UINT App::ShowMenu(HMENU menu, POINT screen)
    {
        menuOpen_ = true;
        Kick();
        // The menu runs a modal loop. A timer keeps the animation running.
        SetTimer(hwnd_, MenuTimer, USER_TIMER_MINIMUM, nullptr);
        SetForegroundWindow(hwnd_);
        const UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screen.x, screen.y, hwnd_, nullptr);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        KillTimer(hwnd_, MenuTimer);
        menuOpen_ = false;
        return cmd;
    }

    void App::ShowItemMenu(int index, POINT screen)
    {
        const Item item = config_.items[index];
        if (item.IsWidget())
        {
            ShowWidgetMenu(index, screen);
            return;
        }
        const bool shell = IsShellPath(item.path);

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, CmdOpen, strings::Open);
        AppendMenuW(menu, MF_STRING | (shell ? MF_GRAYED : 0), CmdRunAsAdmin, strings::RunAsAdmin);
        AppendMenuW(menu, MF_STRING | (shell ? MF_GRAYED : 0), CmdOpenLocation, strings::OpenLocation);
        AppendMenuW(menu, MF_STRING, CmdCopyPath, strings::CopyPath);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdRemove, strings::Remove);
        SetMenuDefaultItem(menu, CmdOpen, FALSE);

        scene_.SetPinned(index);
        const UINT cmd = ShowMenu(menu, screen);
        DestroyMenu(menu);
        scene_.SetPinned(-1);
        RefreshHoverFromCursor();

        switch (cmd)
        {
        case CmdOpen:
            OpenItem(index, false);
            break;
        case CmdRunAsAdmin:
            OpenItem(index, true);
            break;
        case CmdOpenLocation:
            OpenFileLocation(item.path);
            break;
        case CmdCopyPath:
            CopyToClipboard(hwnd_, item.path);
            break;
        case CmdRemove:
            config_.items.erase(config_.items.begin() + index);
            SaveConfig();
            LoadItems();
            PlaceWindow();
            break;
        default:
            break;
        }
    }

    void App::OpenItem(int index, bool admin)
    {
        if (index < 0 || index >= static_cast<int>(config_.items.size()))
        {
            return;
        }
        const Item& item = config_.items[index];
        if (item.IsWidget())
        {
            OpenTarget(item.click.empty() ? Widgets::DefaultClick(*item.widget) : item.click);
        }
        else
        {
            LaunchItem(hwnd_, item, admin || item.admin);
        }
        Dismiss(false);
    }

    void App::ShowWidgetMenu(int index, POINT screen)
    {
        const Item item = config_.items[index];
        const bool canOpen = !item.click.empty() || !Widgets::DefaultClick(*item.widget).empty();

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (canOpen ? 0 : MF_GRAYED), CmdOpen, strings::Open);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdRemove, strings::Remove);
        if (canOpen)
        {
            SetMenuDefaultItem(menu, CmdOpen, FALSE);
        }

        scene_.SetPinned(index);
        const UINT cmd = ShowMenu(menu, screen);
        DestroyMenu(menu);
        scene_.SetPinned(-1);
        RefreshHoverFromCursor();

        if (cmd == CmdOpen)
        {
            OpenItem(index, false);
        }
        else if (cmd == CmdRemove)
        {
            config_.items.erase(config_.items.begin() + index);
            SaveConfig();
            LoadItems();
            PlaceWindow();
        }
    }

    void App::ApplyWidgetText(const std::vector<int>& changed)
    {
        bool any = false;
        for (int index : changed)
        {
            const WidgetText& t = widgets_.Text(index);
            any |= scene_.SetWidgetText(index, t.brief, t.detail);
        }
        if (any)
        {
            regionDirty_ = true;
            FitWindowWidth();
            Kick();
        }
    }

    // The clock changes at the start of each second, so the timer fires just after it.
    void App::ArmWidgetTimer()
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        SetTimer(hwnd_, WidgetTimer, 1000 - t.wMilliseconds + 15, nullptr);
    }

    // Widget text changes the widest layout of the toolbar. The window grows
    // at once. It shrinks only while the user does not use the toolbar.
    void App::FitWindowWidth()
    {
        if (!IsWindowVisible(hwnd_))
        {
            return;
        }
        RECT rc;
        GetWindowRect(hwnd_, &rc);
        const int current = rc.right - rc.left;
        const int needed = static_cast<int>(std::ceil(scene_.WindowWidth() * scale_));
        const bool idle = hover_ < 0 && !runOpen_ && !pressing_ && !menuOpen_;
        if (needed > current || (needed < current && idle))
        {
            PlaceWindow();
        }
    }

    void App::ShowAppMenu(POINT screen)
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, CmdSettings, strings::Settings);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdExit, strings::Exit);
        const UINT cmd = ShowMenu(menu, screen);
        DestroyMenu(menu);
        RefreshHoverFromCursor();

        if (cmd == CmdSettings)
        {
            OpenInNotepad(Config::FilePath());
        }
        else if (cmd == CmdExit)
        {
            DestroyWindow(hwnd_);
        }
    }

    void App::AddTrayIcon()
    {
        NOTIFYICONDATAW nid{ sizeof(nid) };
        nid.hWnd = hwnd_;
        nid.uID = TrayId;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = TrayMessage;
        nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(nid.szTip, strings::AppName);
        Shell_NotifyIconW(NIM_ADD, &nid);
    }

    void App::RemoveTrayIcon()
    {
        NOTIFYICONDATAW nid{ sizeof(nid) };
        nid.hWnd = hwnd_;
        nid.uID = TrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }

    LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            static_cast<App*>(cs->lpCreateParams)->hwnd_ = hwnd;
        }
        if (auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        {
            return app->Handle(msg, wp, lp);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT App::Handle(UINT msg, WPARAM wp, LPARAM lp)
    {
        const POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        switch (msg)
        {
        case WM_MOUSEACTIVATE:
            return (runOpen_ || summoned_) ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !menuOpen_)
            {
                if (runOpen_)
                {
                    CloseRun(false);
                }
                else if (summoned_)
                {
                    Dismiss(false);
                }
            }
            return 0;
        case WM_HOTKEY:
            if (wp == HotkeyRun)
            {
                if (runOpen_ && GetForegroundWindow() == hwnd_)
                {
                    CloseRun(true);
                }
                else
                {
                    OpenRun();
                }
            }
            else if (wp == HotkeyToggle && HiddenMode())
            {
                if (summoned_)
                {
                    Dismiss(true);
                }
                else
                {
                    Summon();
                }
            }
            return 0;
        case WM_KEYDOWN:
            if (runOpen_ && OnRunKey(static_cast<UINT>(wp)))
            {
                return 0;
            }
            if (!runOpen_ && summoned_ && wp == VK_ESCAPE)
            {
                Dismiss(true);
                return 0;
            }
            break;
        case WM_CHAR:
            if (runOpen_)
            {
                OnRunChar(static_cast<wchar_t>(wp));
                return 0;
            }
            break;
        case WM_IME_STARTCOMPOSITION:
            UpdateIme();
            break;
        case CatalogMessage:
            if (runOpen_)
            {
                BuildCandidates();
                if (!Trim(edit_.Text()).empty())
                {
                    UpdateResults();
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(p);
            return 0;
        case WM_MOUSELEAVE:
            OnMouseLeave();
            return 0;
        case WM_LBUTTONDOWN:
            OnLeftDown(p);
            return 0;
        case WM_LBUTTONUP:
            OnLeftUp(p);
            return 0;
        case WM_RBUTTONUP:
            OnRightUp(p);
            return 0;
        case WM_CAPTURECHANGED:
            if (pressing_)
            {
                EndPress();
            }
            selectingText_ = false;
            return 0;
        case WM_SETCURSOR:
            if (dragging_)
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            if (runOpen_ && LOWORD(lp) == HTCLIENT)
            {
                POINT c;
                GetCursorPos(&c);
                ScreenToClient(hwnd_, &c);
                if (selectingText_ || scene_.HitTestInput(c) >= 0)
                {
                    SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                    return TRUE;
                }
            }
            break;
        case WM_TIMER:
            if (wp == MenuTimer && animating_)
            {
                Frame();
            }
            else if (wp == ConfigRetryTimer)
            {
                KillTimer(hwnd_, ConfigRetryTimer);
                OnConfigFileChanged();
            }
            else if (wp == WidgetTimer)
            {
                ApplyWidgetText(widgets_.Update(true));
                ArmWidgetTimer();
            }
            return 0;
        case WidgetMessage:
            ApplyWidgetText(widgets_.Update(false));
            return 0;
        case WM_DPICHANGED:
            scale_ = HIWORD(wp) / 96.f;
            scene_.SetScale(scale_);
            PlaceWindow();
            return 0;
        case WM_DISPLAYCHANGE:
            PlaceWindow();
            return 0;
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETWORKAREA)
            {
                PlaceWindow();
            }
            return 0;
        case TrayMessage:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU)
            {
                POINT c;
                GetCursorPos(&c);
                ShowAppMenu(c);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            if (msg == taskbarCreated_ && taskbarCreated_ != 0)
            {
                AddTrayIcon();
                return 0;
            }
            break;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}
