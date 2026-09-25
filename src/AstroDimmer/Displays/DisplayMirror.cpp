#include "pch.h"
#include "Displays/DisplayMirror.h"

namespace AstroDimmer::Displays
{
    namespace
    {
        std::wstring String(Link::Message const& m, wchar_t const* name)
        {
            auto v = m.Find(name);
            return v ? v->AsString().value_or(L"") : L"";
        }

        int Int(Link::Message const& m, wchar_t const* name)
        {
            auto v = m.Find(name);
            return v ? v->AsInt().value_or(0) : 0;
        }

        bool Bool(Link::Message const& m, wchar_t const* name)
        {
            auto v = m.Find(name);
            return v && v->AsBool().value_or(false);
        }
    }

    DisplayMirror::DisplayMirror(std::function<void(Link::Message const&)> send)
        : m_send(std::move(send))
    {
    }

    std::shared_ptr<DisplayItem> DisplayMirror::Find(std::wstring const& deviceKey) const
    {
        for (auto const& item : m_displays)
            if (item->DeviceKey == deviceKey)
                return item;
        return nullptr;
    }

    void DisplayMirror::Replace(Link::Message const& message)
    {
        std::vector<std::shared_ptr<DisplayItem>> items;

        if (auto list = message.Find(L"displays"))
        {
            for (auto const& d : list->Items())
            {
                auto item = std::make_shared<DisplayItem>();
                item->DeviceKey = String(d, L"key");
                item->Name = String(d, L"name");
                item->Description = String(d, L"description");
                item->SupportsContrast = Bool(d, L"contrastSupported");
                item->IsSimulated = Bool(d, L"simulated");
                item->SetBrightness(Int(d, L"brightness"), BrightnessOrigin::Hardware);
                item->SetContrast(Int(d, L"contrast"), true);

                item->BrightnessChanged.Add([this](DisplayItem& display, BrightnessOrigin)
                {
                    auto m = Link::Make(Link::Type::SetBrightness);
                    m.Set(L"key", display.DeviceKey);
                    m.Set(L"value", display.Brightness());
                    m_send(m);
                });

                item->ContrastChanged.Add([this](DisplayItem& display)
                {
                    auto m = Link::Make(Link::Type::SetContrast);
                    m.Set(L"key", display.DeviceKey);
                    m.Set(L"value", display.Contrast());
                    m_send(m);
                });

                items.push_back(std::move(item));
            }
        }

        m_displays = std::move(items);
        DisplaysChanged();
    }

    void DisplayMirror::UpdateLevels(Link::Message const& message)
    {
        auto item = Find(String(message, L"key"));
        if (!item) return;

        item->SetBrightness(Int(message, L"brightness"), BrightnessOrigin::Hardware);
        item->SetContrast(Int(message, L"contrast"), true);
    }
}
