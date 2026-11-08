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
namespace
{
constexpr int k_walk_page = 3;
constexpr int k_automation_page = 4;
} // namespace

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
    if (menu_open)
    {
        const auto initial_position = initial_menu_layout_position(display.x, display.y);
        ImGui::SetNextWindowPos(initial_position, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({k_menu_width, k_menu_height}, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
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
        draw->PushClipRectFullScreen();
        soft_shadow(draw, min, max, 20.0f, {0.0f, 14.0f}, 34.0f, {0, 0, 0, 1}, 1.0f);
        draw->PopClipRect();
        draw->AddRectFilled(min, max, packed(k_bg_base), 20.0f);
        draw->AddRect(min, max, packed(k_border), 20.0f);
        draw->AddLine({min.x + 20.0f, min.y + 1.0f}, {max.x - 20.0f, min.y + 1.0f}, IM_COL32(255, 255, 255, 20));

        push_menu_font(render::fonts().menu_regular_12);
        draw_sidebar();
        draw_content();
        ImGui::PopFont();

        ImGui::End();

        push_menu_font(render::fonts().menu_regular_12);
        draw_visual_editor();
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(5);
    }

    if (guide_visible)
    {
        push_menu_font(render::fonts().menu_regular_12);
        draw_guide();
        ImGui::PopFont();
    }
    scale_menu_draw_lists(transform_origin, menu_scale);
}

void menu_t::draw_sidebar()
{
    ImGui::SetCursorPos({0.0f, 0.0f});
    ImGui::BeginChild("##sidebar", {240.0f, 650.0f}, false, ImGuiWindowFlags_NoScrollbar);
    const auto min = ImGui::GetWindowPos();
    const auto max = ImVec2{min.x + 240.0f, min.y + 650.0f};
    auto* draw = ImGui::GetWindowDrawList();

    add_vertical_gradient_rounded(draw, min, max, IM_COL32(0, 0, 0, 26), IM_COL32(0, 0, 0, 76), 20.0f,
                                  ImDrawFlags_RoundCornersLeft);
    draw->AddLine({max.x - 1.0f, min.y}, {max.x - 1.0f, max.y}, packed(k_border));

    // The plate logo sits above the wordmark; its PNG already has a transparent background. The source is 1188x840,
    // so keep that aspect ratio rather than stretching it into a square.
    constexpr auto logo_width = 180.0f;
    constexpr auto logo_aspect = 840.0f / 1188.0f;
    constexpr auto logo_height = logo_width * logo_aspect;
    constexpr auto logo_top = 10.0f;
    const auto logo_texture = ui::bound_overlay().pasta_logo_texture();
    if (logo_texture)
    {
        const auto texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(logo_texture));
        const auto logo_min = ImVec2{min.x + (240.0f - logo_width) * 0.5f, min.y + logo_top};
        draw->AddImage(ImTextureRef{texture_id}, logo_min,
                       ImVec2{logo_min.x + logo_width, logo_min.y + logo_height});
    }

    const auto* brand = render::fonts().menu_brand_30;
    const auto brand_size = brand && brand->im_font ? brand->font_size * 1.25f : 0.0f;
    const auto text_top =
        logo_texture ? logo_top + logo_height + 4.0f : (112.0f - brand_size) * 0.5f;
    if (brand && brand->im_font)
    {
        constexpr auto text = std::string_view{"PASTA"};
        float width{};
        for (const auto c : text)
            width += brand->im_font->CalcTextSizeA(brand_size, FLT_MAX, 0.0f, &c, &c + 1).x;
        constexpr auto letter_spacing = 2.5f;
        width += letter_spacing * (text.size() - 1);
        auto x = min.x + (240.0f - width) * 0.5f;
        for (const auto c : text)
        {
            draw->AddText(brand->im_font, brand_size, {x, min.y + text_top}, IM_COL32_WHITE, &c, &c + 1);
            x += brand->im_font->CalcTextSizeA(brand_size, FLT_MAX, 0.0f, &c, &c + 1).x + letter_spacing;
        }
    }

    static constexpr const char* labels[]{"Aimbot", "Triggerbot", "Visuals", "Walk Bot", "Automation", "Misc"};
    static constexpr int icons[]{0, 1, 2, 3, 6, 4};
    ImGui::SetCursorPos({20.0f, logo_texture ? text_top + brand_size + 14.0f : 112.0f});
    // Automation is a child of Walk Bot. It glides out while the Walk Bot section is selected and retracts into it
    // when the user moves to another page; the reveal is advanced every frame so both directions animate.
    const auto automation_selected = m_page == k_walk_page || m_page == k_automation_page;
    m_automation_reveal = approach(m_automation_reveal, automation_selected ? 1.0f : 0.0f, 0.18f);
    if (!automation_selected && m_automation_reveal < 0.002f)
        m_automation_reveal = 0.0f;

    ImVec2 walk_min{}, walk_max{};
    for (int i = 0; i < static_cast<int>(std::size(labels)); ++i)
    {
        const auto automation = i == k_automation_page;
        const auto reveal = std::clamp(m_automation_reveal, 0.0f, 1.0f);
        if (automation && reveal <= 0.0f)
            continue;
        // The row and the gap after it share one animated slot, so the rows below slide instead of jumping when the
        // child appears or retracts.
        const auto slot = automation ? 54.0f * reveal : 0.0f;
        if (automation)
            ImGui::Dummy({0.0f, slot});
        const auto slot_top = automation ? ImGui::GetCursorScreenPos().y - slot : 0.0f;
        if (automation)
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - slot);
            ImGui::PushClipRect({min.x, slot_top}, {max.x, slot_top + slot}, true);
            ImGui::BeginDisabled(reveal < 0.98f);
        }
        ImGui::SetCursorPosX(automation ? 36.0f + (1.0f - reveal) * 10.0f : 20.0f);
        ImGui::PushID(i);
        const auto active = m_page == i;
        const auto clicked = nav_button(labels[i], active, icons[i], automation ? 184.0f : 200.0f, automation ? reveal : 1.0f);
        if (automation)
        {
            ImGui::EndDisabled();
            ImGui::PopClipRect();
            // nav_button always advances a full row; hand the shorter animated slot to the next row instead.
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slot - 48.0f);
        }
        if (clicked && !active)
        {
            m_page = i;
            reset_content_animation();
        }
        const auto row_min = ImGui::GetItemRectMin();
        const auto row_max = ImGui::GetItemRectMax();
        if (i == k_walk_page)
        {
            walk_min = row_min;
            walk_max = row_max;
        }
        else if (automation)
        {
            // Keep the branch endpoint inside the animated slot while the row retracts.
            const auto child_mid = row_min.y + std::min(24.0f, slot * 0.5f);
            auto connector_color = k_border_light;
            auto endpoint_color = k_accent;
            connector_color.w *= reveal;
            endpoint_color.w *= reveal;
            const auto connector = packed(connector_color);
            const auto endpoint = packed(endpoint_color);
            draw->AddLine({walk_min.x, walk_max.y + 1.0f}, {walk_min.x, child_mid}, connector, 1.5f);
            draw->AddLine({walk_min.x, child_mid}, {row_min.x, child_mid}, endpoint, 1.5f);
            draw->AddCircleFilled({row_min.x, child_mid}, 1.7f, endpoint, 12);
        }
        ImGui::PopID();
        if (!automation && i != static_cast<int>(std::size(labels)) - 1)
            ImGui::Dummy({0.0f, 6.0f});
    }

    ImGui::EndChild();
}

void menu_t::draw_content()
{
    ImGui::SetCursorPos({240.0f, 0.0f});
    ImGui::BeginChild("##content", {710.0f, 650.0f}, false, ImGuiWindowFlags_NoScrollbar);
    m_content_animation = std::min(1.0f, m_content_animation + ImGui::GetIO().DeltaTime / 0.14f);
    const auto t = 1.0f - std::pow(1.0f - m_content_animation, 3.0f);
    ImGui::SetCursorPosY((1.0f - t) * 4.0f);

    if (m_page == 0)
        draw_aimbot();
    else if (m_page == 1)
        draw_triggerbot();
    else if (m_page == 2)
        draw_visuals();
    else if (m_page == k_walk_page)
        draw_walk_bot();
    else if (m_page == k_automation_page)
        draw_automation();
    else
        draw_misc();

    ImGui::EndChild();
}
} // namespace ui
