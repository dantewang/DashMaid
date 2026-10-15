#include "pch.h"
#include "Scene.h"
#include "Effects.h"
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
        // Fonts of the prototype. "JetBrains Mono" is not part of Windows,
        // so the monospace text uses the next font in the CSS font list.
        constexpr wchar_t TextFont[] = L"Noto Sans SC";
        constexpr wchar_t MonoFont[] = L"Consolas";
        constexpr wchar_t Locale[] = L"zh-CN";

        // Room around the run button for its glow.
        constexpr float OrbMargin = 20.f;

        // The "Search" glyph of the Windows 11 icon font.
        constexpr wchar_t IconFont[] = L"Segoe Fluent Icons";
        constexpr wchar_t OrbGlyph[] = L"\xE721";
        constexpr float OrbGlyphSize = 15.f;

        // backdrop-filter: blur(24px) saturate(1.6). The host backdrop has its
        // own blur (about 20 CSS px, measured), so a second blur adds the rest.
        constexpr float BackdropBlur = 24.f;
        constexpr float HostBlur = 20.f;
        constexpr float TrackSaturate = 1.6f;
        constexpr float ResultsSaturate = 1.5f;

        constexpr float PulsePeriod = 2.4f;

        struct Palette
        {
            Rgb plate;
            Color tintAcrylic;
            Color tintSolid;
            Color border;
            Color resultsAcrylic;
            Color resultsBorder;
            Color resultsSolid;
            uint32_t inputText;
            uint32_t placeholder;
            float placeholderAlpha;
            uint32_t caret;
            uint32_t resultsText;
            uint32_t highlight;
            uint32_t row;
            float rowAlpha;
            Rgb widgetStops[4];  // Collapsible widget: idle, second neighbor, first neighbor, expanded.
            Rgb chip;            // Widget that always shows its text.
            Color chipLine;
            uint32_t chipInk;
            uint32_t widgetInk;  // Text on the expanded widget.
        };

        // The values come from the CSS of the prototype. CSS alpha values are
        // rounded to 8 bits, like a browser does.
        const Palette& PaletteFor(Theme t)
        {
            static const Palette dark{
                { 246, 246, 246 }, { 163, 30, 30, 30 }, { 255, 27, 27, 27 }, { 18, 255, 255, 255 },
                { 224, 30, 30, 30 }, { 20, 255, 255, 255 }, { 255, 27, 27, 27 },
                0xF2F2F2, 0xFFFFFF, 0.42f, 0x8FE0CF, 0xECECEC, 0x8FE0CF, 0xFFFFFF, 0.1f,
                { { 51, 51, 51 }, { 99, 99, 99 }, { 163, 163, 163 }, { 255, 255, 255 } },
                { 46, 46, 46 }, { 15, 255, 255, 255 }, 0xD0D0D0, 0x111111 };
            static const Palette light{
                { 255, 255, 255 }, { 179, 247, 247, 247 }, { 255, 244, 244, 244 }, { 15, 0, 0, 0 },
                { 230, 248, 248, 248 }, { 15, 0, 0, 0 }, { 255, 244, 244, 244 },
                0x111111, 0x000000, 0.4f, 0x2B7A6A, 0x1B1B1B, 0x2B7A6A, 0x000000, 0.06f,
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

        // CSS rgb() with numbers that are not integers: the browser rounds them.
        ColorF Css(Rgb c, float alpha)
        {
            return { Byte(c.r) / 255.f, Byte(c.g) / 255.f, Byte(c.b) / 255.f, Clamp01(alpha) };
        }

        ColorF Css(uint32_t rgb, float alpha)
        {
            return { ((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, Clamp01(alpha) };
        }

        D2D1_COLOR_F D2DColor(uint32_t rgb, float a = 1.f)
        {
            return D2D1::ColorF(rgb, a);
        }

        uint32_t ToRgb32(Rgb c)
        {
            return (static_cast<uint32_t>(Byte(c.r)) << 16) | (static_cast<uint32_t>(Byte(c.g)) << 8) | Byte(c.b);
        }

        // Changes the transform so that the paint code can use DIPs (CSS px).
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

        DWRITE_FONT_METRICS FontMetrics(IDWriteFactory* dw, const wchar_t* family, DWRITE_FONT_WEIGHT weight)
        {
            DWRITE_FONT_METRICS m{};
            m.designUnitsPerEm = 1000;
            m.ascent = 1160;
            m.descent = 288;
            winrt::com_ptr<IDWriteFontCollection> fonts;
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (FAILED(dw->GetSystemFontCollection(fonts.put())) || FAILED(fonts->FindFamilyName(family, &index, &exists)) || !exists)
            {
                return m;
            }
            winrt::com_ptr<IDWriteFontFamily> fam;
            winrt::com_ptr<IDWriteFont> font;
            if (SUCCEEDED(fonts->GetFontFamily(index, fam.put())) &&
                SUCCEEDED(fam->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put())))
            {
                font->GetMetrics(&m);
            }
            return m;
        }

        // The glow color of an app item: the main color of the icon, with a
        // lightness that the toolbar background does not hide. Black icons
        // get a gray glow in the dark theme, and white icons in the light theme.
        Rgb GlowColor(Rgb d, Theme theme)
        {
            const float l = (std::max({ d.r, d.g, d.b }) + std::min({ d.r, d.g, d.b })) / 2 / 255.f;
            if (theme == Theme::Dark)
            {
                constexpr float minL = 0.62f;
                return l >= minL ? d : Mix(d, { 255, 255, 255 }, (minL - l) / (1.f - l));
            }
            constexpr float maxL = 0.45f;
            return l <= maxL ? d : Mix(d, { 0, 0, 0 }, 1.f - maxL / l);
        }

        // The 1 px outline of collapsed items and of the run button.
        Color OutlineColor(Theme theme, float alpha)
        {
            return theme == Theme::Dark ? ToColor(Rgb{ 255, 255, 255 }, 0.4f * alpha) : ToColor(Rgb{ 0, 0, 0 }, 0.3f * alpha);
        }

        // The box-shadow lists of the prototype.
        const ColorF Black{ 0, 0, 0, 1 };

        ColorF WithAlpha(ColorF c, float a)
        {
            c.a = Clamp01(a);
            return c;
        }

        // The run button: a flat circle with a glow in the accent color that
        // pulses while the run box is open.
        ShadowList OrbShadows(uint32_t accent, float pulse)
        {
            const ColorF c{ (accent >> 16 & 0xFF) / 255.f, (accent >> 8 & 0xFF) / 255.f, (accent & 0xFF) / 255.f, 0.f };
            return { { 0, 0, 12 * pulse, 2 * pulse, WithAlpha(c, 0.45f * pulse) } };
        }

        // The shadow of the original component.
        const ShadowList& PanelShadows()
        {
            static const ShadowList list = {
                { 0, 0.4829f, 2.0282f, -1.5f, WithAlpha(Black, 0.06f) },
                { 0, 4.f, 16.8f, -3.f, WithAlpha(Black, 0.04f) },
            };
            return list;
        }

        // DropShadow.BlurRadius for a CSS blur radius (both in pixels). Measured:
        // DropShadow uses a standard deviation of BlurRadius / 3.
        // CSS uses a standard deviation of blur / 2.
        constexpr float DropShadowBlur = 1.5f;

        // CSS: a spread changes the radius by the spread, but a radius of zero stays zero.
        float SpreadRadius(float radius, float spread)
        {
            return radius <= 0.f ? 0.f : std::max(0.f, radius + spread);
        }
    }

    // ---------------------------------------------------------------- setup

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
        stage_ = compositor_.CreateContainerVisual();
        root_.Children().InsertAtTop(stage_);

        track_ = CreatePanel(TrackSaturate);
        orb_ = CreateDrawn();
        itemsLayer_ = compositor_.CreateContainerVisual();

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
        track_.content.Children().InsertAtTop(runBox_);

        // The caret shows for half of the period and hides for the other half.
        auto step = compositor_.CreateStepEasingFunction();
        blink_ = compositor_.CreateScalarKeyFrameAnimation();
        blink_.InsertKeyFrame(0.f, 1.f);
        blink_.InsertKeyFrame(0.5f, 0.f, step);
        blink_.InsertKeyFrame(1.f, 1.f, step);
        blink_.Duration(std::chrono::milliseconds(1060));
        blink_.IterationBehavior(wuc::AnimationIterationBehavior::Forever);

        results_ = CreatePanel(ResultsSaturate);
        resultsBrush_ = compositor_.CreateSurfaceBrush();
        resultsBrush_.Stretch(wuc::CompositionStretch::None);
        resultsBrush_.HorizontalAlignmentRatio(0.f);
        resultsBrush_.VerticalAlignmentRatio(0.f);
        resultsContent_ = compositor_.CreateSpriteVisual();
        resultsContent_.Brush(resultsBrush_);
        results_.content.Children().InsertAtTop(resultsContent_);

        // Paint order of the prototype: the toolbar box, then its children
        // (the orb and the items, which can extend out of the box), then the results.
        auto stage = stage_.Children();
        // The first shadow in a CSS list is on top.
        stage.InsertAtTop(track_.shadows[1].sprite);
        stage.InsertAtTop(track_.shadows[0].sprite);
        stage.InsertAtTop(track_.root);
        stage.InsertAtTop(orb_.sprite);
        stage.InsertAtTop(itemsLayer_);
        stage.InsertAtTop(results_.shadows[1].sprite);
        stage.InsertAtTop(results_.shadows[0].sprite);
        stage.InsertAtTop(results_.root);
        results_.root.IsVisible(false);

        barHeight_.x = barHeight_.target = BarHeight;
        vis_.x = vis_.target = 1.f;
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
        winrt::check_hresult(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, helper_.put()));

        auto interop = compositor_.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        winrt::check_hresult(interop->CreateGraphicsDevice(
            d2dDevice_.get(), reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(winrt::put_abi(graphics_))));
    }

    // 'lineHeight' > 0 sets a CSS line height: the extra space goes equally
    // above and below the glyphs. Otherwise the format uses "line-height: normal".
    winrt::com_ptr<IDWriteTextFormat> Scene::MakeFormat(const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size,
                                                       float lineHeight, bool trim)
    {
        winrt::com_ptr<IDWriteTextFormat> f;
        winrt::check_hresult(dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                                       DWRITE_FONT_STRETCH_NORMAL, size, Locale, f.put()));
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (lineHeight > 0.f)
        {
            const DWRITE_FONT_METRICS m = FontMetrics(dwrite_.get(), family, weight);
            const float em = size / m.designUnitsPerEm;
            const float ascent = m.ascent * em, descent = m.descent * em;
            f->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lineHeight, (lineHeight - ascent - descent) / 2 + ascent);
        }
        if (trim)
        {
            const DWRITE_TRIMMING t{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            winrt::com_ptr<IDWriteInlineObject> sign;
            dwrite_->CreateEllipsisTrimmingSign(f.get(), sign.put());
            f->SetTrimming(&t, sign.get());
        }
        return f;
    }

    void Scene::CreateTextFormats()
    {
        winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                 reinterpret_cast<IUnknown**>(dwrite_.put())));
        inputFormat_ = MakeFormat(TextFont, DWRITE_FONT_WEIGHT_MEDIUM, RunFontSize, 0.f, false);
        titleFormat_ = MakeFormat(TextFont, DWRITE_FONT_WEIGHT_SEMI_BOLD, TitleFontSize, TitleLineHeight, true);
        subtitleFormat_ = MakeFormat(MonoFont, DWRITE_FONT_WEIGHT_NORMAL, SubtitleFontSize, SubtitleLineHeight, true);
        keyFormat_ = MakeFormat(MonoFont, DWRITE_FONT_WEIGHT_NORMAL, KeyFontSize, KeyFontSize * LineHeightFactor, false);
        widgetFormat_ = MakeFormat(TextFont, DWRITE_FONT_WEIGHT_SEMI_BOLD, WidgetFontSize, 0.f, false);
        widgetFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        orbFormat_ = MakeFormat(IconFont, DWRITE_FONT_WEIGHT_NORMAL, OrbGlyphSize, 0.f, false);
        orbFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        orbFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const DWRITE_FONT_METRICS m = FontMetrics(dwrite_.get(), TextFont, DWRITE_FONT_WEIGHT_MEDIUM);
        inputLineHeight_ = static_cast<float>(m.ascent + m.descent + m.lineGap) / m.designUnitsPerEm * RunFontSize;
    }

    Scene::Drawn Scene::CreateDrawn()
    {
        Drawn d;
        d.brush = compositor_.CreateSurfaceBrush();
        d.brush.Stretch(wuc::CompositionStretch::None);
        d.brush.HorizontalAlignmentRatio(0.f);
        d.brush.VerticalAlignmentRatio(0.f);
        d.sprite = compositor_.CreateSpriteVisual();
        d.sprite.Brush(d.brush);
        return d;
    }

    // backdrop-filter: blur(24px) saturate(n).
    wuc::CompositionBrush Scene::BackdropBrush(float saturate)
    {
        auto host = compositor_.CreateHostBackdropBrush();
        try
        {
            winrt::Windows::Graphics::Effects::IGraphicsEffectSource input = wuc::CompositionEffectSourceParameter(L"backdrop");
            const float extra = std::sqrt(BackdropBlur * BackdropBlur - HostBlur * HostBlur) * scale_;
            input = BlurEffect(extra, input);
            auto factory = compositor_.CreateEffectFactory(SaturateEffect(saturate, input));
            auto brush = factory.CreateBrush();
            brush.SetSourceParameter(L"backdrop", host);
            return brush;
        }
        catch (const winrt::hresult_error&)
        {
            return host;
        }
    }

    Scene::Panel Scene::CreatePanel(float saturate)
    {
        auto& c = compositor_;
        Panel p;
        p.saturate = saturate;
        p.root = c.CreateContainerVisual();
        p.clip = c.CreateRoundedRectangleGeometry();
        p.root.Clip(c.CreateGeometricClip(p.clip));
        p.backdrop = c.CreateSpriteVisual();
        p.backdrop.Brush(BackdropBrush(saturate));
        p.tintBrush = c.CreateColorBrush();
        p.tint = c.CreateSpriteVisual();
        p.tint.Brush(p.tintBrush);
        p.content = c.CreateContainerVisual();
        p.borderGeom = c.CreateRoundedRectangleGeometry();
        p.borderBrush = c.CreateColorBrush();
        p.borderShape = c.CreateSpriteShape(p.borderGeom);
        p.borderShape.StrokeBrush(p.borderBrush);
        p.border = c.CreateShapeVisual();
        p.border.Shapes().Append(p.borderShape);
        p.topLine = c.CreateSpriteVisual();
        p.topLine.Brush(p.borderBrush);
        p.topLine.IsVisible(false);
        p.shadows[0] = CreateCaster();
        p.shadows[1] = CreateCaster();

        auto children = p.root.Children();
        children.InsertAtTop(p.backdrop);
        children.InsertAtTop(p.tint);
        children.InsertAtTop(p.border);  // "inset 0 0 0 1px": above the background, below the children.
        children.InsertAtTop(p.topLine);
        children.InsertAtTop(p.content);
        return p;
    }

    // 'flushTop': the top corners are square (border-radius: 0 0 r r). The
    // rounded shapes extend above the panel by the radius, so only their
    // bottom corners show.
    void Scene::LayoutPanel(Panel& p, float x, float y, float w, float h, const PanelStyle& style, bool flushTop)
    {
        const float s = scale_;
        const float2 size{ w * s, h * s };
        const float r = style.radius * s;
        const float up = flushTop ? r : 0.f;
        p.root.Offset({ x * s, y * s, 0 });
        p.root.Size(size);
        p.clip.Offset({ 0, -up });
        p.clip.Size({ size.x, size.y + up });
        p.clip.CornerRadius({ r, r });
        p.backdrop.Size(size);
        p.backdrop.IsVisible(material_ == Material::Acrylic);
        p.tint.Size(size);
        p.tintBrush.Color(style.tint);
        p.content.Size(size);
        p.border.Size(size);
        p.borderShape.StrokeThickness(s);  // 1 CSS px.
        p.borderGeom.Offset({ s / 2, s / 2 - up });
        p.borderGeom.Size({ std::max(0.f, size.x - s), std::max(0.f, size.y + up - s) });
        p.borderGeom.CornerRadius({ std::max(0.f, r - s / 2), std::max(0.f, r - s / 2) });
        p.borderBrush.Color(style.border);
        // The top edge of the inset border, between the two side edges.
        p.topLine.IsVisible(flushTop);
        p.topLine.Offset({ s, 0, 0 });
        p.topLine.Size({ std::max(0.f, size.x - 2 * s), s });

        const float extend = flushTop ? style.radius : 0.f;
        const ShadowList& list = style.shadows;
        for (size_t i = 0; i < std::size(p.shadows); ++i)
        {
            p.shadows[i].sprite.IsVisible(i < list.size());
            if (i < list.size())
            {
                SetCaster(p.shadows[i], x, y - extend, w, h + extend, style.radius, list[i]);
            }
        }
    }

    Scene::Caster Scene::CreateCaster()
    {
        auto& c = compositor_;
        Caster k;
        k.geom = c.CreateRoundedRectangleGeometry();
        auto shape = c.CreateSpriteShape(k.geom);
        shape.FillBrush(c.CreateColorBrush({ 255, 255, 255, 255 }));
        k.shape = c.CreateShapeVisual();
        k.shape.Shapes().Append(shape);
        k.surface = c.CreateVisualSurface();
        k.surface.SourceVisual(k.shape);
        auto mask = c.CreateSurfaceBrush(k.surface);
        mask.Stretch(wuc::CompositionStretch::None);
        mask.HorizontalAlignmentRatio(0.f);
        mask.VerticalAlignmentRatio(0.f);
        k.shadow = c.CreateDropShadow();
        k.shadow.Mask(mask);
        k.sprite = c.CreateSpriteVisual();
        k.sprite.Shadow(k.shadow);
        return k;
    }

    void Scene::SetCaster(Caster& k, float x, float y, float w, float h, float radius, const Shadow& sh)
    {
        const float s = scale_;
        const float d = sh.spread;
        const float sw = std::max(0.f, w + 2 * d) * s, shh = std::max(0.f, h + 2 * d) * s;
        const float r = std::min(SpreadRadius(radius, d) * s, std::min(sw, shh) / 2);
        k.sprite.Offset({ (x - d) * s, (y - d) * s, 0 });
        k.sprite.Size({ sw, shh });
        k.shape.Size({ sw, shh });
        k.geom.Size({ sw, shh });
        k.geom.CornerRadius({ r, r });
        k.surface.SourceSize({ sw, shh });
        k.shadow.Offset({ sh.x * s, sh.y * s, 0 });
        k.shadow.BlurRadius(sh.blur * s * DropShadowBlur);
        k.shadow.Color({ Byte(sh.color.a * 255.f), Byte(sh.color.r * 255.f), Byte(sh.color.g * 255.f), Byte(sh.color.b * 255.f) });
        k.sprite.IsVisible(sh.color.a > 0.f && sw > 0.f && shh > 0.f);
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
    void Scene::Redraw(Drawn& d, float wDip, float hDip, std::vector<float> key, F&& paint)
    {
        const auto px = PxSize(wDip, hDip, scale_);
        key.push_back(scale_);
        key.push_back(static_cast<float>(theme_));
        key.push_back(static_cast<float>(px.Width));
        key.push_back(static_cast<float>(px.Height));
        if (d.surface && key == d.key)
        {
            return;
        }
        if (!d.surface)
        {
            d.surface = graphics_.CreateDrawingSurface2(px, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                        wgd::DirectXAlphaMode::Premultiplied);
            d.brush.Surface(d.surface);
        }
        else if (d.surface.SizeInt32() != px)
        {
            d.surface.Resize(px);
        }
        d.sprite.Size({ static_cast<float>(px.Width), static_cast<float>(px.Height) });
        Paint(d.surface, px.Width, px.Height, [&](ID2D1DeviceContext* dc) {
            UseDips(dc, scale_);
            paint(dc);
        });
        d.key = std::move(key);
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

    // ---------------------------------------------------------------- drawing

    // paintItem() of the prototype for app items: outer shadows, the plate,
    // inset shadows, then the icon clipped to the plate. The compositor draws
    // all parts, so a frame only writes sizes, offsets, and colors.
    void Scene::RenderItem(ItemVisual& v, float cx, float cy, float)
    {
        const float s = scale_;
        const Palette& pal = PaletteFor(theme_);
        const float w = std::max(0.f, v.w.x), h = std::max(0.f, v.h.x), t = v.tone.x;
        const float pa = Clamp01(t - 2.f), g = Clamp01(t / 2.f), q = 1.f - pa;
        const Rgb d = v.src.main;
        const Rgb black{ 0, 0, 0 }, white{ 255, 255, 255 };
        const CollapsedStyle style = v.src.style.value_or(style_);

        // The effects of the prototype were for a 48 px toolbar.
        constexpr float k = 1.5f;
        const Rgb glow = GlowColor(d, theme_);
        Rgb plate;
        float iconSize, iconOpacity, brightness = 1.f, highlight, ring;
        ShadowList outer;
        if (style == CollapsedStyle::Glow)
        {
            const Rgb st[4] = { Mix(d, black, 0.12f), d, Mix(d, white, 0.22f), pal.plate };
            plate = Stops(st, t);
            iconSize = h * 0.74f;
            iconOpacity = pa * pa;
            highlight = 0.45f * q;  // inset 0 1px 0 rgba(255,255,255,.45q)
            ring = q;
            outer = {
                { 0, 0, (7 + 10 * g) * k, (1 + 2 * g) * k, Css(glow, (0.65f + 0.35f * g) * q) },
                { 0, 6 * pa * k, 18 * pa * k, 0, Css(black, 0.32f * pa) },
                { 0, 0, 18 * pa * k, 0, Css(glow, 0.28f * pa) },
            };
        }
        else
        {
            const Rgb st[4] = { Mix(d, black, 0.62f), Mix(d, black, 0.48f), Mix(d, black, 0.3f), pal.plate };
            plate = Stops(st, t);
            iconSize = h * Lerp(1.3f, 0.74f, pa);
            iconOpacity = 1.f;
            brightness = pa >= 1.f ? 1.f : Lerp(0.72f, 1.f, std::max(g, pa));
            highlight = 0.3f * q;  // inset 0 1px 0 rgba(255,255,255,.3q)
            ring = q;
            // The glow shows the item when the icon has the color of the toolbar.
            outer = {
                { 0, 0, 0, 1, Css(black, 0.55f * q) },
                { 0, 0, (6 + 8 * g) * k, (1 + g) * k, Css(glow, (0.6f + 0.3f * g) * q) },
                { 0, (2 + 5 * pa) * k, (5 + 13 * pa) * k, 0, Css(black, 0.3f + 0.08f * pa) },
            };
        }
        if (theme_ == Theme::Light)
        {
            outer.push_back({ 0, 0, 0, 1, Css(black, 0.1f * pa) });
        }

        const float W = w * s, H = h * s;
        const float r = ItemRadius(w, h), R = r * s;
        v.art.Offset({ (cx - w / 2) * s, (cy - h / 2) * s, 0 });
        v.art.Size({ W, H });
        v.art.CenterPoint({ W / 2, H / 2, 0 });
        v.art.Scale({ v.press.x, v.press.x, 1 });

        // The first shadow in the list is on top. casters[0] is the lowest.
        const size_t n = std::min(outer.size(), std::size(v.casters));
        for (size_t i = 0; i < std::size(v.casters); ++i)
        {
            if (i < n)
            {
                SetCaster(v.casters[i], 0, 0, w, h, r, outer[n - 1 - i]);
            }
            else
            {
                v.casters[i].sprite.IsVisible(false);
            }
        }

        v.box.Size({ W, H });
        v.clip.Size({ W, H });
        v.clip.CornerRadius({ R, R });
        v.plate.Size({ W, H });
        v.plateBrush.Color(ToColor(plate));
        v.highlight.Size({ W, H });
        v.highlightBrush.Color(ToColor(white, highlight));
        v.cover.Offset({ 0, s, 0 });
        v.cover.Size({ W, H });
        v.coverClip.Size({ W, H });
        v.coverClip.CornerRadius({ R, R });
        v.coverBrush.Color(ToColor(plate));
        v.ring.Size({ W, H });
        v.ringShape.StrokeThickness(s);
        v.ringGeom.Offset({ s / 2, s / 2 });
        v.ringGeom.Size({ std::max(0.f, W - s), std::max(0.f, H - s) });
        v.ringGeom.CornerRadius({ std::max(0.f, R - s / 2), std::max(0.f, R - s / 2) });
        v.ringBrush.Color(OutlineColor(theme_, ring));

        const float I = iconSize * s;
        const float3 iconOffset{ (W - I) / 2, (H - I) / 2, 0 };
        v.icon.Offset(iconOffset);
        v.icon.Size({ I, I });
        v.icon.Opacity(iconOpacity);
        v.icon.IsVisible(iconOpacity > 0.f && v.iconSurface != nullptr);
        v.dim.Offset(iconOffset);
        v.dim.Size({ I, I });
        v.dim.Opacity(1.f - brightness);
        v.dim.IsVisible(brightness < 1.f && v.iconSurface != nullptr);
    }

    // Draws the icon once, at the largest size that the item shows.
    void Scene::RenderIcon(ItemVisual& v)
    {
        if (!v.src.icon)
        {
            return;
        }
        const float maxSize = std::max(AppExpanded * 0.74f, AppH[2] * 1.3f);
        const auto px = PxSize(maxSize, maxSize, scale_);
        if (!v.iconSurface)
        {
            v.iconSurface = graphics_.CreateDrawingSurface2(px, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                            wgd::DirectXAlphaMode::Premultiplied);
            auto brush = compositor_.CreateSurfaceBrush(v.iconSurface);
            brush.Stretch(wuc::CompositionStretch::Fill);
            brush.BitmapInterpolationMode(wuc::CompositionBitmapInterpolationMode::Linear);
            v.icon.Brush(brush);
            auto mask = compositor_.CreateMaskBrush();
            mask.Source(compositor_.CreateColorBrush({ 255, 0, 0, 0 }));
            mask.Mask(brush);
            v.dim.Brush(mask);
        }
        else if (v.iconSurface.SizeInt32() != px)
        {
            v.iconSurface.Resize(px);
        }
        Paint(v.iconSurface, px.Width, px.Height, [&](ID2D1DeviceContext* dc) {
            if (ID2D1Bitmap* bmp = BitmapFor(v.src.icon.get(), dc))
            {
                const D2D1_RECT_F dst = D2D1::RectF(0, 0, static_cast<float>(px.Width), static_cast<float>(px.Height));
                dc->DrawBitmap(bmp, &dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            }
        });
    }

    // The run button: a flat circle in the color of the widget chips, with a
    // search glyph. Hover makes the circle and the glyph lighter.
    void Scene::RenderOrb(float cx, float cy, float hover, float pulse)
    {
        const float s = scale_;
        const float size = Orb + OrbMargin * 2;
        const float leftPx = cx * s - size * s / 2, topPx = cy * s - size * s / 2;
        const float spriteX = std::floor(leftPx), spriteY = std::floor(topPx);
        const float fx = (leftPx - spriteX) / s, fy = (topPx - spriteY) / s;
        const Palette& pal = PaletteFor(theme_);
        const ShadowList shadows = OrbShadows(pal.caret, pulse);
        const bool dark = theme_ == Theme::Dark;
        const Rgb fill = dark ? Mix(pal.chip, { 255, 255, 255 }, 0.1f * hover) : Mix(pal.chip, { 0, 0, 0 }, 0.06f * hover);
        const Rgb glyph = dark ? Mix({ 163, 163, 163 }, { 255, 255, 255 }, hover) : Mix({ 98, 98, 98 }, { 24, 24, 24 }, hover);

        Redraw(orb_, size, size, { hover, pulse, fx, fy, dark ? 1.f : 0.f }, [&](ID2D1DeviceContext* dc) {
            const float ox = size / 2 + fx, oy = size / 2 + fy, r = Orb / 2;
            const D2D1_RECT_F box = D2D1::RectF(ox - r, oy - r, ox + r, oy + r);
            PaintShadows(dc, helper_.get(), box, r, shadows, false, s);

            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            dc->CreateSolidColorBrush(D2DColor(ToRgb32(fill)), brush.put());
            dc->FillEllipse(D2D1::Ellipse({ ox, oy }, r, r), brush.get());
            // The outline: inset 0 0 0 1px.
            const Color line = OutlineColor(theme_, 1.f);
            brush->SetColor(D2D1::ColorF(line.R / 255.f, line.G / 255.f, line.B / 255.f, line.A / 255.f));
            dc->DrawEllipse(D2D1::Ellipse({ ox, oy }, r - 0.5f / s, r - 0.5f / s), brush.get(), 1.f / s);

            brush->SetColor(D2DColor(ToRgb32(glyph)));
            dc->DrawTextW(OrbGlyph, 1, orbFormat_.get(), box, brush.get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        });

        orb_.sprite.Offset({ spriteX, spriteY, 0 });
        orb_.sprite.CenterPoint({ cx * s - spriteX, cy * s - spriteY, 0 });
        const float k = orbScale_.Value(time_);
        orb_.sprite.Scale({ k, k, 1 });
    }

    void Scene::RenderLabel(ItemVisual& v, bool detail)
    {
        const std::wstring& text = detail ? v.detail : v.brief;
        winrt::com_ptr<IDWriteTextLayout> layout;
        winrt::check_hresult(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), widgetFormat_.get(),
                                                       100000.f, WidgetLineHeight, layout.put()));
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        const float width = m.widthIncludingTrailingWhitespace;
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

    // .sb-run input: "500 15px" text, the placeholder, the selection, and the caret.
    void Scene::RenderInput()
    {
        const float fieldW = RunWidth - RunTextLeft - RunTextRight;
        const auto px = PxSize(fieldW, inputLineHeight_, scale_);
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
                                                       100000.f, inputLineHeight_, inputLayout_.put()));

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
        const bool selection = !empty && selEnd_ > selStart_;
        Paint(inputSurface_, px.Width, px.Height, [&](ID2D1DeviceContext* dc) {
            UseDips(dc, scale_);
            dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            const DWRITE_TEXT_RANGE range{ static_cast<UINT32>(selStart_), static_cast<UINT32>(selEnd_ - selStart_) };
            if (selection)
            {
                // The browser uses the system highlight colors for a selection.
                DWRITE_HIT_TEST_METRICS ranges[8];
                UINT32 count = 0;
                inputLayout_->HitTestTextRange(range.startPosition, range.length, 0, 0, ranges, ARRAYSIZE(ranges), &count);
                winrt::com_ptr<ID2D1SolidColorBrush> sel, selText;
                const COLORREF hl = GetSysColor(COLOR_HIGHLIGHT), ht = GetSysColor(COLOR_HIGHLIGHTTEXT);
                dc->CreateSolidColorBrush(D2D1::ColorF(GetRValue(hl) / 255.f, GetGValue(hl) / 255.f, GetBValue(hl) / 255.f), sel.put());
                dc->CreateSolidColorBrush(D2D1::ColorF(GetRValue(ht) / 255.f, GetGValue(ht) / 255.f, GetBValue(ht) / 255.f), selText.put());
                for (UINT32 i = 0; i < std::min<UINT32>(count, ARRAYSIZE(ranges)); ++i)
                {
                    const auto& r = ranges[i];
                    dc->FillRectangle(D2D1::RectF(r.left - inputScroll_, 0, r.left + r.width - inputScroll_, inputLineHeight_), sel.get());
                }
                inputLayout_->SetDrawingEffect(selText.get(), range);
            }
            winrt::com_ptr<ID2D1SolidColorBrush> text;
            dc->CreateSolidColorBrush(empty ? D2DColor(pal.placeholder, pal.placeholderAlpha) : D2DColor(pal.inputText), text.put());
            dc->DrawTextLayout({ -inputScroll_, 0.f }, inputLayout_.get(), text.get());
        });

        if (empty)
        {
            inputLayout_ = nullptr;
        }
    }

    float Scene::RowHeight(int index) const
    {
        return rows_[index].command ? RowCommandHeight : RowAppHeight;
    }

    float Scene::RowTop(int index) const
    {
        float y = ResultsPad;
        for (int i = 0; i < index; ++i)
        {
            y += RowHeight(i);
        }
        return y;
    }

    // .sb-results and .rs-row.
    void Scene::RenderResults()
    {
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
            winrt::com_ptr<ID2D1SolidColorBrush> rowBrush, title, subtitle, key, highlight, commandBox;
            dc->CreateSolidColorBrush(D2DColor(pal.row, pal.rowAlpha), rowBrush.put());
            dc->CreateSolidColorBrush(D2DColor(pal.resultsText), title.put());
            dc->CreateSolidColorBrush(D2DColor(pal.resultsText, 0.5f), subtitle.put());
            dc->CreateSolidColorBrush(D2DColor(pal.resultsText, 0.55f), key.put());
            dc->CreateSolidColorBrush(D2DColor(pal.highlight), highlight.put());
            dc->CreateSolidColorBrush(D2D1::ColorF(127 / 255.f, 127 / 255.f, 127 / 255.f, 0.18f), commandBox.put());

            // The key column holds "↵" on the selected row only.
            const wchar_t enter[] = L"↵";
            winrt::com_ptr<IDWriteTextLayout> keyLayout;
            dwrite_->CreateTextLayout(enter, 1, keyFormat_.get(), 1000.f, KeyFontSize * LineHeightFactor, keyLayout.put());
            DWRITE_TEXT_METRICS km{};
            keyLayout->GetMetrics(&km);

            const float rowLeft = ResultsPad, rowRight = W - ResultsPad;
            for (int i = 0; i < n; ++i)
            {
                const ResultRow& row = rows_[i];
                const float top = RowTop(i), rowH = RowHeight(i);
                const bool on = i == selected_;
                if (on)
                {
                    dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rowLeft, top, rowRight, top + rowH), RowRadius, RowRadius),
                                             rowBrush.get());
                }

                const float contentTop = top + RowPadY, contentH = rowH - RowPadY * 2;
                const float iconX = rowLeft + RowPadX, iconY = contentTop + (contentH - ResultIcon) / 2;
                const D2D1_RECT_F iconRect = D2D1::RectF(iconX, iconY, iconX + ResultIcon, iconY + ResultIcon);
                if (row.command)
                {
                    dc->FillRoundedRectangle(D2D1::RoundedRect(iconRect, CommandIconRadius, CommandIconRadius), commandBox.get());
                    // The "run" icon: 16 px, stroke 1.4, round caps and joins.
                    winrt::com_ptr<ID2D1PathGeometry> path;
                    d2dFactory_->CreatePathGeometry(path.put());
                    winrt::com_ptr<ID2D1GeometrySink> sink;
                    path->Open(sink.put());
                    const float gx = iconX + 4, gy = iconY + 4;
                    sink->BeginFigure({ gx + 3, gy + 4 }, D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine({ gx + 7, gy + 8 });
                    sink->AddLine({ gx + 3, gy + 12 });
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->BeginFigure({ gx + 8.5f, gy + 12 }, D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine({ gx + 13, gy + 12 });
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    winrt::com_ptr<ID2D1StrokeStyle> stroke;
                    d2dFactory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                               D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                                   nullptr, 0, stroke.put());
                    dc->DrawGeometry(path.get(), title.get(), 1.4f, stroke.get());
                }
                else if (ID2D1Bitmap* bmp = BitmapFor(row.icon.get(), dc))
                {
                    dc->DrawBitmap(bmp, &iconRect, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
                }

                const float keyW = on ? km.widthIncludingTrailingWhitespace : 0.f;
                const float textX = rowLeft + RowPadX + RowIconColumn + RowGap;
                const float textW = std::max(1.f, rowRight - RowPadX - keyW - RowGap - textX);
                const float textH = row.command ? TitleLineHeight : TitleLineHeight + SubtitleLineHeight;
                const float textTop = contentTop + (contentH - textH) / 2;

                winrt::com_ptr<IDWriteTextLayout> t;
                dwrite_->CreateTextLayout(row.title.c_str(), static_cast<UINT32>(row.title.size()), titleFormat_.get(), textW,
                                          TitleLineHeight, t.put());
                if (t && row.highlightStart >= 0 && row.highlightLength > 0)
                {
                    const DWRITE_TEXT_RANGE range{ static_cast<UINT32>(row.highlightStart), static_cast<UINT32>(row.highlightLength) };
                    t->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
                    t->SetDrawingEffect(highlight.get(), range);
                }
                if (t)
                {
                    dc->DrawTextLayout({ textX, textTop }, t.get(), title.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                if (!row.command)
                {
                    const D2D1_RECT_F sr = D2D1::RectF(textX, textTop + TitleLineHeight, textX + textW,
                                                       textTop + TitleLineHeight + SubtitleLineHeight);
                    dc->DrawText(row.subtitle.c_str(), static_cast<UINT32>(row.subtitle.size()), subtitleFormat_.get(), sr,
                                 subtitle.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                if (on)
                {
                    const float kh = KeyFontSize * LineHeightFactor;
                    dc->DrawTextLayout({ rowRight - RowPadX - keyW, contentTop + (contentH - kh) / 2 }, keyLayout.get(), key.get());
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

    // Draws all surfaces again, for example after a change of the display scale or the theme.
    void Scene::Rerender()
    {
        track_.backdrop.Brush(BackdropBrush(track_.saturate));
        results_.backdrop.Brush(BackdropBrush(results_.saturate));
        orb_.key.clear();
        for (auto& v : items_)
        {
            if (!v.src.widget)
            {
                RenderIcon(v);
            }
            else
            {
                RenderLabel(v, false);
                RenderLabel(v, true);
            }
        }
        RenderInput();
        RenderResults();
    }

    // ---------------------------------------------------------------- state

    void Scene::SetScale(float scale)
    {
        scale_ = scale;
        Rerender();
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
            Rerender();
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
            v.slotClip = c.CreateInsetClip();
            v.box = c.CreateContainerVisual();
            v.clip = c.CreateRoundedRectangleGeometry();
            v.box.Clip(c.CreateGeometricClip(v.clip));
            v.plateBrush = c.CreateColorBrush();
            v.plate = c.CreateSpriteVisual();
            v.plate.Brush(v.plateBrush);
            v.ringGeom = c.CreateRoundedRectangleGeometry();
            v.ringBrush = c.CreateColorBrush();
            v.ringShape = c.CreateSpriteShape(v.ringGeom);
            v.ringShape.StrokeBrush(v.ringBrush);
            v.ring = c.CreateShapeVisual();
            v.ring.Shapes().Append(v.ringShape);
            auto box = v.box.Children();
            box.InsertAtTop(v.plate);
            if (v.src.widget)
            {
                v.labelBrush = c.CreateSurfaceBrush();
                v.labelBrush.Stretch(wuc::CompositionStretch::None);
                v.labelBrush.HorizontalAlignmentRatio(0.f);
                v.labelBrush.VerticalAlignmentRatio(0.f);
                v.label = c.CreateSpriteVisual();
                v.label.Brush(v.labelBrush);
                box.InsertAtTop(v.ring);  // Inset shadow: below the text.
                box.InsertAtTop(v.label);
                v.root.Children().InsertAtTop(v.box);
            }
            else
            {
                // The highlight shows only where the cover (the plate, 1 px lower)
                // does not hide it: that is "inset 0 1px 0".
                v.highlightBrush = c.CreateColorBrush();
                v.highlight = c.CreateSpriteVisual();
                v.highlight.Brush(v.highlightBrush);
                v.coverBrush = c.CreateColorBrush();
                v.coverClip = c.CreateRoundedRectangleGeometry();
                v.cover = c.CreateSpriteVisual();
                v.cover.Brush(v.coverBrush);
                v.cover.Clip(c.CreateGeometricClip(v.coverClip));
                v.icon = c.CreateSpriteVisual();
                v.dim = c.CreateSpriteVisual();
                box.InsertAtTop(v.highlight);
                box.InsertAtTop(v.cover);
                box.InsertAtTop(v.icon);
                box.InsertAtTop(v.dim);
                box.InsertAtTop(v.ring);  // The outline of a collapsed item: above the icon.

                v.art = c.CreateContainerVisual();
                for (auto& k : v.casters)
                {
                    k = CreateCaster();
                    v.art.Children().InsertAtTop(k.sprite);
                }
                v.art.Children().InsertAtTop(v.box);
                v.root.Children().InsertAtTop(v.art);
                RenderIcon(v);
            }
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

    void Scene::SetVisible(bool visible, bool fromCursor)
    {
        visible_ = visible;
        scaleFromCursor_ = fromCursor;
    }

    void Scene::SnapVisible(bool visible)
    {
        visible_ = visible;
        vis_.target = visible ? 1.f : 0.f;
        vis_.Snap();
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
            runOpenedAt_ = time_;
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
        if (rows_.size() > static_cast<size_t>(ResultsMaxRows))
        {
            rows_.resize(ResultsMaxRows);
        }
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

    // ---------------------------------------------------------------- layout

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

    float Scene::TrackLeft() const
    {
        return (windowWidth_ - TrackWidth()) / 2;
    }

    // width: max(100%, 380px) of the toolbar box. The results show only while
    // the run box is open, so the width is the width of the open toolbar. The
    // list does not change its width while the run box opens.
    float Scene::ResultsWidth() const
    {
        return std::max(ResultsMinWidth, PadX * 2 + OrbBlock + RunWidth);
    }

    float Scene::ResultsHeight() const
    {
        float h = ResultsPad * 2;
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        {
            h += RowHeight(i);
        }
        return h;
    }

    float Scene::WindowWidth() const
    {
        float m = std::max({ TrackWidthFor(-1), ResultsMinWidth, PadX * 2 + OrbBlock + RunWidth });
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        {
            m = std::max(m, TrackWidthFor(i));
        }
        return m + OvershootMargin * 2 + ShadowSide * 2;
    }

    float Scene::TopMargin()
    {
        return ShadowTop;
    }

    float Scene::RunContentHeight()
    {
        return BarHeightHover + ResultsGap + ResultsPad * 2 + (ResultsMaxRows - 1) * RowAppHeight + RowCommandHeight;
    }

    float Scene::WindowHeight() const
    {
        if (RunVisible())
        {
            return ShadowTop + RunContentHeight() + ResultsShadowBottom;
        }
        return ShadowTop + BarHeightHover + ShadowBottom;
    }

    RECT Scene::RestTrackRect() const
    {
        const float w = TrackWidthFor(-1);
        const float x0 = (windowWidth_ - w) / 2;
        return { static_cast<LONG>(std::floor(x0 * scale_)), static_cast<LONG>(std::floor(ShadowTop * scale_)),
                 static_cast<LONG>(std::ceil((x0 + w) * scale_)), static_cast<LONG>(std::ceil((ShadowTop + BarHeight) * scale_)) };
    }

    RECT Scene::RestRegion() const
    {
        const float w = TrackWidthFor(-1);
        const float x0 = (windowWidth_ - w) / 2;
        return { static_cast<LONG>(std::floor((x0 - RestShadowSide) * scale_)),
                 static_cast<LONG>(std::floor((ShadowTop - RestShadowTop) * scale_)),
                 static_cast<LONG>(std::ceil((x0 + w + RestShadowSide) * scale_)),
                 static_cast<LONG>(std::ceil((ShadowTop + BarHeight + RestShadowBottom) * scale_)) };
    }

    int Scene::HitTestItem(POINT p) const
    {
        if (RunVisible())
        {
            return -1;
        }
        const float x = p.x / scale_, y = p.y / scale_ - ShadowTop;
        const float trackW = TrackWidth();
        const float x0 = TrackLeft();
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
        const float x = p.x / scale_, y = p.y / scale_ - ShadowTop;
        const float cx = TrackLeft() + PadX + OrbMarginLeft + Orb / 2;
        const float cy = barHeight_.x / 2;
        const float r = Orb / 2 + 3;
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
    }

    bool Scene::HitTestTrack(POINT p) const
    {
        const float x = p.x / scale_, y = p.y / scale_ - ShadowTop;
        const float x0 = TrackLeft();
        return y >= 0 && y <= barHeight_.x && x >= x0 && x <= x0 + TrackWidth();
    }

    int Scene::HitTestResult(POINT p) const
    {
        if (!runOpen_ || rows_.empty())
        {
            return -1;
        }
        const float x = p.x / scale_, y = p.y / scale_ - ShadowTop;
        const float left = TrackLeft();
        const float ry = y - (barHeight_.x + ResultsGap);
        if (x < left + ResultsPad || x > left + ResultsWidth() - ResultsPad)
        {
            return -1;
        }
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
        {
            const float top = RowTop(i);
            if (ry >= top && ry < top + RowHeight(i))
            {
                return i;
            }
        }
        return -1;
    }

    int Scene::HitTestInput(POINT p) const
    {
        if (!runOpen_)
        {
            return -1;
        }
        const float x = p.x / scale_, y = p.y / scale_ - ShadowTop;
        const float fieldLeft = TrackLeft() + PadX + OrbBlock;
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
        inputLayout_->HitTestPoint(x - fieldLeft - RunTextLeft + inputScroll_, inputLineHeight_ / 2, &trailing, &inside, &m);
        return static_cast<int>(m.textPosition + (trailing ? m.length : 0));
    }

    POINT Scene::CaretClientPx() const
    {
        const float x = TrackLeft() + PadX + OrbBlock + RunTextLeft + caretX_ - inputScroll_;
        const float y = ShadowTop + (barHeight_.x - inputLineHeight_) / 2;
        return { static_cast<LONG>(std::lround(x * scale_)), static_cast<LONG>(std::lround(y * scale_)) };
    }

    int Scene::CaretHeightPx() const
    {
        return static_cast<int>(std::lround(inputLineHeight_ * scale_));
    }

    bool Scene::Step(float dt)
    {
        time_ += dt;
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
        barHeight_.target = (hovered >= 0 || runOpen_) ? BarHeightHover : BarHeight;
        run_.target = runOpen_ ? 1.f : 0.f;
        vis_.target = visible_ ? 1.f : 0.f;
        orbScale_.Set(orbHover_ ? 1.08f : 1.f, time_);
        orbGlow_.Set(orbHover_ ? 1.f : 0.f, time_);

        std::vector<Spring*> all{ &barHeight_, &run_, &vis_ };
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
        // The orb pulses while the run box is open, so frames continue.
        return !done || orbScale_.Running(time_) || orbGlow_.Running(time_) || runOpen_;
    }

    void Scene::Apply()
    {
        const float s = scale_;
        const Palette& pal = PaletteFor(theme_);
        const float trackW = TrackWidth();
        const float bh = barHeight_.x;
        const float x0 = TrackLeft();
        const float r = std::max(0.f, run_.x);
        const float ro = Clamp01(run_.x);

        // Visibility. The scale starts at 0.7, like the prototype.
        const float vis = std::max(0.f, vis_.x);
        root_.Opacity(Clamp01(vis));
        root_.Scale({ 0.7f + 0.3f * vis, 0.7f + 0.3f * vis, 1 });
        root_.CenterPoint(scaleFromCursor_ ? float3{ (x0 + PadX + OrbMarginLeft + Orb / 2) * s, ShadowTop * s, 0 }
                                           : float3{ (x0 + trackW / 2) * s, (ShadowTop + bh / 2) * s, 0 });
        stage_.Offset({ 0, ShadowTop * s, 0 });

        // The toolbar box.
        LayoutPanel(track_, x0, 0, trackW, bh,
                    { material_ == Material::Acrylic ? pal.tintAcrylic : pal.tintSolid, pal.border,
                      PanelShadows(), Radius },
                    flushTop_);

        // The orb, with its CSS transitions and the pulse of the run box.
        float pulse = 0.f;
        if (runOpen_)
        {
            const float phase = static_cast<float>(std::fmod(time_ - runOpenedAt_, static_cast<double>(PulsePeriod)));
            const float half = PulsePeriod / 2;
            pulse = phase < half ? EaseInOut(phase / half) : 1.f - EaseInOut((phase - half) / half);
        }
        RenderOrb(x0 + PadX + OrbMarginLeft + Orb / 2, bh / 2, orbGlow_.Value(time_), pulse);

        // Run box.
        runBox_.IsVisible(r > 0.002f);
        runBox_.Offset({ (PadX + OrbBlock) * s, 0, 0 });
        runBox_.Size({ r * RunWidth * s, bh * s });
        runBox_.Opacity(ro);
        input_.Offset({ std::round(RunTextLeft * s), std::round((bh - inputLineHeight_) / 2 * s), 0 });
        caretVisual_.Offset({ std::round((RunTextLeft + caretX_ - inputScroll_) * s), std::round((bh - inputLineHeight_) / 2 * s), 0 });
        caretVisual_.Size({ std::max(1.f, std::round(CaretWidth * s)), std::round(inputLineHeight_ * s) });
        caretBrush_.Color(ToColor(pal.caret, 1.f));

        // Results: shown at once when the run box opens, like the prototype.
        results_.root.IsVisible(runOpen_);
        for (auto& k : results_.shadows)
        {
            k.sprite.IsVisible(runOpen_);
        }
        if (runOpen_)
        {
            LayoutPanel(results_, x0, bh + ResultsGap, ResultsWidth(), ResultsHeight(),
                        { material_ == Material::Acrylic ? pal.resultsAcrylic : pal.resultsSolid, pal.resultsBorder, PanelShadows(),
                          ResultsRadius });
        }

        // Items. While the run box is open, each slot clips its item.
        const float keep = 1.f - ro;
        itemsLayer_.IsVisible(keep > 0.001f);
        itemsLayer_.Offset({ x0 * s, 0, 0 });
        float cx = PadX + OrbBlock + r * RunWidth;
        for (auto& v : items_)
        {
            const float w = std::max(0.f, v.w.x), h = std::max(0.f, v.h.x);
            const float slot = (w + Gap) * keep;
            const float centerX = cx + slot / 2;
            v.root.Opacity(keep);
            if (ro > 0.f)
            {
                v.root.Size({ trackW * s, bh * s });
                v.slotClip.LeftInset(cx * s);
                v.slotClip.RightInset(std::max(0.f, (trackW - cx - slot) * s));
                v.root.Clip(v.slotClip);
            }
            else
            {
                v.root.Clip(nullptr);
            }
            cx += slot;

            if (!v.src.widget)
            {
                RenderItem(v, centerX, bh / 2, bh);
                continue;
            }

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
            v.ring.Size({ W, H });
            v.ringShape.StrokeThickness(s);  // inset 0 0 0 1px
            v.ringGeom.Offset({ s / 2, s / 2 });
            v.ringGeom.Size({ std::max(0.f, W - s), std::max(0.f, H - s) });
            v.ringGeom.CornerRadius({ std::max(0.f, rr - s / 2), std::max(0.f, rr - s / 2) });

            const float t = v.tone.x;
            const float pa = Clamp01(t - 2.f);
            const bool chip = !widgetsCollapse_;
            v.plateBrush.Color(ToColor(chip ? Mix(pal.chip, pal.widgetStops[3], pa) : Stops(pal.widgetStops, t)));
            v.ringBrush.Color(chip ? pal.chipLine : OutlineColor(theme_, 1.f - pa));

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
        }
    }
}
