#pragma once

#include "Config.h"
#include "Scene.h"

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

        UINT ShowMenu(HMENU menu, POINT screen);
        void ShowItemMenu(int index, POINT screen);
        void ShowAppMenu(POINT screen);
        void AddTrayIcon();
        void RemoveTrayIcon();

        HINSTANCE instance_ = nullptr;
        HWND hwnd_ = nullptr;
        Scene scene_;
        Config config_;
        std::string configText_;
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
    };
}
