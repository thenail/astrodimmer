// Built-in panels: tying a WMI instance to the display it drives, and
// predicting where a stepped backlight will settle.

#include "Test.h"
#include "Backlight.h"

using namespace AstroDimmer::Core;

TEST(Instance_name_becomes_the_device_key)
{
    auto key = Backlight::KeyOf(LR"(DISPLAY\BOE0747\4&2d4ab9b&0&UID8388688_0)");
    CHECK(key.has_value());
    CHECK_EQ(std::wstring(LR"(\\?\DISPLAY#BOE0747#4&2d4ab9b&0&UID8388688)"), *key);
}

TEST(Instance_name_without_a_suffix_is_kept_whole)
{
    auto key = Backlight::KeyOf(LR"(DISPLAY\SDC4161\5&1a2b3c&0&UID265988)");
    CHECK(key.has_value());
    CHECK_EQ(std::wstring(LR"(\\?\DISPLAY#SDC4161#5&1a2b3c&0&UID265988)"), *key);
}

TEST(Instance_names_in_other_forms_are_refused)
{
    CHECK(!Backlight::KeyOf(L"").has_value());
    CHECK(!Backlight::KeyOf(L"DISPLAY").has_value());
    CHECK(!Backlight::KeyOf(LR"(DISPLAY\BOE0747)").has_value());
    CHECK(!Backlight::KeyOf(LR"(DISPLAY\BOE0747\)").has_value());
    CHECK(!Backlight::KeyOf(LR"(DISPLAY\\x)").has_value());
    CHECK(!Backlight::KeyOf(LR"(DISPLAY\BOE0747\a\b)").has_value());
}

TEST(Snap_lands_on_the_nearest_level)
{
    std::vector<int> levels{ 0, 10, 25, 50, 75, 100 };
    CHECK_EQ(25, Backlight::Snap(30, levels));
    CHECK_EQ(50, Backlight::Snap(40, levels));
    CHECK_EQ(100, Backlight::Snap(100, levels));
    CHECK_EQ(0, Backlight::Snap(4, levels));
}

TEST(Snap_breaks_a_tie_upwards)
{
    CHECK_EQ(20, Backlight::Snap(15, { 10, 20 }));
}

TEST(Snap_without_levels_keeps_the_percentage)
{
    CHECK_EQ(37, Backlight::Snap(37, {}));
}
