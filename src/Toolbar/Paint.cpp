#include "pch.h"
#include "Paint.h"

namespace dm
{
    namespace
    {
        float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        ColorF LerpColor(const ColorF& a, const ColorF& b, float t)
        {
            // Premultiplied interpolation, like CSS.
            const float alpha = Lerp(a.a, b.a, t);
            if (alpha <= 0.f)
            {
                return {};
            }
            return { Lerp(a.r * a.a, b.r * b.a, t) / alpha, Lerp(a.g * a.a, b.g * b.a, t) / alpha,
                     Lerp(a.b * a.a, b.b * b.a, t) / alpha, alpha };
        }

        D2D1_RECT_F Inflate(const D2D1_RECT_F& r, float d)
        {
            return { r.left - d, r.top - d, r.right + d, r.bottom + d };
        }

        D2D1_RECT_F Offset(const D2D1_RECT_F& r, float x, float y)
        {
            return { r.left + x, r.top + y, r.right + x, r.bottom + y };
        }

        winrt::com_ptr<ID2D1Geometry> Rounded(ID2D1Factory* f, const D2D1_RECT_F& r, float radius)
        {
            winrt::com_ptr<ID2D1RoundedRectangleGeometry> g;
            const float w = std::max(0.f, r.right - r.left), h = std::max(0.f, r.bottom - r.top);
            const float rr = std::clamp(radius, 0.f, std::min(w, h) / 2);
            f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, rr, rr), g.put());
            return g;
        }

        // CSS: a spread changes the radius by the spread, but a radius of zero stays zero.
        float SpreadRadius(float radius, float spread)
        {
            if (radius <= 0.f)
            {
                return 0.f;
            }
            return std::max(0.f, radius + spread);
        }

        void PaintOne(ID2D1DeviceContext* dc, ID2D1DeviceContext* helper, const D2D1_RECT_F& box, float radius,
                      const Shadow& s, float scale)
        {
            if (s.color.a <= 0.f)
            {
                return;
            }
            winrt::com_ptr<ID2D1Factory> factory;
            dc->GetFactory(factory.put());

            winrt::com_ptr<ID2D1Geometry> shape;
            if (!s.inset)
            {
                shape = Rounded(factory.get(), Offset(Inflate(box, s.spread), s.x, s.y), SpreadRadius(radius, s.spread));
            }
            else
            {
                // The area outside the inner box, painted inside the box.
                const auto hole = Rounded(factory.get(), Offset(Inflate(box, -s.spread), s.x, s.y), SpreadRadius(radius, -s.spread));
                const float pad = s.blur * 2 + std::fabs(s.x) + std::fabs(s.y) + std::fabs(s.spread) + 4;
                winrt::com_ptr<ID2D1RectangleGeometry> outer;
                factory->CreateRectangleGeometry(Inflate(box, pad), outer.put());
                ID2D1Geometry* parts[] = { outer.get(), hole.get() };
                winrt::com_ptr<ID2D1GeometryGroup> group;
                factory->CreateGeometryGroup(D2D1_FILL_MODE_ALTERNATE, parts, 2, group.put());
                shape = group;
            }

            D2D1_MATRIX_3X2_F transform;
            dc->GetTransform(&transform);

            winrt::com_ptr<ID2D1Layer> layer;
            if (s.inset)
            {
                const auto clip = Rounded(factory.get(), box, radius);
                dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.get()), nullptr);
            }

            if (s.blur <= 0.01f)
            {
                winrt::com_ptr<ID2D1SolidColorBrush> brush;
                dc->CreateSolidColorBrush(ToD2D(s.color), brush.put());
                dc->FillGeometry(shape.get(), brush.get());
            }
            else
            {
                // Record the shape in device pixels, then blur it with the
                // standard deviation of half the blur radius.
                winrt::com_ptr<ID2D1CommandList> list;
                helper->CreateCommandList(list.put());
                helper->SetTarget(list.get());
                helper->BeginDraw();
                helper->SetTransform(transform);
                winrt::com_ptr<ID2D1SolidColorBrush> white;
                helper->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), white.put());
                helper->FillGeometry(shape.get(), white.get());
                helper->EndDraw();
                helper->SetTarget(nullptr);
                list->Close();

                winrt::com_ptr<ID2D1Effect> effect;
                if (SUCCEEDED(dc->CreateEffect(CLSID_D2D1Shadow, effect.put())))
                {
                    effect->SetInput(0, list.get());
                    effect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, s.blur / 2 * scale);
                    effect->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(s.color.r, s.color.g, s.color.b, s.color.a));
                    effect->SetValue(D2D1_SHADOW_PROP_OPTIMIZATION, D2D1_SHADOW_OPTIMIZATION_QUALITY);
                    dc->SetTransform(D2D1::Matrix3x2F::Identity());
                    dc->DrawImage(effect.get());
                    dc->SetTransform(transform);
                }
            }

            if (s.inset)
            {
                dc->PopLayer();
            }
        }
    }

    D2D1_COLOR_F ToD2D(const ColorF& c)
    {
        return D2D1::ColorF(c.r, c.g, c.b, c.a);
    }

    ShadowList Interpolate(const ShadowList& a, const ShadowList& b, float t)
    {
        const size_t n = std::max(a.size(), b.size());
        ShadowList out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            Shadow sa = i < a.size() ? a[i] : Shadow{};
            Shadow sb = i < b.size() ? b[i] : Shadow{};
            if (i >= a.size())
            {
                sa.inset = sb.inset;
            }
            if (i >= b.size())
            {
                sb.inset = sa.inset;
            }
            if (sa.inset != sb.inset)
            {
                // CSS cannot interpolate these lists. It changes at the middle.
                return t < 0.5f ? a : b;
            }
            out.push_back({ Lerp(sa.x, sb.x, t), Lerp(sa.y, sb.y, t), std::max(0.f, Lerp(sa.blur, sb.blur, t)),
                            Lerp(sa.spread, sb.spread, t), LerpColor(sa.color, sb.color, t), sa.inset });
        }
        return out;
    }

    float CubicBezier(float x1, float y1, float x2, float y2, float x)
    {
        if (x <= 0.f)
        {
            return 0.f;
        }
        if (x >= 1.f)
        {
            return 1.f;
        }
        auto bx = [&](float t) { return 3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t; };
        auto by = [&](float t) { return 3 * (1 - t) * (1 - t) * t * y1 + 3 * (1 - t) * t * t * y2 + t * t * t; };
        // x(t) increases on [0, 1] because 0 <= x1, x2 <= 1, so bisection finds t.
        float lo = 0.f, hi = 1.f, t = x;
        for (int i = 0; i < 40; ++i)
        {
            t = (lo + hi) / 2;
            if (bx(t) < x)
            {
                lo = t;
            }
            else
            {
                hi = t;
            }
        }
        return by(t);
    }

    void Transition::Set(float target, double now)
    {
        if (target == to_)
        {
            return;
        }
        const bool running = Running(now);
        const float current = Value(now);
        if (running && target == reversingFrom_)
        {
            const float x = static_cast<float>((now - start_) / length_);
            const float progress = CubicBezier(x1_, y1_, x2_, y2_, x);
            factor_ = std::clamp(std::fabs(progress * factor_ + 1 - factor_), 0.f, 1.f);
            reversingFrom_ = to_;
        }
        else
        {
            factor_ = 1.f;
            reversingFrom_ = current;
        }
        from_ = current;
        to_ = target;
        start_ = now;
        length_ = duration_ * factor_;
    }

    float Transition::Value(double now)
    {
        if (length_ <= 0 || now >= start_ + length_)
        {
            value_ = to_;
            return value_;
        }
        const float x = static_cast<float>(std::max(0.0, (now - start_) / length_));
        value_ = from_ + (to_ - from_) * CubicBezier(x1_, y1_, x2_, y2_, x);
        return value_;
    }

    void PaintShadows(ID2D1DeviceContext* dc, ID2D1DeviceContext* helper, const D2D1_RECT_F& box, float radius,
                      const ShadowList& shadows, bool inset, float scale)
    {
        // The first shadow in the list is on top, so paint the list from the end.
        for (auto it = shadows.rbegin(); it != shadows.rend(); ++it)
        {
            if (it->inset == inset)
            {
                PaintOne(dc, helper, box, radius, *it, scale);
            }
        }
    }
}
