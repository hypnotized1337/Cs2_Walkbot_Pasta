#include "launcher.hpp"

#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
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

constexpr wchar_t k_release_api[] =
    L"https://api.github.com/repos/hypnotized1337/Pasta-Cs2-Assistant/releases/latest";

[[nodiscard]] std::wstring widen_utf8(const std::string& text)
{
    if (text.empty())
        return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

[[nodiscard]] std::wstring human_size(const std::uint64_t bytes)
{
    wchar_t buffer[64]{};
    if (bytes >= 1024ull * 1024ull)
        ::swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else
        ::swprintf_s(buffer, L"%.0f KB", static_cast<double>(bytes) / 1024.0);
    return buffer;
}

[[nodiscard]] std::filesystem::path documents_directory()
{
    PWSTR folder = nullptr;
    const HRESULT result = ::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &folder);
    std::filesystem::path path;
    if (SUCCEEDED(result) && folder != nullptr)
        path = folder;
    ::CoTaskMemFree(folder);
    if (!path.empty())
        return path;

    std::array<wchar_t, 32768> buffer{};
    const DWORD length = ::GetEnvironmentVariableW(L"USERPROFILE", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length != 0 && static_cast<std::size_t>(length) < buffer.size())
        return std::filesystem::path(std::wstring(buffer.data(), length)) / L"Documents";
    return {};
}

[[nodiscard]] std::filesystem::path pasta_install_directory()
{
    const auto documents = documents_directory();
    if (documents.empty())
        return {};
    return documents / L"PASTA";
}

[[nodiscard]] std::filesystem::path installed_pasta_path()
{
    std::error_code error;
    const auto install = pasta_install_directory();
    if (!install.empty())
    {
        const auto installed = install / L"pasta.exe";
        if (std::filesystem::is_regular_file(installed, error))
            return installed;
    }

    error.clear();
    const auto legacy = executable_directory() / L"pasta.exe";
    if (std::filesystem::is_regular_file(legacy, error))
        return legacy;
    return {};
}

[[nodiscard]] std::string read_version_tag(const std::filesystem::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        return {};
    std::string tag;
    std::getline(stream, tag);
    while (!tag.empty() && (tag.back() == '\r' || tag.back() == '\n' || tag.back() == ' ' || tag.back() == '\t'))
        tag.pop_back();
    if (tag.size() > 128)
        tag.resize(128);
    return tag;
}

[[nodiscard]] bool write_version_tag(const std::filesystem::path& file, const std::string& tag)
{
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream << tag << '\n';
    return stream.good();
}

struct release_version
{
    int major{};
    int minor{};
    int patch{};
    int build = -1;
};

[[nodiscard]] bool parse_release_version(std::string_view tag, release_version& out)
{
    if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V'))
        tag.remove_prefix(1);
    const std::string text(tag);
    if (std::sscanf(text.c_str(), "%d.%d.%d", &out.major, &out.minor, &out.patch) != 3)
        return false;

    const auto build = tag.find("-build.");
    if (build != std::string_view::npos)
    {
        const auto number = tag.substr(build + 7);
        int value = 0;
        const auto result = std::from_chars(number.data(), number.data() + number.size(), value);
        if (result.ec != std::errc{} || result.ptr == number.data())
            return false;
        out.build = value;
    }
    return true;
}

[[nodiscard]] int compare_versions(const release_version& left, const release_version& right)
{
    if (left.major != right.major)
        return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor)
        return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch)
        return left.patch < right.patch ? -1 : 1;
    if (left.build != right.build)
        return left.build < right.build ? -1 : 1;
    return 0;
}

struct release_info
{
    std::string tag;
    std::wstring download_url;
    std::uint64_t size{};
};

class winhttp_handle
{
public:
    winhttp_handle() = default;
    explicit winhttp_handle(const HINTERNET value) noexcept
        : m_value(value)
    {
    }

    ~winhttp_handle() { reset(); }

    winhttp_handle(const winhttp_handle&) = delete;
    winhttp_handle& operator=(const winhttp_handle&) = delete;

    winhttp_handle(winhttp_handle&& other) noexcept
        : m_value(other.m_value)
    {
        other.m_value = nullptr;
    }

    winhttp_handle& operator=(winhttp_handle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_value = other.m_value;
            other.m_value = nullptr;
        }
        return *this;
    }

    [[nodiscard]] HINTERNET get() const noexcept { return m_value; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_value != nullptr; }

    void reset(const HINTERNET value = nullptr) noexcept
    {
        if (m_value != nullptr)
            ::WinHttpCloseHandle(m_value);
        m_value = value;
    }

private:
    HINTERNET m_value{};
};

[[nodiscard]] bool open_http_request(const std::wstring& url, const std::wstring& accept, winhttp_handle& session,
                                     winhttp_handle& connection, winhttp_handle& request, std::wstring& error)
{
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!::WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) ||
        components.dwHostNameLength == 0)
    {
        error = L"The update URL is invalid.";
        return false;
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring target(components.lpszUrlPath, components.dwUrlPathLength);
    if (target.empty())
        target = L"/";
    if (components.dwExtraInfoLength > 0)
        target.append(components.lpszExtraInfo, components.dwExtraInfoLength);

    session.reset(::WinHttpOpen(L"PASTA-Loader/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
    {
        error = L"Could not open WinHTTP: " + win32_error(::GetLastError());
        return false;
    }
    ::WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 15000);

    connection.reset(::WinHttpConnect(session.get(), host.c_str(), components.nPort, 0));
    if (!connection)
    {
        error = L"Could not connect to " + host + L": " + win32_error(::GetLastError());
        return false;
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    request.reset(::WinHttpOpenRequest(connection.get(), L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request)
    {
        error = L"Could not create the update request: " + win32_error(::GetLastError());
        return false;
    }

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    ::WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

    const std::wstring headers = L"Accept: " + accept + L"\r\n";
    if (!::WinHttpAddRequestHeaders(request.get(), headers.c_str(), static_cast<DWORD>(-1),
                                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !::WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !::WinHttpReceiveResponse(request.get(), nullptr))
    {
        error = L"The update request failed: " + win32_error(::GetLastError());
        return false;
    }

    DWORD status = 0;
    DWORD length = sizeof(status);
    if (!::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX))
    {
        error = L"Could not read the update response: " + win32_error(::GetLastError());
        return false;
    }
    if (status != 200)
    {
        error = L"GitHub returned HTTP " + std::to_wstring(status) + L".";
        return false;
    }
    return true;
}

[[nodiscard]] bool read_http_body(winhttp_handle& request, std::string& body, std::wstring& error)
{
    for (;;)
    {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request.get(), &available))
        {
            error = L"Could not read the update response: " + win32_error(::GetLastError());
            return false;
        }
        if (available == 0)
            return true;

        const DWORD chunk_size = (std::min)(available, 65536UL);
        std::vector<char> chunk(chunk_size);
        DWORD read = 0;
        if (!::WinHttpReadData(request.get(), chunk.data(), chunk_size, &read))
        {
            error = L"Could not read the update response: " + win32_error(::GetLastError());
            return false;
        }
        if (read == 0)
            return true;
        body.append(chunk.data(), read);
        if (body.size() > 2 * 1024 * 1024)
        {
            error = L"The update response was too large.";
            return false;
        }
    }
}

[[nodiscard]] bool http_get(const std::wstring& url, const std::wstring& accept, std::string& body,
                            std::wstring& error)
{
    winhttp_handle session;
    winhttp_handle connection;
    winhttp_handle request;
    if (!open_http_request(url, accept, session, connection, request, error))
        return false;
    return read_http_body(request, body, error);
}

[[nodiscard]] bool fetch_latest_release(release_info& info, std::wstring& error,
                                        const std::atomic<bool>& cancelled)
{
    if (cancelled.load(std::memory_order_relaxed))
    {
        error = L"Update cancelled.";
        return false;
    }

    std::string body;
    if (!http_get(k_release_api, L"application/vnd.github+json", body, error))
        return false;

    try
    {
        const auto document = nlohmann::json::parse(body);
        if (!document.is_object())
        {
            error = L"GitHub returned an invalid release document.";
            return false;
        }
        if (document.contains("message") && document["message"].is_string())
        {
            error = widen_utf8(document["message"].get<std::string>());
            return false;
        }
        if (!document.contains("tag_name") || !document["tag_name"].is_string())
        {
            error = L"The latest release has no version tag.";
            return false;
        }
        info.tag = document["tag_name"].get<std::string>();

        const auto assets = document.find("assets");
        if (assets == document.end() || !assets->is_array())
        {
            error = L"The latest release has no files.";
            return false;
        }
        for (const auto& asset : *assets)
        {
            if (!asset.is_object() || asset.value("name", std::string{}) != "pasta.exe")
                continue;
            if (!asset.contains("browser_download_url") || !asset["browser_download_url"].is_string())
                break;
            info.download_url = widen_utf8(asset["browser_download_url"].get<std::string>());
            info.size = asset.value("size", std::uint64_t{0});
            if (info.download_url.empty())
                break;
            return true;
        }
        error = L"The latest PASTA release has no pasta.exe download.";
        return false;
    }
    catch (const std::exception& exception)
    {
        error = L"Could not read the latest release: " + widen_utf8(exception.what());
        return false;
    }
}

[[nodiscard]] bool download_pasta(const std::wstring& url, const std::filesystem::path& temporary,
                                  const std::filesystem::path& destination, const std::string& tag,
                                  const std::uint64_t expected_size, const std::atomic<bool>& cancelled,
                                  const progress_report& report, std::wstring& error)
{
    winhttp_handle session;
    winhttp_handle connection;
    winhttp_handle request;
    if (!open_http_request(url, L"application/octet-stream", session, connection, request, error))
        return false;

    unique_handle file(::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file)
    {
        error = L"Could not create the download file: " + win32_error(::GetLastError());
        return false;
    }

    bool success = true;
    std::uint64_t received = 0;
    int last_percent = -1;
    auto last_report = clock::now();
    const auto display_tag = widen_utf8(tag);

    for (;;)
    {
        if (cancelled.load(std::memory_order_relaxed))
        {
            error = L"Update cancelled.";
            success = false;
            break;
        }

        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request.get(), &available))
        {
            error = L"The download stopped: " + win32_error(::GetLastError());
            success = false;
            break;
        }
        if (available == 0)
            break;

        const DWORD chunk_size = (std::min)(available, 65536UL);
        std::vector<char> chunk(chunk_size);
        DWORD read = 0;
        if (!::WinHttpReadData(request.get(), chunk.data(), chunk_size, &read))
        {
            error = L"The download stopped: " + win32_error(::GetLastError());
            success = false;
            break;
        }
        if (read == 0)
            break;

        DWORD written = 0;
        if (!::WriteFile(file.get(), chunk.data(), read, &written, nullptr) || written != read)
        {
            error = L"Could not write pasta.exe: " + win32_error(::GetLastError());
            success = false;
            break;
        }
        received += read;

        const int percent = expected_size == 0
                                ? 0
                                : static_cast<int>((std::min)(received, expected_size) * 100 / expected_size);
        const auto now = clock::now();
        if (percent != last_percent && (percent >= 100 || now - last_report >= std::chrono::milliseconds(100)))
        {
            std::wstring detail = L"Build " + display_tag;
            if (expected_size > 0)
                detail += L" - " + human_size(received) + L" / " + human_size(expected_size);
            report(6 + (percent * 93) / 100, L"Downloading PASTA", std::move(detail));
            last_percent = percent;
            last_report = now;
        }
    }

    if (success && expected_size > 0 && received != expected_size)
    {
        error = L"The download was incomplete (" + std::to_wstring(received) + L" of " +
                std::to_wstring(expected_size) + L" bytes).";
        success = false;
    }
    if (success && !::FlushFileBuffers(file.get()))
    {
        error = L"Could not finish writing pasta.exe: " + win32_error(::GetLastError());
        success = false;
    }
    file.reset();

    if (success)
    {
        if (!::MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            error = L"Could not replace pasta.exe: " + win32_error(::GetLastError());
            success = false;
        }
    }
    if (!success)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return success;
}

[[nodiscard]] bool needs_install(const release_info& latest, const std::string& local_tag, const bool local_exists)
{
    if (!local_exists)
        return true;

    release_version latest_version;
    if (!parse_release_version(latest.tag, latest_version))
        return local_tag != latest.tag;

    release_version local_version;
    if (local_tag.empty() || !parse_release_version(local_tag, local_version))
        return true;
    return compare_versions(latest_version, local_version) > 0;
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

    if (find_process(L"pasta.exe"))
    {
        report(100, L"PASTA is already running.", L"Press Insert in game to open the menu.");
        return std::nullopt;
    }

    const auto pasta = installed_pasta_path();
    std::error_code file_error;
    if (pasta.empty() || !std::filesystem::is_regular_file(pasta, file_error))
        return failure{L"pasta.exe was not found.",
                       L"Automatic setup did not finish. Restart pasta_loader.exe to retry."};

    report(94, L"Starting PASTA", L"Launching pasta.exe.");
    std::wstring error;
    if (!start_process(pasta, L"", pasta.parent_path(), error))
        return failure{L"Could not start PASTA.", std::move(error)};

    report(100, L"PASTA is running.", L"Press Insert in game to open the menu.");
    return std::nullopt;
}

std::optional<failure> prepare(const std::atomic<bool>& cancelled, const progress_report& report)
{
    report(2, L"Checking for updates", L"Contacting the PASTA release feed.");
    if (cancelled.load(std::memory_order_relaxed))
        return cancelled_failure();
    if (find_process(L"pasta.exe"))
    {
        report(100, L"PASTA is already running.", L"Close it before checking for updates.");
        return std::nullopt;
    }

    const auto install_directory = pasta_install_directory();
    if (install_directory.empty())
        return failure{L"Could not find the Documents folder.",
                       L"Automatic setup needs Documents to install pasta.exe."};

    std::error_code directory_error;
    std::filesystem::create_directories(install_directory, directory_error);
    if (directory_error)
        return failure{L"Could not create the PASTA folder.", widen_utf8(directory_error.message())};

    const auto target = install_directory / L"pasta.exe";
    const auto version_file = install_directory / L"version.txt";
    const auto local = installed_pasta_path();
    const bool local_exists = !local.empty();
    std::error_code target_error;
    const bool target_exists = std::filesystem::is_regular_file(target, target_error);
    // A version marker belongs to the copied executable, not to a legacy pasta.exe that may still sit next to the
    // loader. Ignore it when the Documents copy is missing so a stale fallback cannot look current.
    const auto local_tag = target_exists ? read_version_tag(version_file) : std::string{};
    const std::wstring public_note =
        L" The GitHub release repository must be public for automatic downloads.";

    release_info latest;
    std::wstring update_error;
    if (!fetch_latest_release(latest, update_error, cancelled))
    {
        if (cancelled.load(std::memory_order_relaxed))
            return cancelled_failure();
        if (local_exists)
        {
            report(100, L"Update check failed", L"Using installed PASTA. " + update_error);
            return std::nullopt;
        }
        return failure{L"Could not check for PASTA updates.", update_error + public_note};
    }

    if (cancelled.load(std::memory_order_relaxed))
        return cancelled_failure();
    if (!needs_install(latest, local_tag, local_exists))
    {
        report(100, L"PASTA is up to date", L"Build " + widen_utf8(latest.tag) + L".");
        return std::nullopt;
    }

    const auto display_tag = widen_utf8(latest.tag);
    report(6, L"Update available", L"Downloading " + display_tag + L".");
    const auto temporary = install_directory / L"pasta.exe.download";
    if (!download_pasta(latest.download_url, temporary, target, latest.tag, latest.size, cancelled, report,
                        update_error))
    {
        if (cancelled.load(std::memory_order_relaxed))
            return cancelled_failure();
        if (local_exists)
        {
            report(100, L"Update failed", L"Using installed PASTA. " + update_error);
            return std::nullopt;
        }
        return failure{L"Could not download PASTA.", update_error + public_note};
    }

    if (!write_version_tag(version_file, latest.tag))
    {
        report(100, L"PASTA updated", L"Build " + display_tag + L" is ready.");
        return std::nullopt;
    }

    report(100, L"PASTA updated", L"Build " + display_tag + L" is ready.");
    return std::nullopt;
}
} // namespace loader
