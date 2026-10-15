#pragma once

#include "Config.h"
#include "Paint.h"
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

    // Owns the Composition visual tree of the toolbar.
    // The layout runs on the CPU with springs. Apply() writes the current
    // values to the visuals. Hit tests use the same current values.
    // The look follows the CSS of the prototype: lengths are CSS px (DIPs),
    // shadows are CSS box-shadows, and timing follows CSS transitions.
    // The window has a margin on each side for the shadows. Client
    // coordinates include this margin.
    class Scene
    {
    public:
        void Initialize(HWND hwnd, float scale);
        void SetScale(float scale);
        void SetAppearance(Theme theme, Material material, CollapsedStyle style, SpringParams spring);
        void SetItems(std::vector<SceneItem> items);
        void SetWindowWidth(float dip) { windowWidth_ = dip; }
        void SetWidgetsCollapse(bool collapse) { widgetsCollapse_ = collapse; }
        void SetFlushTop(bool flush) { flushTop_ = flush; }  // Square top corners, for the screen top.
        // Returns true if the text changed.
        bool SetWidgetText(int index, const std::wstring& brief, const std::wstring& detail);

        void SetHover(int index) { hover_ = index; }
        void SetPinned(int index) { pinned_ = index; }
        void SetPressed(int index);
        void SetOrbHover(bool hover) { orbHover_ = hover; }

        // Shows or hides the whole toolbar with a scale and fade animation.
        // The scale origin is near the orb when "fromCursor" is true, else the toolbar center.
        void SetVisible(bool visible, bool fromCursor);
        void SnapVisible(bool visible);
        bool VisibleNow() const { return visible_ || vis_.x > 0.003f; }

        void SetRunOpen(bool open);
        bool RunVisible() const { return runOpen_ || run_.x > 0.002f; }
        void SetInput(const std::wstring& text, size_t caret, size_t selStart, size_t selEnd);
        void SetResults(std::vector<ResultRow> rows, int selected);
        void SetSelected(int selected);

        // Advances the springs and transitions. Returns true while something still moves.
        bool Step(float dt);
        void Apply();

        float WindowWidth() const;   // DIPs
        float WindowHeight() const;  // DIPs
        static float TopMargin();    // DIPs above the toolbar, for the shadows.
        static float RunContentHeight();  // DIPs from the toolbar top to the bottom of the results.
        RECT RestTrackRect() const;  // Client pixels.
        RECT RestRegion() const;  // Client pixels: the toolbar and its visible shadow at rest.
        int HitTestItem(POINT clientPx) const;
        bool HitTestOrb(POINT clientPx) const;
        bool HitTestTrack(POINT clientPx) const;
        int HitTestResult(POINT clientPx) const;
        int HitTestInput(POINT clientPx) const;  // Text position, or -1.
        POINT CaretClientPx() const;
        int CaretHeightPx() const;

    private:
        // A surface that is drawn again only when its key changes.
        struct Drawn
        {
            winrt::Windows::UI::Composition::SpriteVisual sprite{ nullptr };
            winrt::Windows::UI::Composition::CompositionSurfaceBrush brush{ nullptr };
            winrt::Windows::UI::Composition::CompositionDrawingSurface surface{ nullptr };
            std::vector<float> key;
        };

        // One outer CSS box-shadow of a rounded box, drawn by the compositor.
        // The sprite has no content. The shadow takes its shape from a mask.
        struct Caster
        {
            winrt::Windows::UI::Composition::SpriteVisual sprite{ nullptr };
            winrt::Windows::UI::Composition::DropShadow shadow{ nullptr };
            winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry geom{ nullptr };
            winrt::Windows::UI::Composition::ShapeVisual shape{ nullptr };
            winrt::Windows::UI::Composition::CompositionVisualSurface surface{ nullptr };
        };

        struct ItemVisual
        {
            SceneItem src;
            winrt::Windows::UI::Composition::ContainerVisual root{ nullptr };  // Slot. Clips while the run box is open.
            winrt::Windows::UI::Composition::InsetClip slotClip{ nullptr };
            Spring w, h, tone, press;
            bool fresh = true;

            // Apps: Composition visuals only, so that an animation frame only
            // writes sizes and colors. 'art' has the box size and the press scale.
            winrt::Windows::UI::Composition::ContainerVisual art{ nullptr };
            Caster casters[4];  // Outer shadows. Index 0 is the lowest.
            winrt::Windows::UI::Composition::SpriteVisual highlight{ nullptr };  // inset 0 1px 0: shows above 'cover'.
            winrt::Windows::UI::Composition::CompositionColorBrush highlightBrush{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual cover{ nullptr };  // The plate, 1 px lower.
            winrt::Windows::UI::Composition::CompositionColorBrush coverBrush{ nullptr };
            winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry coverClip{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual icon{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual dim{ nullptr };  // CSS brightness(): black, masked by the icon.
            winrt::Windows::UI::Composition::CompositionDrawingSurface iconSurface{ nullptr };

            // Widgets.
            winrt::Windows::UI::Composition::ContainerVisual box{ nullptr };
            winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry clip{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual plate{ nullptr };
            winrt::Windows::UI::Composition::CompositionColorBrush plateBrush{ nullptr };
            winrt::Windows::UI::Composition::ShapeVisual ring{ nullptr };
            winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry ringGeom{ nullptr };
            winrt::Windows::UI::Composition::CompositionColorBrush ringBrush{ nullptr };
            winrt::Windows::UI::Composition::CompositionSpriteShape ringShape{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual label{ nullptr };
            winrt::Windows::UI::Composition::CompositionSurfaceBrush labelBrush{ nullptr };
            winrt::Windows::UI::Composition::CompositionDrawingSurface briefSurface{ nullptr };
            winrt::Windows::UI::Composition::CompositionDrawingSurface detailSurface{ nullptr };
            std::wstring brief, detail;
            float briefW = 0.f, detailW = 0.f;
            bool chipInk = true;
            bool showingDetail = false;
        };

        // A rounded panel with the backdrop, the tint, a border, and a shadow.
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
            winrt::Windows::UI::Composition::CompositionSpriteShape borderShape{ nullptr };
            winrt::Windows::UI::Composition::SpriteVisual topLine{ nullptr };
            Caster shadows[2];
            float saturate = 1.f;
        };

        struct PanelStyle
        {
            winrt::Windows::UI::Color tint;
            winrt::Windows::UI::Color border;
            ShadowList shadows;
            float radius;
        };

        void CreateDevice();
        void CreateTextFormats();
        winrt::com_ptr<IDWriteTextFormat> MakeFormat(const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size,
                                                    float lineHeight, bool trim);
        Drawn CreateDrawn();
        Caster CreateCaster();
        // Box and radius in DIPs, in the coordinates of the parent of the caster.
        void SetCaster(Caster& c, float x, float y, float w, float h, float radius, const Shadow& s);
        Panel CreatePanel(float saturate);
        void LayoutPanel(Panel& p, float x, float y, float w, float h, const PanelStyle& style, bool flushTop = false);
        winrt::Windows::UI::Composition::CompositionBrush BackdropBrush(float saturate);
        template <class F>
        void Paint(winrt::Windows::UI::Composition::CompositionDrawingSurface& surface, int w, int h, F&& paint);
        template <class F>
        void Redraw(Drawn& d, float wDip, float hDip, std::vector<float> key, F&& paint);
        ID2D1Bitmap* BitmapFor(IWICBitmapSource* src, ID2D1DeviceContext* dc);
        void RenderItem(ItemVisual& v, float cx, float cy, float bh);
        void RenderOrb(float cx, float cy, float hover, float pulse);
        void RenderIcon(ItemVisual& v);
        void RenderLabel(ItemVisual& v, bool detail);
        void RenderInput();
        void RenderResults();
        void RestartCaret();
        void Rerender();

        void Targets(int index, int hovered, float& w, float& h, float& tone) const;
        float TrackWidth() const;
        float TrackWidthFor(int hovered) const;
        float TrackLeft() const;  // DIPs, from the window left.
        float ResultsWidth() const;
        float ResultsHeight() const;
        float RowTop(int index) const;
        float RowHeight(int index) const;

        float scale_ = 1.f;
        float windowWidth_ = 0.f;
        Theme theme_ = Theme::Dark;
        Material material_ = Material::Acrylic;
        CollapsedStyle style_ = CollapsedStyle::Slice;
        bool widgetsCollapse_ = false;
        bool flushTop_ = false;
        SpringParams spring_;
        int hover_ = -1;
        int pinned_ = -1;
        bool orbHover_ = false;
        bool runOpen_ = false;
        bool visible_ = true;
        bool scaleFromCursor_ = false;
        Spring barHeight_;
        Spring run_;
        Spring vis_;

        // CSS transitions of the orb, and its pulse while the run box is open.
        double time_ = 0;
        double runOpenedAt_ = 0;
        Transition orbScale_{ 0.28f, 0.3f, 1.6f, 0.5f, 1.f, 1.f };
        Transition orbGlow_{ 0.25f, 0.25f, 0.1f, 0.25f, 1.f, 0.f };

        std::wstring inputText_;
        size_t caret_ = 0, selStart_ = 0, selEnd_ = 0;
        float inputScroll_ = 0.f;
        float caretX_ = 0.f;
        float inputLineHeight_ = 0.f;  // CSS "normal" line height of the input font.
        std::vector<ResultRow> rows_;
        int selected_ = 0;

        winrt::Windows::System::DispatcherQueueController queue_{ nullptr };
        winrt::Windows::UI::Composition::Compositor compositor_{ nullptr };
        winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target_{ nullptr };
        winrt::Windows::UI::Composition::CompositionGraphicsDevice graphics_{ nullptr };
        winrt::com_ptr<ID2D1Factory1> d2dFactory_;
        winrt::com_ptr<ID2D1Device> d2dDevice_;
        winrt::com_ptr<ID2D1DeviceContext> helper_;  // Records shadow shapes.
        winrt::com_ptr<IDWriteFactory> dwrite_;
        winrt::com_ptr<IDWriteTextFormat> inputFormat_;
        winrt::com_ptr<IDWriteTextFormat> titleFormat_;
        winrt::com_ptr<IDWriteTextFormat> subtitleFormat_;
        winrt::com_ptr<IDWriteTextFormat> keyFormat_;
        winrt::com_ptr<IDWriteTextFormat> widgetFormat_;
        winrt::com_ptr<IDWriteTextFormat> orbFormat_;
        winrt::com_ptr<IDWriteTypography> tabular_;
        winrt::com_ptr<IDWriteTextLayout> inputLayout_;

        // The key stays valid because the entry holds a reference to the source.
        struct CachedBitmap
        {
            winrt::com_ptr<IWICBitmapSource> source;
            winrt::com_ptr<ID2D1Bitmap> bitmap;
        };
        std::unordered_map<IWICBitmapSource*, CachedBitmap> bitmaps_;

        winrt::Windows::UI::Composition::ContainerVisual root_{ nullptr };
        winrt::Windows::UI::Composition::ContainerVisual stage_{ nullptr };  // Moved down by the top margin.
        Panel track_;
        winrt::Windows::UI::Composition::ContainerVisual itemsLayer_{ nullptr };
        Drawn orb_;

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
    };
}
