#pragma once

#include <game/input/bindings.hpp>
#include <game/memory/memory_services.hpp>
#include <features/misc/input_lifecycle.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <foundation/math/vector.hpp>
#include <render/draw.hpp>
#include <vector>

namespace features::misc
{

class auto_accept_t
{
public:
    auto_accept_t();
    ~auto_accept_t();
    auto_accept_t(const auto_accept_t&) = delete;
    auto_accept_t& operator=(const auto_accept_t&) = delete;

    void tick();

private:
    enum class click_phase : std::uint8_t
    {
        idle,
        moved,
        pressed,
    };

public:
    // What one pass over the Panorama registry saw: panels read, panels with the CUIPanel vtable, button-class
    // panels among them, panels whose id is the accept button, and accept buttons inside the accept popup. Journaled
    // when it changes; it tells a broken registry walk from a broken button test.
    struct scan_stats
    {
        std::uint32_t panels{}, ui_panels{}, buttons{}, accept_ids{}, accept_layout{};
        [[nodiscard]] bool operator==(const scan_stats&) const = default;
    };

    // Incremental walk over the Panorama panel registry, a chunk per tick. A slot is keyed by (panel, generation):
    // a panel is registered before its id and parent are filled in, and a freed panel can return at the same address.
    struct registry_scan
    {
        std::uint32_t cursor{};
        std::vector<std::uintptr_t> panels{};
        std::vector<std::uint32_t> generations{};
        // Panels that appeared after the first full pass and were not recognised yet: looked at again for a few
        // seconds. Panels present at the first pass are the baseline and are not tracked.
        std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> young{};
        bool baseline_done{};
        std::uintptr_t best_panel{};
        std::uint32_t best_generation{};
        scan_stats stats{};
        scan_stats reported{};
    };

private:
    // Panorama engine, panel registry and class table, resolved once per game session.
    struct panorama;
    std::unique_ptr<panorama> m_panorama;
    std::uintptr_t m_signal_identity{};
    std::uintptr_t m_candidate_panel{};
    std::uintptr_t m_clicked_panel{};
    registry_scan m_scan{};
    std::chrono::steady_clock::time_point m_last_blocked_report{};
    click_phase m_click_phase{click_phase::idle};
    std::uintptr_t m_click_window{};
    std::uintptr_t m_click_panel{};
    std::int32_t m_saved_cursor_x{};
    std::int32_t m_saved_cursor_y{};
    std::int32_t m_click_x{};
    std::int32_t m_click_y{};
    bool m_saved_cursor_valid{};
    bool m_clicked_panel_hidden{};
};

class auto_crouch_t
{
public:
    void tick();
    void reset();
    // False while combat must wait for the game to report the duck. The combat worker calls tick() first, then this.
    [[nodiscard]] bool combat_ready() const noexcept { return !m_waiting_for_duck; }

private:
    using owned_control = input_lifecycle::owned_control;

    [[nodiscard]] bool set_duck(bool pressed);

    owned_control m_duck{};
    // The pawn the duck is held for, and when combat last published it. A short grace bridges the gap between
    // combat ticks without keeping the duck after target selection is genuinely lost.
    std::uintptr_t m_engaged_target{};
    std::chrono::steady_clock::time_point m_target_seen_at{};
    // Sight gate. Before crouching, the target must stay exposed from the lower crouched eye; once committed, the
    // duck holds through a narrow angle and is lifted only when the enemy is dead or fully behind cover.
    std::chrono::steady_clock::time_point m_sight_ok_at{};
    std::chrono::steady_clock::time_point m_sight_bad_at{};
    std::chrono::steady_clock::time_point m_waiting_since{};
    bool m_waiting_for_duck{};
    bool m_duck_wait_timed_out{};
};

class auto_bonus_t
{
public:
    void tick();
    void reset();
    // Auto Knife has priority over the bonus pickup. While this is raised Auto Bonus sends no keys; clearing it is the
    // green flag from Auto Knife that lets Auto Bonus resume.
    void set_auto_knife_active(const bool active) noexcept
    {
        m_auto_knife_active.store(active, std::memory_order_release);
    }
    // Raised for the whole knife engagement. Other automations that would draw a weapon or click must keep their hands
    // off while it is set.
    [[nodiscard]] bool auto_knife_active() const noexcept
    {
        return m_auto_knife_active.load(std::memory_order_acquire);
    }
    // Auto Health Shot claims the weapon slot the same way while it draws, injects or restores, so a bonus pickup tap
    // cannot switch the weapon out from under the syringe.
    void set_auto_health_shot_active(const bool active) noexcept
    {
        m_auto_health_shot_active.store(active, std::memory_order_release);
    }
    // True while the pickup key is held. Only read from the combat worker (the same thread that ticks Auto Bonus): the
    // health shot waits a held tap out before it starts drawing, so two weapon-switch keys are never in flight.
    [[nodiscard]] bool key_pressed() const noexcept { return m_key.pressed; }

private:
    using owned_control = input_lifecycle::owned_control;

    [[nodiscard]] bool send_key(bool pressed);

    owned_control m_key{};
    std::chrono::steady_clock::time_point m_next_tap{};
    std::chrono::steady_clock::time_point m_press_started{};
    std::atomic<bool> m_auto_knife_active{};
    std::atomic<bool> m_auto_health_shot_active{};
    bool m_auto_knife_was_active{};
    bool m_auto_health_shot_was_active{};
};

// Auto Health Shot: the Auto Bonus Weapon pattern (a key held long enough for one game input sample, then released)
// applied to the player's health shot bind. Below the configured health and out of a fight (not aiming and not
// shooting), the bind is pressed until the syringe is in hand; the primary attack is then held for the injection and a
// firearm is drawn again. It is a standalone automation: it does not need the walk bot to be running.
class auto_health_shot_t
{
public:
    void tick();
    void reset();
    void on_render(render::draw::draw_list& draw_list);

private:
    enum class phase : std::uint8_t
    {
        idle,
        drawing,   // the health shot bind is being pressed; waiting for the syringe
        injecting, // the primary attack is held for the injection
        restoring, // a firearm is being drawn again
    };

    // Why the idle state is not starting a use; shown by the debug overlay.
    enum class block : std::uint8_t
    {
        none,
        disabled,
        no_key,
        dead,
        health_ok,
        auto_knife,
        recent_damage,
        player_near,
        aiming,
        autofire,
        attack_held,
        cooldown,
        no_syringe,
        no_firearm,
        input_blocked,
        not_running,
    };

    using owned_control = input_lifecycle::owned_control;

    [[nodiscard]] bool send_key(owned_control& control, std::uint16_t key, bool pressed);
    [[nodiscard]] bool send_attack(bool pressed);
    [[nodiscard]] bool release_all();
    // The aimbot/trigger currently holds a target, or autofire/attack is running. Reads the engagement timestamps the
    // aimbot publishes every active tick instead of the last selected target, which stays behind when its tick stops.
    [[nodiscard]] static block combat_block(std::chrono::steady_clock::time_point now);
    // Raises or lowers the Auto Bonus weapon-slot reservation for the current phase.
    void sync_auto_bonus_priority() noexcept;
    void publish_debug(block reason) noexcept;

    owned_control m_key{};
    owned_control m_slot{};
    owned_control m_attack{};
    phase m_phase{phase::idle};
    int m_attempts{};
    std::chrono::steady_clock::time_point m_key_started{};
    std::chrono::steady_clock::time_point m_slot_started{};
    std::chrono::steady_clock::time_point m_next_action{};
    std::chrono::steady_clock::time_point m_inject_at{};
    std::chrono::steady_clock::time_point m_injection_until{};
    std::chrono::steady_clock::time_point m_retry_at{};
    // Health-drop tracking: any decrease is damage taken, and the use waits out the configured window so it does not
    // start injecting in the middle of a fight that is still going on.
    int m_last_health{-1};
    std::chrono::steady_clock::time_point m_damage_at{};
    std::chrono::steady_clock::time_point m_damage_until{};
    // Distance to the nearest living enemy measured by the last gate check; negative when there was none.
    float m_near_distance{-1.0f};

    // Debug values the render thread reads.
    std::atomic<std::uint8_t> m_debug_phase{};
    std::atomic<std::uint8_t> m_debug_block{};
    std::atomic<std::int32_t> m_debug_health{};
    std::atomic<int> m_debug_attempts{};
    std::atomic<bool> m_debug_syringe{};
    std::atomic<bool> m_debug_attack{};
    std::atomic<bool> m_debug_key{};
    std::atomic<int> m_debug_near{};
    std::atomic<int> m_debug_next_ms{};
};

// Bunny Hop ported from Vesta (features/misc/bhop.cpp, Apache-2.0). Only the Bunny branch is kept: Edge Jump and its
// collision prediction are deliberately absent. The jump key is owned through the input gateway's key gate, and
// release() tears that ownership down on every exit path.
class bhop_t
{
public:
    void tick();
    // Clears the key gate and the per-ground-sample state. When restore_physical_jump is true and the gate had swallowed
    // a physical jump press, the player's own jump is put back so leaving the feature does not eat a held key.
    void release(bool restore_physical_jump = false) noexcept;

private:
    void reset_sample() noexcept;

    int m_reported_jump_key{-1};
    std::uint16_t m_jump_key{};
    bool m_gate_requested{};
    float m_last_simulation_time{-1.0f};
    std::uintptr_t m_bunny_pawn{};
    std::chrono::steady_clock::time_point m_last_bunny_tap{};
    // Round identity used to release input across a round or map change.
    std::uintptr_t m_rules_object{};
    int m_rules_rounds{};
    bool m_rules_known{};
    std::chrono::steady_clock::time_point m_next_rules_check{};
};

inline auto_accept_t& auto_accept()
{
    static auto_accept_t value{};
    return value;
}
inline auto_crouch_t& auto_crouch()
{
    static auto_crouch_t value{};
    return value;
}
inline auto_bonus_t& auto_bonus()
{
    static auto_bonus_t value{};
    return value;
}
inline auto_health_shot_t& auto_health_shot()
{
    static auto_health_shot_t value{};
    return value;
}
inline bhop_t& bhop()
{
    static bhop_t value{};
    return value;
}
// Writes the live Auto Accept resolver report for a diagnostic run; 0 when the Panorama registry was readable.
int auto_accept_report(std::ostream& out, const game::attach_status& attached);

} // namespace features::misc
