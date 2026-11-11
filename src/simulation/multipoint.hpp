#pragma once

#include <config/combat.hpp>
#include <game/body/body.hpp>
#include <foundation/math/vector.hpp>

#include <algorithm>
#include <array>

namespace simulation
{
// Candidate points on one hitbox capsule. The centre is always present; multipoint adds the two capsule ends and
// four points around the lateral plane. Seven is the full set, so the caller can iterate without allocation.
struct aim_point_buffer
{
    std::array<foundation::vec3, 7> points{};
    int count{};
};

// Builds the candidate points on the same placed hitbox the trigger traces. Keeping the geometry here means the
// Aimbot and the Walk Bot's own aim offer identical cap/side points, and every point returned is inside the hitbox.
[[nodiscard]] inline aim_point_buffer
build_aim_points(const game::skeleton_reader::data::bone& bone, const game::hitbox_catalog::entry& hitbox,
                 const bool multipoint, const int part,
                 const config::combat_profile::multipoint_settings& settings)
{
    aim_point_buffer out{};
    const auto capsule = game::place_hitbox(bone, hitbox);
    const auto center = capsule.center();
    out.points[out.count++] = center;

    if (!multipoint)
        return out;

    // The preset is a budget, not a different geometry: cap and side points keep their usual positions, and a lower
    // budget simply leaves the later candidates out. Cap and side candidates are interleaved so the highest preset
    // still samples both axes instead of spending its whole budget on the capsule ends.
    const auto limit = std::clamp(settings.max_points(), 1, 7);
    const auto add = [&](const foundation::vec3& point)
    {
        if (out.count < limit)
            out.points[out.count++] = point;
    };
    if (settings.caps)
        add(capsule.start);

    // Lateral spread perpendicular to the capsule axis: the sides of a head or shoulder, not only its length.
    auto axis = capsule.end - capsule.start;
    if (settings.sides && axis.length_sqr() > 0.0001f)
    {
        axis.normalize();
        auto right = axis.cross(foundation::vec3{0.0f, 0.0f, 1.0f});
        if (right.length_sqr() < 0.0001f)
            right = axis.cross(foundation::vec3{1.0f, 0.0f, 0.0f});
        right.normalize();
        const auto up = right.cross(axis).normalized();

        using part_t = config::combat_profile::aim_part;
        const auto scale = part == part_t::head   ? settings.head_scale
                           : part == part_t::body ? settings.body_scale
                                                  : settings.limb_scale;
        const auto reach = capsule.radius * std::clamp(scale, 0.05f, 0.95f);
        add(center + right * reach);
        if (settings.caps)
            add(capsule.end);
        add(center - right * reach);
        add(center + up * reach);
        add(center - up * reach);
    }
    else if (settings.caps)
    {
        add(capsule.end);
    }

    return out;
}
} // namespace simulation
