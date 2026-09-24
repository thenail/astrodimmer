#pragma once

#include "AstroEngine.h"
#include "AstroSettings.h"

namespace AstroDimmer
{
    /// Where the sun currently has us, for anything that shows an icon.
    enum class AstroStage
    {
        Day,

        /// Sunrise or sunset: the sun is crossing a boundary.
        Transition,

        Night,
    };

    /// The glyph each stage is drawn with. Each is the one the settings page
    /// already uses for the same idea - the daytime brightness sun, the
    /// "Shift sunrise" sun, the night-time moon - so the tray does not teach a
    /// second vocabulary for the same three states.
    inline wchar_t const* StageGlyph(AstroStage stage)
    {
        switch (stage)
        {
        case AstroStage::Transition: return L"";
        case AstroStage::Night: return L"";
        default: return L"";
        }
    }

    /// The stage for a given moment, or Day when there are no coordinates to
    /// work it out from - an icon has to be something.
    ///
    /// Sunrise and sunset last exactly as long as the fade does: the icon
    /// reports what AstroDimmer is DOING, so it shows the crossing while
    /// brightness is being walked from one level to the other, and nothing in
    /// between when fading is off and the switch is instant.
    inline AstroStage StageFor(Core::AstroSettings const& settings, Core::DateTime const& localNow)
    {
        auto state = Core::AstroEngine::GetState(settings.Latitude, settings.Longitude, settings.DayOffset,
                                                 settings.NightOffset, settings.EffectiveFadeMinutes(), localNow);
        if (!state)
            return AstroStage::Day;

        if (state->Progress < 1)
            return AstroStage::Transition;

        return state->Current == Core::AstroEngine::Period::Day ? AstroStage::Day : AstroStage::Night;
    }
}
