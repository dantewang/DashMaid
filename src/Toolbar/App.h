#pragma once

#include "Config.h"
#include "AppCatalog.h"
#include "Scene.h"
#include "Search.h"
#include "Widgets.h"
#include "TextEdit.h"

namespace dm
{
    class App
    {
    public:
        bool Initialize(HINSTANCE instance);
        int Run();

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);

        void ApplyConfig();
        void LoadItems();
        void PlaceWindow();
        void MoveWindowToAnchor();
        void ResetAnchor();
        void SaveConfig();
        void OnConfigFileChanged();

        void Kick();
        void Frame();
        void UpdateRegion();
        void RefreshHoverFromCursor();

        void OnMouseMove(POINT p);
        void OnMouseLeave();
        void OnLeftDown(POINT p);
        void OnLeftUp(POINT p);
        void OnRightUp(POINT p);
        void EndPress();

        void OpenRun();
        void CloseRun(bool restoreFocus);
        void Activate();
        void Deactivate(bool restoreFocus);
        bool HiddenMode() const;
        void UpdateVisibility();
        void Summon();
        void Dismiss(bool restoreFocus);
        void SummonPosition(int& x, int& y);
        void RegisterHotkeys();
        void BuildCandidates();
        void UpdateResults();
        void PushInput(bool textChanged);
        void UpdateIme();
        void RunSelected(bool admin);
        void MoveSelection(int delta);
        bool OnRunKey(UINT vk);
        void OnRunChar(wchar_t ch);
        void OnRunMouseMove(POINT p);
        void OnRunLeftDown(POINT p);
        void OnRunLeftUp(POINT p);
        winrt::com_ptr<IWICBitmapSource> ResultIcon(const std::wstring& path);

        UINT ShowMenu(HMENU menu, POINT screen);
        void ShowItemMenu(int index, POINT screen);
        void ShowAppMenu(POINT screen);
        void ShowWidgetMenu(int index, POINT screen);
        void OpenItem(int index, bool admin);
        void ApplyWidgetText(const std::vector<int>& changed);
        void ArmWidgetTimer();
        void FitWindowWidth();
        void AddTrayIcon();
        void RemoveTrayIcon();

        HINSTANCE instance_ = nullptr;
        HWND hwnd_ = nullptr;
        HWND visual_ = nullptr;  // Shows the content. Owned by hwnd_, so it is always above it.
        Scene scene_;
        Config config_;
        Widgets widgets_;
        std::string configText_;
        bool configBroken_ = false;  // The file is not valid. Do not overwrite it.
        winrt::com_ptr<IWICImagingFactory> wic_;
        HANDLE configWatch_ = INVALID_HANDLE_VALUE;
        UINT taskbarCreated_ = 0;

        float scale_ = 1.f;
        POINT anchor_{};  // Window center X and window top Y, screen pixels.

        bool animating_ = false;
        LARGE_INTEGER lastTick_{};
        LARGE_INTEGER frequency_{};

        int hover_ = -1;
        bool tracking_ = false;
        bool menuOpen_ = false;
        bool fullRegion_ = false;
        bool regionDirty_ = true;

        int pressItem_ = -1;
        bool pressOrb_ = false;
        bool pressing_ = false;
        bool dragging_ = false;
        POINT pressCursor_{};
        POINT pressAnchor_{};

        // Run box.
        struct RunSource
        {
            int item = -1;  // Toolbar item index, or -1 for a catalog entry.
            CatalogEntry entry;
        };
        struct RunTarget
        {
            int source = -1;
            bool command = false;
        };
        bool runOpen_ = false;
        bool shrinkPending_ = false;
        bool summoned_ = false;     // Center and cursor modes: the hotkey showed the toolbar.
        bool hidePending_ = false;  // Hide the window when the hide animation ends.
        POINT summonCursor_{};
        HWND previousForeground_ = nullptr;
        TextEdit edit_;
        wchar_t pendingHigh_ = 0;
        AppCatalog catalog_;
        std::vector<Candidate> candidates_;
        std::vector<RunSource> sources_;
        std::vector<RunTarget> targets_;
        int selected_ = 0;
        int pressResult_ = -1;
        bool selectingText_ = false;
        std::unordered_map<std::wstring, winrt::com_ptr<IWICBitmapSource>> resultIcons_;
    };
}
