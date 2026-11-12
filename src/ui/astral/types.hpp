// Astral-Remake - made by kdt255 (https://github.com/kdt255/Astral-Remake)
// Free software: MIT + Commons Clause, selling is not permitted. If you paid for this, you got scammed.
#pragma once

#include "imgui.h"

#include <cmath>
#include <cstdint>

namespace ui::astral {

struct color_value_t {
    ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    float rainbow = 0.0f;
    float pulse = 0.0f;

    [[nodiscard]] ImVec4 resolve(double time) const
    {
        ImVec4 result = color;
        if (rainbow > 0.0f) {
            float hue = 0.0f, saturation = 0.0f, value = 0.0f;
            ImGui::ColorConvertRGBtoHSV(result.x, result.y, result.z, hue, saturation, value);
            const double cycles = time * static_cast<double>(rainbow) / 1000.0;
            hue = static_cast<float>(std::fmod(static_cast<double>(hue) + cycles, 1.0));
            if (saturation < 0.05f) saturation = 0.75f;
            if (value < 0.05f) value = 1.0f;
            ImGui::ColorConvertHSVtoRGB(hue, saturation, value, result.x, result.y, result.z);
        }
        if (pulse > 0.0f) {
            const float wave = 0.5f + 0.5f * static_cast<float>(std::sin(time * 3.0));
            result.w *= 1.0f - (pulse / 100.0f) * 0.85f * wave;
        }
        return result;
    }
};

[[nodiscard]] constexpr color_value_t make_color(std::uint32_t rgb, float alpha = 1.0f) noexcept
{
    return color_value_t{ ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                                 static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                                 static_cast<float>(rgb & 0xFF) / 255.0f, alpha), 0.0f, 0.0f };
}

} // namespace ui::astral
