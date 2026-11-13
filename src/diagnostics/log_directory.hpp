#pragma once
#include <filesystem>

namespace diagnostics
{
// Documents\PASTA\logs, or %TEMP%\vesta\logs when Documents is unavailable. Creates the folder through the shared
// storage resolver so the journal and the application logging agree. Empty only if neither location exists.
[[nodiscard]] std::filesystem::path default_logs_directory();
} // namespace diagnostics
