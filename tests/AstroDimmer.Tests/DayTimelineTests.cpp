// Ported from Glimmer.Tests/DayTimelineTests.cs.

#include "Test.h"
#include "AstroEngine.h"
#include "DayTimeline.h"

using namespace AstroDimmer::Core;

namespace
{
    constexpr int Day = AstroEngine::MinutesPerDay;
}

TEST(An_ordinary_day_is_one_segment)
{
    auto segments = DayTimeline::DaySegments(6 * 60, 20 * 60);

    CHECK_EQ(size_t{ 1 }, segments.size());
    CHECK_EQ(6 * 60, segments[0].Start);
    CHECK_EQ(14 * 60, segments[0].Length);
}

TEST(Day_wrapping_midnight_is_drawn_at_both_ends)
{
    // Night begins after midnight, so daylight owns both ends of the strip.
    auto segments = DayTimeline::DaySegments(4 * 60, 1 * 60);

    CHECK_EQ(size_t{ 2 }, segments.size());
    CHECK_EQ(0, segments[0].Start);
    CHECK_EQ(60, segments[0].End());
    CHECK_EQ(4 * 60, segments[1].Start);
    CHECK_EQ(Day, segments[1].End());
}

TEST(Identical_boundaries_draw_nothing)
{
    CHECK(DayTimeline::DaySegments(7 * 60, 7 * 60).empty());
}

TEST(Segments_never_leave_the_strip)
{
    std::pair<int, int> cases[] = { { 6 * 60, 20 * 60 }, { 4 * 60, 1 * 60 }, { 0, 12 * 60 }, { 23 * 60, 30 } };

    for (auto [dayStart, nightStart] : cases)
    {
        for (auto const& segment : DayTimeline::DaySegments(dayStart, nightStart))
        {
            CHECK(segment.Start >= 0);
            CHECK(segment.End() <= Day);
            CHECK(segment.Length > 0);
        }
    }
}

TEST(A_band_inside_the_day_is_one_piece)
{
    auto pieces = DayTimeline::SplitBand(6 * 60, 30);

    CHECK_EQ(size_t{ 1 }, pieces.size());
    CHECK_EQ(6 * 60, pieces[0].Start);
    CHECK_EQ(30, pieces[0].Length);
    CHECK_EQ(0, pieces[0].Offset);
}

TEST(A_band_running_past_midnight_carries_its_offset_to_the_second_piece)
{
    // 23:50 with a 30 minute fade: 10 minutes tonight, 20 after midnight.
    auto pieces = DayTimeline::SplitBand(23 * 60 + 50, 30);

    CHECK_EQ(size_t{ 2 }, pieces.size());
    CHECK_EQ(23 * 60 + 50, pieces[0].Start);
    CHECK_EQ(10, pieces[0].Length);
    CHECK_EQ(0, pieces[0].Offset);
    CHECK_EQ(0, pieces[1].Start);
    CHECK_EQ(20, pieces[1].Length);
    CHECK_EQ(10, pieces[1].Offset);
}

TEST(Pieces_of_a_split_band_add_up_to_the_whole_band)
{
    int total = 0;
    for (auto const& piece : DayTimeline::SplitBand(23 * 60, 180))
        total += piece.Length;

    CHECK_EQ(180, total);
}

TEST(A_band_ending_exactly_at_midnight_is_not_split)
{
    auto pieces = DayTimeline::SplitBand(Day - 60, 60);

    CHECK_EQ(size_t{ 1 }, pieces.size());
    CHECK_EQ(Day - 60, pieces[0].Start);
    CHECK_EQ(60, pieces[0].Length);
}

TEST(A_start_outside_the_day_wraps_onto_the_strip)
{
    auto pieces = DayTimeline::SplitBand(-30, 15);

    CHECK_EQ(size_t{ 1 }, pieces.size());
    CHECK_EQ(Day - 30, pieces[0].Start);
}

TEST(A_band_with_no_length_is_not_drawn)
{
    CHECK(DayTimeline::SplitBand(6 * 60, 0).empty());
    CHECK(DayTimeline::SplitBand(6 * 60, -30).empty());
}

TEST(A_band_longer_than_a_day_is_clipped_rather_than_lapping_itself)
{
    auto pieces = DayTimeline::SplitBand(0, Day * 2);

    CHECK_EQ(size_t{ 1 }, pieces.size());
    CHECK_EQ(0, pieces[0].Start);
    CHECK_EQ(Day, pieces[0].Length);
}
