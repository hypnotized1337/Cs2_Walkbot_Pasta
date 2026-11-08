#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace loader
{
struct failure
{
    std::wstring status;
    std::wstring detail;
};

// progress percentage, short status line, detail line
using progress_report = std::function<void(int, std::wstring, std::wstring)>;

// Checks the public GitHub release feed and installs pasta.exe in Documents\PASTA when the local copy is missing or
// older. A private repository cannot be read anonymously; in that case an existing local copy is still usable.
[[nodiscard]] std::optional<failure> prepare(const std::atomic<bool>& cancelled, const progress_report& report);

// Starts Steam and Counter-Strike 2 when needed, waits for the modules Vesta attaches to, then starts the installed
// pasta.exe from Documents\PASTA (or the legacy copy next to the loader). This is a launcher, not an injector: Vesta
// remains an external process.
[[nodiscard]] std::optional<failure> launch(const std::atomic<bool>& cancelled, const progress_report& report);
} // namespace loader
