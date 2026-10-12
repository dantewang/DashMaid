#include "pch.h"
#include "Scene.h"
#include "Metrics.h"
#include "Strings.h"

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

        constexpr wchar_t TextFont[] = L"Segoe UI Variable Text";
        constexpr wchar_t Locale[] = L"zh-CN";
        constexpr wchar_t CommandGlyph[] = L"";

        struct Palette
        {
            Rgb plate;
            Color tintAcrylic;
            Color tintSolid;
            Color border;
            uint32_t text;
            uint32_t highlight;
            float selectionAlpha;
            float rowAlpha;
            Rgb widgetStops[4];  // Collapsible widget: idle, second neighbor, first neighbor, expanded.
            Rgb chip;            // Widget that always shows its text.
            Color chipLine;
            uint32_t chipInk;
            uint32_t widgetInk;  // Text on the expanded widget.
        };

        const Palette& PaletteFor(Theme t)
        {
            static const Palette dark{ { 246, 246, 246 }, { 163, 30, 30, 30 }, { 255, 27, 27, 27 }, { 18, 255, 255, 255 },
                                       0xFFFFFF, 0x8FE0CF, 0.34f, 0.1f,
                                       { { 51, 51, 51 }, { 99, 99, 99 }, { 163, 163, 163 }, { 255, 255, 255 } },
                                       { 46, 46, 46 }, { 15, 255, 255, 255 }, 0xD0D0D0, 0x111111 };
            static const Palette light{ { 255, 255, 255 }, { 179, 247, 247, 247 }, { 255, 244, 244, 244 }, { 15, 0, 0, 0 },
                                        0x000000, 0x2B7A6A, 0.24f, 0.06f,
                                        { { 206, 206, 206 }, { 158, 158, 158 }, { 98, 98, 98 }, { 24, 24, 24 } },
                                        { 226, 226, 226 }, { 13, 0, 0, 0 }, 0x3A3A3A, 0xFFFFFF };
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

        Color ToColor(uint32_t rgb, float alpha)
        {
            return { Byte(Clamp01(alpha) * 255.f), static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
                     static_cast<uint8_t>(rgb) };
        }

        D2D1_COLOR_F D2DColor(uint32_t rgb, float a = 1.f)
        {
            return D2D1::ColorF(rgb, a);
        }

        uint32_t ToRgb32(Rgb c)
        {
            return (static_cast<uint32_t>(Byte(c.r)) << 16) | (static_cast<uint32_t>(Byte(c.g)) << 8) | Byte(c.b);
        }

        // Changes the transform so that the paint code can use DIPs.
        void UseDips(ID2D1DeviceContext* dc, float scale)
        {
            D2D1_MATRIX_3X2_F t;
            dc->GetTransform(&t);
            dc->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::ReinterpretBaseType(&t)[0]);
        }

        winrt::Windows::Graphics::SizeInt32 PxSize(float wDip, float hDip, float scale)
        {
            return { std::max(1, static_cast<int>(std::ceil(wDip * scale))), std::max(1, static_cast<int>(std::ceil(hDip * scale))) };
        }

        bool HasFont(IDWriteFactory* dw, const wchar_t* family)
        {
            winrt::com_ptr<IDWriteFontCollection> fonts;
            if (FAILED(dw->GetSystemFontCollection(fonts.put())))
            {
                return false;
            }
            UINT32 index = 0;
            BOOL exists = FALSE;
            fonts->FindFamilyName(family, &index, &exists);
            return exists != FALSE;
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
        CreateTextFormats();

        root_ = compositor_.CreateContainerVisual();
        root_.RelativeSizeAdjustment({ 1.f, 1.f });
        target_.Root(root_);

        track_ = CreatePanel();
        root_.Children().InsertAtTop(track_.root);

        itemsLayer_ = compositor_.CreateContainerVisual();
        orbBrush_ = compositor_.CreateSurfaceBrush();
        orb_ = compositor_.CreateSpriteVisual();
        orb_.Brush(orbBrush_);

        runBox_ = compositor_.CreateContainerVisual();
        runBox_.Clip(compositor_.CreateInsetClip());
        inputBrush_ = compositor_.CreateSurfaceBrush();
        inputBrush_.Stretch(wuc::CompositionStretch::None);
        inputBrush_.HorizontalAlignmentRatio(0.f);
        inputBrush_.VerticalAlignmentRatio(0.f);
        input_ = compositor_.CreateSpriteVisual();
        input_.Brush(inputBrush_);
        caretBrush_ = compositor_.CreateColorBrush();
        caretVisual_ = compositor_.CreateSpriteVisual();
        caretVisual_.Brush(caretBrush_);
        runBox_.Children().InsertAtTop(input_);
        runBox_.Children().InsertAtTop(caretVisual_);

        // The caret shows for half of the period and hides for the other half.
        auto step = compositor_.CreateStepEasingFunction();
        blink_ = compositor_.CreateScalarKeyFrameAnimation();
        blink_.InsertKeyFrame(0.f, 1.f);
        blink_.InsertKeyFrame(0.5f, 0.f, step);
        blink_.InsertKeyFrame(1.f, 1.f, step);
        blink_.Duration(std::chrono::milliseconds(1060));
        blink_.IterationBehavior(wuc::AnimationIterationBehavior::Forever);

        auto content = track_.content.Children();
        content.InsertAtTop(itemsLayer_);
        content.InsertAtTop(runBox_);
        content.InsertAtTop(orb_);

        results_ = CreatePanel();
        resultsBrush_ = compositor_.CreateSurfaceBrush();
        resultsBrush_.Stretch(wuc::CompositionStretch::None);
        resultsBrush_.HorizontalAlignmentRatio(0.f);
        resultsBrush_.VerticalAlignmentRatio(0.f);
        resultsContent_ = compositor_.CreateSpriteVisual();
        resultsContent_.Brush(resultsBrush_);
        results_.content.Children().InsertAtTop(resultsContent_);
        results_.root.IsVisible(false);
        root_.Children().InsertAtTop(results_.root);

        barHeight_.x = barHeight_.target = BarHeight;
        orbScale_.x = orbScale_.target = 1.f;
        RenderOrb();
        RenderInput();
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

    void Scene::CreateTextFormats()
    {
        winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                 reinterpret_cast<IUnknown**>(dwrite_.put())));

        auto make = [&](const wchar_t* family, float size, bool trim) {
            winrt::com_ptr<IDWriteTextFormat> f;
            winrt::check_hresult(dwrite_->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                           DWRITE_FONT_STRETCH_NORMAL, size, Locale, f.put()));
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (trim)
            {
                const DWRITE_TRIMMING t{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                winrt::com_ptr<IDWriteInlineObject> sign;
                dwrite_->CreateEllipsisTrimmingSign(f.get(), sign.put());
                f->SetTrimming(&t, sign.get());
            }
            return f;
        };

        inputFormat_ = make(TextFont, RunFontSize, false);
        titleFormat_ = make(TextFont, TitleFontSize, true);
        subtitleFormat_ = make(TextFont, SubtitleFontSize, true);
        const wchar_t* glyphs = HasFont(dwrite_.get(), L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
        glyphFormat_ = make(glyphs, 16.f, false);
        glyphFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

        winrt::check_hresult(dwrite_->CreateTextFormat(TextFont, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                                       DWRITE_FONT_STRETCH_NORMAL, WidgetFontSize, Locale, widgetFormat_.put()));
        widgetFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        widgetFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        // Digits of the same width keep the widget width stable while values change.
        winrt::check_hresult(dwrite_->CreateTypography(tabular_.put()));
        tabular_->AddFontFeature({ DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1 });
    }

    void Scene::RenderLabel(ItemVisual& v, bool detail)
    {
        const std::wstring& text = detail ? v.detail : v.brief;
        winrt::com_ptr<IDWriteTextLayout> layout;
        winrt::check_hresult(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), widgetFormat_.get(),
                                                       100000.f, WidgetLineHeight, layout.put()));
        layout->SetTypography(tabular_.get(), { 0, static_cast<UINT32>(text.size()) });
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        const float width = std::ceil(m.widthIncludingTrailingWhitespace);
        (detail ? v.detailW : v.briefW) = width;

        const Palette& pal = PaletteFor(theme_);
        uint32_t color = pal.widgetInk;
        if (v.chipInk)
        {
            color = (theme_ == Theme::Dark && v.src.textColor) ? ToRgb32(*v.src.textColor) : pal.chipInk;
        }

        auto& surface = detail ? v.detailSurface : v.briefSurface;
        const auto px = PxSize(width + 2.f, WidgetLineHeight, scale_);
        if (!surface)
        {
            surface = graphics_.CreateDrawingSurface2(px, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                      wgd::DirectXAlphaMode::Premultiplied);
        }
        else if (surface.SizeInt32() != px)
        {
            surface.Resize(px);
        }
        Paint(surface, px.Width, px.Height, [&](ID2D1DeviceContext* dc) {
            UseDips(dc, scale_);
            dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            dc->CreateSolidColorBrush(D2DColor(color), brush.put());
            dc->DrawTextLayout({ 1.f, 0.f }, layout.get(), brush.get());
        });
        if (detail == v.showingDetail)
        {
            v.labelBrush.Surface(surface);
        }
    }

    bool Scene::SetWidgetText(int index, const std::wstring& brief, const std::wstring& detail)
    {
        if (index < 0 || index >= static_cast<int>(items_.size()) || !items_[index].src.widget)
        {
            return false;
        }
        ItemVisual& v = items_[index];
        bool changed = false;
        if (brief != v.brief || !v.briefSurface)
        {
            v.brief = brief;
            RenderLabel(v, false);
            changed = true;
        }
        if (detail != v.detail || !v.detailSurface)
        {
            v.detail = detail;
            RenderLabel(v, true);
            changed = true;
        }
        return changed;
    }

    Scene::Panel Scene::CreatePanel()
    {
        auto& c = compositor_;
        Panel p;
        p.root = c.CreateContainerVisual();
        p.clip = c.CreateRoundedRectangleGeometry();
        p.root.Clip(c.CreateGeometricClip(p.clip));
        p.backdrop = c.CreateSpriteVisual();
        p.backdrop.Brush(c.CreateHostBackdropBrush());
        p.tintBrush = c.CreateColorBrush();
        p.tint = c.CreateSpriteVisual();
        p.tint.Brush(p.tintBrush);
        p.content = c.CreateContainerVisual();
        p.borderGeom = c.CreateRoundedRectangleGeometry();
        p.borderBrush = c.CreateColorBrush();
        auto shape = c.CreateSpriteShape(p.borderGeom);
        shape.StrokeBrush(p.borderBrush);
        shape.StrokeThickness(1.f);
        p.border = c.CreateShapeVisual();
        p.border.Shapes().Append(shape);

        auto children = p.root.Children();
        children.InsertAtTop(p.backdrop);
        children.InsertAtTop(p.tint);
        children.InsertAtTop(p.content);
        children.InsertAtTop(p.border);
        return p;
    }

    void Scene::LayoutPanel(Panel& p, float w, float h, float r)
    {
        const Palette& pal = PaletteFor(theme_);
        const float2 size{ w, h };
        p.root.Size(size);
        p.clip.Size(size);
        p.clip.CornerRadius({ r, r });
        p.backdrop.Size(size);
        p.backdrop.IsVisible(material_ == Material::Acrylic);
        p.tint.Size(size);
        p.tintBrush.Color(material_ == Material::Acrylic ? pal.tintAcrylic : pal.tintSolid);
        p.content.Size(size);
        p.border.Size(size);
        p.borderGeom.Offset({ 0.5f, 0.5f });
        p.borderGeom.Size({ std::max(0.f, w - 1), std::max(0.f, h - 1) });
        p.borderGeom.CornerRadius({ std::max(0.f, r - 0.5f), std::max(0.f, r - 0.5f) });
        p.borderBrush.Color(pal.border);
    }

    template <class F>
    void Scene::Paint(wuc::CompositionDrawingSurface& surface, int w, int h, F&& paint)
    {
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
    }

    template <class F>
    wuc::CompositionDrawingSurface Scene::Draw(int w, int h, F&& paint)
    {
        auto surface = graphics_.CreateDrawingSurface(
            { static_cast<float>(w), static_cast<float>(h) },
            wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, wgd::DirectXAlphaMode::Premultiplied);
        Paint(surface, w, h, std::forward<F>(paint));
        return surface;
    }

    // Bitmaps belong to the device, so all drawing contexts of the device can use them.
    ID2D1Bitmap* Scene::BitmapFor(IWICBitmapSource* src, ID2D1DeviceContext* dc)
    {
        if (!src)
        {
            return nullptr;
        }
        if (auto it = bitmaps_.find(src); it != bitmaps_.end())
        {
            return it->second.bitmap.get();
        }
        CachedBitmap entry;
        entry.source.copy_from(src);
        if (FAILED(dc->CreateBitmapFromWicBitmap(src, nullptr, entry.bitmap.put())))
        {
            return nullptr;
        }
        ID2D1Bitmap* result = entry.bitmap.get();
        bitmaps_.emplace(src, std::move(entry));
        return result;
    }

    wuc::CompositionDrawingSurface Scene::RenderIcon(IWICBitmapSource* icon)
    {
        const int px = static_cast<int>(std::ceil(AppExpanded * scale_));
        return Draw(px, px, [&](ID2D1DeviceContext* dc) {
            if (ID2D1Bitmap* bmp = BitmapFor(icon, dc))
            {
                const D2D1_RECT_F dst = D2D1::RectF(0, 0, static_cast<float>(px), static_cast<float>(px));
                dc->DrawBitmap(bmp, &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
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

    void Scene::RenderInput()
    {
        const float fieldW = RunWidth - RunTextLeft - RunTextRight;
        const auto px = PxSize(fieldW, RunLineHeight, scale_);
        if (!inputSurface_)
        {
            inputSurface_ = graphics_.CreateDrawingSurface2(px, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                            wgd::DirectXAlphaMode::Premultiplied);
            inputBrush_.Surface(inputSurface_);
        }
        else if (inputSurface_.SizeInt32() != px)
        {
            inputSurface_.Resize(px);
        }
        input_.Size({ static_cast<float>(px.Width), static_cast<float>(px.Height) });

        const bool empty = inputText_.empty();
        const std::wstring_view shown = empty ? std::wstring_view(strings::RunPlaceholder) : std::wstring_view(inputText_);
        inputLayout_ = nullptr;
        winrt::check_hresult(dwrite_->CreateTextLayout(shown.data(), static_cast<UINT32>(shown.size()), inputFormat_.get(),
                                                       100000.f, RunLineHeight, inputLayout_.put()));

        caretX_ = 0.f;
        float textW = 0.f;
        if (!empty)
        {
            float x = 0, y = 0;
            DWRITE_HIT_TEST_METRICS m{};
            inputLayout_->HitTestTextPosition(static_cast<UINT32>(caret_), FALSE, &x, &y, &m);
            caretX_ = x;
            DWRITE_TEXT_METRICS tm{};
            inputLayout_->GetMetrics(&tm);
            textW = tm.widthIncludingTrailingWhitespace;
        }

        // Scroll so that the caret stays in the field.
        const float room = fieldW - CaretWidth;
        if (textW <= room)
        {
            inputScroll_ = 0.f;
        }
        else
        {
            if (caretX_ - inputScroll_ > room)
            {
                inputScroll_ = caretX_ - room;
            }
            if (caretX_ < inputScroll_)
            {
                inputScroll_ = caretX_;
            }
            inputScroll_ = std::clamp(inputScroll_, 0.f, textW - room);
        }

        const Palette& pal = PaletteFor(theme_);
        Paint(inputSurface_, px.Width, px.Height, [&](ID2D1DeviceContext* dc) {
            UseDips(dc, scale_);
            dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            if (!empty && selEnd_ > selStart_)
            {
                DWRITE_HIT_TEST_METRICS ranges[8];
                UINT32 count = 0;
                inputLayout_->HitTestTextRange(static_cast<UINT32>(selStart_), static_cast<UINT32>(selEnd_ - selStart_), 0, 0,
                                               ranges, ARRAYSIZE(ranges), &count);
                winrt::com_ptr<ID2D1SolidColorBrush> sel;
                dc->CreateSolidColorBrush(D2DColor(pal.highlight, pal.selectionAlpha), sel.put());
                for (UINT32 i = 0; i < std::min<UINT32>(count, ARRAYSIZE(ranges)); ++i)
                {
                    const auto& r = ranges[i];
                    const float top = (RunLineHeight - CaretHeight) / 2;
                    dc->FillRectangle(D2D1::RectF(r.left - inputScroll_, top, r.left + r.width - inputScroll_, top + CaretHeight), sel.get());
                }
            }
            winrt::com_ptr<ID2D1SolidColorBrush> text;
            dc->CreateSolidColorBrush(D2DColor(pal.text, empty ? 0.46f : 0.94f), text.put());
            dc->DrawTextLayout({ -inputScroll_, 0.f }, inputLayout_.get(), text.get());
        });

        if (empty)
        {
            inputLayout_ = nullptr;
        }
    }

    void Scene::RenderResults()
    {
        if (rows_.empty())
        {
            return;
        }
        const float W = ResultsWidth();
        const float H = ResultsHeight();
        const auto px = PxSize(W, H, scale_);
        if (!resultsSurface_)
        {
            resultsSurface_ = graphics_.CreateDrawingSurface2(px, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                              wgd::DirectXAlphaMode::Premultiplied);
            resultsBrush_.Surface(resultsSurface_);
        }
        else if (resultsSurface_.SizeInt32() != px)
        {
            resultsSurface_.Resize(px);
        }
        resultsContent_.Size({ static_cast<float>(px.Width), static_cast<float>(px.Height) });

        const Palette& pal = PaletteFor(theme_);
        const int n = std::min(static_cast<int>(rows_.size()), ResultsMaxRows);
        Paint(resultsSurface_, px.Width, px.Height, [&](ID2D1DeviceContext* dc) {
            UseDips(dc, scale_);
            dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            winrt::com_ptr<ID2D1SolidColorBrush> rowBrush, title, subtitle, highlight;
            dc->CreateSolidColorBrush(D2DColor(pal.text, pal.rowAlpha), rowBrush.put());
            dc->CreateSolidColorBrush(D2DColor(pal.text, 0.94f), title.put());
            dc->CreateSolidColorBrush(D2DColor(pal.text, 0.56f), subtitle.put());
            dc->CreateSolidColorBrush(D2DColor(pal.highlight), highlight.put());

            const float textX = ResultsPad + ResultTextLeft;
            const float textW = std::max(1.f, W - ResultsPad - 10.f - textX);
            for (int i = 0; i < n; ++i)
            {
                const ResultRow& row = rows_[i];
                const float top = ResultsPad + i * ResultsRowHeight;
                if (i == selected_)
                {
                    const D2D1_RECT_F r = D2D1::RectF(ResultsPad, top, W - ResultsPad, top + ResultsRowHeight);
                    dc->FillRoundedRectangle(D2D1::RoundedRect(r, 8.f, 8.f), rowBrush.get());
                }

                const float iconX = ResultsPad + 10.f;
                const float iconY = top + (ResultsRowHeight - ResultIcon) / 2;
                const D2D1_RECT_F iconRect = D2D1::RectF(iconX, iconY, iconX + ResultIcon, iconY + ResultIcon);
                if (row.command)
                {
                    dc->DrawText(CommandGlyph, 1, glyphFormat_.get(), iconRect, title.get());
                }
                else if (ID2D1Bitmap* bmp = BitmapFor(row.icon.get(), dc))
                {
                    dc->DrawBitmap(bmp, &iconRect, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
                }

                const bool twoLines = !row.subtitle.empty();
                winrt::com_ptr<IDWriteTextLayout> t;
                dwrite_->CreateTextLayout(row.title.c_str(), static_cast<UINT32>(row.title.size()), titleFormat_.get(), textW,
                                          twoLines ? 18.f : ResultsRowHeight, t.put());
                if (t && row.highlightStart >= 0 && row.highlightLength > 0)
                {
                    const DWRITE_TEXT_RANGE range{ static_cast<UINT32>(row.highlightStart), static_cast<UINT32>(row.highlightLength) };
                    t->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
                    t->SetDrawingEffect(highlight.get(), range);
                }
                if (t)
                {
                    dc->DrawTextLayout({ textX, twoLines ? top + 4.f : top }, t.get(), title.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                if (twoLines)
                {
                    const D2D1_RECT_F sr = D2D1::RectF(textX, top + 21.f, textX + textW, top + 37.f);
                    dc->DrawText(row.subtitle.c_str(), static_cast<UINT32>(row.subtitle.size()), subtitleFormat_.get(), sr,
                                 subtitle.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }
        });
    }

    void Scene::RestartCaret()
    {
        caretVisual_.StopAnimation(L"Opacity");
        caretVisual_.Opacity(1.f);
        caretVisual_.IsVisible(runOpen_);
        if (runOpen_)
        {
            caretVisual_.StartAnimation(L"Opacity", blink_);
        }
    }

    void Scene::SetScale(float scale)
    {
        scale_ = scale;
        RenderOrb();
        for (auto& v : items_)
        {
            if (v.src.widget)
            {
                RenderLabel(v, false);
                RenderLabel(v, true);
            }
            else
            {
                v.iconBrush.Surface(RenderIcon(v.src.icon.get()));
            }
        }
        RenderInput();
        RenderResults();
    }

    void Scene::SetAppearance(Theme theme, Material material, CollapsedStyle style, SpringParams spring)
    {
        const bool recolor = theme != theme_;
        theme_ = theme;
        material_ = material;
        style_ = style;
        spring_ = spring;
        if (recolor && inputSurface_)
        {
            RenderInput();
            RenderResults();
            for (auto& v : items_)
            {
                if (v.src.widget)
                {
                    RenderLabel(v, false);
                    RenderLabel(v, true);
                }
            }
        }
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
            v.box = c.CreateContainerVisual();
            v.clip = c.CreateRoundedRectangleGeometry();
            v.box.Clip(c.CreateGeometricClip(v.clip));
            v.plateBrush = c.CreateColorBrush();
            v.plate = c.CreateSpriteVisual();
            v.plate.Brush(v.plateBrush);
            v.ringGeom = c.CreateRoundedRectangleGeometry();
            v.ringBrush = c.CreateColorBrush();
            auto shape = c.CreateSpriteShape(v.ringGeom);
            shape.StrokeBrush(v.ringBrush);
            shape.StrokeThickness(1.f);
            v.ring = c.CreateShapeVisual();
            v.ring.Shapes().Append(shape);

            auto box = v.box.Children();
            auto root = v.root.Children();
            box.InsertAtTop(v.plate);
            if (v.src.widget)
            {
                v.labelBrush = c.CreateSurfaceBrush();
                v.labelBrush.Stretch(wuc::CompositionStretch::None);
                v.labelBrush.HorizontalAlignmentRatio(0.f);
                v.labelBrush.VerticalAlignmentRatio(0.f);
                v.label = c.CreateSpriteVisual();
                v.label.Brush(v.labelBrush);
                box.InsertAtTop(v.label);
            }
            else
            {
                v.shadow = c.CreateSpriteVisual();
                v.shadow.Brush(SoftBrush({ 0, 0, 0 }));
                v.glow = c.CreateSpriteVisual();
                v.glow.Brush(SoftBrush(v.src.main));
                v.iconBrush = c.CreateSurfaceBrush(RenderIcon(v.src.icon.get()));
                v.iconBrush.Stretch(wuc::CompositionStretch::Uniform);
                v.icon = c.CreateSpriteVisual();
                v.icon.Brush(v.iconBrush);
                v.dim = c.CreateSpriteVisual();
                v.dim.Brush(c.CreateColorBrush({ 255, 0, 0, 0 }));
                box.InsertAtTop(v.icon);
                box.InsertAtTop(v.dim);
                root.InsertAtTop(v.shadow);
                root.InsertAtTop(v.glow);
            }
            box.InsertAtTop(v.ring);
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

    void Scene::SetRunOpen(bool open)
    {
        if (open == runOpen_)
        {
            return;
        }
        runOpen_ = open;
        if (open)
        {
            hover_ = pinned_ = -1;
        }
        RestartCaret();
    }

    void Scene::SetInput(const std::wstring& text, size_t caret, size_t selStart, size_t selEnd)
    {
        inputText_ = text;
        caret_ = caret;
        selStart_ = selStart;
        selEnd_ = selEnd;
        RenderInput();
        RestartCaret();
    }

    void Scene::SetResults(std::vector<ResultRow> rows, int selected)
    {
        rows_ = std::move(rows);
        selected_ = selected;
        RenderResults();
    }

    void Scene::SetSelected(int selected)
    {
        if (selected != selected_)
        {
            selected_ = selected;
            RenderResults();
        }
    }

    void Scene::Targets(int index, int hovered, float& w, float& h, float& tone) const
    {
        const int d = hovered < 0 ? 99 : std::abs(index - hovered);
        const ItemVisual& v = items_[index];
        const int level = d == 1 ? 2 : d == 2 ? 1 : 0;
        if (v.src.widget)
        {
            if (d == 0)
            {
                w = std::min(v.detailW + WidgetPadX * 2, WidgetMaxW);
                h = WidgetExpandedH;
                tone = 3.f;
            }
            else if (!widgetsCollapse_)
            {
                w = std::min(v.briefW + ChipPadX * 2, WidgetMaxW);
                h = ChipH;
                tone = 2.f;
            }
            else
            {
                w = WidgetW[level];
                h = WidgetH[level];
                tone = static_cast<float>(level);
            }
            return;
        }
        if (d == 0)
        {
            w = h = AppExpanded;
            tone = 3.f;
            return;
        }
        w = AppW[level];
        h = AppH[level];
        tone = static_cast<float>(level);
    }

    float Scene::TrackWidth() const
    {
        const float r = std::max(0.f, run_.x);
        const float keep = 1.f - Clamp01(run_.x);
        float w = PadX * 2 + OrbBlock + r * RunWidth;
        for (auto& v : items_)
        {
            w += (std::max(0.f, v.w.x) + Gap) * keep;
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

    float Scene::ResultsWidth() const
    {
        return std::max(ResultsMinWidth, PadX * 2 + OrbBlock + RunWidth);
    }

    float Scene::ResultsHeight() const
    {
        const int n = std::min(static_cast<int>(rows_.size()), ResultsMaxRows);
        return ResultsPad * 2 + n * ResultsRowHeight;
    }

    float Scene::WindowWidth() const
    {
        float m = std::max(TrackWidthFor(-1), ResultsWidth());
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        {
            m = std::max(m, TrackWidthFor(i));
        }
        return m + OvershootMargin * 2;
    }

    float Scene::WindowHeight() const
    {
        if (!RunVisible())
        {
            return BarHeightHover;
        }
        return BarHeight + ResultsGap + ResultsPad * 2 + ResultsMaxRows * ResultsRowHeight + OvershootMargin;
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
        if (RunVisible())
        {
            return -1;
        }
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

    bool Scene::HitTestTrack(POINT p) const
    {
        const float x = p.x / scale_, y = p.y / scale_;
        const float trackW = TrackWidth();
        const float x0 = (windowWidth_ - trackW) / 2;
        return y >= 0 && y <= barHeight_.x && x >= x0 && x <= x0 + trackW;
    }

    int Scene::HitTestResult(POINT p) const
    {
        if (!runOpen_ || rows_.empty())
        {
            return -1;
        }
        const float x = p.x / scale_, y = p.y / scale_;
        const float W = ResultsWidth();
        const float left = (windowWidth_ - W) / 2;
        const float top = BarHeight + ResultsGap;
        if (x < left + ResultsPad || x > left + W - ResultsPad)
        {
            return -1;
        }
        const float ry = y - top - ResultsPad;
        const int n = std::min(static_cast<int>(rows_.size()), ResultsMaxRows);
        if (ry < 0 || ry >= n * ResultsRowHeight)
        {
            return -1;
        }
        return static_cast<int>(ry / ResultsRowHeight);
    }

    int Scene::HitTestInput(POINT p) const
    {
        if (!runOpen_)
        {
            return -1;
        }
        const float x = p.x / scale_, y = p.y / scale_;
        const float x0 = (windowWidth_ - TrackWidth()) / 2;
        const float fieldLeft = x0 + PadX + OrbBlock;
        if (y < 0 || y > barHeight_.x || x < fieldLeft || x > fieldLeft + RunWidth)
        {
            return -1;
        }
        if (!inputLayout_)
        {
            return 0;
        }
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS m{};
        inputLayout_->HitTestPoint(x - fieldLeft - RunTextLeft + inputScroll_, RunLineHeight / 2, &trailing, &inside, &m);
        return static_cast<int>(m.textPosition + (trailing ? m.length : 0));
    }

    POINT Scene::CaretClientPx() const
    {
        const float x0 = (windowWidth_ - TrackWidth()) / 2;
        const float x = x0 + PadX + OrbBlock + RunTextLeft + caretX_ - inputScroll_;
        const float y = (barHeight_.x - CaretHeight) / 2;
        return { static_cast<LONG>(std::lround(x * scale_)), static_cast<LONG>(std::lround(y * scale_)) };
    }

    int Scene::CaretHeightPx() const
    {
        return static_cast<int>(std::lround(CaretHeight * scale_));
    }

    bool Scene::Step(float dt)
    {
        const int hovered = runOpen_ ? -1 : pinned_ >= 0 ? pinned_ : hover_;
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
        run_.target = runOpen_ ? 1.f : 0.f;

        std::vector<Spring*> all{ &barHeight_, &orbScale_, &run_ };
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
        const float r = std::max(0.f, run_.x);
        const float ro = Clamp01(run_.x);

        track_.root.Offset({ x0 * s, 0, 0 });
        LayoutPanel(track_, trackW * s, bh * s, Radius * s);

        const float2 orbSize = orb_.Size();
        orb_.Offset({ (PadX + OrbMarginLeft + Orb / 2) * s - orbSize.x / 2, bh / 2 * s - orbSize.y / 2, 0 });
        orb_.CenterPoint({ orbSize.x / 2, orbSize.y / 2, 0 });
        orb_.Scale({ orbScale_.x, orbScale_.x, 1 });

        // Run box.
        runBox_.IsVisible(r > 0.002f);
        runBox_.Offset({ (PadX + OrbBlock) * s, 0, 0 });
        runBox_.Size({ r * RunWidth * s, bh * s });
        runBox_.Opacity(ro);
        input_.Offset({ std::round(RunTextLeft * s), std::round((bh - RunLineHeight) / 2 * s), 0 });
        caretVisual_.Offset({ std::round((RunTextLeft + caretX_ - inputScroll_) * s), std::round((bh - CaretHeight) / 2 * s), 0 });
        caretVisual_.Size({ std::max(1.f, std::round(CaretWidth * s)), std::round(CaretHeight * s) });
        caretBrush_.Color(ToColor(pal.text, 0.94f));

        // Results.
        const bool showResults = r > 0.002f && !rows_.empty();
        results_.root.IsVisible(showResults);
        if (showResults)
        {
            const float W = ResultsWidth(), H = ResultsHeight();
            results_.root.Offset({ std::round((windowWidth_ - W) / 2 * s), (BarHeight + ResultsGap - 6.f * (1.f - ro)) * s, 0 });
            LayoutPanel(results_, W * s, H * s, ResultsRadius * s);
            results_.root.Opacity(ro * ro);
        }

        // Items.
        const float keep = 1.f - ro;
        itemsLayer_.IsVisible(keep > 0.001f);
        float cx = PadX + OrbBlock + r * RunWidth;
        for (auto& v : items_)
        {
            const float w = std::max(0.f, v.w.x), h = std::max(0.f, v.h.x);
            const float slot = (w + Gap) * keep;
            const float centerX = cx + slot / 2;
            cx += slot;
            v.root.Opacity(keep);

            const float W = w * s, H = h * s;
            const float left = (centerX - w / 2) * s, top = (bh / 2 - h / 2) * s;
            const float rr = ItemRadius(w, h) * s;

            v.box.Offset({ left, top, 0 });
            v.box.Size({ W, H });
            v.box.CenterPoint({ W / 2, H / 2, 0 });
            v.box.Scale({ v.press.x, v.press.x, 1 });
            v.clip.Size({ W, H });
            v.clip.CornerRadius({ rr, rr });
            v.plate.Size({ W, H });
            if (v.dim)
            {
                v.dim.Size({ W, H });
            }
            v.ring.Size({ W, H });
            v.ringGeom.Offset({ 0.5f, 0.5f });
            v.ringGeom.Size({ std::max(0.f, W - 1), std::max(0.f, H - 1) });
            v.ringGeom.CornerRadius({ std::max(0.f, rr - 0.5f), std::max(0.f, rr - 0.5f) });

            if (v.src.widget)
            {
                const float t = v.tone.x;
                const float pa = Clamp01(t - 2.f);
                const bool chip = !widgetsCollapse_;
                v.plateBrush.Color(ToColor(chip ? Mix(pal.chip, pal.widgetStops[3], pa) : Stops(pal.widgetStops, t)));
                v.ringBrush.Color(chip ? pal.chipLine : Color{ 0, 0, 0, 0 });

                // The text color changes once, at the middle of the expansion.
                const bool chipInk = chip && pa < 0.5f;
                if (chipInk != v.chipInk)
                {
                    v.chipInk = chipInk;
                    RenderLabel(v, false);
                    RenderLabel(v, true);
                }
                const bool showDetail = v.tone.target >= 3.f;
                if (showDetail != v.showingDetail)
                {
                    v.showingDetail = showDetail;
                    v.labelBrush.Surface(showDetail ? v.detailSurface : v.briefSurface);
                }
                const auto surface = showDetail ? v.detailSurface : v.briefSurface;
                if (surface)
                {
                    const auto px = surface.SizeInt32();
                    const float lw = static_cast<float>(px.Width), lh = static_cast<float>(px.Height);
                    v.label.Size({ lw, lh });
                    v.label.Offset({ std::round((W - lw) / 2), std::round((H - lh) / 2), 0 });
                }
                v.label.Opacity(chip ? 1.f : pa * pa);
                continue;
            }

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
