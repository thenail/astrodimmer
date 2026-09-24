// Ported from Glimmer.Tests/MonitorMatcherTests.cs.
//
// Matching decides which physical screen a slider drives, and the setups
// where it can go wrong - clone mode, identical panels, QueryDisplayConfig
// failing - are ones no single desk has.

#include "Test.h"
#include "MonitorMatcher.h"

using namespace AstroDimmer::Core;

namespace
{
    const std::wstring Src1 = LR"(\\.\DISPLAY1)";
    const std::wstring Src2 = LR"(\\.\DISPLAY2)";
    const std::wstring Guid = L"#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}";

    std::wstring Path(std::wstring const& id) { return LR"(\\?\DISPLAY#)" + id + Guid; }
    std::wstring Key(std::wstring const& id) { return LR"(\\?\DISPLAY#)" + id; }
}

TEST(Key_drops_the_interface_class_suffix)
{
    CHECK_EQ(Key(L"DEL436E#4&3dfd0c9&0&UID24642"), MonitorMatcher::KeyOf(Path(L"DEL436E#4&3dfd0c9&0&UID24642")));
}

TEST(Key_of_a_path_without_suffix_is_the_path)
{
    CHECK_EQ(Key(L"DEL436E#1"), MonitorMatcher::KeyOf(Key(L"DEL436E#1")));
}

TEST(The_development_machine_matches_as_it_did_under_the_old_layer)
{
    // dxva2 says "Dell U3225QE (DP)" while Windows says "DELL U3225QE", so
    // this is a positional match - and the key must come out byte-identical
    // to Glimmer's, since settings are keyed by it.
    const std::wstring id = L"DEL436E#4&3dfd0c9&0&UID24642";
    MonitorMatcher matcher({ { Src1, Path(id), L"DELL U3225QE" } },
                           { { Src1, LR"(\\.\DISPLAY1\Monitor0)", Path(id) } });

    auto m = matcher.Match(Src1, { L"Dell U3225QE (DP)" }, 0);

    CHECK(m.has_value());
    CHECK_EQ(Key(id), m->DeviceKey);
    CHECK_EQ(Path(id), m->DevicePath);
    CHECK_EQ(std::wstring(LR"(\\.\DISPLAY1\Monitor0)"), m->DeviceName);
    CHECK(m->Method == MatchMethod::TargetOrder);
}

TEST(Description_beats_position_when_the_orders_disagree)
{
    // Clone mode: two monitors on one source, listed in opposite orders.
    MonitorMatcher matcher({ { Src1, Path(L"AAA0001#1"), L"Alpha" }, { Src1, Path(L"BBB0002#2"), L"Beta" } }, {});
    std::vector<std::wstring> descriptions{ L"Beta", L"Alpha" };

    auto first = matcher.Match(Src1, descriptions, 0);
    auto second = matcher.Match(Src1, descriptions, 1);

    CHECK_EQ(Key(L"BBB0002#2"), first->DeviceKey);
    CHECK_EQ(Key(L"AAA0001#1"), second->DeviceKey);
    CHECK(first->Method == MatchMethod::Description);
    CHECK(second->Method == MatchMethod::Description);
}

TEST(Identical_panels_fall_back_to_position)
{
    MonitorMatcher matcher({ { Src1, Path(L"DEL0001#1"), L"DELL P2419H" }, { Src1, Path(L"DEL0001#2"), L"DELL P2419H" } }, {});
    std::vector<std::wstring> descriptions{ L"DELL P2419H", L"DELL P2419H" };

    CHECK_EQ(Key(L"DEL0001#1"), matcher.Match(Src1, descriptions, 0)->DeviceKey);
    CHECK_EQ(Key(L"DEL0001#2"), matcher.Match(Src1, descriptions, 1)->DeviceKey);
}

TEST(A_key_is_never_handed_out_twice)
{
    // Two handles on one key would put one screen's slider on the other.
    MonitorMatcher matcher({ { Src1, Path(L"AAA0001#1"), L"Alpha" } }, {});
    std::vector<std::wstring> descriptions{ L"Alpha", L"Unknown" };

    CHECK_EQ(Key(L"AAA0001#1"), matcher.Match(Src1, descriptions, 0)->DeviceKey);
    CHECK(!matcher.Match(Src1, descriptions, 1));
}

TEST(Targets_on_other_sources_are_not_considered)
{
    MonitorMatcher matcher({ { Src1, Path(L"AAA0001#1"), L"Alpha" }, { Src2, Path(L"BBB0002#2"), L"Beta" } }, {});

    auto m = matcher.Match(Src2, { L"Alpha" }, 0);

    CHECK_EQ(Key(L"BBB0002#2"), m->DeviceKey);
    CHECK(m->Method == MatchMethod::TargetOrder);
}

TEST(Without_QueryDisplayConfig_devices_are_matched_by_enumeration_order)
{
    MonitorMatcher matcher({}, {
        { Src1, LR"(\\.\DISPLAY1\Monitor0)", Path(L"AAA0001#1") },
        { Src2, LR"(\\.\DISPLAY2\Monitor0)", Path(L"BBB0002#2") },
        { Src2, LR"(\\.\DISPLAY2\Monitor1)", Path(L"CCC0003#3") },
    });

    auto m = matcher.Match(Src2, { L"x", L"y" }, 1);

    CHECK(m.has_value());
    CHECK_EQ(Key(L"CCC0003#3"), m->DeviceKey);
    CHECK_EQ(std::wstring(LR"(\\.\DISPLAY2\Monitor1)"), m->DeviceName);
    CHECK(m->Method == MatchMethod::DeviceOrder);
}

TEST(A_target_with_no_device_entry_gets_a_synthesised_name)
{
    MonitorMatcher matcher({ { Src2, Path(L"AAA0001#1"), L"Alpha" }, { Src2, Path(L"BBB0002#2"), L"Beta" } }, {});

    auto m = matcher.Match(Src2, { L"", L"" }, 1);

    CHECK_EQ(std::wstring(LR"(\\.\DISPLAY2\Monitor1)"), m->DeviceName);
    CHECK_EQ(Path(L"BBB0002#2"), m->DevicePath);
}

TEST(A_monitor_with_no_evidence_at_all_is_unmatched)
{
    MonitorMatcher matcher({}, {});
    CHECK(!matcher.Match(Src1, { L"Alpha" }, 0));
}
