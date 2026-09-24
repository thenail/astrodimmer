#pragma once

#include "AstroTimeline.g.h"

namespace winrt::AstroDimmer::implementation
{
    /// The day/night strip.
    ///
    /// The transition is drawn as a chequerboard whose squares change size,
    /// not as an alpha gradient. A gradient reads as a blur - an edge the
    /// renderer was not sure about - while the squares read as a quantity:
    /// the day colour's squares are small where little of the daytime level
    /// applies and full where it applies entirely, so sunset is drawn as
    /// sunrise mirrored. That also survives the fade being four pixels wide,
    /// which is what 15 minutes comes to on this strip.
    struct AstroTimeline : AstroTimelineT<AstroTimeline>
    {
        AstroTimeline();

        void SetSchedule(int32_t dayStart, int32_t nightStart, int32_t fadeMinutes);
        void ClearSchedule();

        /// The strip in words, for screen readers: the same facts in reading order.
        static std::wstring Describe(std::optional<int> dayStart, std::optional<int> nightStart, int fadeMinutes, int now);

    private:
        bool HasSchedule() const { return m_dayStart && m_nightStart && *m_dayStart != *m_nightStart; }
        void Redraw();
        void DrawTransition(double width, int start, int fade, bool towardsDay);
        void AddRect(Microsoft::UI::Xaml::Controls::Canvas const& canvas, Microsoft::UI::Xaml::Media::Brush const& brush,
                     double left, double top, double width, double height);

        std::optional<int> m_dayStart;
        std::optional<int> m_nightStart;
        int m_fade{ 0 };
        int m_now{ 0 };

        Microsoft::UI::Dispatching::DispatcherQueueTimer m_clock{ nullptr };
    };
}

namespace winrt::AstroDimmer::factory_implementation
{
    struct AstroTimeline : AstroTimelineT<AstroTimeline, implementation::AstroTimeline>
    {
    };
}
