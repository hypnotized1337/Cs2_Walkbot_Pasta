#include <stdafx.hpp>
#include <config/library.hpp>
#include <platform/windows/data_paths.hpp>
#include <platform/windows/open_folder.hpp>
#include <render/fonts.hpp>
#include <render/presentation.hpp>
#include <render/text/localization.hpp>
#include <ui/menu/library_widgets.hpp>
#include <ui/menu/menu.hpp>
#include <ui/overlay/overlay.hpp>
#include <ui/presentation.hpp>
#include <imgui.h>

#include <array>
#include <ctime>

namespace ui::menu::detail
{
namespace
{
using config::config_library;
using outcome = config_library::outcome;

constexpr float k_tile_height = 64.0f;
constexpr float k_tile_gap = 8.0f;

// What the Configs page keeps between frames: the name being typed, the tile being renamed and the last result.
struct page_state
{
    char new_name[config::file::k_max_name_bytes + 1]{};
    std::filesystem::path renaming{};
    char rename_buffer[config::file::k_max_name_bytes + 1]{};
    bool focus_rename{};
    feedback result{};
};

page_state g_page;

[[nodiscard]] std::string utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

// A path as the user would type it: without the \\?\ prefix of long-path names.
[[nodiscard]] std::string display_path(const std::filesystem::path& path)
{
    return utf8(platform::windows::data_storage::plain_path(path));
}

[[nodiscard]] std::string format_date(const std::int64_t unix_seconds)
{
    if (unix_seconds <= 0)
        return {};
    const auto time = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
    if (::localtime_s(&local, &time) != 0)
        return {};
    char text[32]{};
    std::strftime(text, sizeof(text), "%d.%m.%Y %H:%M", &local);
    return text;
}

// The English key that tells what happened; failures name their reason.
[[nodiscard]] const char* describe(const outcome result, const char* success)
{
    if (result == outcome::done)
        return success;
    if (result == outcome::rejected && config::library().last_problem() != config::file::problem::none)
        return config::file::describe(config::library().last_problem());
    return config_library::describe(result);
}

void report(std::string key, const outcome result, const char* success)
{
    g_page.result.raise(std::move(key), describe(result, success), result != outcome::done);
}

void save_new()
{
    const auto result = config::library().save_current_as(g_page.new_name);
    report({}, result, "Saved");
    if (result == outcome::done)
        g_page.new_name[0] = '\0';
}

constexpr std::array<const char*, 3> k_header_labels{"Import", "Open folder", "Change folder"};

[[nodiscard]] float header_width()
{
    return small_button_width(k_header_labels[0], glyph::import) +
           small_button_width(k_header_labels[1], glyph::folder) + small_button_width(k_header_labels[2], glyph::none) +
           16.0f;
}

void header_actions()
{
    const auto has_folder = config::library().folder().has_value();
    ImGui::BeginDisabled(!has_folder);
    if (small_button("##import", glyph::import, k_header_labels[0], tone::accent))
        bound_overlay().request_file(ui::overlay_t::file_request::import_config);
    ImGui::SameLine(0.0f, 8.0f);
    if (small_button("##open_folder", glyph::folder, k_header_labels[1], tone::accent) && has_folder)
        static_cast<void>(platform::windows::open_folder(*config::library().folder()));
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 8.0f);
    if (small_button("##change_folder", glyph::none, k_header_labels[2], tone::accent))
        bound_overlay().request_file(ui::overlay_t::file_request::choose_config_folder);
}

// Under the name field: the result of the last save, import or folder change, otherwise the folder path.
void status_line()
{
    const auto& notice = config::library().last_notice();
    const auto notice_fresh = notice.at != std::chrono::steady_clock::time_point{} &&
                              std::chrono::steady_clock::now() - notice.at < std::chrono::milliseconds(2500);
    if (notice_fresh && notice.at > g_page.result.at)
        g_page.result.raise({},
                            notice.result == outcome::done ? notice.success
                            : notice.result == outcome::rejected && notice.problem != config::file::problem::none
                                ? config::file::describe(notice.problem)
                                : config_library::describe(notice.result),
                            notice.result != outcome::done);

    std::string text{};
    ImVec4 color = k_text_muted;
    if (g_page.result.visible_for({}))
    {
        text = render::localization::tr(g_page.result.text);
        color = g_page.result.color();
    }
    else if (const auto& folder = config::library().folder())
        text = display_path(*folder);
    else
        text = render::localization::tr("No folder selected");
    // Ellipsis plus the full path tooltip; the helper also keeps the line from spilling over the panel edge.
    clipped_row_text(text.empty() ? std::string_view{" "} : std::string_view{text}, color);
}

void empty_state(const char* message, const bool offer_folder)
{
    const auto avail = ImGui::GetContentRegionAvail();
    const auto* text = render::localization::tr(message);
    const auto size = ImGui::CalcTextSize(text);
    const auto top = std::max(0.0f, avail.y * 0.38f);
    ImGui::SetCursorPos({(avail.x - size.x) * 0.5f, top});
    ImGui::TextColored(k_text_muted, "%s", text);
    if (!offer_folder)
        return;
    ImGui::SetCursorPos({(avail.x - 160.0f) * 0.5f, top + size.y + 12.0f});
    if (pill_button("##choose_folder", "Choose folder", 160.0f, tone::accent))
        bound_overlay().request_file(ui::overlay_t::file_request::choose_config_folder);
}

// One saved config. Returns true when the list changed and must not be drawn further this frame.
bool config_tile(const config::library_entry& entry, const float width)
{
    auto& library = config::library();
    auto* draw = ImGui::GetWindowDrawList();
    const auto key = utf8(entry.path.filename());
    const auto min = ImGui::GetCursorScreenPos();
    const auto max = ImVec2{min.x + width, min.y + k_tile_height};
    ImGui::Dummy({width, k_tile_height});
    const auto hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max);
    const auto hover_t = animate_state(g_hover_animations, ImGui::GetID("##tile"), hovered, 0.15f);
    const auto active = library.is_active(entry);
    tile_background(draw, min, max, hover_t, active);

    const auto broken = entry.problem != config::file::problem::none;
    const auto renaming = !g_page.renaming.empty() && g_page.renaming == entry.path;
    bool changed = false;

    // Actions from the right edge: the menu of everything else, then the main action.
    constexpr float button = 24.0f;
    float x = max.x - 10.0f - button;
    ImGui::SetCursorScreenPos({x, min.y + (k_tile_height - button) * 0.5f});
    static constexpr std::array<menu_action, 4> k_actions{
        {{"Overwrite with current"}, {"Rename"}, {"Duplicate"}, {"Delete", tone::danger, true}}};
    static constexpr std::array<menu_action, 1> k_broken_actions{{{"Delete", tone::danger, true}}};
    const auto chosen =
        broken ? (action_menu("##menu", k_broken_actions) == 0 ? 3 : -1) : action_menu("##menu", k_actions);
    switch (chosen)
    {
        case 0:
            report(key, library.overwrite(entry), "Overwritten");
            changed = true;
            break;
        case 1:
            g_page.renaming = entry.path;
            std::snprintf(g_page.rename_buffer, sizeof(g_page.rename_buffer), "%s", entry.name.c_str());
            g_page.focus_rename = true;
            break;
        case 2:
            report(key, library.duplicate(entry), "Duplicated");
            changed = true;
            break;
        case 3:
        {
            const auto result = library.remove(entry);
            report(result == outcome::done ? std::string{} : key, result, "Deleted");
            changed = true;
            break;
        }
        default:
            break;
    }
    if (!broken && !changed)
    {
        x -= 6.0f + 74.0f;
        ImGui::SetCursorScreenPos({x, min.y + (k_tile_height - button) * 0.5f});
        if (pill_button("##load", "Load", 74.0f, tone::accent))
        {
            report(key, library.load(entry), "Loaded");
            changed = true;
        }
    }

    // Name and subtitle, or the rename field.
    const auto text_x = min.x + 14.0f;
    const auto text_right = x - 8.0f;
    if (renaming && !changed)
    {
        ImGui::SetCursorScreenPos({text_x - 4.0f, min.y + (k_tile_height - 24.0f) * 0.5f});
        if (g_page.focus_rename)
        {
            ImGui::SetKeyboardFocusHere();
            g_page.focus_rename = false;
        }
        const auto submitted = hint_input("##rename_input", "Name", g_page.rename_buffer, sizeof(g_page.rename_buffer),
                                          std::max(120.0f, text_right - text_x), ImGuiInputTextFlags_AutoSelectAll);
        if (submitted)
        {
            const auto result = library.rename(entry, g_page.rename_buffer);
            report(result == outcome::done ? std::string{} : key, result, "Renamed");
            g_page.renaming.clear();
            changed = true;
        }
        else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape))
            g_page.renaming.clear();
    }
    else
    {
        ImFont* font = ImGui::GetFont();
        const auto size = ImGui::GetFontSize();
        draw->PushClipRect({text_x, min.y}, {text_right, max.y}, true);
        draw->AddText(font, size, {text_x, min.y + 10.0f}, packed(broken ? k_text_muted : k_text_main),
                      entry.name.c_str());

        std::string subtitle{};
        ImVec4 color = k_text_muted;
        if (g_page.result.visible_for(key))
        {
            subtitle = render::localization::tr(g_page.result.text);
            color = g_page.result.color();
        }
        else if (broken)
        {
            subtitle = render::localization::tr(config::file::describe(entry.problem));
            color = {1.0f, 0.45f, 0.48f, 1.0f};
        }
        else
        {
            if (active)
                subtitle = std::string(render::localization::tr("In use")) + "  ·  ";
            subtitle +=
                entry.legacy ? std::string(render::localization::tr("Old format")) : format_date(entry.saved_at);
        }
        draw->AddText({text_x, min.y + 37.0f}, packed(color), subtitle.c_str());
        draw->PopClipRect();
    }

    ImGui::SetCursorScreenPos({min.x, max.y + k_tile_gap});
    return changed;
}

void config_list(const float width)
{
    auto& library = config::library();
    library.refresh();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f);
    ImGui::BeginChild("##config_list", {width, std::max(80.0f, ImGui::GetContentRegionAvail().y)}, false);
    const auto tile_width = ImGui::GetContentRegionAvail().x;

    if (!library.folder())
        empty_state("No folder selected", true);
    else if (library.entries().empty())
        empty_state("No configs yet", false);
    else
    {
        // A copy: an operation on a tile rescans the folder and replaces the entries.
        const auto entries = library.entries();
        for (const auto& entry : entries)
        {
            ImGui::PushID(utf8(entry.path.filename()).c_str());
            const auto changed = config_tile(entry, tile_width);
            ImGui::PopID();
            if (changed)
                break;
        }
        ImGui::Dummy({0.0f, 0.0f});
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
} // namespace

void draw_config_library()
{
    wide_card(
        "config_library", "CONFIGS",
        [&]
        {
            const auto width = ImGui::GetContentRegionAvail().x;
            constexpr float save_width = 90.0f;
            constexpr float row_gap = 8.0f;
            const auto has_folder = config::library().folder().has_value();
            if (hint_input("##new_config", "New config", g_page.new_name, sizeof(g_page.new_name),
                           std::max(120.0f, width - save_width - row_gap)))
                save_new();
            ImGui::SameLine(0.0f, row_gap);
            ImGui::BeginDisabled(!has_folder || g_page.new_name[0] == '\0');
            if (pill_button("##save_new", "Save", save_width, tone::accent, 24.0f))
                save_new();
            ImGui::EndDisabled();
            ImGui::Dummy({0.0f, 7.0f});
            status_line();
            ImGui::Dummy({0.0f, 6.0f});
            config_list(width);
        },
        header_width(), [] { header_actions(); });
}
} // namespace ui::menu::detail

namespace ui
{
void menu_t::draw_configs()
{
    using namespace ui::menu::detail;

    ImGui::SetCursorPos({14.0f, 12.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##configs_cards", {772.0f, 578.0f}, false);
    begin_cards("##configs_grid");
    draw_config_library();
    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace ui

