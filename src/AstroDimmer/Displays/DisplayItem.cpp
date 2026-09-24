#include "pch.h"
#include "Displays/DisplayItem.h"

namespace AstroDimmer::Displays
{
    void DisplayItem::SetBrightness(int value, BrightnessOrigin origin)
    {
        value = std::clamp(value, 0, 100);
        if (value == m_brightness) return;

        m_brightness = value;
        Changed();

        // Writing a value just read would be pointless DDC traffic.
        if (origin != BrightnessOrigin::Hardware)
            BrightnessChanged(*this, origin);
    }

    void DisplayItem::SetContrast(int value, bool fromHardware)
    {
        value = std::clamp(value, 0, 100);
        if (value == m_contrast) return;

        m_contrast = value;
        Changed();

        if (!fromHardware)
            ContrastChanged(*this);
    }

    void DisplayItem::SetShowContrast(bool value)
    {
        if (value == m_showContrast) return;
        m_showContrast = value;
        Changed();
    }
}
