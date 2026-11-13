#include <diagnostics/log_directory.hpp>
#include <platform/windows/data_paths.hpp>
#include <platform/windows/storage.hpp>

namespace diagnostics
{
std::filesystem::path default_logs_directory()
{
    // Storage is initialized after the journal opens, so use its live choice once available. Before that, resolution
    // creates Documents\PASTA\logs or falls back to %TEMP%\vesta\logs if Documents is not writable.
    if (const auto& logs = platform::windows::runtime_storage::paths().logs; !logs.empty())
        return logs;
    return platform::windows::data_storage::resolve_current_user().destination.logs;
}
} // namespace diagnostics
