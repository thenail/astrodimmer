#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Displays/DisplayItem.h"
#include "Event.h"
#include "Native/DdcChannel.h"

namespace AstroDimmer::Displays
{
    /// Bridges the DDC/CI layer to the UI. Lives on the UI thread; every
    /// hardware call hops onto the DDC thread and back.
    ///
    /// Beyond plain marshalling:
    ///
    ///   1. Coalescing writes. A slider drag produces hundreds of updates;
    ///      DDC/CI over I2C manages a handful a second. Only the latest value
    ///      per display is sent, and only once the user pauses.
    ///   2. Display power. Every write is gated on the screens being awake:
    ///      a write to a panel in DPMS off can wake it. Pending values are
    ///      held, not dropped, and go out when the display comes back.
    ///   3. Retrying. When Windows reports monitors but DDC/CI found none, the
    ///      probe probably failed, so it is tried again on a bounded ladder.
    class DisplayService
    {
    public:
        explicit DisplayService(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher);
        ~DisplayService();

        DisplayService(DisplayService const&) = delete;
        DisplayService& operator=(DisplayService const&) = delete;

        std::vector<std::shared_ptr<DisplayItem>> const& Displays() const { return m_displays; }
        std::shared_ptr<DisplayItem> Find(std::wstring const& deviceKey) const;

        /// Enumerates displays and reads their levels. Existing items are
        /// replaced. Completes on the UI thread.
        winrt::Windows::Foundation::IAsyncAction RefreshAsync();

        /// Reads each display's brightness back from the panel itself, into
        /// readings by device key, and updates the item when it differs.
        /// Displays that cannot be trusted to answer truthfully right now are
        /// left out: asleep, simulated, or with a write still queued. The
        /// map must outlive the operation; completes on the UI thread.
        winrt::Windows::Foundation::IAsyncAction ReadBrightnessAsync(std::map<std::wstring, int>& readings);

        /// Clears the retry ladder, so a new trigger starts from the top.
        void ResetRetries();

        /// Windows powered the displays down or back up.
        winrt::Windows::Foundation::IAsyncAction SetDisplayPowerAsync(bool on);

        /// The display list was replaced.
        Event<> DisplaysChanged;

        /// A refresh finished; empty text means all is well.
        Event<std::wstring const&> StatusChanged;

        /// An automatic retry was scheduled or abandoned.
        Event<std::wstring const&> RetryStatus;

        /// User-driven brightness changes only, so the schedule can yield.
        Event<DisplayItem&> UserChangedBrightness;

    private:
        struct Probed;

        winrt::Windows::Foundation::IAsyncAction RefreshCore(bool accurate, bool isRetry);
        void EvaluateRetry(size_t found);
        void CancelRetry();
        void Attach(std::shared_ptr<DisplayItem> const& item);
        void ScheduleFlush(std::chrono::milliseconds delay);
        winrt::fire_and_forget FlushAsync();

        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher;
        Native::DdcChannel m_ddc;

        std::vector<std::shared_ptr<DisplayItem>> m_displays;

        /// Extra UI-only displays from --simulate-displays; 0 normally.
        int m_simulatedCount{ 0 };

        std::map<std::wstring, int> m_pending;

        /// Kept apart from brightness: a different VCP code, and a display
        /// can want one without the other.
        std::map<std::wstring, int> m_pendingContrast;

        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_flushTimer{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_retryTimer{ nullptr };
        size_t m_retryAttempt{ 0 };

        bool m_displaysAwake{ true };

        /// A refresh asked for while the screens were asleep. Probing then is
        /// futile and risky: a sleeping panel does not answer its bus, every
        /// read fails, the list empties, and the retry ladder starts hammering
        /// a monitor that is doing nothing wrong.
        bool m_refreshDeferred{ false };
    };
}
