// Starting levels for a display seen for the first time, and the one-off
// bookkeeping that decides which displays count as "first time".

#include "Test.h"
#include "DisplayDefaults.h"
#include "Json.h"
#include "Settings.h"

using namespace AstroDimmer::Core;

namespace
{
    std::vector<uint8_t> EdidWithSize(uint8_t widthCm, uint8_t heightCm)
    {
        std::vector<uint8_t> edid(128, 0);
        uint8_t header[] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
        std::copy(std::begin(header), std::end(header), edid.begin());
        edid[21] = widthCm;
        edid[22] = heightCm;
        return edid;
    }

    ObservedDisplay Display(std::wstring key, int brightness, std::optional<int> contrast = 70,
                            std::optional<double> inches = 31.5)
    {
        return { std::move(key), brightness, contrast, inches };
    }
}

TEST(Night_ratio_shrinks_with_screen_size)
{
    CHECK_NEAR(0.40, NightRatio(13.3), 1e-9);
    CHECK_NEAR(0.40, NightRatio(24.0), 1e-9);
    CHECK_NEAR(0.35, NightRatio(27.0), 1e-9);
    CHECK_NEAR(0.30, NightRatio(32.0), 1e-9);
    CHECK_NEAR(0.30, NightRatio(55.0), 1e-9);
    CHECK_NEAR(0.325, NightRatio(29.5), 1e-9);
}

TEST(Night_ratio_falls_back_when_the_size_is_unknown)
{
    CHECK_NEAR(0.35, NightRatio(std::nullopt), 1e-9);
    CHECK_NEAR(0.35, NightRatio(0.0), 1e-9);
}

TEST(Day_is_the_current_level_and_night_a_fraction_of_it)
{
    // The Dell U3225QE as it reads: 46% brightness, 70 contrast, 31.5".
    auto levels = RecommendLevels(Display(L"dell", 46));
    CHECK_EQ(46, levels.Day);
    CHECK_EQ(14, levels.Night);
    CHECK(!levels.Contrast);
    CHECK_EQ(70, levels.DayContrast);
    CHECK_EQ(70, levels.NightContrast);
}

TEST(A_very_low_current_level_is_not_taken_as_the_day_level)
{
    auto levels = RecommendLevels(Display(L"dim", 5, 50, 24.0));
    CHECK_EQ(20, levels.Day);
    CHECK_EQ(8, levels.Night);
}

TEST(A_display_without_contrast_keeps_the_contrast_defaults)
{
    auto levels = RecommendLevels(Display(L"laptop", 80, std::nullopt, 14.0));
    DisplayLevels defaults;
    CHECK_EQ(defaults.DayContrast, levels.DayContrast);
    CHECK_EQ(defaults.NightContrast, levels.NightContrast);
    CHECK_EQ(32, levels.Night);
}

TEST(Edid_size_is_read_in_centimetres)
{
    CHECK_NEAR(31.5, *EdidDiagonalInches(EdidWithSize(70, 39)), 0.1);
    CHECK(!EdidDiagonalInches(EdidWithSize(0, 0)));
    CHECK(!EdidDiagonalInches(EdidWithSize(79, 0)));
    CHECK(!EdidDiagonalInches(std::vector<uint8_t>(64, 0)));

    auto bad = EdidWithSize(70, 39);
    bad[1] = 0x00;
    CHECK(!EdidDiagonalInches(bad));
}

TEST(A_new_display_is_seeded_once)
{
    AppSettings s;
    CHECK(AdoptDisplays(s, { Display(L"dell", 46) }));
    CHECK_EQ(46, s.Astro.LevelsFor(L"dell").Day);
    CHECK_EQ(14, s.Astro.LevelsFor(L"dell").Night);

    // Seen again at the level the schedule set it to: nothing changes.
    CHECK(!AdoptDisplays(s, { Display(L"dell", 14) }));
    CHECK_EQ(46, s.Astro.LevelsFor(L"dell").Day);
}

TEST(A_display_with_its_own_levels_keeps_them)
{
    AppSettings s;
    s.Astro.PerDisplay[L"dell"] = DisplayLevels{ 80, 10 };
    CHECK(AdoptDisplays(s, { Display(L"dell", 46) }));
    CHECK_EQ(80, s.Astro.LevelsFor(L"dell").Day);
    CHECK(s.KnownDisplays.contains(L"dell"));
}

TEST(An_upgrade_records_present_displays_without_seeding_them)
{
    // A file from before KnownDisplays: its monitors were already scheduled.
    auto root = Json::Parse(LR"({ "Astro": { "DayBrightness": 90, "NightBrightness": 20 } })");
    auto s = AppSettings::FromJson(*root);
    CHECK(!s.TracksKnownDisplays);

    // A failed probe must not complete the upgrade.
    CHECK(!AdoptDisplays(s, {}));
    CHECK(!s.TracksKnownDisplays);

    CHECK(AdoptDisplays(s, { Display(L"dell", 20) }));
    CHECK(s.TracksKnownDisplays);
    CHECK(!s.Astro.PerDisplay.contains(L"dell"));
    CHECK_EQ(90, s.Astro.LevelsFor(L"dell").Day);

    // A monitor plugged in afterwards is new, and is seeded.
    CHECK(AdoptDisplays(s, { Display(L"dell", 20), Display(L"lg", 60, 50, 27.0) }));
    CHECK_EQ(60, s.Astro.LevelsFor(L"lg").Day);
    CHECK_EQ(21, s.Astro.LevelsFor(L"lg").Night);
}

TEST(Known_displays_survive_a_round_trip)
{
    AppSettings s;
    AdoptDisplays(s, { Display(L"dell", 46) });

    auto back = AppSettings::FromJson(*Json::Parse(Json::Write(s.ToJson())));
    CHECK(back.TracksKnownDisplays);
    CHECK(back.KnownDisplays.contains(L"dell"));
}

TEST(An_untracked_file_is_saved_without_a_partial_list)
{
    auto s = AppSettings::FromJson(*Json::Parse(LR"({ "Astro": {} })"));
    s.KnownDisplays.insert(L"stray");

    auto back = AppSettings::FromJson(*Json::Parse(Json::Write(s.ToJson())));
    CHECK(!back.TracksKnownDisplays);
}

TEST(A_fresh_install_seeds_the_first_displays_it_sees)
{
    // No settings file at all: nothing has driven these monitors yet.
    AppSettings s;
    CHECK(s.TracksKnownDisplays);
    CHECK(AdoptDisplays(s, { Display(L"dell", 46) }));
    CHECK(s.Astro.PerDisplay.contains(L"dell"));
}
