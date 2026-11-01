#pragma once
#include <algorithm>
#include <cmath>

// Accessibility: one multiplier for the size of every game text and HUD panel
// (cvar ui_scale, Settings > Other). Main thread only.
inline float& uiScale()
{
    static float scale = 1.0f;
    return scale;
}

// Size that follows the window height (`fraction` of it, kept within lo..hi) times ui_scale.
inline float uiSize(float height, float fraction, float lo, float hi)
{
    return std::round(std::clamp(height * fraction, lo, hi) * uiScale());
}
