#pragma once

#include <algorithm>

// All lengths are in DIPs. The toolbar and item sizes are the sizes of the
// reference component (72 px track). The run box, the effects of the items,
// and the shadow margins come from the prototype, which used a 48 px
// toolbar, multiplied by 1.5. The results list keeps the prototype sizes.
namespace dm::metrics
{
    constexpr float BarHeight = 72.f;
    constexpr float BarHeightHover = 80.f;
    constexpr float PadX = 20.f;
    constexpr float Gap = 24.f;
    constexpr float Radius = 24.f;

    constexpr float Orb = 32.f;
    constexpr float OrbMarginLeft = (BarHeight - Orb) / 2 - PadX;
    constexpr float OrbMarginRight = Gap / 2;
    constexpr float OrbBlock = OrbMarginLeft + Orb + OrbMarginRight;

    // Index 0: idle. Index 1: second neighbor. Index 2: first neighbor.
    constexpr float AppW[3] = { 8.f, 10.f, 12.f };
    constexpr float AppH[3] = { 24.f, 30.f, 36.f };
    constexpr float AppExpanded = 48.f;

    // Widgets. Index 0: idle. Index 1: second neighbor. Index 2: first neighbor.
    constexpr float WidgetW[3] = { 8.f, 10.f, 12.f };
    constexpr float WidgetH[3] = { 24.f, 30.f, 36.f };
    constexpr float WidgetExpandedH = 48.f;
    constexpr float WidgetPadX = 18.f;
    constexpr float WidgetMaxW = 540.f;
    constexpr float ChipH = 42.f;  // A widget that always shows its text.
    constexpr float ChipPadX = 15.f;
    constexpr float WidgetFontSize = 19.f;
    constexpr float WidgetLineHeight = 27.f;

    // Run box (.sb-run).
    constexpr float RunWidth = 480.f;
    constexpr float RunTextLeft = 16.f;
    constexpr float RunTextRight = 14.f;
    constexpr float RunFontSize = 20.f;
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

    // Window margins for the shadows of the expanded items. Each margin holds
    // 2.5 standard deviations of the largest shadow on that side.
    constexpr float ShadowSide = 21.f;
    constexpr float ShadowTop = 24.f;
    constexpr float ShadowBottom = 36.f;
    constexpr float ResultsShadowBottom = 15.f;

    // The visible part of the toolbar shadow at rest. The window region
    // includes it.
    constexpr float RestShadowSide = 8.f;
    constexpr float RestShadowTop = 5.f;
    constexpr float RestShadowBottom = 12.f;

    // Extra width on each side for spring overshoot.
    constexpr float OvershootMargin = 6.f;

    // Distance in DIPs before a press becomes a drag.
    constexpr float DragThreshold = 5.f;

    constexpr float DockTopOffset = 9.f;
    constexpr float FloatDefaultTop = 12.f;

    inline float ItemRadius(float w, float h)
    {
        // border-radius: 6px of the reference component. CSS limits it to half the shorter side.
        return std::max(0.f, std::min({ w / 2, h / 2, 6.f }));
    }
}
