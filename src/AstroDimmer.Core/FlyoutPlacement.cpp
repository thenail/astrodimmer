#include "pch.h"
#include "FlyoutPlacement.h"

namespace AstroDimmer::Core::FlyoutPlacement
{
    std::optional<ScreenEdge> DockedEdge(double screenLeft, double screenTop, double screenRight,
                                         double screenBottom, double workLeft, double workTop,
                                         double workRight, double workBottom)
    {
        double left = workLeft - screenLeft;
        double top = workTop - screenTop;
        double right = screenRight - workRight;
        double bottom = screenBottom - workBottom;

        double widest = std::max(std::max(left, top), std::max(right, bottom));

        // A hair of rounding is not a taskbar.
        if (widest < 1.0) return std::nullopt;

        // Bottom first, then right: the common cases win ties.
        if (bottom >= widest) return ScreenEdge::Bottom;
        if (right >= widest) return ScreenEdge::Right;
        if (top >= widest) return ScreenEdge::Top;
        return ScreenEdge::Left;
    }

    Point Rest(ScreenEdge edge, double workLeft, double workTop, double workRight, double workBottom,
               double width, double height, double gap)
    {
        double left, top;
        switch (edge)
        {
        case ScreenEdge::Top:
            left = workRight - width - gap;
            top = workTop + gap;
            break;
        case ScreenEdge::Left:
            left = workLeft + gap;
            top = workBottom - height - gap;
            break;
        default:
            left = workRight - width - gap;
            top = workBottom - height - gap;
            break;
        }

        left = std::clamp(left, workLeft, std::max(workLeft, workRight - width));
        top = std::clamp(top, workTop, std::max(workTop, workBottom - height));

        return { left, top };
    }

    Point SlideOrigin(ScreenEdge edge, double restLeft, double restTop, double gap)
    {
        switch (edge)
        {
        case ScreenEdge::Top: return { restLeft, restTop - gap };
        case ScreenEdge::Left: return { restLeft - gap, restTop };
        case ScreenEdge::Right: return { restLeft + gap, restTop };
        default: return { restLeft, restTop + gap };
        }
    }

    bool SlidesHorizontally(ScreenEdge edge)
    {
        return edge == ScreenEdge::Left || edge == ScreenEdge::Right;
    }
}
