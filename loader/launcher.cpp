#include "launcher.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace loader
{
namespace
{
using clock = std::chrono::steady_clock;

constexpr auto k_process_timeout = std::chrono::seconds(180);
constexpr auto k_module_timeout = std::chrono::seconds(30);
constexpr auto k_poll_interval = std::chrono::milliseconds(250);
constexpr int k_settle_ticks = 30;

class unique_handle
{
public:
    unique_handle() = default;
    explicit unique_handle(const HANDLE value) noexcept
        : m_value(value)
    {
    }

    ~unique_handle() { reset(); }

    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;

    unique_handle(unique_handle&& other) noexcept
        : m_value(other.m_value)
    {
        other.m_value = nullptr;
    }

    unique_handle& operator=(unique_handle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_value = other.m_value;
            other.m_value = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return m_value; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_value != nullptr && m_value != INVALID_HANDLE_VALUE;
    }

    void reset(const HANDLE value = nullptr) noexcept
    {
        if (m_value != nullptr && m_value != INVALID_HANDLE_VALUE)
            ::CloseHandle(m_value);
        m_value = value;
    }

private:
    HANDLE m_value{};
};

[[nodiscard]] std::wstring win32_error(const DWORD code)
{
    wchar_t* text = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
        reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    std::wstring message;
    if (length != 0 && text != nullptr)
        message.assign(text, length);
    if (text != nullptr)
        ::LocalFree(text);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        message.pop_back();
    if (message.empty())
        message = L"Windows error " + std::to_wstring(code);
    return message;
}

[[nodiscard]] std::wstring read_registry_string(const HKEY root, const wchar_t* key, const wchar_t* name)
{
    DWORD bytes{};
    if (::RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes <= sizeof(wchar_t))
        return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (::RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS)
        return {};
    const auto terminator = value.find(L'\0');
    if (terminator != std::wstring::npos)
        value.resize(terminator);
    return value;
}

[[nodiscard]] std::wstring strip_quotes(std::wstring value)
{
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
    {
        value.erase(value.begin());
        value.pop_back();
    }
    return value;
}

[[nodiscard]] std::wstring expand_environment(const std::wstring& value)
{
    if (value.empty())
        return {};
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        ::ExpandEnvironmentStringsW(value.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || static_cast<std::size_t>(length) > buffer.size())
        return value;
    return std::wstring(buffer.data(), length - 1);
}

[[nodiscard]] std::optional<std::filesystem::path> find_steam()
{
    std::vector<std::wstring> candidates;
    candidates.push_back(strip_quotes(read_registry_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe")));
    const auto steam_path =
        strip_quotes(read_registry_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"));
    if (!steam_path.empty())
        candidates.push_back(steam_path + L"\\steam.exe");
    const auto install = strip_quotes(
        read_registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"));
    if (!install.empty())
        candidates.push_back(install + L"\\steam.exe");

    for (const auto* variable : {L"ProgramFiles(x86)", L"ProgramFiles"})
    {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = ::GetEnvironmentVariableW(variable, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length != 0 && static_cast<std::size_t>(length) < buffer.size())
            candidates.push_back(std::wstring(buffer.data(), length) + L"\\Steam\\steam.exe");
    }

    for (auto candidate : candidates)
    {
        candidate = expand_environment(candidate);
        if (candidate.empty())
            continue;
        std::filesystem::path path(candidate);
        if (path.filename() != L"steam.exe")
            path /= L"steam.exe";
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
            return path;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<DWORD> find_process(const std::wstring& executable)
{
    unique_handle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot)
        return std::nullopt;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!::Process32FirstW(snapshot.get(), &entry))
        return std::nullopt;
    do
    {
        if (::_wcsicmp(entry.szExeFile, executable.c_str()) == 0)
            return entry.th32ProcessID;
    } while (::Process32NextW(snapshot.get(), &entry));
    return std::nullopt;
}

struct window_search
{
    DWORD process_id{};
    bool found{};
};

BOOL CALLBACK find_game_window(const HWND window, const LPARAM parameter)
{
    auto& search = *reinterpret_cast<window_search*>(parameter);
    DWORD process_id{};
    ::GetWindowThreadProcessId(window, &process_id);
    if (process_id != search.process_id || !::IsWindowVisible(window))
        return TRUE;

    constexpr int title_capacity = 256;
    wchar_t title[title_capacity]{};
    if (::GetWindowTextW(window, title, title_capacity) <= 0)
        return TRUE;
    if (std::wcsstr(title, L"Counter-Strike 2") == nullptr)
        return TRUE;

    RECT bounds{};
    if (!::GetWindowRect(window, &bounds))
        return TRUE;
    if (bounds.right - bounds.left < 200 || bounds.bottom - bounds.top < 200)
        return TRUE;

    search.found = true;
    return FALSE;
}

// The game window is visible before the module list is safe to read and does not need a handle to the target.
[[nodiscard]] bool game_window_ready(const DWORD process_id)
{
    window_search search{process_id, false};
    ::EnumWindows(find_game_window, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

// Vesta opens the game with exactly these rights. Checking them here turns an elevation mismatch into a clear
// message instead of a loader that waits forever and a Vesta that cannot attach.
[[nodiscard]] bool can_read_process(const DWORD process_id, DWORD& error)
{
    unique_handle process(
        ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, process_id));
    if (!process)
    {
        error = ::GetLastError();
        return false;
    }
    return true;
}

enum class module_state
{
    ready,
    pending,
    inaccessible,
    failed,
};

struct module_scan
{
    module_state state{module_state::pending};
    DWORD error{};
};

// Vesta fails its attach when any of these are missing; checking them avoids starting it during the loading screen.
[[nodiscard]] module_scan scan_modules(const DWORD process_id)
{
    unique_handle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id));
    if (!snapshot)
    {
        const DWORD error = ::GetLastError();
        return {error == ERROR_ACCESS_DENIED ? module_state::inaccessible : module_state::failed, error};
    }

    constexpr std::array<const wchar_t*, 6> required{
        L"client.dll", L"engine2.dll", L"inputsystem.dll", L"tier0.dll", L"schemasystem.dll", L"vphysics2.dll"};
    std::array<bool, required.size()> found{};

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (!::Module32FirstW(snapshot.get(), &module))
    {
        const DWORD error = ::GetLastError();
        return {error == ERROR_ACCESS_DENIED ? module_state::inaccessible : module_state::failed, error};
    }
    do
    {
        for (std::size_t i = 0; i < required.size(); ++i)
        {
            if (!found[i] && ::_wcsicmp(module.szModule, required[i]) == 0)
                found[i] = true;
        }
    } while (::Module32NextW(snapshot.get(), &module));

    if (std::all_of(found.begin(), found.end(), [](const bool value) { return value; }))
        return {module_state::ready, 0};
    return {module_state::pending, 0};
}

[[nodiscard]] bool start_process(const std::filesystem::path& executable, const std::wstring& arguments,
                                 const std::filesystem::path& directory, std::wstring& error)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + executable.wstring() + L"\"";
    if (!arguments.empty())
        command += L" " + arguments;

    const wchar_t* working_directory = directory.empty() ? nullptr : directory.c_str();
    if (!::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, working_directory,
                          &startup, &process))
    {
        error = win32_error(::GetLastError());
        return false;
    }
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return true;
}

[[nodiscard]] std::filesystem::path executable_directory()
{
    std::wstring path(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= path.size())
    {
        std::error_code error;
        const auto current = std::filesystem::current_path(error);
        return error ? std::filesystem::path{} : current;
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

enum class wait_result
{
    ready,
    timeout,
    cancelled,
};

template<typename predicate_t>
[[nodiscard]] wait_result wait_until(const std::chrono::seconds timeout, predicate_t&& predicate, const int from,
                                     const int to, const std::atomic<bool>& cancelled,
                                     const progress_report& report, const std::wstring& label,
                                     const std::wstring& detail)
{
    const auto started = clock::now();
    auto reported_at = started;

    for (;;)
    {
        if (cancelled.load(std::memory_order_relaxed))
            return wait_result::cancelled;
        if (predicate())
            return wait_result::ready;

        const auto now = clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - started);
        if (elapsed >= timeout)
            return wait_result::timeout;

        if (now - reported_at >= std::chrono::milliseconds(500))
        {
            const auto ratio =
                std::min(1.0, static_cast<double>(elapsed.count()) / static_cast<double>(timeout.count()));
            report(from + static_cast<int>(static_cast<double>(to - from) * ratio),
                   label + L" (" + std::to_wstring(elapsed.count()) + L"s)", detail);
            reported_at = now;
        }
        std::this_thread::sleep_for(k_poll_interval);
    }
}

[[nodiscard]] failure cancelled_failure()
{
    return {L"Launch cancelled.", L"PASTA was not started."};
}

[[nodiscard]] failure access_failure(const DWORD error)
{
    if (error == ERROR_ACCESS_DENIED)
        return {L"Cannot read Counter-Strike 2.",
                L"CS2 is running with higher privileges. Start Steam normally, or run pasta_loader.exe as "
                L"administrator."};
    return {L"Could not open Counter-Strike 2.", win32_error(error)};
}
} // namespace

std::optional<failure> launch(const std::atomic<bool>& cancelled, const progress_report& report)
{
    report(5, L"Checking Counter-Strike 2", L"Looking for a running game.");
    if (cancelled.load(std::memory_order_relaxed))
        return cancelled_failure();

    auto process_id = find_process(L"cs2.exe");
    if (!process_id)
    {
        report(12, L"Finding Steam", L"Reading the Steam installation path.");
        const auto steam = find_steam();
        if (!steam)
            return failure{L"Steam was not found.",
                           L"Install Steam or start CS2 once so its install path is registered."};

        report(20, L"Starting Counter-Strike 2", L"Steam is launching the game.");
        std::wstring error;
        if (!start_process(*steam, L"-applaunch 730", steam->parent_path(), error))
            return failure{L"Could not start Counter-Strike 2.", std::move(error)};

        report(25, L"Waiting for Counter-Strike 2", L"Steam is starting the game.");
        const auto result = wait_until(
            k_process_timeout, [&] { process_id = find_process(L"cs2.exe"); return process_id.has_value(); }, 25, 45,
            cancelled, report, L"Waiting for Counter-Strike 2", L"Steam is starting the game.");
        if (result == wait_result::cancelled)
            return cancelled_failure();
        if (result == wait_result::timeout || !process_id)
            return failure{L"Counter-Strike 2 did not start.",
                           L"cs2.exe did not appear within three minutes."};
    }
    else
    {
        report(30, L"Counter-Strike 2 is already running", L"Waiting for the game window.");
    }

    report(40, L"Waiting for Counter-Strike 2", L"Waiting for the game window.");
    const auto window = wait_until(k_process_timeout, [&] { return process_id && game_window_ready(*process_id); },
                                   40, 58, cancelled, report, L"Waiting for Counter-Strike 2",
                                   L"Waiting for the game window.");
    if (window == wait_result::cancelled)
        return cancelled_failure();
    if (window == wait_result::timeout || !process_id)
        return failure{L"Counter-Strike 2 did not open.",
                       L"The game window did not appear within three minutes."};

    DWORD access_error{};
    if (!can_read_process(*process_id, access_error))
        return access_failure(access_error);

    report(64, L"Checking game modules", L"Verifying client.dll and the other modules Vesta needs.");
    auto scan = scan_modules(*process_id);
    if (scan.state == module_state::pending)
    {
        const auto result = wait_until(
            k_module_timeout, [&] { scan = scan_modules(*process_id); return scan.state != module_state::pending; },
            64, 78, cancelled, report, L"Waiting for Counter-Strike 2 to load",
            L"Checking client.dll and the other game modules.");
        if (result == wait_result::cancelled)
            return cancelled_failure();
    }
    if (scan.state != module_state::ready)
    {
        // The window and process access are already good. Vesta performs its own module discovery, so a failed or slow
        // module scan must not strand the loader; start it and let Vesta report anything the game is missing.
        std::wstring detail = L"Game modules could not be verified; Vesta will attach when ready.";
        if (scan.error != 0)
            detail += L" (" + win32_error(scan.error) + L")";
        report(80, L"Starting PASTA", std::move(detail));
    }

    report(84, L"Letting the game settle", L"Waiting for the main menu.");
    for (int tick = 0; tick < k_settle_ticks; ++tick)
    {
        if (cancelled.load(std::memory_order_relaxed))
            return cancelled_failure();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!find_process(L"cs2.exe"))
        return failure{L"Counter-Strike 2 closed.", L"The game exited before PASTA could start."};

    if (find_process(L"vesta.exe"))
    {
        report(100, L"PASTA is already running.", L"Press Insert in game to open the menu.");
        return std::nullopt;
    }

    const auto directory = executable_directory();
    const auto vesta = directory / L"vesta.exe";
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(vesta, file_error))
        return failure{L"vesta.exe was not found.",
                       L"Keep pasta_loader.exe in the same folder as vesta.exe."};

    report(94, L"Starting PASTA", L"Launching vesta.exe.");
    std::wstring error;
    if (!start_process(vesta, L"", directory, error))
        return failure{L"Could not start PASTA.", std::move(error)};

    report(100, L"PASTA is running.", L"Press Insert in game to open the menu.");
    return std::nullopt;
}
} // namespace loader
