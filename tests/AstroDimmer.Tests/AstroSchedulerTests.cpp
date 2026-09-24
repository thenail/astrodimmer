// Ported from Glimmer.Tests/AstroSchedulerTests.cs.
//
// The scheduler's value is entirely in its edge cases - manual override, echo
// rejection, fade stepping. Each is pinned here, because none of them are
// observable without waiting hours in the real app.

#include "Test.h"
#include "AstroScheduler.h"

using namespace AstroDimmer::Core;
using AstroEngine::Period;

namespace
{
    constexpr double Lat = 59.3293;
    constexpr double Lon = 18.0686;

    BrightnessTarget Target(std::wstring key, int brightness, bool hidden = false, int day = 100, int night = 30)
    {
        return { std::move(key), brightness, hidden, day, night };
    }

    AstroScheduler Make(bool fade = false, int fadeMinutes = 60)
    {
        AstroScheduler s;
        s.Settings.Enabled = true;
        s.Settings.Latitude = Lat;
        s.Settings.Longitude = Lon;
        s.Settings.DayBrightness = 100;
        s.Settings.NightBrightness = 30;
        s.Settings.FadeEnabled = fade;
        s.Settings.FadeMinutes = fadeMinutes;
        return s;
    }

    /// Solar noon on a summer day: unambiguously daytime.
    DateTime Midday() { return { 2026, 6, 21, 12, 0 }; }

    /// Deep winter night: unambiguously night.
    DateTime Midnight() { return { 2026, 12, 21, 23, 30 }; }

    std::vector<BrightnessTarget> One(int brightness = 0) { return { Target(L"a", brightness) }; }

    DateTime MidFade()
    {
        auto b = *AstroEngine::GetBoundaries(Lat, Lon, 0, 0, Midday());
        return Midday().Date().AddMinutes(b.DayStart + 30);
    }
}

TEST(Disabled_does_nothing)
{
    auto s = Make();
    s.Settings.Enabled = false;
    CHECK(s.Tick(Midday(), One()).Outcome == AstroOutcome::Disabled);
}

TEST(Paused_does_nothing)
{
    auto s = Make();
    s.Paused = true;
    CHECK(s.Tick(Midday(), One()).Outcome == AstroOutcome::Paused);
}

TEST(Unset_coordinates_report_no_solar_data)
{
    auto s = Make();
    s.Settings.Latitude = 0;
    s.Settings.Longitude = 0;
    CHECK(s.Tick(Midday(), One()).Outcome == AstroOutcome::NoSolarData);
}

TEST(Applies_day_brightness_at_midday)
{
    auto tick = Make().Tick(Midday(), One());

    CHECK(tick.Outcome == AstroOutcome::Applied);
    CHECK_EQ(100, tick.Brightness);
    CHECK(tick.Period == Period::Day);
    CHECK_EQ(size_t{ 1 }, tick.Writes.size());
}

TEST(Applies_night_brightness_at_night)
{
    auto tick = Make().Tick(Midnight(), One());

    CHECK_EQ(30, tick.Brightness);
    CHECK(tick.Period == Period::Night);
}

TEST(Second_tick_is_settled_once_displays_match)
{
    auto s = Make();
    s.Tick(Midday(), One());

    // Display now reports the level we just wrote.
    CHECK(s.Tick(Midday(), One(100)).Outcome == AstroOutcome::Settled);
}

TEST(Drift_triggers_a_re_apply)
{
    auto s = Make();
    s.Tick(Midday(), One());

    // Something else moved the display; we should re-assert.
    auto tick = s.Tick(Midday(), One(55));

    CHECK(tick.Outcome == AstroOutcome::Applied);
    CHECK_EQ(100, tick.Brightness);
}

TEST(Manual_change_holds_for_the_rest_of_the_period)
{
    auto s = Make();
    s.Tick(Midday(), One());

    s.NoteManualChange(42, true, Midday());

    CHECK(s.ManualOverridePeriod() == Period::Day);
    CHECK(s.Tick(Midday(), One(42)).Outcome == AstroOutcome::ManualOverride);
}

TEST(Manual_override_survives_a_forced_tick)
{
    auto s = Make();
    s.Tick(Midday(), One());
    s.NoteManualChange(42, true, Midday());

    CHECK(s.Tick(Midday(), One(42), true).Outcome == AstroOutcome::ManualOverride);
}

TEST(An_override_set_before_the_first_tick_is_discarded)
{
    // The first tick sees the last seen period change from "none" and treats
    // that as a boundary crossing. Preserved rather than fixed: the
    // prototype's behaviour is the specification.
    auto s = Make();
    s.NoteManualChange(42, true, Midday());

    CHECK(s.Tick(Midday(), One(42)).Outcome == AstroOutcome::Applied);
    CHECK(!s.ManualOverridePeriod());
}

TEST(Crossing_a_boundary_clears_the_override)
{
    auto s = Make();
    s.NoteManualChange(42, true, Midday());
    CHECK(s.ManualOverridePeriod().has_value());

    auto tick = s.Tick(Midnight(), One(42));

    CHECK(tick.Outcome == AstroOutcome::Applied);
    CHECK(!s.ManualOverridePeriod());
}

TEST(A_level_we_just_wrote_is_an_echo_not_a_manual_change)
{
    auto s = Make();
    s.Tick(Midday(), One()); // writes 100

    s.NoteManualChange(100, true, Midday());

    CHECK(!s.ManualOverridePeriod());
}

TEST(Either_periods_configured_level_is_treated_as_an_echo)
{
    auto s = Make();

    // 30 is the night level; a window pushing it during the day is an echo.
    s.NoteManualChange(30, true, Midday());

    CHECK(!s.ManualOverridePeriod());
}

TEST(Changes_not_from_the_user_never_override)
{
    auto s = Make();
    s.NoteManualChange(42, false, Midday());
    CHECK(!s.ManualOverridePeriod());
}

TEST(Hidden_displays_are_not_written)
{
    auto s = Make();
    auto tick = s.Tick(Midday(), { Target(L"visible", 0), Target(L"hidden", 0, true) });

    CHECK_EQ(size_t{ 1 }, tick.Writes.size());
    CHECK_EQ(std::wstring(L"visible"), tick.Writes[0].Key);
}

TEST(Mid_fade_the_same_level_is_not_written_twice)
{
    auto s = Make(true, 60);

    auto first = s.Tick(MidFade(), One());
    CHECK(first.Outcome == AstroOutcome::Applied);
    CHECK_EQ(size_t{ 1 }, first.Writes.size());

    // Same minute, same computed level: nothing more to send.
    auto second = s.Tick(MidFade(), One());
    CHECK(second.Outcome == AstroOutcome::Applied);
    CHECK(second.Writes.empty());
}

TEST(Fading_blends_between_the_two_levels)
{
    auto s = Make(true, 60);
    auto tick = s.Tick(MidFade(), One());

    CHECK(tick.IsFading);
    CHECK_EQ(65, tick.Brightness); // halfway from 30 to 100
}

// ------------------------------------------------ fade read-back

namespace
{
    BrightnessTarget ReadAt(int readBack, std::wstring key = L"a")
    {
        auto t = Target(std::move(key), readBack);
        t.ReadBack = readBack;
        return t;
    }

    DateTime MidFadePlus(int minutes) { return MidFade().AddMinutes(minutes); }
}

TEST(A_fade_continues_while_displays_read_back_our_step)
{
    auto s = Make(true, 60);
    int step = s.Tick(MidFade(), One()).Writes.at(0).Brightness;

    auto next = s.Tick(MidFadePlus(1), { ReadAt(step) });

    CHECK(next.Outcome == AstroOutcome::Applied);
    CHECK(!s.ManualOverridePeriod());
}

TEST(A_display_changed_mid_fade_stops_the_fade)
{
    auto s = Make(true, 60);
    int step = s.Tick(MidFade(), One()).Writes.at(0).Brightness;

    auto tick = s.Tick(MidFadePlus(1), { ReadAt(step - 20) });

    CHECK(tick.Outcome == AstroOutcome::Interrupted);
    CHECK(tick.Writes.empty());
    CHECK(s.ManualOverridePeriod() == Period::Day);
}

TEST(An_interrupted_fade_stays_stopped_until_the_next_boundary)
{
    auto s = Make(true, 60);
    int step = s.Tick(MidFade(), One()).Writes.at(0).Brightness;
    s.Tick(MidFadePlus(1), { ReadAt(step - 20) });

    // Later in the same fade, and after it would have finished.
    CHECK(s.Tick(MidFadePlus(10), One(step - 20)).Outcome == AstroOutcome::ManualOverride);
    CHECK(s.Tick(Midday(), One(step - 20)).Outcome == AstroOutcome::ManualOverride);

    // Sunset hands control back.
    CHECK(s.Tick(Midnight(), One(step - 20)).Outcome == AstroOutcome::Applied);
}

TEST(One_changed_display_stops_the_fade_for_all)
{
    auto s = Make(true, 60);
    auto first = s.Tick(MidFade(), { Target(L"a", 0), Target(L"b", 0) });
    int step = first.Writes.at(0).Brightness;

    auto tick = s.Tick(MidFadePlus(1), { ReadAt(step, L"a"), ReadAt(step + 5, L"b") });

    CHECK(tick.Outcome == AstroOutcome::Interrupted);
}

TEST(The_first_step_of_a_fade_is_not_judged_against_the_previous_period)
{
    // Night was overridden to 42; at sunrise the panel still reads 42, which
    // is not a change made during this fade.
    auto s = Make(true, 60);
    s.Tick(Midnight(), One());
    s.NoteManualChange(42, true, Midnight());

    auto tick = s.Tick(MidFade(), { ReadAt(42) });

    CHECK(tick.Outcome == AstroOutcome::Applied);
}

TEST(A_forced_run_mid_fade_ignores_the_read_back)
{
    // A display that has just appeared reads whatever it powered on with.
    auto s = Make(true, 60);
    int step = s.Tick(MidFade(), One()).Writes.at(0).Brightness;

    CHECK(s.Tick(MidFadePlus(1), { ReadAt(step - 20) }, true).Outcome == AstroOutcome::Applied);
    CHECK(!s.ManualOverridePeriod());
}

TEST(A_hidden_display_does_not_stop_the_fade)
{
    auto s = Make(true, 60);
    int step = s.Tick(MidFade(), One()).Writes.at(0).Brightness;

    auto hidden = ReadAt(step - 20);
    hidden.Hidden = true;

    CHECK(s.Tick(MidFadePlus(1), { hidden }).Outcome == AstroOutcome::Applied);
}

// ------------------------------------------------ per-display levels

TEST(Each_display_is_written_its_own_level)
{
    auto s = Make();
    auto tick = s.Tick(Midday(), { Target(L"bright", 0, false, 100, 30), Target(L"dim", 0, false, 60, 10) });

    std::vector<AstroWrite> expected{ { L"bright", 100 }, { L"dim", 60 } };
    CHECK(tick.Writes == expected);
}

TEST(Night_uses_each_displays_own_night_level)
{
    auto s = Make();
    auto tick = s.Tick(Midnight(), { Target(L"a", 0, false, 100, 30), Target(L"b", 0, false, 100, 5) });

    CHECK_EQ(size_t{ 2 }, tick.Writes.size());
    CHECK_EQ(30, tick.Writes[0].Brightness);
    CHECK_EQ(5, tick.Writes[1].Brightness);
}

TEST(Settled_requires_every_display_to_be_at_its_own_level)
{
    auto s = Make();
    std::vector<BrightnessTarget> atRest{ Target(L"a", 100, false, 100), Target(L"b", 60, false, 60) };

    s.Tick(Midday(), atRest);
    CHECK(s.Tick(Midday(), atRest).Outcome == AstroOutcome::Settled);

    // One display drifts off ITS level - which is not the other's level, the
    // case a single global figure could not tell apart.
    std::vector<BrightnessTarget> drifted{ Target(L"a", 100, false, 100), Target(L"b", 100, false, 60) };
    CHECK(s.Tick(Midday(), drifted).Outcome == AstroOutcome::Applied);
}

TEST(Fade_step_skipping_is_tracked_per_display)
{
    auto s = Make(true, 60);
    std::vector<BrightnessTarget> targets{ Target(L"a", 0, false, 100, 30), Target(L"b", 0, false, 60, 10) };

    auto first = s.Tick(MidFade(), targets);
    CHECK_EQ(size_t{ 2 }, first.Writes.size());

    // Both are already at their own step, so neither is written again.
    CHECK(s.Tick(MidFade(), targets).Writes.empty());
}

TEST(An_echo_is_judged_against_that_displays_own_levels)
{
    auto s = Make();
    DisplayLevels dim;
    dim.Day = 60;
    dim.Night = 10;
    s.Settings.PerDisplay[L"dim"] = dim;

    s.Tick(Midday(), { Target(L"dim", 0, false, 60, 10) });

    // 60 is what that display was just set to: an echo, not a choice.
    s.NoteManualChange(60, true, Midday(), std::wstring(L"dim"));

    CHECK(!s.ManualOverridePeriod());
}

TEST(Reset_clears_override_and_history)
{
    auto s = Make();
    s.NoteManualChange(42, true, Midday());
    s.Reset();

    CHECK(!s.ManualOverridePeriod());
    CHECK(s.Tick(Midday(), One(100)).Outcome == AstroOutcome::Applied);
}
