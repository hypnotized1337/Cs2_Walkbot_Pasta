#include <stdafx.hpp>
#include <platform/windows/storage.hpp>
#include <diagnostics/health.hpp>
#include <ui/presentation.hpp>
#include <game/input/bindings.hpp>
#include <config/hotkeys.hpp>
#include <features/visuals/visuals.hpp>
#include <features/visuals/hitsound.hpp>
#include <features/chams/preview.hpp>
#include <features/chams/renderer.hpp>
#include <render/text/localization.hpp>
#include <diagnostics/report.hpp>
#include <platform/windows/open_folder.hpp>
#include <config/first_run.hpp>
#include <ui/menu/menu.hpp>
#include <ui/overlay/input.hpp>
#include <imgui.h>
#include <unordered_map>
#include <cstdio>
#include <imgui_internal.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <ui/menu/internal.hpp>
#include <ui/menu/library_widgets.hpp>
#include <dxgi.h>
#include <wrl/client.h>
#include <ui/menu/card_metrics.hpp>
#include <ui/overlay/overlay.hpp>

namespace ui
{
using namespace ui::menu::detail;

namespace
{
enum class report_outcome
{
    none,
    saved,
    failed
};
report_outcome g_report_outcome{}; // Render thread only.

// The "System" line of a bug report: Windows build number and the first graphics adapter's marketing name. Neither
// identifies a person; both are what the issue template asks for.
std::string system_summary()
{
    std::string text;
    using rtl_get_version = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (const auto ntdll = ::GetModuleHandleW(L"ntdll.dll"))
    {
        if (const auto get = reinterpret_cast<rtl_get_version>(::GetProcAddress(ntdll, "RtlGetVersion")))
        {
            OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (get(&info) == 0)
                text += std::format(" windows={}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
        }
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapters1(0, &adapter)))
    {
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)))
        {
            std::string gpu;
            for (const wchar_t c : std::wstring_view{description.Description})
                gpu += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
            text += " gpu=\"" + gpu + "\"";
        }
    }
    return text;
}

// One user-initiated action: write the report; only fixed state names go into the summary.
void export_diagnostic_report()
{
    namespace readiness = diagnostics::readiness;
    const auto geometry = diagnostics::health().geometry.snapshot();
    std::string summary{"geometry="};
    summary += readiness::name(geometry.value);
    summary += " reason=";
    summary += readiness::name(geometry.cause);
    summary += " overlay=";
    summary += readiness::name(diagnostics::health().overlay.snapshot().value);
    summary += system_summary();
    const auto result =
        diagnostics::export_report(diagnostics::default_logs_directory(), diagnostics::desktop_directory(), summary);
    g_report_outcome = result.ok ? report_outcome::saved : report_outcome::failed;
}

void open_log_folder()
{
    (void)platform::windows::open_folder(diagnostics::default_logs_directory());
}
void draw_general_tab()
{
    auto& p = config::general_settings;
    // The game's own binds: what the features press (movement, attack, use, reload, buy menu) has to match them.
    // Automation no longer lives here; it has its own top-level tab.
    auto& keys = p.m_game_controls;
    card_in_column("game_movement", "GAME MOVEMENT BINDS", 7, 0,
                   [&]
                   {
                       keybind_row("Forward", keys.forward, true, true);
                       keybind_row("Back", keys.back, true, true);
                       keybind_row("Left", keys.left, true, true);
                       keybind_row("Right", keys.right, true, true);
                       keybind_row("Walk", keys.walk, true, true);
                       keybind_row("Duck", keys.duck, true, true);
                       keybind_row("Jump", keys.jump, true, true);
                   });
    card_in_column("game_actions", "GAME ACTION BINDS", 6, 1,
                   [&]
                   {
                       keybind_row("Attack", keys.attack, false, true);
                       keybind_row("Secondary Attack", keys.attack2, false, true);
                       keybind_row("Use", keys.use, true, true);
                       keybind_row("Reload Weapon", keys.reload, true, true);
                       keybind_row("Buy Menu", keys.buy_menu, true, true);
                       if (button_row("Reset Game Binds", "Reset", row_action_icon::none))
                           keys = {};
                   });
    // A short status under the engine rows: the last report, or a crash of the previous session.
    const char* status{};
    bool status_failed{};
    if (g_report_outcome != report_outcome::none)
    {
        status = g_report_outcome == report_outcome::saved ? "Report saved to Desktop" : "Report failed";
        status_failed = g_report_outcome == report_outcome::failed;
    }
    else if (diagnostics::health().previous_session_crashed.load(std::memory_order_acquire))
    {
        status = "Last session crashed";
        status_failed = true;
    }
    card_in_column("support", "SUPPORT", 4 + (status ? 1 : 0), 1,
                   [&]
                   {
                       if (button_row("Diagnostic Report", "Save", row_action_icon::save))
                           export_diagnostic_report();
                       if (button_row("Logs", "Open", row_action_icon::folder))
                           open_log_folder();
                       if (button_row("First-run Guide", "Show", row_action_icon::none))
                           config::first_run::repeat(platform::windows::runtime_storage::paths().configs);
                       if (button_row("Unload PASTA", "Unload", row_action_icon::none))
                           bound_overlay().request_shutdown();
                       if (status)
                           ImGui::TextColored(status_failed ? ImVec4{1.0f, 0.42f, 0.45f, 1.0f} : k_text_muted, "%s",
                                              render::localization::tr(status));
                   });
}

void draw_overlay_tab()
{
    auto& p = config::general_settings;
    card_in_column("interface", "INTERFACE", 3, 0,
                   [&]
                   {
                       static constexpr const char* languages[]{"English", "Русский"};
                       select_row("Language", p.language, languages);
                       p.language = std::clamp(p.language, 0, config::general_profile::k_language_count - 1);
                       static constexpr std::array dpi_scales{0.50f, 0.75f, 1.00f, 1.25f, 1.50f};
                       static constexpr const char* dpi_labels[]{"50%", "75%", "100%", "125%", "150%"};
                       const auto closest_scale = std::min_element(
                           dpi_scales.begin(), dpi_scales.end(), [&](const float left, const float right)
                           { return std::abs(left - p.menu_scale) < std::abs(right - p.menu_scale); });
                       int dpi_index = static_cast<int>(std::distance(dpi_scales.begin(), closest_scale));
                       select_row("DPI Scale", dpi_index, dpi_labels);
                       p.menu_scale = dpi_scales[std::clamp(dpi_index, 0, static_cast<int>(dpi_scales.size()) - 1)];
                       settings_popup_row("Interface Colors", 3,
                                          [&]
                                          {
                                              settings_popup_row("Typography", 2,
                                                                 [&]
                                                                 {
                                                                     color_row("Primary Text", p.palette.text);
                                                                     color_row("Muted Text", p.palette.muted_text);
                                                                 });
                                              settings_popup_row("Surfaces", 4,
                                                                 [&]
                                                                 {
                                                                     color_row("Menu Background", p.palette.background);
                                                                     color_row("Controls", p.palette.panel);
                                                                     color_row("Containers", p.palette.card);
                                                                     color_row("Popups", p.palette.popup);
                                                                 });
                                              settings_popup_row("Interaction", 3,
                                                                 [&]
                                                                 {
                                                                     color_row("Accent", p.palette.accent);
                                                                     color_row("Hover", p.palette.hover);
                                                                     color_row("Borders", p.palette.border);
                                                                 });
                                          });
                   });
    card_in_column("engine", "ENGINE", 4, 1,
                   [&]
                   {
                       toggle_popup_row("FPS Limiter", p.limit_fps, 1,
                                        [&] { slider_row("Maximum FPS", p.fps_limit, 30, 1000); });
                       toggle_row("GPU Effects", p.use_gpu);
                       static constexpr const char* priorities[]{"Game First", "Balanced", "Overlay First"};
                       select_row("CPU Priority", p.cpu_priority, priorities);
                       select_row("GPU Priority", p.gpu_priority, priorities);
                   });

    card_in_column("overlay", "OVERLAY", 7, 0,
                   [&]
                   {
                       toggle_row("OBS Bypass", p.obs_bypass);
                       toggle_popup_row("Show Watermark", p.m_watermark.enabled, 4,
                                        [&]
                                        {
                                            toggle_row("Show Ping", p.m_watermark.show_ping);
                                            toggle_row("Show Loss", p.m_watermark.show_loss);
                                            toggle_row("Show CPU Load", p.m_watermark.show_cpu);
                                            toggle_row("Show FPS", p.m_watermark.show_fps);
                                        });
                       toggle_row("Show Spectators", p.m_spectator_list.enabled);
                       toggle_popup_row("Show Event Log", p.m_event_log.enabled, 3,
                                        [&]
                                        {
                                            slider_row("Duration", p.m_event_log.duration, 0.5f, 20.0f, " s", 0.5f);
                                            slider_row("Maximum Entries", p.m_event_log.max_entries, 1, 5);
                                            int event_mask = (p.m_event_log.show_shots ? 1 : 0) |
                                                             (p.m_event_log.show_hits ? 2 : 0) |
                                                             (p.m_event_log.show_kills ? 4 : 0) |
                                                             (p.m_event_log.show_misses ? 8 : 0) |
                                                             (p.m_event_log.show_blocked ? 16 : 0) |
                                                             (p.m_event_log.show_info ? 32 : 0);
                                            static constexpr std::pair<const char*, int> event_options[]{
                                                {"Shots", 1},  {"Hits", 2},     {"Kills", 4},
                                                {"Misses", 8}, {"Blocked", 16}, {"Info", 32}};
                                            multiselect_row("Events", event_mask, event_options, 63);
                                            p.m_event_log.show_shots = (event_mask & 1) != 0;
                                            p.m_event_log.show_hits = (event_mask & 2) != 0;
                                            p.m_event_log.show_kills = (event_mask & 4) != 0;
                                            p.m_event_log.show_misses = (event_mask & 8) != 0;
                                            p.m_event_log.show_blocked = (event_mask & 16) != 0;
                                            p.m_event_log.show_info = (event_mask & 32) != 0;
                                        });
                       toggle_popup_row("Show Active Binds", p.m_keybind_list.enabled, 3,
                                        [&]
                                        {
                                            toggle_row("Always On Binds", p.m_keybind_list.show_always);
                                            toggle_row("Hold Binds", p.m_keybind_list.show_hold);
                                            toggle_row("Toggle Binds", p.m_keybind_list.show_toggle);
                                        });
                       toggle_row("Show Bomb Info", config::visual_settings.m_bomb.show_info_panel);
                       auto overlay_opacity = p.overlay_opacity * 100.0f;
                       slider_row("Overlay Opacity", overlay_opacity, 25.0f, 100.0f, "%", 1.0f);
                       p.overlay_opacity = overlay_opacity / 100.0f;
                   });
}

// Configs moved out of the sidebar into the Misc tab: the same library card, drawn beside General and Overlay.
void draw_configs_tab()
{
    draw_config_library();
}

} // namespace

void menu_t::draw_misc()
{
    static constexpr void (*tab_pages[])(){draw_general_tab, draw_overlay_tab, draw_configs_tab};
    static constexpr const char* tabs[]{"General", "Overlay", "Configs"};
    static_assert(std::size(tabs) == std::size(tab_pages));
    ImGui::SetCursorPos({14.0f, 8.0f});
    for (int i = 0; i < static_cast<int>(std::size(tabs)); ++i)
    {
        if (i)
            ImGui::SameLine(0.0f, 8.0f);
        if (tab_button(tabs[i], m_misc_group == i) && m_misc_group != i)
        {
            m_misc_group = i;
            reset_content_animation();
        }
    }

    // Header-level unload: the same action as the General support row, reachable from any Misc subtab. It uses the
    // page-tab primitive so it inherits the source underline/hover language without adding another card.
    const auto* unload_text = render::localization::tr("Unload");
    ImGui::SetCursorPos({772.0f - ImGui::CalcTextSize(unload_text).x - 24.0f, 8.0f});
    if (tab_button("Unload", false))
    {
        bound_overlay().request_shutdown();
        return;
    }

    ImGui::SetCursorPos({14.0f, 42.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##misc_cards", {772.0f, 548.0f}, false);
    begin_cards("##misc_grid");
    tab_pages[std::clamp(m_misc_group, 0, static_cast<int>(std::size(tab_pages)) - 1)]();
    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace ui
