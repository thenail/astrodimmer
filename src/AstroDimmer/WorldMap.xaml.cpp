#include "pch.h"
#include "WorldMap.xaml.h"
#if __has_include("WorldMap.g.cpp")
#include "WorldMap.g.cpp"
#endif
#include "DateTime.h"
#include "Native/Shell.h"
#include "SolarTimes.h"
#include "Strings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Media;
using Windows::Foundation::Point;
using Windows::Foundation::Size;

namespace winrt::AstroDimmer::implementation
{
    namespace
    {
        /// Map space: x = longitude + 180, y = 90 - latitude.
        constexpr double MapWidth = 360;
        constexpr double MapHeight = 180;

        /// Graticule spacing in degrees.
        constexpr double GridStep = 30;

        /// How finely the terminator is sampled, in degrees of longitude.
        /// The curve is smooth and the map a few hundred pixels wide, so two
        /// degrees is already below a pixel per step.
        constexpr double TerminatorStep = 2;

        /// Two decimals, as written to settings.json: roughly a kilometre on
        /// the ground, far finer than sunrise maths needs, and it keeps the
        /// readout from jittering through meaningless digits while dragging.
        double Round2(double value) { return std::round(value * 100.0) / 100.0; }

        constexpr double Pi = 3.14159265358979323846;

        using Ring = std::vector<std::pair<float, float>>;

        /// The coastline, parsed once per process. It is a static asset, and
        /// re-reading 2600 points on every resize would be visible.
        std::vector<Ring> const& LandRings()
        {
            static std::vector<Ring> rings = []
            {
                std::vector<Ring> parsed;

                auto exe = ::AstroDimmer::Native::Shell::ExecutablePath();
                auto path = exe.substr(0, exe.find_last_of(L'\\')) + L"\\Assets\\world-110m.path";

                HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                    return parsed; // the graticule and pin still carry the meaning

                std::string text(static_cast<size_t>(GetFileSize(file, nullptr)), '\0');
                DWORD read = 0;
                ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
                CloseHandle(file);
                text.resize(read);

                // Only M, L and Z appear: rings of absolute coordinates.
                // Comment lines start with '#'.
                Ring current;
                size_t i = 0;
                auto skipLine = [&] { while (i < text.size() && text[i] != '\n') ++i; };

                while (i < text.size())
                {
                    char c = text[i];
                    if (c == '#') { skipLine(); continue; }
                    if (c == 'M')
                    {
                        if (!current.empty()) parsed.push_back(std::move(current));
                        current.clear();
                        ++i;
                        continue;
                    }
                    if (c == 'Z' || c == 'z')
                    {
                        if (!current.empty()) parsed.push_back(std::move(current));
                        current.clear();
                        ++i;
                        continue;
                    }
                    if (c == '-' || c == '.' || (c >= '0' && c <= '9'))
                    {
                        char* end = nullptr;
                        float x = std::strtof(text.c_str() + i, &end);
                        i = static_cast<size_t>(end - text.c_str());
                        if (i < text.size() && text[i] == ',') ++i;
                        float y = std::strtof(text.c_str() + i, &end);
                        i = static_cast<size_t>(end - text.c_str());
                        current.emplace_back(x, y);
                        continue;
                    }
                    ++i; // L, spaces, newlines
                }

                if (!current.empty()) parsed.push_back(std::move(current));
                return parsed;
            }();

            return rings;
        }
    }

    Point WorldMap::Projection::Project(double longitude, double latitude) const
    {
        return { static_cast<float>(OffsetX + (longitude + 180.0) * Scale),
                 static_cast<float>(OffsetY + (90.0 - latitude) * Scale) };
    }

    WorldMap::WorldMap()
    {
        InitializeComponent();

        SizeChanged([this](auto&&, auto&&) { Rebuild(); });

        // The night overlay is a clock face as much as a map: it has to keep
        // up with the world rather than freeze at whenever Settings opened.
        // The terminator moves a quarter of a degree a minute - well under a
        // pixel here - so a minute is already finer than the map can show.
        m_nightTimer = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_nightTimer.Interval(std::chrono::minutes(1));
        m_nightTimer.Tick([this](auto&&, auto&&) { UpdateNight(); });

        Loaded([this](auto&&, auto&&) { m_nightTimer.Start(); });
        Unloaded([this](auto&&, auto&&) { m_nightTimer.Stop(); });
    }

    Size WorldMap::MeasureOverride(Size const& available)
    {
        // 2:1, so height follows width rather than being set by hand at every
        // call site - a wrong height would letterbox or squash it.
        float width = std::isinf(available.Width) ? static_cast<float>(MapWidth) : available.Width;
        float height = width * static_cast<float>(MapHeight / MapWidth);

        if (!std::isinf(available.Height) && height > available.Height)
        {
            height = available.Height;
            width = height * static_cast<float>(MapWidth / MapHeight);
        }

        Surface().Measure({ width, height });
        return { width, height };
    }

    WorldMap::Projection WorldMap::CurrentProjection() const
    {
        double w = ActualWidth(), h = ActualHeight();
        double scale = std::min(w / MapWidth, h / MapHeight);
        return { scale, (w - MapWidth * scale) / 2, (h - MapHeight * scale) / 2 };
    }

    void WorldMap::Rebuild()
    {
        double w = ActualWidth(), h = ActualHeight();
        if (w <= 0 || h <= 0) return;

        auto p = CurrentProjection();

        // Nothing may spill outside the control's box.
        RectangleGeometry clip;
        clip.Rect({ 0, 0, static_cast<float>(w), static_cast<float>(h) });
        Surface().Clip(clip);

        // Hairline graticule, built in pixels so the stroke stays one pixel.
        GeometryGroup grid;
        for (double lon = -180; lon <= 180; lon += GridStep)
        {
            LineGeometry line;
            line.StartPoint(p.Project(lon, 90));
            line.EndPoint(p.Project(lon, -90));
            grid.Children().Append(line);
        }
        for (double lat = -90; lat <= 90; lat += GridStep)
        {
            LineGeometry line;
            line.StartPoint(p.Project(-180, lat));
            line.EndPoint(p.Project(180, lat));
            grid.Children().Append(line);
        }
        Graticule().Data(grid);

        PathGeometry land;
        for (auto const& ring : LandRings())
        {
            PathFigure figure;
            figure.IsClosed(true);
            figure.IsFilled(true);
            figure.StartPoint({ static_cast<float>(p.OffsetX + ring[0].first * p.Scale),
                                static_cast<float>(p.OffsetY + ring[0].second * p.Scale) });

            PolyLineSegment segment;
            auto points = segment.Points();
            for (size_t i = 1; i < ring.size(); ++i)
                points.Append({ static_cast<float>(p.OffsetX + ring[i].first * p.Scale),
                                static_cast<float>(p.OffsetY + ring[i].second * p.Scale) });

            figure.Segments().Append(segment);
            land.Figures().Append(figure);
        }
        Land().Data(land);

        UpdateNight();
        UpdateMarker();
    }

    void WorldMap::UpdateNight()
    {
        if (ActualWidth() <= 0) return;

        auto p = CurrentProjection();

        // The terminator is the set of points 90 degrees of arc from the
        // subsolar point - on this projection, one latitude per longitude:
        //
        //     tan(lat) = -cos(lon - subsolar lon) / tan(declination)
        //
        // The shaded side is the pole turned away from the sun, so the
        // polygon is closed along that edge of the map.
        auto sun = ::AstroDimmer::Core::SolarTimes::SubsolarPoint(::AstroDimmer::Core::Now());
        double tanDec = std::tan(sun.Latitude * Pi / 180.0);

        // Within minutes of an equinox the declination is ~0 and the curve
        // degenerates to two vertical lines. Nudging it keeps the arithmetic
        // finite; the shape it produces is the right one anyway.
        if (std::abs(tanDec) < 1e-6)
            tanDec = tanDec < 0 ? -1e-6 : 1e-6;

        // Positive declination lights the north, so the dark pole is the south.
        double darkPole = sun.Latitude >= 0 ? -90 : 90;

        PathFigure figure;
        figure.IsClosed(true);
        figure.IsFilled(true);

        PolyLineSegment segment;
        auto points = segment.Points();
        bool started = false;

        for (double lon = -180; lon <= 180; lon += TerminatorStep)
        {
            double lat = std::atan(-std::cos((lon - sun.Longitude) * Pi / 180.0) / tanDec) * 180.0 / Pi;
            auto point = p.Project(lon, lat);

            if (!started)
            {
                figure.StartPoint(point);
                started = true;
            }
            else
            {
                points.Append(point);
            }
        }

        // Down the far edge, along the dark pole, and back up.
        points.Append(p.Project(180, darkPole));
        points.Append(p.Project(-180, darkPole));
        figure.Segments().Append(segment);

        PathGeometry night;
        night.Figures().Append(figure);
        Night().Data(night);
    }

    void WorldMap::UpdateMarker()
    {
        bool visible = m_latitude && m_longitude && ActualWidth() > 0;
        MarkerHalo().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        MarkerRing().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        MarkerDot().Visibility(visible ? Visibility::Visible : Visibility::Collapsed);

        Automation::AutomationProperties::SetHelpText(*this, Describe(m_latitude, m_longitude));

        if (!visible) return;

        auto point = CurrentProjection().Project(*m_longitude, *m_latitude);
        Controls::Canvas::SetLeft(MarkerHalo(), point.X - MarkerHalo().Width() / 2);
        Controls::Canvas::SetTop(MarkerHalo(), point.Y - MarkerHalo().Height() / 2);
        Controls::Canvas::SetLeft(MarkerRing(), point.X - MarkerRing().Width() / 2);
        Controls::Canvas::SetTop(MarkerRing(), point.Y - MarkerRing().Height() / 2);
        Controls::Canvas::SetLeft(MarkerDot(), point.X - MarkerDot().Width() / 2);
        Controls::Canvas::SetTop(MarkerDot(), point.Y - MarkerDot().Height() / 2);
    }

    void WorldMap::ShowLocation(double latitude, double longitude)
    {
        m_latitude = Round2(std::clamp(latitude, -90.0, 90.0));
        m_longitude = Round2(std::clamp(longitude, -180.0, 180.0));
        UpdateMarker();
    }

    void WorldMap::ClearLocation()
    {
        m_latitude.reset();
        m_longitude.reset();
        UpdateMarker();
    }

    void WorldMap::Commit(double latitude, double longitude)
    {
        // Clamped, rounded, and announced only when it actually changed - a
        // drag fires a move per pixel, and saving on each would be gratuitous.
        double lat = Round2(std::clamp(latitude, -90.0, 90.0));
        double lon = Round2(std::clamp(longitude, -180.0, 180.0));

        if (m_latitude == lat && m_longitude == lon) return;

        m_latitude = lat;
        m_longitude = lon;
        UpdateMarker();
        m_locationChanged(*this, nullptr);
    }

    void WorldMap::SetFromPoint(Point point)
    {
        auto p = CurrentProjection();
        if (p.Scale <= 0) return;

        Commit(90.0 - (point.Y - p.OffsetY) / p.Scale, (point.X - p.OffsetX) / p.Scale - 180.0);
    }

    void WorldMap::OnPointerPressed(Input::PointerRoutedEventArgs const& e)
    {
        Focus(FocusState::Pointer);
        m_dragging = CapturePointer(e.Pointer());
        SetFromPoint(e.GetCurrentPoint(*this).Position());
        e.Handled(true);
    }

    void WorldMap::OnPointerMoved(Input::PointerRoutedEventArgs const& e)
    {
        if (m_dragging)
            SetFromPoint(e.GetCurrentPoint(*this).Position());
    }

    void WorldMap::OnPointerReleased(Input::PointerRoutedEventArgs const& e)
    {
        if (m_dragging)
        {
            ReleasePointerCapture(e.Pointer());
            m_dragging = false;
            e.Handled(true);
        }
    }

    void WorldMap::OnPointerCaptureLost(Input::PointerRoutedEventArgs const&)
    {
        m_dragging = false;
    }

    void WorldMap::OnKeyDown(Input::KeyRoutedEventArgs const& e)
    {
        using Windows::System::VirtualKey;

        // Shift for a tenth of a degree, which is finer than a pixel of drag.
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        double step = shift ? 0.1 : 1.0;

        double dLat = 0, dLon = 0;
        switch (e.Key())
        {
        case VirtualKey::Left: dLon = -step; break;
        case VirtualKey::Right: dLon = step; break;
        case VirtualKey::Up: dLat = step; break;
        case VirtualKey::Down: dLat = -step; break;
        default: return;
        }

        // Nothing to nudge yet: start from the middle rather than ignoring the
        // key, so the keyboard alone can reach any point.
        Commit(m_latitude.value_or(0) + dLat, m_longitude.value_or(0) + dLon);
        e.Handled(true);
    }

    std::wstring WorldMap::Describe(std::optional<double> latitude, std::optional<double> longitude)
    {
        if (!latitude || !longitude)
            return ::AstroDimmer::Strings::Get(L"NoLocationSet");

        // "0.##": up to two decimals, trailing zeros dropped.
        auto format = [](double value)
        {
            wchar_t buffer[32];
            swprintf_s(buffer, L"%.2f", std::abs(value));
            std::wstring text = buffer;
            while (text.back() == L'0') text.pop_back();
            if (text.back() == L'.') text.pop_back();

            // Written in the user's own decimal separator.
            wchar_t separator[8]{};
            if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, separator, 8) > 0 && separator[0] != L'.')
                std::replace(text.begin(), text.end(), L'.', separator[0]);
            return text;
        };

        using ::AstroDimmer::Strings::Format;
        return Format(L"Coordinates",
                      { Format(*latitude >= 0 ? L"LatitudeNorth" : L"LatitudeSouth", { format(*latitude) }),
                        Format(*longitude >= 0 ? L"LongitudeEast" : L"LongitudeWest", { format(*longitude) }) });
    }
}
