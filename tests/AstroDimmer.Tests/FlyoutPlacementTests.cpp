// Ported from Glimmer.Tests/FlyoutPlacementTests.cs.

#include "Test.h"
#include "FlyoutPlacement.h"

using namespace AstroDimmer::Core;

namespace
{
    // The machine this was diagnosed on: 2560x1440 DIPs, 48 DIP taskbar.
    constexpr double W = 2560, H = 1440, Bar = 48;
}

TEST(Bottom_taskbar_is_read_from_the_work_area_inset)
{
    // The shell reported ABE_LEFT for exactly this layout, because it never
    // wrote uEdge and ABE_LEFT is zero.
    CHECK(FlyoutPlacement::DockedEdge(0, 0, W, H, 0, 0, W, H - Bar) == ScreenEdge::Bottom);
}

TEST(Each_edge_is_derived_from_its_own_inset)
{
    CHECK(FlyoutPlacement::DockedEdge(0, 0, W, H, 0, 0, W, H - Bar) == ScreenEdge::Bottom);
    CHECK(FlyoutPlacement::DockedEdge(0, 0, W, H, 0, Bar, W, H) == ScreenEdge::Top);
    CHECK(FlyoutPlacement::DockedEdge(0, 0, W, H, Bar, 0, W, H) == ScreenEdge::Left);
    CHECK(FlyoutPlacement::DockedEdge(0, 0, W, H, 0, 0, W - Bar, H) == ScreenEdge::Right);
}

TEST(An_uninset_work_area_has_no_derivable_edge)
{
    // Auto-hide reserves nothing, so the caller has to fall back.
    CHECK(!FlyoutPlacement::DockedEdge(0, 0, W, H, 0, 0, W, H));
}

TEST(The_widest_inset_wins_over_a_second_appbar)
{
    CHECK(FlyoutPlacement::DockedEdge(0, 0, W, H, 30, 0, W, H - Bar) == ScreenEdge::Bottom);
}

TEST(A_bottom_taskbar_rests_the_panel_in_the_bottom_right_corner)
{
    auto p = FlyoutPlacement::Rest(ScreenEdge::Bottom, 0, 0, W, H - Bar, 356, 123, 12);

    CHECK_EQ(2560.0 - 356 - 12, p.Left);
    CHECK_EQ(1440.0 - 48 - 123 - 12, p.Top);
}

TEST(A_left_taskbar_rests_the_panel_in_the_bottom_left_corner)
{
    auto p = FlyoutPlacement::Rest(ScreenEdge::Left, Bar, 0, W, H, 356, 123, 12);

    CHECK_EQ(Bar + 12, p.Left);
    CHECK_EQ(1440.0 - 123 - 12, p.Top);
}

TEST(A_top_taskbar_rests_the_panel_in_the_top_right_corner)
{
    auto p = FlyoutPlacement::Rest(ScreenEdge::Top, 0, Bar, W, H, 356, 123, 12);

    CHECK_EQ(2560.0 - 356 - 12, p.Left);
    CHECK_EQ(Bar + 12, p.Top);
}

TEST(The_slide_starts_flush_against_a_bottom_taskbar)
{
    auto rest = FlyoutPlacement::Rest(ScreenEdge::Bottom, 0, 0, W, H - Bar, 356, 97, 12);
    auto start = FlyoutPlacement::SlideOrigin(ScreenEdge::Bottom, 2192, rest.Top, 12);

    CHECK_EQ(2192.0, start.Left);         // unchanged across the slide
    CHECK_EQ(H - Bar - 97, start.Top);    // bottom edge on the taskbar
    CHECK_EQ(rest.Top + 12, start.Top);
}

TEST(The_slide_always_begins_on_the_taskbar_side_of_rest)
{
    CHECK((FlyoutPlacement::SlideOrigin(ScreenEdge::Bottom, 100, 200, 12) == Point{ 100, 212 }));
    CHECK((FlyoutPlacement::SlideOrigin(ScreenEdge::Top, 100, 200, 12) == Point{ 100, 188 }));
    CHECK((FlyoutPlacement::SlideOrigin(ScreenEdge::Left, 100, 200, 12) == Point{ 88, 200 }));
    CHECK((FlyoutPlacement::SlideOrigin(ScreenEdge::Right, 100, 200, 12) == Point{ 112, 200 }));
}

TEST(A_side_taskbar_slides_the_panel_sideways)
{
    CHECK(FlyoutPlacement::SlidesHorizontally(ScreenEdge::Left));
    CHECK(FlyoutPlacement::SlidesHorizontally(ScreenEdge::Right));
    CHECK(!FlyoutPlacement::SlidesHorizontally(ScreenEdge::Top));
    CHECK(!FlyoutPlacement::SlidesHorizontally(ScreenEdge::Bottom));
}

TEST(A_panel_taller_than_the_work_area_is_clamped_on_screen)
{
    auto p = FlyoutPlacement::Rest(ScreenEdge::Bottom, 0, 0, W, 200, 356, 900, 12);

    CHECK_EQ(0.0, p.Top);
    CHECK_RANGE(0, W - 356, p.Left);
}
