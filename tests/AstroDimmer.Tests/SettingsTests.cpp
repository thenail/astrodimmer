// New in AstroDimmer: Glimmer leaned on System.Text.Json, so it had nothing
// to test here. The JSON and the settings mapping are hand-written now, and
// the one thing they must never do is misread a Glimmer file on import.

#include "Test.h"
#include "DisplayNames.h"
#include "Json.h"
#include "Settings.h"

using namespace AstroDimmer::Core;

TEST(A_glimmer_settings_file_is_read_field_for_field)
{
    // Glimmer's file as System.Text.Json writes it, read-only extras and all.
    auto root = Json::Parse(LR"({
      "Astro": {
        "Enabled": true,
        "Latitude": 57.71,
        "Longitude": -11.97,
        "DayOffset": 30,
        "NightOffset": -15,
        "DayBrightness": 90,
        "NightBrightness": 20,
        "PerDisplay": {
          "\\\\?\\DISPLAY#DEL436E#4&3dfd0c9&0&UID24642": {
            "Day": 80, "Night": 10, "Contrast": true, "DayContrast": 70, "NightContrast": 40
          }
        },
        "FadeEnabled": true,
        "FadeMinutes": 45,
        "EffectiveFadeMinutes": 45
      },
      "HiddenDisplays": [ "SIMULATED\\2" ],
      "SoftwareRendering": false
    })");

    CHECK(root.has_value());
    auto s = AppSettings::FromJson(*root);

    CHECK(s.Astro.Enabled);
    CHECK_EQ(57.71, s.Astro.Latitude);
    CHECK_EQ(-11.97, s.Astro.Longitude);
    CHECK_EQ(30, s.Astro.DayOffset);
    CHECK_EQ(-15, s.Astro.NightOffset);
    CHECK_EQ(90, s.Astro.DayBrightness);
    CHECK_EQ(20, s.Astro.NightBrightness);
    CHECK(s.Astro.FadeEnabled);
    CHECK_EQ(45, s.Astro.EffectiveFadeMinutes());

    auto levels = s.Astro.LevelsFor(LR"(\\?\DISPLAY#DEL436E#4&3dfd0c9&0&UID24642)");
    CHECK_EQ(80, levels.Day);
    CHECK_EQ(10, levels.Night);
    CHECK(levels.Contrast);
    CHECK_EQ(70, levels.DayContrast);
    CHECK_EQ(40, levels.NightContrast);

    CHECK(s.HiddenDisplays.contains(LR"(SIMULATED\2)"));
}

TEST(Settings_survive_a_round_trip)
{
    AppSettings s;
    s.Astro.Enabled = true;
    s.Astro.Latitude = 59.33;
    s.Astro.Longitude = 18.07;
    s.Astro.FadeEnabled = true;
    s.Astro.FadeMinutes = 60;
    s.Astro.PerDisplay[L"key \"quoted\""] = DisplayLevels{ 55, 12, true, 60, 30 };
    s.HiddenDisplays.insert(L"hidden");

    auto text = Json::Write(s.ToJson());
    auto back = AppSettings::FromJson(*Json::Parse(text));

    CHECK_EQ(59.33, back.Astro.Latitude);
    CHECK_EQ(18.07, back.Astro.Longitude);
    CHECK_EQ(60, back.Astro.FadeMinutes);
    CHECK_EQ(55, back.Astro.LevelsFor(L"key \"quoted\"").Day);
    CHECK(back.HiddenDisplays.contains(L"hidden"));
}

TEST(A_missing_or_broken_file_gives_defaults)
{
    CHECK(!Json::Parse(L"{ \"Astro\": "));
    CHECK(!Json::Parse(L"not json"));

    auto s = AppSettings::Load(L"C:\\this\\path\\does\\not\\exist.json");
    CHECK(!s.Astro.Enabled);
    CHECK_EQ(100, s.Astro.DayBrightness);
    CHECK_EQ(30, s.Astro.NightBrightness);
}

TEST(Numbers_are_read_whatever_the_locale)
{
    // A decimal-comma locale must not turn 57.71 into 57.
    auto value = Json::Parse(L"[57.71, -0.5, 1e3]");
    CHECK_EQ(57.71, *value->Items()[0].AsNumber());
    CHECK_EQ(-0.5, *value->Items()[1].AsNumber());
    CHECK_EQ(1000.0, *value->Items()[2].AsNumber());
}

TEST(Whole_numbers_are_written_without_a_fraction)
{
    CHECK_EQ(std::wstring(L"30"), Json::Write(Json::Value(30)));
    CHECK_EQ(std::wstring(L"57.71"), Json::Write(Json::Value(57.71)));
}

// ------------------------------------------------------------ names

TEST(The_vendor_comes_from_the_pnp_code)
{
    CHECK_EQ(std::wstring(L"Dell"), *DisplayNames::Vendor(LR"(\\?\DISPLAY#DEL436E#4&3dfd0c9&0&UID24642)"));
    CHECK_EQ(std::wstring(L"XYZ"), *DisplayNames::Vendor(LR"(\\?\DISPLAY#XYZ1234#1)"));
    CHECK(!DisplayNames::Vendor(L"SIMULATED\\1"));
}

TEST(The_display_number_is_the_one_windows_shows)
{
    CHECK_EQ(2, *DisplayNames::DisplayNumber(LR"(\\.\DISPLAY2)"));
    CHECK_EQ(12, *DisplayNames::DisplayNumber(LR"(\\.\DISPLAY12)"));
    CHECK(!DisplayNames::DisplayNumber(L"nothing here"));
}

TEST(A_generic_name_gives_way_to_the_model_on_the_wire)
{
    auto name = DisplayNames::Resolve(std::wstring(L"Generic PnP Monitor"),
                                      L"(prot(monitor)type(lcd)model(U3225QE)cmds(01 02))",
                                      LR"(\\.\DISPLAY1)", LR"(\\?\DISPLAY#DEL436E#1)");
    CHECK_EQ(std::wstring(L"U3225QE"), name);
}

TEST(Windows_friendly_name_wins_when_it_is_real)
{
    auto name = DisplayNames::Resolve(std::wstring(L"DELL U3225QE"), L"model(Other)", LR"(\\.\DISPLAY1)",
                                      LR"(\\?\DISPLAY#DEL436E#1)");
    CHECK_EQ(std::wstring(L"DELL U3225QE"), name);
}

TEST(With_nothing_else_the_maker_and_number_are_used)
{
    auto name = DisplayNames::Resolve(std::nullopt, L"", LR"(\\.\DISPLAY2)", LR"(\\?\DISPLAY#DEL436E#1)");
    CHECK_EQ(std::wstring(L"Dell Display 2"), name);
}
