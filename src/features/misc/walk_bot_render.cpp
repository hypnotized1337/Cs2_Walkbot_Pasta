#include <stdafx.hpp>
#include <features/misc/walk_bot_detail.hpp>
#include <features/multipoint_overlay.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <game/state/services.hpp>
#include <render/fonts.hpp>
#include <simulation/collision.hpp>

#include <algorithm>
#include <cmath>

namespace features::misc
{
using namespace walk_detail;

void walk_bot_t::on_render(render::draw::draw_list& draw_list)
{
    const auto snapshot = config::get_runtime_snapshot();
    if (!snapshot)
        return;
    const auto& cfg = snapshot->general.m_walk_bot;
    if (!cfg.enabled)
        return;

    if (cfg.aim_multipoint && cfg.aim_multipoint_config.display)
    {
        const auto players = game::world().players();
        if (players)
            features::multipoint_overlay::draw(draw_list, *players, cfg.aim_hitboxes, true,
                                               cfg.aim_multipoint_config);
    }

    // Recording: a red dot and REC in the top-left corner.
    bool recording{};
    {
        std::scoped_lock lock(m_route_mutex);
        recording = m_recording_active;
    }
    if (recording)
    {
        draw_list.add_circle_filled(24.0f, 24.0f, 6.0f, render::draw::rgba{235, 60, 60, 230}, 16);
        draw_list.add_text(36.0f, 16.0f, "REC", render::fonts().menu_regular_12,
                           render::draw::rgba{235, 235, 240, 230});
    }

    std::shared_ptr<const simulation::nav_graph> graph;
    std::shared_ptr<const path> route;
    std::shared_ptr<const std::vector<walk_logic::recording>> recordings;
    foundation::vec3 destination{};
    bool has_destination{};
    {
        std::scoped_lock lock(m_route_mutex);
        graph = m_graph;
        route = m_path;
        recordings = m_recordings_shown;
        destination = m_destination;
        has_destination = m_has_destination;
    }
    const auto progress = m_progress.load(std::memory_order_acquire);
    const auto& camera = game::camera();
    const auto eye = camera.origin();
    const auto with_alpha = [](render::draw::rgba color, const float factor)
    {
        color.a = static_cast<std::uint8_t>(std::clamp(static_cast<float>(color.a) * factor, 0.0f, 255.0f));
        return color;
    };

    // Knife eligibility readout: per enemy, the anchor it was first seen at, its spawn stamp, whether it ever moved,
    // and why the auto knife will or will not select it. The watch list is copied under its mutex because the
    // movement worker updates it every tick.
    if (cfg.auto_knife_debug)
    {
        const auto players = game::world().players();
        const auto* font = render::fonts().menu_regular_12;
        if (players && font && font->im_font)
        {
            std::unordered_map<std::uintptr_t, knife_watch> watches;
            {
                std::scoped_lock lock(m_knife_watch_mutex);
                watches = m_knife_watch;
            }
            const auto local_pawn = game::local_player().pawn();
            const game::player_snapshot* local = nullptr;
            for (const auto& other : *players)
                if (other.pawn == local_pawn)
                {
                    local = &other;
                    break;
                }
            const auto feet = local ? local->origin : eye - foundation::vec3{0.0f, 0.0f, 64.0f};
            const auto& world = simulation::collision();
            const auto now = clock::now();
            const auto [display_w, display_h] = render::draw::get_display_size();
            constexpr auto line_height = 15.0f;
            constexpr render::draw::rgba text_color{225, 232, 245, 245};
            constexpr render::draw::rgba detail_color{185, 196, 215, 230};
            constexpr render::draw::rgba ready_color{115, 235, 150, 255};
            constexpr render::draw::rgba blocked_color{255, 115, 120, 255};
            constexpr render::draw::rgba anchor_color{255, 196, 90, 220};

            for (const auto& other : *players)
            {
                if (!other.pawn || other.pawn == local_pawn || other.health <= 0 ||
                    !game::local_player().is_enemy(other.team))
                    continue;
                const auto watch = watches.find(other.pawn);
                if (watch == watches.end() || now - watch->second.seen > std::chrono::seconds(5))
                    continue;
                const auto& record = watch->second;
                const auto head = other.origin + foundation::vec3{0.0f, 0.0f, other.is_ducked ? 46.0f : 64.0f};
                const auto screen = camera.project(head + foundation::vec3{0.0f, 0.0f, 8.0f});
                if (!camera.projection_valid(screen))
                    continue;

                const auto distance = other.origin.distance(feet);
                const auto in_range = distance <= cfg.auto_knife_distance &&
                                      std::abs(other.origin.z - feet.z) <= 48.0f;
                const auto reachable = !world.valid() ||
                                       !world.trace_ray(feet + foundation::vec3{0.0f, 0.0f, 20.0f},
                                                        other.origin + foundation::vec3{0.0f, 0.0f, 20.0f})
                                            .hit;
                const auto visible = !world.valid() || !world.trace_ray(eye, head).hit;
                const auto grace_left = record.respawn_until > now
                                            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  record.respawn_until - now)
                                            : std::chrono::milliseconds{0};
                const auto still_left = now - record.still_since < std::chrono::milliseconds(1200)
                                            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::milliseconds(1200) - (now - record.still_since))
                                            : std::chrono::milliseconds{0};

                auto eligible = false;
                std::string status{};
                if (!cfg.auto_knife)
                    status = "feature off";
                else if (record.dead)
                    status = "dead";
                else if (grace_left.count() > 0)
                    status = std::format("grace {:.1f}s", static_cast<float>(grace_left.count()) / 1000.0f);
                else if (other.invulnerable)
                    status = "spawn protected";
                else if (!in_range)
                    status = "out of range";
                else if (cfg.auto_knife_afk_only && record.moved)
                    status = "moved";
                else if (cfg.auto_knife_afk_only && still_left.count() > 0)
                    status = std::format("still {:.1f}s", static_cast<float>(still_left.count()) / 1000.0f);
                else if (!reachable)
                    status = "no path";
                else if (!visible)
                    status = "no sight";
                else
                {
                    status = "READY";
                    eligible = true;
                }

                const std::array<std::string, 4> lines{
                    other.display_name.empty() ? std::string{"enemy"} : other.display_name,
                    std::format("Anchor {:.0f} {:.0f} {:.0f}", record.anchor.x, record.anchor.y, record.anchor.z),
                    std::format("Spawn {}  Moved {}",
                                record.spawn > 0.0f ? std::format("{:.2f}", record.spawn) : std::string{"n/a"},
                                record.moved ? "YES" : "NO"),
                    std::format("Eligible {}  {}", eligible ? "YES" : "NO", status),
                };
                auto width = 0.0f;
                for (const auto& line : lines)
                    width = std::max(width, render::draw::measure_text(line, font).first);
                width = std::ceil(width);
                const auto panel_height = line_height * static_cast<float>(lines.size()) + 10.0f;
                auto x = screen.x + 16.0f;
                auto y = screen.y - panel_height + 4.0f;
                if (x + width + 12.0f > static_cast<float>(display_w))
                    x = screen.x - width - 28.0f;
                x = std::clamp(x, 4.0f, std::max(4.0f, static_cast<float>(display_w) - width - 12.0f));
                y = std::clamp(y, 4.0f, std::max(4.0f, static_cast<float>(display_h) - panel_height - 1.0f));

                const auto anchor_screen = camera.project(record.anchor + foundation::vec3{0.0f, 0.0f, 2.0f});
                if (camera.projection_valid(anchor_screen))
                {
                    draw_list.add_circle(anchor_screen.x, anchor_screen.y, 4.0f, anchor_color, 12, 1.5f);
                    const auto feet_screen = camera.project(other.origin + foundation::vec3{0.0f, 0.0f, 2.0f});
                    if (record.moved && camera.projection_valid(feet_screen))
                        draw_list.add_line(anchor_screen.x, anchor_screen.y, feet_screen.x, feet_screen.y,
                                           with_alpha(anchor_color, 0.45f), 1.0f);
                }

                draw_list.add_rect_filled(x - 6.0f, y - 5.0f, width + 12.0f, panel_height,
                                          render::draw::rgba{6, 9, 14, 190});
                draw_list.add_rect_filled(x - 6.0f, y - 5.0f, 2.0f, panel_height,
                                          eligible ? ready_color : blocked_color);
                for (std::size_t i = 0; i < lines.size(); ++i)
                {
                    const auto color = i == 0 ? text_color : i == 3 ? (eligible ? ready_color : blocked_color)
                                                                    : detail_color;
                    draw_list.add_text(x, y + line_height * static_cast<float>(i), lines[i], font, color,
                                       render::draw::text_style::shadowed);
                }
            }
        }
    }

    // Recorded routes of this map and side, in amber, each as its own line (jumps and crouches are on the points).
    if (cfg.show_recordings && recordings)
    {
        constexpr render::draw::rgba recorded_color{255, 196, 90, 170};
        std::vector<float> line;
        for (const auto& recording : *recordings)
        {
            line.clear();
            for (const auto& point : recording.points)
            {
                const auto projected = camera.project(point.position + foundation::vec3{0.0f, 0.0f, 2.0f});
                if (!camera.projection_valid(projected))
                {
                    draw_list.add_polyline(line, recorded_color, false, 1.5f);
                    line.clear();
                    continue;
                }
                line.push_back(projected.x);
                line.push_back(projected.y);
            }
            draw_list.add_polyline(line, recorded_color, false, 1.5f);
        }
    }

    // The navigation mesh around the player: polygon edges the eye can see, fading with distance.
    if (cfg.show_mesh && graph)
    {
        const auto& edges = graph->edges();
        if (m_edge_graph != graph.get() || m_edge_visible.size() != edges.size())
        {
            m_edge_graph = graph.get();
            m_edge_visible.assign(edges.size(), 0);
            m_edge_cursor = 0;
        }
        const auto range = cfg.mesh_distance;
        const auto lift = foundation::vec3{0.0f, 0.0f, 1.5f};
        // Line of sight is refreshed for a slice of the edges every frame.
        if (simulation::collision().valid() && !edges.empty())
        {
            for (int checked = 0, scanned = 0; checked < 320 && scanned < static_cast<int>(edges.size()); ++scanned)
            {
                const auto index = m_edge_cursor;
                m_edge_cursor = (m_edge_cursor + 1) % edges.size();
                const auto mid = (edges[index].first + edges[index].second) * 0.5f + foundation::vec3{0, 0, 4.0f};
                const auto length = mid.distance(eye);
                if (length > range)
                {
                    m_edge_visible[index] = 0;
                    continue;
                }
                ++checked;
                const auto hit = simulation::collision().trace_ray(eye, mid);
                m_edge_visible[index] = !hit.hit || hit.distance >= length - 8.0f ? 1 : 0;
            }
        }
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            if (!m_edge_visible[i])
                continue;
            const auto& [a, b] = edges[i];
            const auto distance = ((a + b) * 0.5f).distance(eye);
            if (distance > range)
                continue;
            const auto sa = camera.project(a + lift);
            const auto sb = camera.project(b + lift);
            if (!camera.projection_valid(sa) || !camera.projection_valid(sb))
                continue;
            draw_list.add_line(sa.x, sa.y, sb.x, sb.y, with_alpha(cfg.mesh_color, 1.0f - distance / range), 1.0f);
        }
    }

    if (!cfg.show_path || !route || route->empty())
        return;
    // A door or breakable the bot is dealing with.
    {
        const auto mark = m_render_obstacle.load(std::memory_order_acquire);
        if (mark.x != 0.0f || mark.y != 0.0f)
        {
            const auto projected = camera.project(mark);
            if (camera.projection_valid(projected))
                draw_list.add_circle(projected.x, projected.y, 9.0f, render::draw::rgba{255, 190, 70, 220}, 20, 2.0f);
        }
    }
    const auto total = route->back().distance;
    const auto lift = foundation::vec3{0.0f, 0.0f, 2.0f};
    // The line ahead, as one stroke per stretch of samples so its alpha can fade along the way.
    std::vector<float> stroke;
    float stroke_alpha = 1.0f;
    const auto flush = [&]
    {
        if (stroke.size() >= 4)
            draw_list.add_polyline(stroke, with_alpha(cfg.path_color, stroke_alpha), false, cfg.path_thickness);
        stroke.clear();
    };
    float next_marker = std::ceil(progress / 64.0f) * 64.0f;
    int samples_in_stroke = 0;
    for (std::size_t i = 0; i < route->size(); ++i)
    {
        const auto& point = (*route)[i];
        if (point.distance + k_sample_spacing < progress)
            continue;
        const auto along = std::clamp((point.distance - progress) / std::max(total - progress, 1.0f), 0.0f, 1.0f);
        const auto projected = camera.project(point.position + lift);
        if (!camera.projection_valid(projected))
        {
            flush();
            samples_in_stroke = 0;
            continue;
        }
        stroke.push_back(projected.x);
        stroke.push_back(projected.y);
        if (++samples_in_stroke >= 8)
        {
            stroke_alpha = 1.0f - along * 0.7f;
            flush();
            stroke.push_back(projected.x);
            stroke.push_back(projected.y);
            samples_in_stroke = 1;
        }
        const auto marker_color = with_alpha(cfg.path_color, 1.0f - along * 0.6f);
        if (point.distance >= next_marker)
        {
            draw_list.add_circle_filled(projected.x, projected.y, point.duck ? 3.5f : 2.2f, marker_color, 10);
            next_marker += 64.0f;
        }
        switch (point.act)
        {
            case action::jump:
            case action::crouch_jump:
            {
                draw_list.add_circle(projected.x, projected.y, 6.0f, marker_color, 16, 1.5f);
                const auto top = camera.project(
                    point.position + foundation::vec3{0.0f, 0.0f, point.act == action::jump ? 24.0f : 40.0f});
                if (camera.projection_valid(top))
                    draw_list.add_line(projected.x, projected.y, top.x, top.y, marker_color, 1.5f);
                break;
            }
            case action::ladder_up:
            case action::ladder_down:
            {
                const auto end = camera.project(point.goal + lift);
                if (camera.projection_valid(end))
                    draw_list.add_line(projected.x, projected.y, end.x, end.y, marker_color, cfg.path_thickness + 1.0f);
                break;
            }
            default:
                break;
        }
    }
    flush();

    if (has_destination)
    {
        constexpr int k_segments = 24;
        std::vector<float> ring;
        for (int i = 0; i < k_segments; ++i)
        {
            const auto theta = static_cast<float>(i) / static_cast<float>(k_segments) * 6.2831853f;
            const auto projected =
                camera.project(destination + foundation::vec3{std::cos(theta) * 16.0f, std::sin(theta) * 16.0f, 2.0f});
            if (!camera.projection_valid(projected))
            {
                ring.clear();
                break;
            }
            ring.push_back(projected.x);
            ring.push_back(projected.y);
        }
        if (!ring.empty())
            draw_list.add_polyline(ring, cfg.path_color, true, cfg.path_thickness);
    }
}
} // namespace features::misc
