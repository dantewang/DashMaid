#pragma once

#include <vector>

namespace dm
{
    struct ColorF
    {
        float r = 0, g = 0, b = 0, a = 0;  // 0..1, straight alpha.
    };

    // One CSS box-shadow. Lengths are CSS px.
    struct Shadow
    {
        float x = 0, y = 0, blur = 0, spread = 0;
        ColorF color;
        bool inset = false;
    };
    using ShadowList = std::vector<Shadow>;

    // Interpolates two shadow lists like CSS does. The shorter list gets
    // transparent shadows with the "inset" value of the other list.
    // Colors are interpolated with premultiplied alpha.
    ShadowList Interpolate(const ShadowList& a, const ShadowList& b, float t);

    // CSS cubic-bezier() timing function. 'x' is the time fraction, 0..1.
    float CubicBezier(float x1, float y1, float x2, float y2, float x);
    inline float Ease(float x) { return CubicBezier(0.25f, 0.1f, 0.25f, 1.f, x); }
    inline float EaseInOut(float x) { return CubicBezier(0.42f, 0.f, 0.58f, 1.f, x); }

    // A CSS transition of one number, with the reversing rule of CSS
    // Transitions: a change back to the start value takes a shorter time.
    class Transition
    {
    public:
        Transition(float duration, float x1, float y1, float x2, float y2, float value)
            : duration_(duration), x1_(x1), y1_(y1), x2_(x2), y2_(y2), from_(value), to_(value), value_(value) {}

        void Set(float target, double now);
        float Value(double now);
        bool Running(double now) const { return from_ != to_ && now < start_ + length_; }
        float Target() const { return to_; }

    private:
        float duration_, x1_, y1_, x2_, y2_;
        float from_, to_, value_;
        float reversingFrom_ = 0;  // Start value before the reversing adjustment.
        float factor_ = 1;         // Reversing shortening factor.
        double start_ = 0;
        double length_ = 0;
    };
}

#ifdef _WIN32
#include <d2d1_1.h>

namespace dm
{
    // Paints the shadows of a rounded box like a browser. 'box' and 'radius'
    // are in CSS px. The transform of 'dc' maps CSS px to device pixels and
    // has the scale 'scale'. 'helper' records the shadow shapes. It must be a
    // context of the same device that is not drawing now.
    // 'inset' selects the inset shadows (paint them after the background) or
    // the outer shadows (paint them before the background).
    void PaintShadows(ID2D1DeviceContext* dc, ID2D1DeviceContext* helper, const D2D1_RECT_F& box, float radius,
                      const ShadowList& shadows, bool inset, float scale);

    D2D1_COLOR_F ToD2D(const ColorF& c);
}
#endif
