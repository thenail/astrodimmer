#include "pch.h"
#include "AstroTimeline.xaml.h"
#if __has_include("AstroTimeline.g.cpp")
#include "AstroTimeline.g.cpp"
#endif
#include "AstroEngine.h"
#include "DayTimeline.h"
#include "ThemeBrush.h"
#include "Strings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
namespace Core = ::AstroDimmer::Core;

namespace winrt::AstroDimmer::implementation
{
    namespace
    {
        constexpr int MinutesPerDay = Core::AstroEngine::MinutesPerDay;

        constexpr double TrackHeight = 26;
        constexpr double Overhang = 3;
        constexpr double TrackCorner = 4;
        constexpr double AxisFontSize = 11;

        /// Labelled every third hour: hourly would collide below ~400 DIPs.
        constexpr int AxisStepHours = 3;

        /// Chequer rows across the track. Four gives a square around 6 DIPs:
        /// three made a three hour fade six columns of scattered boxes, and
        /// five is below the point where a square reads as a square at all.
        constexpr int CheckerRows = 4;

        int NowMinutes()
        {
            auto now = Core::LocalNow();
            return now.MinuteOfDay();
        }

        std::wstring Clock(int minutes)
        {
            return ::AstroDimmer::Strings::Time(Core::AstroEngine::WrapMinutes(minutes));
        }
    }

    AstroTimeline::AstroTimeline()
    {
        InitializeComponent();

        m_now = NowMinutes();

        // The marker only has to be right to the minute, and this control is
        // only on screen while Settings is open - so the clock is tied to
        // being loaded.
        m_clock = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_clock.Interval(std::chrono::seconds(30));
        m_clock.Tick([this](auto&&, auto&&)
        {
            int now = NowMinutes();
            if (now == m_now) return;
            m_now = now;
            Redraw();
        });

        Loaded([this](auto&&, auto&&) { m_now = NowMinutes(); m_clock.Start(); Redraw(); });
        Unloaded([this](auto&&, auto&&) { m_clock.Stop(); });
        SizeChanged([this](auto&&, auto&&) { Redraw(); });

        // Shapes built in code do not follow {ThemeResource} by themselves.
        ActualThemeChanged([this](auto&&, auto&&) { Redraw(); });

        Root().Visibility(Visibility::Collapsed);
    }

    void AstroTimeline::SetSchedule(int32_t dayStart, int32_t nightStart, int32_t fadeMinutes)
    {
        m_dayStart = dayStart;
        m_nightStart = nightStart;
        m_fade = std::max(0, fadeMinutes);
        Redraw();
    }

    void AstroTimeline::ClearSchedule()
    {
        m_dayStart.reset();
        m_nightStart.reset();
        Redraw();
    }

    void AstroTimeline::AddRect(Canvas const& canvas, Brush const& brush, double left, double top, double width,
                                double height)
    {
        Shapes::Rectangle rect;
        rect.Width(width);
        rect.Height(height);
        rect.Fill(brush);
        Canvas::SetLeft(rect, left);
        Canvas::SetTop(rect, top);
        canvas.Children().Append(rect);
    }

    void AstroTimeline::Redraw()
    {
        Automation::AutomationProperties::SetHelpText(*this, Describe(m_dayStart, m_nightStart, m_fade, m_now));

        Track().Children().Clear();
        Overlay().Children().Clear();
        Axis().Children().Clear();

        if (!HasSchedule())
        {
            Root().Visibility(Visibility::Collapsed);
            return;
        }

        Root().Visibility(Visibility::Visible);

        double w = ActualWidth();
        if (w <= 0) return;

        int dayStart = Core::AstroEngine::WrapMinutes(*m_dayStart);
        int nightStart = Core::AstroEngine::WrapMinutes(*m_nightStart);
        auto x = [w](double minutes) { return minutes / MinutesPerDay * w; };

        auto day = ::AstroDimmer::ThemeBrush(*this, L"TimelineDayBrush");
        auto night = ::AstroDimmer::ThemeBrush(*this, L"TimelineNightBrush");

        // Everything inside the track is clipped to its rounded outline, so a
        // band that runs to midnight cannot square off the corner. A XAML clip
        // can only be a rectangle; the compositor can round it.
        auto visual = Hosting::ElementCompositionPreview::GetElementVisual(Track());
        auto compositor = visual.Compositor();
        auto outline = compositor.CreateRoundedRectangleGeometry();
        outline.Size({ static_cast<float>(w), static_cast<float>(TrackHeight) });
        outline.CornerRadius({ static_cast<float>(TrackCorner), static_cast<float>(TrackCorner) });
        visual.Clip(compositor.CreateGeometricClip(outline));

        // Night is the ground the day is painted onto, which is what makes
        // the wrapped case work without drawing night segments separately.
        AddRect(Track(), night, 0, 0, w, TrackHeight);

        for (auto const& segment : Core::DayTimeline::DaySegments(dayStart, nightStart))
            AddRect(Track(), day, x(segment.Start), 0, x(segment.Length), TrackHeight);

        // Each band overpaints the first minutes of the period it opens, so it
        // has to come after the flat fills.
        DrawTransition(w, dayStart, m_fade, true);
        DrawTransition(w, nightStart, m_fade, false);

        // Hour ticks. Midnight and 24:00 are the strip's own edges.
        auto tick = ::AstroDimmer::ThemeBrush(*this, L"TimelineTickBrush");
        for (int hour = AxisStepHours; hour < 24; hour += AxisStepHours)
            AddRect(Track(), tick, std::round(x(hour * 60.0)), 0, 1, TrackHeight);

        // Now, on a pale casing where it crosses the track, so the accent
        // keeps its edge against the night. The overhangs sit on the page,
        // which gives the accent contrast enough on its own.
        Shapes::Rectangle halo;
        halo.Width(4);
        halo.Height(TrackHeight);
        halo.RadiusX(1);
        halo.RadiusY(1);
        halo.Fill(::AstroDimmer::ThemeBrush(*this, L"TimelineNowHaloBrush"));
        Canvas::SetLeft(halo, x(m_now) - 2);
        Canvas::SetTop(halo, Overhang);
        Overlay().Children().Append(halo);

        Shapes::Rectangle marker;
        marker.Width(2);
        marker.Height(TrackHeight + 2 * Overhang);
        marker.RadiusX(1);
        marker.RadiusY(1);
        marker.Fill(::AstroDimmer::ThemeBrush(*this, L"TimelineNowBrush"));
        Canvas::SetLeft(marker, x(m_now) - 1);
        Canvas::SetTop(marker, 0);
        Overlay().Children().Append(marker);

        // Hour labels. The end labels are pulled inside the strip; centring
        // them would hang half of "00" and "24" off the control.
        auto axisBrush = ::AstroDimmer::ThemeBrush(*this, L"TimelineAxisBrush");
        for (int hour = 0; hour <= 24; hour += AxisStepHours)
        {
            TextBlock label;
            wchar_t text[4];
            swprintf_s(text, L"%02d", hour);
            label.Text(text);
            label.FontSize(AxisFontSize);
            label.Foreground(axisBrush);
            label.Measure({ std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() });

            double width = label.DesiredSize().Width;
            double centre = hour / 24.0 * w;
            double left = hour == 0 ? 0 : hour == 24 ? w - width : centre - width / 2;

            Canvas::SetLeft(label, left);
            Canvas::SetTop(label, -2);
            Axis().Children().Append(label);
        }
    }

    void AstroTimeline::DrawTransition(double width, int start, int fade, bool towardsDay)
    {
        // The board is keyed to how much DAYLIGHT applies at each point, not
        // to how far the fade has run: the ground is always night and the
        // squares always day, so a square's size reads as "how much of the
        // daytime level is in effect here" wherever it sits - which makes
        // sunset the mirror image of sunrise, as it physically is.
        //
        // The two colours of the board are offset by half the band, so the
        // sequence runs night, growing day squares, an even board at the
        // midpoint, then the last of the night shrinking away.
        if (fade <= 0) return;

        auto x = [width](double minutes) { return minutes / MinutesPerDay * width; };

        double bandWidth = x(std::min(fade, MinutesPerDay));
        if (bandWidth <= 0) return;

        double cell = TrackHeight / CheckerRows;
        int columns = std::max(1, static_cast<int>(std::ceil(bandWidth / cell)));

        auto day = ::AstroDimmer::ThemeBrush(*this, L"TimelineDayBrush");
        auto night = ::AstroDimmer::ThemeBrush(*this, L"TimelineNightBrush");

        for (auto const& piece : Core::DayTimeline::SplitBand(start, fade))
        {
            double pieceLeft = x(piece.Start);
            double pieceWidth = x(piece.Length);
            double bandOffset = x(piece.Offset);
            if (pieceWidth <= 0) continue;

            // Each piece gets its own canvas, clipped to its own extent, so
            // squares near its edges cannot spill onto the flat fills.
            Canvas holder;
            holder.Width(pieceWidth);
            holder.Height(TrackHeight);
            Canvas::SetLeft(holder, pieceLeft);
            RectangleGeometry clip;
            clip.Rect({ 0, 0, static_cast<float>(pieceWidth), static_cast<float>(TrackHeight) });
            holder.Clip(clip);
            AddRect(holder, night, 0, 0, pieceWidth, TrackHeight);

            // Columns are counted in the whole band's own space, so a piece
            // cut off at midnight resumes the pattern rather than restarting.
            int first = static_cast<int>(std::floor(bandOffset / cell));
            int last = static_cast<int>(std::ceil((bandOffset + pieceWidth) / cell));

            for (int column = first; column < last; ++column)
            {
                double bandX = column * cell;

                // Sampled at the middle of the column: the leading edge would
                // leave the last column short of full size.
                double progress = std::clamp((bandX + cell / 2) / bandWidth, 0.0, 1.0);

                // Daylight climbs across a sunrise band and falls across a
                // sunset one; columns are counted from the night end for the
                // same reason, so the two boards are phase-mirrored.
                double daylight = towardsDay ? progress : 1 - progress;
                int patternColumn = towardsDay ? column : columns - 1 - column;

                double left = bandX - bandOffset;

                for (int row = 0; row < CheckerRows; ++row)
                {
                    int parity = (patternColumn + row) & 1;

                    // The two parities sweep the first and second halves of
                    // the daylight range respectively.
                    double side = cell * std::clamp(2 * daylight - parity, 0.0, 1.0);
                    if (side <= 0) continue;

                    double inset = (cell - side) / 2;
                    AddRect(holder, day, left + inset, row * cell + inset, side, side);
                }
            }

            Track().Children().Append(holder);
        }
    }

    std::wstring AstroTimeline::Describe(std::optional<int> dayStart, std::optional<int> nightStart, int fadeMinutes,
                                         int now)
    {
        if (!dayStart || !nightStart || *dayStart == *nightStart)
            return ::AstroDimmer::Strings::Get(L"NoSchedule");

        if (fadeMinutes > 0)
            return ::AstroDimmer::Strings::Format(L"ScheduleSummaryWithFade", { Clock(*dayStart), Clock(*nightStart),
                                                                             Clock(now), std::to_wstring(fadeMinutes) });

        return ::AstroDimmer::Strings::Format(L"ScheduleSummary", { Clock(*dayStart), Clock(*nightStart), Clock(now) });
    }
}
