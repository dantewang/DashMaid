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

    // Widgets. Index 0: idle. Index 1: second neighbor. Index 2: first neighbor.
    constexpr float WidgetW[3] = { 6.f, 7.f, 8.f };
    constexpr float WidgetH[3] = { 16.f, 20.f, 24.f };
    constexpr float WidgetExpandedH = 34.f;
    constexpr float WidgetPadX = 13.f;
    constexpr float WidgetMaxW = 360.f;
    constexpr float ChipH = 28.f;  // A widget that always shows its text.
    constexpr float ChipPadX = 10.f;
    constexpr float WidgetFontSize = 13.f;
    constexpr float WidgetLineHeight = 18.f;

    // Run box (.sb-run).
    constexpr float RunWidth = 330.f;
    constexpr float RunTextLeft = 12.f;
    constexpr float RunTextRight = 10.f;
    constexpr float RunFontSize = 15.f;
    constexpr float CaretWidth = 1.f;

    // Results (.sb-results, .rs-row). Line heights follow "line-height: 1.55".
    constexpr float ResultsGap = 8.f;
    constexpr float ResultsMinWidth = 380.f;
    constexpr float ResultsPad = 6.f;
    constexpr float ResultsRadius = 14.f;
    constexpr int ResultsMaxRows = 6;
    constexpr float RowPadX = 10.f;
    constexpr float RowPadY = 7.f;
    constexpr float RowRadius = 9.f;
    constexpr float RowGap = 10.f;
    constexpr float RowIconColumn = 26.f;
    constexpr float ResultIcon = 24.f;
    constexpr float CommandIconRadius = 6.f;
    constexpr float TitleFontSize = 13.f;
    constexpr float SubtitleFontSize = 11.5f;
    constexpr float KeyFontSize = 11.f;
    constexpr float LineHeightFactor = 1.55f;
    constexpr float TitleLineHeight = TitleFontSize * LineHeightFactor;
    constexpr float SubtitleLineHeight = SubtitleFontSize * LineHeightFactor;
    constexpr float RowAppHeight = RowPadY * 2 + TitleLineHeight + SubtitleLineHeight;
    constexpr float RowCommandHeight = RowPadY * 2 + ResultIcon;

    // Window margins for the shadows. Each margin holds 3 standard
    // deviations of the largest shadow on that side, plus its offset.
    constexpr float ShadowSide = 60.f;
    constexpr float ShadowTop = 40.f;
    constexpr float ShadowBottom = 55.f;
    constexpr float ResultsShadowBottom = 74.f;

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
