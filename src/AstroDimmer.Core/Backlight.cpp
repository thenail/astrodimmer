#include "pch.h"
#include "Backlight.h"

namespace AstroDimmer::Core::Backlight
{
    std::optional<std::wstring> KeyOf(std::wstring const& instanceName)
    {
        // Three parts - enumerator, hardware ID, instance - and a "_N" WMI
        // appends to tell the monitor's instances apart.
        auto first = instanceName.find(L'\\');
        auto second = first == std::wstring::npos ? first : instanceName.find(L'\\', first + 1);
        if (first == 0 || second == std::wstring::npos || second == first + 1 ||
            instanceName.find(L'\\', second + 1) != std::wstring::npos)
            return std::nullopt;

        auto instance = instanceName.substr(second + 1);
        if (auto suffix = instance.rfind(L'_'); suffix != std::wstring::npos && suffix + 1 < instance.size() &&
            std::all_of(instance.begin() + suffix + 1, instance.end(), [](wchar_t c) { return iswdigit(c); }))
            instance.erase(suffix);

        if (instance.empty()) return std::nullopt;

        return LR"(\\?\)" + instanceName.substr(0, first) + L"#" +
               instanceName.substr(first + 1, second - first - 1) + L"#" + instance;
    }

    int Snap(int percent, std::vector<int> const& levels)
    {
        if (levels.empty()) return percent;

        int best = levels.front();
        for (int level : levels)
        {
            int distance = std::abs(level - percent), bestDistance = std::abs(best - percent);
            if (distance < bestDistance || (distance == bestDistance && level > best))
                best = level;
        }
        return best;
    }
}
