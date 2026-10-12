#include "pch.h"
#include "App.h"
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
        constexpr UINT TrayId = 1;
        constexpr UINT_PTR MenuTimer = 1;
        constexpr UINT IconSourcePx = 128;

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

        config_ = Config::Load(configText_);
        ApplyConfig();

        const auto dir = Config::FilePath().parent_path();
        configWatch_ = FindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);

        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        AddTrayIcon();
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
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
    }

    void App::LoadItems()
    {
        std::vector<SceneItem> items;
        items.reserve(config_.items.size());
        for (auto& a : config_.items)
        {
            IconData icon = LoadAppIcon(wic_.get(), a.path, IconSourcePx);
            items.push_back({ icon.bitmap, icon.main, a.style });
        }
        scene_.SetItems(std::move(items));
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
        const int w = static_cast<int>(std::ceil(scene_.MaxTrackWidth() * scale_));
        const int h = static_cast<int>(std::ceil(BarHeightHover * scale_));
        scene_.SetWindowWidth(w / scale_);

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
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        regionDirty_ = true;
        Kick();
    }

    void App::MoveWindowToAnchor()
    {
        RECT rc;
        GetWindowRect(hwnd_, &rc);
        const int w = rc.right - rc.left;
        SetWindowPos(hwnd_, HWND_TOPMOST, anchor_.x - w / 2, anchor_.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
    }

    void App::SaveConfig()
    {
        configText_ = config_.Save();
    }

    void App::OnConfigFileChanged()
    {
        std::string text;
        Config next = Config::Load(text);
        if (text == configText_)
        {
            return;
        }
        configText_ = std::move(text);
        config_ = std::move(next);
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
    }

    // The window is wider than the toolbar at rest. Outside the toolbar,
    // the window must not take mouse input, so at rest the window region
    // is the toolbar shape. While the toolbar is open or moves, the region
    // is the full window.
    void App::UpdateRegion()
    {
        const bool full = hover_ >= 0 || menuOpen_ || pressing_ || animating_;
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
            const RECT r = scene_.RestTrackRect();
            const int d = static_cast<int>(std::lround(scene_.RadiusPx() * 2));
            SetWindowRgn(hwnd_, CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, d, d), FALSE);
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
            const AppItem& a = config_.items[item];
            LaunchItem(hwnd_, a, a.admin);
        }
        else if (orb && scene_.HitTestOrb(p))
        {
            ShowRunDialog();
        }
    }

    void App::OnRightUp(POINT p)
    {
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
        const AppItem item = config_.items[index];
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
            LaunchItem(hwnd_, item, false);
            break;
        case CmdRunAsAdmin:
            LaunchItem(hwnd_, item, true);
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
            return MA_NOACTIVATE;
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
            return 0;
        case WM_SETCURSOR:
            if (dragging_)
            {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (wp == MenuTimer && animating_)
            {
                Frame();
            }
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
