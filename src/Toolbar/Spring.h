#pragma once

#include <cmath>

namespace dm
{
    struct SpringParams
    {
        float stiffness = 420.f;
        float damping = 30.f;
    };

    // Damped spring, integrated with semi-implicit Euler steps.
    struct Spring
    {
        float x = 0.f;
        float v = 0.f;
        float target = 0.f;

        void Step(float h, const SpringParams& p)
        {
            const float a = p.stiffness * (target - x) - p.damping * v;
            v += a * h;
            x += v * h;
        }

        void Snap()
        {
            x = target;
            v = 0.f;
        }

        bool AtRest() const
        {
            return std::fabs(target - x) < 0.01f && std::fabs(v) < 0.02f;
        }
    };

    struct Rgb
    {
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
    };
}
