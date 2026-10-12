#include "pch.h"
#include "Scene.h"
#include "Metrics.h"

namespace wuc = winrt::Windows::UI::Composition;
namespace wgd = winrt::Windows::Graphics::DirectX;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;
using winrt::Windows::UI::Color;
using namespace dm::metrics;

namespace dm
{
    namespace
    {
        // Soft shadow source: a 24 px rounded square, blurred, centered in 64 px.
        constexpr int SoftSourcePx = 64;
        constexpr float SoftPadPx = 20.f;
        constexpr float SoftInsetPx = 24.f;

        struct Palette
        {
            Rgb plate;
            Color tintAcrylic;
            Color tintSolid;
            Color border;
        };

        const Palette& PaletteFor(Theme t)
        {
            static const Palette dark{ { 246, 246, 246 }, { 163, 30, 30, 30 }, { 255, 27, 27, 27 }, { 18, 255, 255, 255 } };
            static const Palette light{ { 255, 255, 255 }, { 179, 247, 247, 247 }, { 255, 244, 244, 244 }, { 15, 0, 0, 0 } };
            return t == Theme::Light ? light : dark;
        }

        float Clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
        float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        Rgb Mix(Rgb a, Rgb b, float t)
        {
            return { Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t) };
        }

        Rgb Stops(const Rgb (&s)[4], float t)
        {
            t = std::clamp(t, 0.f, 3.f);
            const int i = std::min(2, static_cast<int>(std::floor(t)));
            return Mix(s[i], s[i + 1], t - static_cast<float>(i));
        }

        uint8_t Byte(float v) { return static_cast<uint8_t>(std::clamp(v, 0.f, 255.f) + 0.5f); }

        Color ToColor(Rgb c, float alpha = 1.f)
        {
            return { Byte(Clamp01(alpha) * 255.f), Byte(c.r), Byte(c.g), Byte(c.b) };
        }

        D2D1_COLOR_F D2DColor(uint32_t rgb, float a = 1.f)
        {
            return D2D1::ColorF(rgb, a);
        }
    }

    void Scene::Initialize(HWND hwnd, float scale)
    {
        scale_ = scale;

        DispatcherQueueOptions options{ sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
        winrt::check_hresult(CreateDispatcherQueueController(
            options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(queue_))));

        compositor_ = wuc::Compositor();
        auto desktop = compositor_.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
        winrt::check_hresult(desktop->CreateDesktopWindowTarget(
            hwnd, TRUE, reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target_))));

        BOOL useBackdrop = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, &useBackdrop, sizeof(useBackdrop));

        CreateDevice();

        root_ = compositor_.CreateContainerVisual();
        root_.RelativeSizeAdjustment({ 1.f, 1.f });
        target_.Root(root_);

        track_ = compositor_.CreateContainerVisual();
        trackClip_ = compositor_.CreateRoundedRectangleGeometry();
        track_.Clip(compositor_.CreateGeometricClip(trackClip_));
        root_.Children().InsertAtTop(track_);

        backdrop_ = compositor_.CreateSpriteVisual();
        backdrop_.Brush(compositor_.CreateHostBackdropBrush());
        tintBrush_ = compositor_.CreateColorBrush();
        tint_ = compositor_.CreateSpriteVisual();
        tint_.Brush(tintBrush_);
        itemsLayer_ = compositor_.CreateContainerVisual();
        orbBrush_ = compositor_.CreateSurfaceBrush();
        orb_ = compositor_.CreateSpriteVisual();
        orb_.Brush(orbBrush_);

        borderGeom_ = compositor_.CreateRoundedRectangleGeometry();
        borderBrush_ = compositor_.CreateColorBrush();
        auto borderShape = compositor_.CreateSpriteShape(borderGeom_);
        borderShape.StrokeBrush(borderBrush_);
        borderShape.StrokeThickness(1.f);
        border_ = compositor_.CreateShapeVisual();
        border_.Shapes().Append(borderShape);

        auto children = track_.Children();
        children.InsertAtTop(backdrop_);
        children.InsertAtTop(tint_);
        children.InsertAtTop(itemsLayer_);
        children.InsertAtTop(orb_);
        children.InsertAtTop(border_);

        barHeight_.x = barHeight_.target = BarHeight;
        orbScale_.x = orbScale_.target = 1.f;
        RenderOrb();
    }

    void Scene::CreateDevice()
    {
        winrt::com_ptr<ID3D11Device> d3d;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr);
        if (FAILED(hr))
        {
            winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                   nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr));
        }
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.put()));
        winrt::check_hresult(d2dFactory_->CreateDevice(d3d.as<IDXGIDevice>().get(), d2dDevice_.put()));
        winrt::check_hresult(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, offscreen_.put()));

        auto interop = compositor_.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        winrt::check_hresult(interop->CreateGraphicsDevice(
            d2dDevice_.get(), reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(winrt::put_abi(graphics_))));
    }

    template <class F>
    wuc::CompositionDrawingSurface Scene::Draw(int w, int h, F&& paint)
    {
        auto surface = graphics_.CreateDrawingSurface(
            { static_cast<float>(w), static_cast<float>(h) },
            wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, wgd::DirectXAlphaMode::Premultiplied);
        auto interop = surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
        winrt::com_ptr<ID2D1DeviceContext> dc;
        POINT offset{};
        winrt::check_hresult(interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
        dc->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
        dc->PushAxisAlignedClip(D2D1::RectF(0, 0, static_cast<float>(w), static_cast<float>(h)), D2D1_ANTIALIAS_MODE_ALIASED);
        dc->Clear(D2D1::ColorF(0, 0.f));
        paint(dc.get());
        dc->PopAxisAlignedClip();
        winrt::check_hresult(interop->EndDraw());
        return surface;
    }

    wuc::CompositionDrawingSurface Scene::RenderIcon(IWICBitmapSource* icon)
    {
        const int px = static_cast<int>(std::ceil(AppExpanded * scale_));
        return Draw(px, px, [&](ID2D1DeviceContext* dc) {
            if (!icon)
            {
                return;
            }
            winrt::com_ptr<ID2D1Bitmap> bmp;
            if (SUCCEEDED(dc->CreateBitmapFromWicBitmap(icon, nullptr, bmp.put())))
            {
                const D2D1_RECT_F dst = D2D1::RectF(0, 0, static_cast<float>(px), static_cast<float>(px));
                dc->DrawBitmap(bmp.get(), &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            }
        });
    }

    wuc::CompositionBrush Scene::SoftBrush(Rgb color)
    {
        const uint32_t key = (Byte(color.r) << 16) | (Byte(color.g) << 8) | Byte(color.b);
        if (auto it = softBrushes_.find(key); it != softBrushes_.end())
        {
            return it->second;
        }

        // Draw the sharp shape offscreen, then blur it into the surface.
        winrt::com_ptr<ID2D1Bitmap1> sharp;
        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        winrt::check_hresult(offscreen_->CreateBitmap({ SoftSourcePx, SoftSourcePx }, nullptr, 0, &props, sharp.put()));
        offscreen_->SetTarget(sharp.get());
        offscreen_->BeginDraw();
        offscreen_->Clear(D2D1::ColorF(0, 0.f));
        winrt::com_ptr<ID2D1SolidColorBrush> fill;
        offscreen_->CreateSolidColorBrush(D2D1::ColorF(color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f), fill.put());
        const float a = SoftPadPx, b = SoftSourcePx - SoftPadPx;
        offscreen_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(a, a, b, b), 4.f, 4.f), fill.get());
        winrt::check_hresult(offscreen_->EndDraw());
        offscreen_->SetTarget(nullptr);

        auto surface = Draw(SoftSourcePx, SoftSourcePx, [&](ID2D1DeviceContext* dc) {
            winrt::com_ptr<ID2D1Effect> blur;
            if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, blur.put())))
            {
                blur->SetInput(0, sharp.get());
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 5.f);
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
                dc->DrawImage(blur.get());
            }
        });

        auto nine = compositor_.CreateNineGridBrush();
        nine.Source(compositor_.CreateSurfaceBrush(surface));
        nine.SetInsets(SoftInsetPx);
        softBrushes_.emplace(key, nine);
        return nine;
    }

    void Scene::RenderOrb()
    {
        const float d = std::round(Orb * scale_);
        const int margin = 2;
        const int size = static_cast<int>(d) + margin * 2;
        auto surface = Draw(size, size, [&](ID2D1DeviceContext* dc) {
            const D2D1_GRADIENT_STOP stops[] = {
                { 0.00f, D2DColor(0xFFFFFF) },
                { 0.20f, D2DColor(0xE8EDF0) },
                { 0.62f, D2DColor(0xA9B2BA) },
                { 1.00f, D2DColor(0x69737C) },
            };
            winrt::com_ptr<ID2D1GradientStopCollection> coll;
            dc->CreateGradientStopCollection(stops, ARRAYSIZE(stops), coll.put());
            const D2D1_POINT_2F focus{ margin + d * 0.34f, margin + d * 0.30f };
            winrt::com_ptr<ID2D1RadialGradientBrush> fill;
            dc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(focus, { 0, 0 }, d * 0.96f, d * 0.96f), coll.get(), fill.put());
            const D2D1_ELLIPSE e = D2D1::Ellipse({ margin + d / 2, margin + d / 2 }, d / 2, d / 2);
            dc->FillEllipse(e, fill.get());
            winrt::com_ptr<ID2D1SolidColorBrush> ring;
            dc->CreateSolidColorBrush(D2D1::ColorF(0, 0.45f), ring.put());
            dc->DrawEllipse(D2D1::Ellipse(e.point, d / 2 - 0.5f, d / 2 - 0.5f), ring.get(), 1.f);
        });
        orbBrush_.Surface(surface);
        orb_.Size({ static_cast<float>(size), static_cast<float>(size) });
    }

    void Scene::SetScale(float scale)
    {
        scale_ = scale;
        RenderOrb();
        for (auto& v : items_)
        {
            v.iconBrush.Surface(RenderIcon(v.src.icon.get()));
        }
    }

    void Scene::SetAppearance(Theme theme, Material material, CollapsedStyle style, SpringParams spring)
    {
        theme_ = theme;
        material_ = material;
        style_ = style;
        spring_ = spring;
    }

    void Scene::SetItems(std::vector<SceneItem> items)
    {
        itemsLayer_.Children().RemoveAll();
        items_.clear();
        items_.reserve(items.size());
        hover_ = pinned_ = -1;

        for (auto& src : items)
        {
            ItemVisual v;
            v.src = std::move(src);
            auto& c = compositor_;
            v.root = c.CreateContainerVisual();
            v.shadow = c.CreateSpriteVisual();
            v.shadow.Brush(SoftBrush({ 0, 0, 0 }));
            v.glow = c.CreateSpriteVisual();
            v.glow.Brush(SoftBrush(v.src.main));

            v.box = c.CreateContainerVisual();
            v.clip = c.CreateRoundedRectangleGeometry();
            v.box.Clip(c.CreateGeometricClip(v.clip));
            v.plateBrush = c.CreateColorBrush();
            v.plate = c.CreateSpriteVisual();
            v.plate.Brush(v.plateBrush);
            v.iconBrush = c.CreateSurfaceBrush(RenderIcon(v.src.icon.get()));
            v.iconBrush.Stretch(wuc::CompositionStretch::Uniform);
            v.icon = c.CreateSpriteVisual();
            v.icon.Brush(v.iconBrush);
            v.dim = c.CreateSpriteVisual();
            v.dim.Brush(c.CreateColorBrush({ 255, 0, 0, 0 }));
            v.ringGeom = c.CreateRoundedRectangleGeometry();
            v.ringBrush = c.CreateColorBrush();
            auto shape = c.CreateSpriteShape(v.ringGeom);
            shape.StrokeBrush(v.ringBrush);
            shape.StrokeThickness(1.f);
            v.ring = c.CreateShapeVisual();
            v.ring.Shapes().Append(shape);

            auto box = v.box.Children();
            box.InsertAtTop(v.plate);
            box.InsertAtTop(v.icon);
            box.InsertAtTop(v.dim);
            box.InsertAtTop(v.ring);
            auto root = v.root.Children();
            root.InsertAtTop(v.shadow);
            root.InsertAtTop(v.glow);
            root.InsertAtTop(v.box);
            itemsLayer_.Children().InsertAtTop(v.root);

            v.press.x = v.press.target = 1.f;
            items_.push_back(std::move(v));
        }
    }

    void Scene::SetPressed(int index)
    {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        {
            items_[i].press.target = (i == index) ? 0.88f : 1.f;
        }
    }

    void Scene::Targets(int index, int hovered, float& w, float& h, float& tone) const
    {
        const int d = hovered < 0 ? 99 : std::abs(index - hovered);
        if (d == 0)
        {
            w = h = AppExpanded;
            tone = 3.f;
            return;
        }
        const int level = d == 1 ? 2 : d == 2 ? 1 : 0;
        w = AppW[level];
        h = AppH[level];
        tone = static_cast<float>(level);
    }

    float Scene::TrackWidth() const
    {
        float w = PadX * 2 + OrbBlock;
        for (auto& v : items_)
        {
            w += std::max(0.f, v.w.x) + Gap;
        }
        return w;
    }

    float Scene::TrackWidthFor(int hovered) const
    {
        float w = PadX * 2 + OrbBlock;
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        {
            float iw, ih, tone;
            Targets(i, hovered, iw, ih, tone);
            w += iw + Gap;
        }
        return w;
    }

    float Scene::MaxTrackWidth() const
    {
        float m = TrackWidthFor(-1);
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        {
            m = std::max(m, TrackWidthFor(i));
        }
        return m + OvershootMargin * 2;
    }

    float Scene::RadiusPx() const
    {
        return Radius * scale_;
    }

    RECT Scene::RestTrackRect() const
    {
        const float w = TrackWidthFor(-1);
        const float x0 = (windowWidth_ - w) / 2;
        return { static_cast<LONG>(std::floor(x0 * scale_)), 0,
                 static_cast<LONG>(std::ceil((x0 + w) * scale_)), static_cast<LONG>(std::ceil(BarHeight * scale_)) };
    }

    int Scene::HitTestItem(POINT p) const
    {
        const float x = p.x / scale_, y = p.y / scale_;
        const float trackW = TrackWidth();
        const float x0 = (windowWidth_ - trackW) / 2;
        if (y < 0 || y > barHeight_.x || x < x0 || x > x0 + trackW)
        {
            return -1;
        }
        float cx = x0 + PadX + OrbBlock;
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        {
            const float slot = std::max(0.f, items_[i].w.x) + Gap;
            if (x >= cx && x < cx + slot)
            {
                return i;
            }
            cx += slot;
        }
        return -1;
    }

    bool Scene::HitTestOrb(POINT p) const
    {
        const float x = p.x / scale_, y = p.y / scale_;
        const float x0 = (windowWidth_ - TrackWidth()) / 2;
        const float cx = x0 + PadX + OrbMarginLeft + Orb / 2;
        const float cy = barHeight_.x / 2;
        const float r = Orb / 2 + 3;
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
    }

    bool Scene::Step(float dt)
    {
        const int hovered = pinned_ >= 0 ? pinned_ : hover_;
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        {
            auto& v = items_[i];
            Targets(i, hovered, v.w.target, v.h.target, v.tone.target);
            if (v.fresh)
            {
                v.w.Snap();
                v.h.Snap();
                v.tone.Snap();
                v.fresh = false;
            }
        }
        barHeight_.target = hovered >= 0 ? BarHeightHover : BarHeight;
        orbScale_.target = orbHover_ ? 1.12f : 1.f;

        std::vector<Spring*> all{ &barHeight_, &orbScale_ };
        for (auto& v : items_)
        {
            all.insert(all.end(), { &v.w, &v.h, &v.tone, &v.press });
        }

        const int steps = std::max(1, static_cast<int>(std::ceil(dt / (1.f / 240.f))));
        const float h = dt / steps;
        for (int k = 0; k < steps; ++k)
        {
            for (auto* s : all)
            {
                s->Step(h, spring_);
            }
        }

        const bool done = std::all_of(all.begin(), all.end(), [](Spring* s) { return s->AtRest(); });
        if (done)
        {
            for (auto* s : all)
            {
                s->Snap();
            }
        }
        return !done;
    }

    void Scene::Apply()
    {
        const float s = scale_;
        const Palette& pal = PaletteFor(theme_);
        const float trackW = TrackWidth();
        const float bh = barHeight_.x;
        const float x0 = (windowWidth_ - trackW) / 2;
        const float2 trackSize{ trackW * s, bh * s };

        track_.Offset({ x0 * s, 0, 0 });
        track_.Size(trackSize);
        trackClip_.Size(trackSize);
        trackClip_.CornerRadius({ Radius * s, Radius * s });
        backdrop_.Size(trackSize);
        backdrop_.IsVisible(material_ == Material::Acrylic);
        tint_.Size(trackSize);
        tintBrush_.Color(material_ == Material::Acrylic ? pal.tintAcrylic : pal.tintSolid);
        border_.Size(trackSize);
        borderGeom_.Offset({ 0.5f, 0.5f });
        borderGeom_.Size({ trackSize.x - 1, trackSize.y - 1 });
        borderGeom_.CornerRadius({ Radius * s - 0.5f, Radius * s - 0.5f });
        borderBrush_.Color(pal.border);

        const float2 orbSize = orb_.Size();
        orb_.Offset({ (PadX + OrbMarginLeft + Orb / 2) * s - orbSize.x / 2, bh / 2 * s - orbSize.y / 2, 0 });
        orb_.CenterPoint({ orbSize.x / 2, orbSize.y / 2, 0 });
        orb_.Scale({ orbScale_.x, orbScale_.x, 1 });

        float cx = PadX + OrbBlock;
        for (auto& v : items_)
        {
            const float w = std::max(0.f, v.w.x), h = std::max(0.f, v.h.x);
            const float slot = w + Gap;
            const float centerX = cx + slot / 2;
            cx += slot;

            const float W = w * s, H = h * s;
            const float left = (centerX - w / 2) * s, top = (bh / 2 - h / 2) * s;
            const float r = ItemRadius(w, h) * s;

            v.box.Offset({ left, top, 0 });
            v.box.Size({ W, H });
            v.box.CenterPoint({ W / 2, H / 2, 0 });
            v.box.Scale({ v.press.x, v.press.x, 1 });
            v.clip.Size({ W, H });
            v.clip.CornerRadius({ r, r });
            v.plate.Size({ W, H });
            v.dim.Size({ W, H });
            v.ring.Size({ W, H });
            v.ringGeom.Offset({ 0.5f, 0.5f });
            v.ringGeom.Size({ std::max(0.f, W - 1), std::max(0.f, H - 1) });
            v.ringGeom.CornerRadius({ std::max(0.f, r - 0.5f), std::max(0.f, r - 0.5f) });

            const float t = v.tone.x;
            const float pa = Clamp01(t - 2.f);
            const float g = Clamp01(t / 2.f);
            const float q = 1.f - pa;
            const Rgb d = v.src.main;
            const Rgb black{ 0, 0, 0 }, white{ 255, 255, 255 };
            const CollapsedStyle style = v.src.style.value_or(style_);

            float iconSize, iconOpacity, dimOpacity, ringAlpha, glowOpacity, glowSpread, shadowOpacity, shadowY;
            if (style == CollapsedStyle::Glow)
            {
                const Rgb stops[4] = { Mix(d, black, 0.12f), d, Mix(d, white, 0.22f), pal.plate };
                v.plateBrush.Color(ToColor(Stops(stops, t)));
                iconSize = H * 0.74f;
                iconOpacity = pa * pa;
                dimOpacity = 0.f;
                ringAlpha = 0.3f * q;
                glowOpacity = std::max((0.5f + 0.4f * g) * q, 0.28f * pa);
                glowSpread = ((0.5f + 1.5f * g) * q + 2.f * pa) * s;
                shadowOpacity = 0.32f * pa;
                shadowY = 6.f * pa * s;
            }
            else
            {
                const Rgb stops[4] = { Mix(d, black, 0.62f), Mix(d, black, 0.48f), Mix(d, black, 0.3f), pal.plate };
                v.plateBrush.Color(ToColor(Stops(stops, t)));
                iconSize = H * Lerp(1.3f, 0.74f, pa);
                iconOpacity = 1.f;
                dimOpacity = 1.f - Lerp(0.72f, 1.f, std::max(g, pa));
                ringAlpha = 0.14f * q;
                glowOpacity = 0.f;
                glowSpread = 0.f;
                shadowOpacity = 0.45f * q + 0.32f * pa;
                shadowY = (2.f + 5.f * pa) * s;
            }

            v.icon.Size({ iconSize, iconSize });
            v.icon.Offset({ (W - iconSize) / 2, (H - iconSize) / 2, 0 });
            v.icon.Opacity(iconOpacity);
            v.dim.Opacity(dimOpacity);
            if (theme_ == Theme::Light && pa > 0.5f)
            {
                v.ringBrush.Color(ToColor(black, 0.1f * pa));
            }
            else
            {
                v.ringBrush.Color(ToColor(white, ringAlpha));
            }

            const float pressScale = v.press.x;
            const float bw = W * pressScale, bhPx = H * pressScale;
            const float bl = left + (W - bw) / 2, bt = top + (H - bhPx) / 2;
            const float gp = SoftPadPx + glowSpread;
            v.glow.Offset({ bl - gp, bt - gp, 0 });
            v.glow.Size({ bw + gp * 2, bhPx + gp * 2 });
            v.glow.Opacity(glowOpacity);
            v.glow.IsVisible(glowOpacity > 0.001f);
            v.shadow.Offset({ bl - SoftPadPx, bt - SoftPadPx + shadowY, 0 });
            v.shadow.Size({ bw + SoftPadPx * 2, bhPx + SoftPadPx * 2 });
            v.shadow.Opacity(shadowOpacity);
            v.shadow.IsVisible(shadowOpacity > 0.001f);
        }
    }
}
