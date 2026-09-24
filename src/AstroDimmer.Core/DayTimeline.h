#pragma once

#include <vector>

/// Where the day/night bands fall on a 00:00-24:00 strip.
///
/// Split out from the control that draws it because all of the awkwardness
/// here is arithmetic, not painting: day can wrap past midnight, and so can a
/// fade that starts late enough in the evening. Both cases produce TWO pieces
/// that have to read as one band.
///
/// Minutes past local midnight throughout, matching AstroEngine.
namespace AstroDimmer::Core::DayTimeline
{
    /// A stretch of the strip, in minutes past midnight.
    struct Segment
    {
        int Start;
        int Length;

        int End() const { return Start + Length; }
    };

    /// One drawn piece of a band that may have been cut at midnight. Offset is
    /// how far into the WHOLE band this piece begins, so a split band still
    /// shades continuously across the cut instead of restarting.
    struct BandPiece
    {
        int Start;
        int Length;
        int Offset;
    };

    /// The stretches that use the daytime level. Empty when the boundaries are
    /// identical, which AstroEngine treats as "cannot be determined" rather
    /// than a zero-length day.
    std::vector<Segment> DaySegments(int dayStart, int nightStart);

    /// A band of the given length beginning at start, cut into the pieces
    /// that fit on the strip. A band longer than the day is clipped to one
    /// full day rather than lapping itself, since the second lap would paint
    /// over the first with the wrong shading.
    std::vector<BandPiece> SplitBand(int start, int length);
}
