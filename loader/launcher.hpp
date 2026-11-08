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

// Starts Steam and Counter-Strike 2 when needed, waits for the modules Vesta attaches to, then starts vesta.exe from
// the loader's folder. This is a launcher, not an injector: Vesta remains an external process.
[[nodiscard]] std::optional<failure> launch(const std::atomic<bool>& cancelled, const progress_report& report);
} // namespace loader
