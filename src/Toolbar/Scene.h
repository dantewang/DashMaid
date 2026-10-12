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

        void SetHover(int index) { hover_ = index; }
        void SetPinned(int index) { pinned_ = index; }
        void SetPressed(int index);
        void SetOrbHover(bool hover) { orbHover_ = hover; }

        // Advances the springs. Returns true while a value still moves.
        bool Step(float dt);
        void Apply();

        float MaxTrackWidth() const;
        RECT RestTrackRect() const;  // Client pixels.
        float RadiusPx() const;
        int HitTestItem(POINT clientPx) const;
        bool HitTestOrb(POINT clientPx) const;

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
        };

        void CreateDevice();
        template <class F>
        winrt::Windows::UI::Composition::CompositionDrawingSurface Draw(int w, int h, F&& paint);
        winrt::Windows::UI::Composition::CompositionDrawingSurface RenderIcon(IWICBitmapSource* icon);
        winrt::Windows::UI::Composition::CompositionBrush SoftBrush(Rgb color);
        void RenderOrb();
        void Targets(int index, int hovered, float& w, float& h, float& tone) const;
        float TrackWidth() const;
        float TrackWidthFor(int hovered) const;

        float scale_ = 1.f;
        float windowWidth_ = 0.f;
        Theme theme_ = Theme::Dark;
        Material material_ = Material::Acrylic;
        CollapsedStyle style_ = CollapsedStyle::Slice;
        SpringParams spring_;
        int hover_ = -1;
        int pinned_ = -1;
        bool orbHover_ = false;
        Spring barHeight_;
        Spring orbScale_;

        winrt::Windows::System::DispatcherQueueController queue_{ nullptr };
        winrt::Windows::UI::Composition::Compositor compositor_{ nullptr };
        winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target_{ nullptr };
        winrt::Windows::UI::Composition::CompositionGraphicsDevice graphics_{ nullptr };
        winrt::com_ptr<ID2D1Factory1> d2dFactory_;
        winrt::com_ptr<ID2D1Device> d2dDevice_;
        winrt::com_ptr<ID2D1DeviceContext> offscreen_;

        winrt::Windows::UI::Composition::ContainerVisual root_{ nullptr };
        winrt::Windows::UI::Composition::ContainerVisual track_{ nullptr };
        winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry trackClip_{ nullptr };
        winrt::Windows::UI::Composition::SpriteVisual backdrop_{ nullptr };
        winrt::Windows::UI::Composition::SpriteVisual tint_{ nullptr };
        winrt::Windows::UI::Composition::CompositionColorBrush tintBrush_{ nullptr };
        winrt::Windows::UI::Composition::ContainerVisual itemsLayer_{ nullptr };
        winrt::Windows::UI::Composition::SpriteVisual orb_{ nullptr };
        winrt::Windows::UI::Composition::CompositionSurfaceBrush orbBrush_{ nullptr };
        winrt::Windows::UI::Composition::ShapeVisual border_{ nullptr };
        winrt::Windows::UI::Composition::CompositionRoundedRectangleGeometry borderGeom_{ nullptr };
        winrt::Windows::UI::Composition::CompositionColorBrush borderBrush_{ nullptr };

        std::vector<ItemVisual> items_;
        std::unordered_map<uint32_t, winrt::Windows::UI::Composition::CompositionBrush> softBrushes_;
    };
}
