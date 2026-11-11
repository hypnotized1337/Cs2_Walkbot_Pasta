#include <diagnostics/log_directory.hpp>
#include <platform/windows/data_paths.hpp>

namespace diagnostics
{
std::filesystem::path default_logs_directory()
{
    const auto locations = platform::windows::data_storage::resolve_current_user();
    if (locations.error)
        return {};
    return locations.destination.logs;
}
} // namespace diagnostics
