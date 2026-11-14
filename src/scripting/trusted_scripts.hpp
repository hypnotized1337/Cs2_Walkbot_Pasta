#pragma once

#include <array>
#include <string_view>

namespace scripting::trusted
{
// PASTA ships no trusted Lua scripts. Every script, including a script whose bytes match an older Vesta build, runs
// inside the restricted sandbox; add a digest here only for a reviewed, built-in script.
inline constexpr std::array<std::string_view, 0> k_sha256{};
} // namespace scripting::trusted
