#pragma once

#include <algorithm>

// All lengths are in DIPs. The values come from the reference component
// (72 px track) and are scaled to a 48 px toolbar.
namespace dm::metrics
{
    constexpr float BarHeight = 48.f;
    constexpr float BarHeightHover = 54.f;
    constexpr float PadX = 7.f;
    constexpr float Gap = 14.f;
    constexpr float Radius = 16.f;

    constexpr float Orb = 22.f;
    constexpr float OrbMarginLeft = (BarHeight - Orb) / 2 - PadX;
    constexpr float OrbMarginRight = Gap / 2;
    constexpr float OrbBlock = OrbMarginLeft + Orb + OrbMarginRight;

    // Index 0: idle. Index 1: second neighbor. Index 2: first neighbor.
    constexpr float AppW[3] = { 8.f, 9.f, 11.f };
    constexpr float AppH[3] = { 18.f, 22.f, 26.f };
    constexpr float AppExpanded = 38.f;

    // Run box.
    constexpr float RunWidth = 330.f;
    constexpr float RunTextLeft = 12.f;
    constexpr float RunTextRight = 10.f;
    constexpr float RunLineHeight = 24.f;
    constexpr float RunFontSize = 15.f;
    constexpr float CaretWidth = 1.5f;
    constexpr float CaretHeight = 18.f;

    // Results under the run box.
    constexpr float ResultsGap = 8.f;
    constexpr float ResultsMinWidth = 380.f;
    constexpr float ResultsPad = 6.f;
    constexpr float ResultsRowHeight = 42.f;
    constexpr int ResultsMaxRows = 6;
    constexpr float ResultsRadius = 14.f;
    constexpr float ResultIcon = 24.f;
    constexpr float ResultTextLeft = 44.f;  // From the row left edge.
    constexpr float TitleFontSize = 13.5f;
    constexpr float SubtitleFontSize = 11.5f;

    // Extra width on each side for spring overshoot.
    constexpr float OvershootMargin = 4.f;

    // Distance in DIPs before a press becomes a drag.
    constexpr float DragThreshold = 5.f;

    constexpr float DockTopOffset = 6.f;
    constexpr float FloatDefaultTop = 8.f;

    inline float ItemRadius(float w, float h)
    {
        const float m = std::min(w, h);
        return std::max(0.f, std::min({ w / 2, h / 2, 3.f + std::max(0.f, m - 6.f) * 0.18f }));
    }
}
