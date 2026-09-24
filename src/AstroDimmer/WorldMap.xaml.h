#pragma once

#include "WorldMap.g.h"

namespace winrt::AstroDimmer::implementation
{
    /// A monochrome world map whose pin IS the location setting: drag it, or
    /// click anywhere, to choose where you are. In Glimmer it replaced a pair
    /// of decimal text boxes, where typing 18.07 for -18.07 stayed invisible
    /// until the schedule ran at the wrong time.
    ///
    /// Dragging is coarse - under a degree per pixel - so the arrow keys nudge
    /// by a degree, or a tenth with Shift. Without that the control would be
    /// both imprecise AND unreachable from the keyboard.
    ///
    /// Equirectangular on purpose: longitude and latitude are LINEAR in x and
    /// y, so the pin position is arithmetic a reader can do in their head.
    ///
    /// The outline is Natural Earth 1:110m (public domain), simplified offline
    /// to Assets/world-110m.path. Nothing is fetched at runtime.
    struct WorldMap : WorldMapT<WorldMap>
    {
        WorldMap();

        bool HasLocation() const { return m_latitude.has_value(); }
        double Latitude() const { return m_latitude.value_or(0); }
        double Longitude() const { return m_longitude.value_or(0); }

        void SetLocation(double latitude, double longitude) { Commit(latitude, longitude); }
        void ShowLocation(double latitude, double longitude);
        void ClearLocation();

        event_token LocationChanged(Windows::Foundation::EventHandler<Windows::Foundation::IInspectable> const& handler)
        {
            return m_locationChanged.add(handler);
        }
        void LocationChanged(event_token const& token) noexcept { m_locationChanged.remove(token); }

        /// The pin written the way people write coordinates, e.g.
        /// "59.33° N, 18.07° W". Hemisphere letters rather than signs: a
        /// leading minus is exactly the thing that used to go unnoticed.
        static std::wstring Describe(std::optional<double> latitude, std::optional<double> longitude);

        Windows::Foundation::Size MeasureOverride(Windows::Foundation::Size const& available);
        void OnPointerPressed(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerMoved(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerReleased(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerCaptureLost(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnKeyDown(Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);

    private:
        struct Projection
        {
            double Scale;
            double OffsetX;
            double OffsetY;

            Windows::Foundation::Point Project(double longitude, double latitude) const;
        };

        Projection CurrentProjection() const;
        void Rebuild();
        void UpdateNight();
        void UpdateMarker();
        void SetFromPoint(Windows::Foundation::Point point);
        void Commit(double latitude, double longitude);

        std::optional<double> m_latitude;
        std::optional<double> m_longitude;
        bool m_dragging{ false };

        Microsoft::UI::Dispatching::DispatcherQueueTimer m_nightTimer{ nullptr };
        event<Windows::Foundation::EventHandler<Windows::Foundation::IInspectable>> m_locationChanged;
    };
}

namespace winrt::AstroDimmer::factory_implementation
{
    struct WorldMap : WorldMapT<WorldMap, implementation::WorldMap>
    {
    };
}
