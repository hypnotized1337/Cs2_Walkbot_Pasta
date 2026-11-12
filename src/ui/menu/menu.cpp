#include <stdafx.hpp>
#include <render/presentation.hpp>
#include <ui/presentation.hpp>
#include <platform/windows/input.hpp>
#include <config/first_run.hpp>
#include <game/input/bindings.hpp>
#include <config/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <features/chams/preview.hpp>
#include <features/chams/renderer.hpp>
#include <render/text/localization.hpp>
#include <ui/menu/menu.hpp>
#include <ui/overlay/input.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <ui/menu/internal.hpp>
#include <ui/overlay/overlay.hpp>

namespace ui
{
using namespace ui::menu::detail;

bool menu_t::initialize(HWND hwnd)
{
    m_hwnd = hwnd;
    return true;
}

void menu_t::map_pointer_to_layout(float& x, float& y, const float display_width,
                                   const float display_height) const noexcept
{
    if (!is_open())
        return;
    const auto scale = current_menu_scale(display_width, display_height);
    if (std::abs(scale - 1.0f) < 0.0001f)
        return;
    const auto origin = menu_transform_origin(display_width, display_height);
    x = origin.x + (x - origin.x) / scale;
    y = origin.y + (y - origin.y) / scale;
}

void menu_t::map_pointer_to_screen(float& x, float& y, const float display_width,
                                   const float display_height) const noexcept
{
    if (!is_open())
        return;
    const auto scale = current_menu_scale(display_width, display_height);
    if (std::abs(scale - 1.0f) < 0.0001f)
        return;
    const auto origin = menu_transform_origin(display_width, display_height);
    x = origin.x + (x - origin.x) * scale;
    y = origin.y + (y - origin.y) * scale;
}

void menu_t::reset_content_animation()
{
    m_content_animation = 0.0f;
}

void menu_t::poll_hotkey()
{
    // A fresh installation shows the guide by itself, without the menu; it waits for the movement keys like a normal open.
    const bool guide_wanted = config::first_run::pending();
    if (!guide_wanted)
        m_guide_open_pending = false;
    else if (!m_guide_visible.load(std::memory_order_relaxed) && !m_open.load(std::memory_order_relaxed))
        m_guide_open_pending = true;

    const auto menu_key = config::lifecycle_keys().menu;
    const bool hotkey_is_down = (::GetAsyncKeyState(menu_key) & 0x8000) != 0;
    // While the guide is up it is modal: the menu key waits until the guide is finished.
    const bool guide_modal = guide_wanted && m_guide_visible.load(std::memory_order_relaxed);
    if (hotkey_is_down && !m_menu_hotkey_was_down && !guide_modal)
    {
        if (m_open.load(std::memory_order_relaxed))
        {
            m_open.store(false, std::memory_order_release);
            m_open_pending = false;
        }
        else
            m_open_pending = !m_open_pending;
    }
    m_menu_hotkey_was_down = hotkey_is_down;

    const bool open_menu = m_open_pending && !m_open.load(std::memory_order_relaxed);
    if (!open_menu && !m_guide_open_pending)
        return;

    std::vector<std::uint16_t> movement_keys{};
    constexpr std::array movement_actions{
        game::input_action::forward,
        game::input_action::back,
        game::input_action::left,
        game::input_action::right,
    };
    for (const auto action : movement_actions)
    {
        for (const auto& binding : game::input_bindings().candidates(action))
        {
            if (binding.device == game::input_device::keyboard && binding.virtual_key &&
                std::ranges::find(movement_keys, binding.virtual_key) == movement_keys.end())
            {
                movement_keys.push_back(binding.virtual_key);
            }
        }
    }
    if (std::ranges::any_of(movement_keys, [](const std::uint16_t key)
                            { return platform::windows::bound_input().physical_key_down(key); }))
    {
        return;
    }

    if (open_menu)
        m_open.store(true, std::memory_order_release);
    if (m_guide_open_pending)
    {
        m_guide_front_pending = true;
        m_guide_open_pending = false;
        m_guide_visible.store(true, std::memory_order_release);
    }
    std::vector<platform::windows::input_gateway::key_transition> releases{};
    releases.reserve(movement_keys.size());
    for (const auto key : movement_keys)
        releases.push_back({key, false});
    for (std::size_t offset = 0; offset < releases.size(); offset += 8)
        platform::windows::bound_input().keys(
            std::span{releases}.subspan(offset, std::min<std::size_t>(8, releases.size() - offset)));
    platform::windows::bound_input().clear_movement_gates();
    m_open_pending = false;
    g_toggle_animations.clear();
    reset_content_animation();
}

void menu_t::draw()
{
    // "Show the guide again" from the open menu: nothing to wait for.
    if (config::first_run::pending() && m_open.load(std::memory_order_acquire) &&
        !m_guide_visible.load(std::memory_order_acquire))
    {
        m_guide_front_pending = true;
        m_guide_visible.store(true, std::memory_order_release);
    }
    const bool menu_open = m_open.load(std::memory_order_acquire);
    const bool guide_visible = m_guide_visible.load(std::memory_order_acquire);
    if (!menu_open && !guide_visible)
    {
        return;
    }
    synchronize_menu_palette();

    const auto display = ImGui::GetIO().DisplaySize;
    const auto menu_scale = current_menu_scale(display.x, display.y);
    const auto transform_origin = menu_transform_origin(display.x, display.y);
    const menu_render_scope render_scope{display, transform_origin, menu_scale};
#if defined(VESTA_ASTRAL_MENU)
    const astral_style_scope style_scope;
#endif
    if (menu_open)
    {
#if defined(VESTA_ASTRAL_MENU)
        draw_astral(display.x, display.y);
        push_menu_font(render::fonts().menu_regular_12);
        draw_visual_editor();
        ImGui::PopFont();
#else
        const auto initial_position = initial_menu_layout_position(display.x, display.y);
        ImGui::SetNextWindowPos(initial_position, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({k_menu_width, k_menu_height}, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, k_text_main);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4{0, 0, 0, 0});
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, k_border_light);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, k_text_muted);
        ImGui::Begin("##vesta_native_menu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoSavedSettings);

        const auto min = ImGui::GetWindowPos();
        const auto max = ImVec2{min.x + k_menu_width, min.y + k_menu_height};
        g_menu_min = min;
        g_menu_max = max;
        auto* draw = ImGui::GetWindowDrawList();
        // Source Ring(): nearly black outside, warm-grey edge, then the window fill. No drop shadow: the source window
        // is a flat panel and the outer ring is what separates it from the game.
        draw->AddRectFilled(min, max, packed(k_outline_dark), 8.0f);
        draw->AddRectFilled(min + ImVec2{1.0f, 1.0f}, max - ImVec2{1.0f, 1.0f}, packed(k_border), 7.0f);
        draw->AddRectFilled(min + ImVec2{2.0f, 2.0f}, max - ImVec2{2.0f, 2.0f}, packed(k_bg_base), 6.0f);

        // One source hairline separates the title/tab row from the page. The group boxes repeat it under their titles.
        draw->AddLine({min.x + 6.0f, min.y + 30.0f}, {max.x - 6.0f, min.y + 30.0f}, packed(k_line), 1.0f);

        push_menu_font(render::fonts().menu_source_18);
        draw_top_bar();
        draw_content();
        ImGui::PopFont();

        ImGui::End();

        push_menu_font(render::fonts().menu_regular_12);
        draw_visual_editor();
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(5);
#endif
    }

    if (guide_visible)
    {
        push_menu_font(render::fonts().menu_regular_12);
        draw_guide();
        ImGui::PopFont();
    }
    scale_menu_draw_lists(transform_origin, menu_scale);
}

void menu_t::draw_top_bar()
{
    // The shell is deliberately close to the source: a wordmark at the left, muted lowercase sections at the right,
    // and a single active underline. No navigation rail, icons or logo plate.
    const auto min = ImGui::GetWindowPos();
    auto* draw = ImGui::GetWindowDrawList();

    draw->AddText({min.x + 12.0f, min.y + 5.0f}, packed(k_text_main), "PASTA");
    const auto wordmark_width = ImGui::CalcTextSize("PASTA").x;
    draw->AddText({min.x + 12.0f + wordmark_width + 5.0f, min.y + 5.0f}, packed(k_text_muted), "(retard)");

    // Stable IDs use the translation keys; English is shown in the source's lowercase style, other languages use the
    // existing localized labels. This keeps both the source look and the old localization behaviour intact.
    static constexpr const char* label_keys[]{"Aimbot", "Triggerbot", "Visuals", "Walk Bot", "Automation", "Misc", "Configs"};
    static constexpr const char* english_labels[]{"aimbot", "triggerbot", "visuals", "walk bot", "automation", "misc", "configs"};
    constexpr int tab_count = static_cast<int>(std::size(label_keys));
    const auto english = render::localization::current() == render::localization::id::en;
    const auto tab_text = [&](const int index) {
        return english ? english_labels[index] : render::localization::tr(label_keys[index]);
    };
    std::array<float, tab_count> tab_x{};
    constexpr auto tab_gap = 14.0f;
    auto right = k_menu_width - 12.0f;
    for (int i = tab_count - 1; i >= 0; --i)
    {
        const auto width = ImGui::CalcTextSize(tab_text(i)).x;
        right -= width;
        tab_x[static_cast<std::size_t>(i)] = right;
        right -= tab_gap;
    }

    for (int i = 0; i < tab_count; ++i)
    {
        const auto active = m_page == i;
        const auto* label = tab_text(i);
        const auto width = ImGui::CalcTextSize(label).x;
        ImGui::SetCursorScreenPos({min.x + tab_x[static_cast<std::size_t>(i)] - 5.0f, min.y + 3.0f});
        ImGui::PushID(i);
        ImGui::InvisibleButton(label_keys[i], {width + 10.0f, 26.0f});
        const auto clicked = ImGui::IsItemClicked();
        const auto hovered = ImGui::IsItemHovered();
        pointer_cursor_if_hovered();
        if (clicked && !active)
        {
            m_page = i;
            reset_content_animation();
        }
        ImGui::PopID();

        const auto ink = active ? k_text_main : hovered ? k_text_main : k_text_muted;
        draw->AddText({min.x + tab_x[static_cast<std::size_t>(i)], min.y + 5.0f}, packed(ink), label);
        if (active)
        {
            draw->AddLine({min.x + tab_x[static_cast<std::size_t>(i)] - 1.0f, min.y + 28.0f},
                          {min.x + tab_x[static_cast<std::size_t>(i)] + width + 1.0f, min.y + 28.0f},
                          packed(k_text_main), 1.5f);
        }
    }
}

void menu_t::draw_content()
{
    ImGui::SetCursorPos({0.0f, 32.0f});
    ImGui::BeginChild("##content", {k_menu_width, k_menu_height - 32.0f}, false, ImGuiWindowFlags_NoScrollbar);
    m_content_animation = std::min(1.0f, m_content_animation + ImGui::GetIO().DeltaTime / 0.14f);
    const auto t = 1.0f - std::pow(1.0f - m_content_animation, 3.0f);
    ImGui::SetCursorPosY((1.0f - t) * 4.0f);

    if (m_page == 0)
        draw_aimbot();
    else if (m_page == 1)
        draw_triggerbot();
    else if (m_page == 2)
        draw_visuals();
    else if (m_page == 3)
        draw_walk_bot();
    else if (m_page == 4)
        draw_automation();
    else if (m_page == 5)
        draw_misc();
    else
        draw_configs();

    ImGui::EndChild();
}
} // namespace ui
