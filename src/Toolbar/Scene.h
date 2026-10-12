#pragma once

#include "Config.h"
#include "Spring.h"

namespace dm
{
    struct SceneItem
    {
        winrt::com_ptr<IWICBitmapSource> icon;
        Rgb main;
        std::optional<CollapsedStyle> style;
        bool widget = false;
        std::optional<Rgb> textColor;  // Widget text color in the dark theme.
    };

    struct ResultRow
    {
        winrt::com_ptr<IWICBitmapSource> icon;
        std::wstring title;
        std::wstring subtitle;
        int highlightStart = -1;
        int highlightLength = 0;
        bool command = false;
    };

    // Owns the Composition visual tree of the toolbar window.
    // The layout runs on the CPU with springs. Apply() writes the current
    // values to the visuals. Hit tests use the same current values.
    class Scene
    {
    public:
        void Initialize(HWND hwnd, float scale);
        void SetScale(float scale);
        void SetAppearance(Theme theme, Material material, CollapsedStyle style, SpringParams spring);
        void SetItems(std::vector<SceneItem> items);
        void SetWindowWidth(float dip) { windowWidth_ = dip; }
        void SetWidgetsCollapse(bool collapse) { widgetsCollapse_ = collapse; }
        // Returns true if the text changed.
        bool SetWidgetText(int index, const std::wstring& brief, const std::wstring& detail);

        void SetHover(int index) { hover_ = index; }
        void SetPinned(int index) { pinned_ = index; }
        void SetPressed(int index);
        void SetOrbHover(bool hover) { orbHover_ = hover; }

        // Shows or hides the whole toolbar with a scale and fade animation.
        // The scale origin is the orb when "fromCursor" is true, else the toolbar center.
        void SetVisible(bool visible, bool fromCursor);
        void SnapVisible(bool visible);
        bool VisibleNow() const { return visible_ || vis_.x > 0.003f; }
        static float RunWindowHeight();  // DIPs

        void SetRunOpen(bool open);
        bool RunVisible() const { return runOpen_ || run_.x > 0.002f; }
        void SetInput(const std::wstring& text, size_t caret, size_t selStart, size_t selEnd);
        void SetResults(std::vector<ResultRow> rows, int selected);
        void SetSelected(int selected);

        // Advances the springs. Returns true while a value still moves.
        bool Step(float dt);
        void Apply();

        float WindowWidth() const;   // DIPs
        float WindowHeight() const;  // DIPs
        RECT RestTrackRect() const;  // Client pixels.
        float RadiusPx() const;
        int HitTestItem(POINT clientPx) const;
        bool HitTestOrb(POINT clientPx) const;
        bool HitTestTrack(POINT clientPx) const;
        int HitTestResult(POINT clientPx) const;
        int HitTestInput(POINT clientPx) const;  // Text position, or -1.
        POINT CaretClientPx() const;
        int CaretHeightPx() const;

    private:
        struct ItemVisual
        {
            SceneItem src;
            winrt::Windows::UI::Composition::ContainerVisual root{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual shadow{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual glow{ nullptr };
            winrt::Windows::UI::Composition::ContainerVisual box{ nullptr };
            winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry clip{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual plate{ nullptr };
            winrt::Windows::UI::Composition::CompositionColorBrush plateBrush{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual icon{ nullptr };
            winrt::Windows::UI::Composition::CompositionSurfaceBrush iconBrush{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual dim{ nullptr };
            winrt::Windows::UI::Composition::ShapeVisual ring{ nullptr };
            winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry ringGeom{ nullptr };
            winrt::Windows::UI::Composition::CompositionColorBrush ringBrush{ nullptr };
            Spring w, h, tone, press;
            bool fresh = true;

            // Widgets only.
            winrt::Windows::UI::Composition::SpriteVisual label{ nullptr };
            winrt::Windows::UI::Composition::CompositionSurfaceBrush labelBrush{ nullptr };
            winrt::Windows::UI::Composition::CompositionDrawingSurface briefSurface{ nullptr };
            winrt::Windows::UI::Composition::CompositionDrawingSurface detailSurface{ nullptr };
            std::wstring brief, detail;
            float briefW = 0.f, detailW = 0.f;
            bool chipInk = true;
            bool showingDetail = false;
        };

        // A rounded panel with the backdrop, the tint, and a border.
        struct Panel
        {
            winrt::Windows::UI::Composition::ContainerVisual root{ nullptr };
            winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry clip{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual backdrop{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual tint{ nullptr };
            winrt::Windows::UI::Composition::CompositionColorBrush tintBrush{ nullptr };
            winrt::Windows::UI::Composition::ContainerVisual content{ nullptr };
            winrt::Windows::UI::Composition::ShapeVisual border{ nullptr };
            winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry borderGeom{ nullptr };
            winrt::Windows::UI::Composition::CompositionColorBrush borderBrush{ nullptr };
        };

        void CreateDevice();
        void CreateTextFormats();
        Panel CreatePanel();
        void LayoutPanel(Panel& p, float wPx, float hPx, float radiusPx);
        template <class F>
        void Paint(winrt::Windows::UI::Composition::CompositionDrawingSurface& surface, int w, int h, F&& paint);
        template <class F>
        winrt::Windows::UI::Composition::CompositionDrawingSurface Draw(int w, int h, F&& paint);
        ID2D1Bitmap* BitmapFor(IWICBitmapSource* src, ID2D1DeviceContext* dc);
        winrt::Windows::UI::Composition::CompositionDrawingSurface RenderIcon(IWICBitmapSource* icon);
        winrt::Windows::UI::Composition::CompositionBrush SoftBrush(Rgb color);
        void RenderOrb();
        void RenderLabel(ItemVisual& v, bool detail);
        void RenderInput();
        void RenderResults();
        void RestartCaret();

        void Targets(int index, int hovered, float& w, float& h, float& tone) const;
        float TrackWidth() const;
        float TrackWidthFor(int hovered) const;
        float ResultsWidth() const;
        float ResultsHeight() const;

        float scale_ = 1.f;
        float windowWidth_ = 0.f;
        Theme theme_ = Theme::Dark;
        Material material_ = Material::Acrylic;
        CollapsedStyle style_ = CollapsedStyle::Slice;
        bool widgetsCollapse_ = false;
        SpringParams spring_;
        int hover_ = -1;
        int pinned_ = -1;
        bool orbHover_ = false;
        bool runOpen_ = false;
        bool visible_ = true;
        bool scaleFromCursor_ = false;
        Spring vis_;
        Spring barHeight_;
        Spring orbScale_;
        Spring run_;

        std::wstring inputText_;
        size_t caret_ = 0, selStart_ = 0, selEnd_ = 0;
        float inputScroll_ = 0.f;
        float caretX_ = 0.f;
        std::vector<ResultRow> rows_;
        int selected_ = 0;

        winrt::Windows::System::DispatcherQueueController queue_{ nullptr };
        winrt::Windows::UI::Composition::Compositor compositor_{ nullptr };
        winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target_{ nullptr };
        winrt::Windows::UI::Composition::CompositionGraphicsDevice graphics_{ nullptr };
        winrt::com_ptr<ID2D1Factory1> d2dFactory_;
        winrt::com_ptr<ID2D1Device> d2dDevice_;
        winrt::com_ptr<ID2D1DeviceContext> offscreen_;
        winrt::com_ptr<IDWriteFactory> dwrite_;
        winrt::com_ptr<IDWriteTextFormat> inputFormat_;
        winrt::com_ptr<IDWriteTextFormat> titleFormat_;
        winrt::com_ptr<IDWriteTextFormat> subtitleFormat_;
        winrt::com_ptr<IDWriteTextFormat> glyphFormat_;
        winrt::com_ptr<IDWriteTextFormat> widgetFormat_;
        winrt::com_ptr<IDWriteTypography> tabular_;
        // The key stays valid because the entry holds a reference to the source.
        struct CachedBitmap
        {
            winrt::com_ptr<IWICBitmapSource> source;
            winrt::com_ptr<ID2D1Bitmap> bitmap;
        };
        std::unordered_map<IWICBitmapSource*, CachedBitmap> bitmaps_;
        winrt::com_ptr<IDWriteTextLayout> inputLayout_;

        winrt::Windows::UI::Composition::ContainerVisual root_{ nullptr };
        Panel track_;
        winrt::Windows::UI::Composition::ContainerVisual itemsLayer_{ nullptr };
        winrt::Windows::UI::Composition::SpriteVisual orb_{ nullptr };
        winrt::Windows::UI::Composition::CompositionSurfaceBrush orbBrush_{ nullptr };

        winrt::Windows::UI::Composition::ContainerVisual runBox_{ nullptr };
        winrt::Windows::UI::Composition::SpriteVisual input_{ nullptr };
        winrt::Windows::UI::Composition::CompositionSurfaceBrush inputBrush_{ nullptr };
        winrt::Windows::UI::Composition::CompositionDrawingSurface inputSurface_{ nullptr };
        winrt::Windows::UI::Composition::SpriteVisual caretVisual_{ nullptr };
        winrt::Windows::UI::Composition::CompositionColorBrush caretBrush_{ nullptr };
        winrt::Windows::UI::Composition::ScalarKeyFrameAnimation blink_{ nullptr };

        Panel results_;
        winrt::Windows::UI::Composition::SpriteVisual resultsContent_{ nullptr };
        winrt::Windows::UI::Composition::CompositionSurfaceBrush resultsBrush_{ nullptr };
        winrt::Windows::UI::Composition::CompositionDrawingSurface resultsSurface_{ nullptr };

        std::vector<ItemVisual> items_;
        std::unordered_map<uint32_t, winrt::Windows::UI::Composition::CompositionBrush> softBrushes_;
    };
}
