#pragma once

#include <config/combat.hpp>
#include <game/state/entities.hpp>
#include <game/state/services.hpp>
#include <render/draw.hpp>
#include <render/presentation.hpp>
#include <simulation/aim_parts.hpp>
#include <simulation/multipoint.hpp>

#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace features::multipoint_overlay
{
[[nodiscard]] inline render::draw::rgba point_color(const int part) noexcept
{
    using part_t = config::combat_profile::aim_part;
    if (part == part_t::head)
        return {255, 188, 72, 235};
    if (part == part_t::body)
        return {105, 210, 255, 220};
    return {185, 198, 220, 190};
}

// Draws every candidate the current multipoint setting would offer and labels the player with the total. This is a
// render-only view: it rebuilds the cheap local geometry, but performs no collision, smoke or ballistics work.
inline void draw(render::draw::draw_list& draw_list, const std::vector<game::player_snapshot>& players,
                 const int hitbox_parts, const bool multipoint,
                 const config::combat_profile::multipoint_settings& settings)
{
    if (!multipoint || !settings.display)
        return;

    const auto& camera = game::camera();
    const auto local_pawn = game::local_player().pawn();
    const auto* font = render::fonts().menu_regular_12;
    const auto preset = settings.performance == config::combat_profile::multipoint_settings::preset::low     ? "LOW"
                        : settings.performance == config::combat_profile::multipoint_settings::preset::medium ? "MED"
                                                                                                             : "HIGH";

    for (const auto& other : players)
    {
        if (!other.pawn || other.pawn == local_pawn || other.health <= 0 ||
            !game::local_player().is_enemy(other.team) || !other.bones.is_valid() || other.hitboxes.count <= 0)
        {
            continue;
        }

        auto total = 0;
        auto label_anchor = other.origin + foundation::vec3{0.0f, 0.0f, other.is_ducked ? 46.0f : 64.0f};
        for (const auto& box : other.hitboxes)
        {
            if (box.index < 0 || box.bone < 0 || box.bone >= static_cast<int>(other.bones.bones.size()))
                continue;
            const auto hitgroup = game::hitbox_data().hitgroup_from_hitbox(box.index);
            const auto part = simulation::aim_part_of(box, hitgroup);
            if ((hitbox_parts & part) == 0)
                continue;

            const auto& bone = other.bones.bones[static_cast<std::size_t>(box.bone)];
            if (part == config::combat_profile::aim_part::head)
                label_anchor = game::place_hitbox(bone, box).center() + foundation::vec3{0.0f, 0.0f, 9.0f};
            const auto candidates = simulation::build_aim_points(bone, box, multipoint, part, settings);
            total += candidates.count;

            const auto color = point_color(part);
            for (int i = 0; i < candidates.count; ++i)
            {
                const auto screen = camera.project(candidates.points[static_cast<std::size_t>(i)]);
                if (!camera.projection_valid(screen))
                    continue;
                draw_list.add_circle_filled(screen.x, screen.y, 2.4f, color, 8);
                draw_list.add_circle(screen.x, screen.y, 3.2f, render::draw::rgba{8, 10, 14, 200}, 8, 1.0f);
            }
        }

        if (total <= 0 || !font || !font->im_font)
            continue;
        const auto screen = camera.project(label_anchor);
        if (!camera.projection_valid(screen))
            continue;
        const auto label = std::format("MP {} {}", total, preset);
        const auto [width, height] = render::draw::measure_text(label, font);
        draw_list.add_text(std::floor(screen.x - width * 0.5f), std::floor(screen.y - height),
                           label, font, render::draw::rgba{245, 248, 255, 240},
                           render::draw::text_style::shadowed);
    }
}
} // namespace features::multipoint_overlay
