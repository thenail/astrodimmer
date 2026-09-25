#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "Displays/DisplayItem.h"
#include "Event.h"
#include "Link.h"

namespace AstroDimmer::Displays
{
    /// The host's display list, as the UI process sees it.
    ///
    /// The windows bind to these items exactly as they did to the real ones:
    /// a slider sets a level with BrightnessOrigin::User, and the item raises
    /// Changed when the level moves. The difference is at the edges - a
    /// change made here is sent to the host, which owns the monitors, and a
    /// level the host reports is set here as read from the hardware, so it is
    /// not sent back.
    class DisplayMirror
    {
    public:
        explicit DisplayMirror(std::function<void(Link::Message const&)> send);

        DisplayMirror(DisplayMirror const&) = delete;
        DisplayMirror& operator=(DisplayMirror const&) = delete;

        std::vector<std::shared_ptr<DisplayItem>> const& Displays() const { return m_displays; }
        std::shared_ptr<DisplayItem> Find(std::wstring const& deviceKey) const;

        /// From the host: a new list, as a "displays" or "state" message has it.
        void Replace(Link::Message const& message);

        /// From the host: one display's levels moved.
        void UpdateLevels(Link::Message const& message);

        /// The display list was replaced.
        Event<> DisplaysChanged;

        /// Where the display list stands, for the panel to show while it has
        /// no rows; empty means all is well.
        Event<std::wstring const&> StatusChanged;

    private:
        std::function<void(Link::Message const&)> m_send;
        std::vector<std::shared_ptr<DisplayItem>> m_displays;
    };
}
