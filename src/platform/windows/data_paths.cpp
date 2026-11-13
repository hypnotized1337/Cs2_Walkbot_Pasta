#include <platform/windows/data_paths.hpp>
#include <windows.h>
#include <shlobj.h>
#include <foundation/scope_exit.hpp>
#include <array>
#include <string>
#include <utility>

namespace platform::windows::data_storage
{
std::filesystem::path extended_path(const std::filesystem::path& path)
{
    if (path.empty() || !path.is_absolute())
        return path;
    auto normalized = path.lexically_normal();
    normalized.make_preferred();
    const auto& name = normalized.native();
    if (name.starts_with(L"\\\\?\\"))
        return normalized;
    // Only filesystem drive/UNC names are converted, never device namespaces.
    if (name.starts_with(L"\\\\.\\"))
        return normalized;
    if (name.size() >= 3 && name[1] == L':' && name[2] == L'\\')
        return std::filesystem::path(L"\\\\?\\" + name);
    if (name.starts_with(L"\\\\"))
        return std::filesystem::path(L"\\\\?\\UNC\\" + name.substr(2));
    return normalized;
}

std::filesystem::path plain_path(const std::filesystem::path& path)
{
    const auto& name = path.native();
    if (name.starts_with(L"\\\\?\\UNC\\"))
        return std::filesystem::path(L"\\\\" + name.substr(8));
    if (name.starts_with(L"\\\\?\\") && name.size() >= 7 && name[5] == L':' && name[6] == L'\\')
        return std::filesystem::path(name.substr(4));
    return path;
}

locations locations::under(std::filesystem::path root)
{
    locations value;
    value.root = extended_path(root);
    if (value.root.empty())
        return value;
    value.configs = value.root / L"configs";
    value.cache = value.root / L"cache";
    value.logs = value.root / L"logs";
    value.mesh_cache = value.cache / L"chams";
    value.research_logs = value.logs / L"research";
    return value;
}

std::filesystem::path known_documents_directory() noexcept
{
    PWSTR folder = nullptr;
    const HRESULT result = ::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &folder);
    std::filesystem::path path;
    if (SUCCEEDED(result) && folder != nullptr)
        path = folder;
    ::CoTaskMemFree(folder);
    if (!path.empty())
        return path;

    // Match pasta_loader.exe's fallback for systems where the known-folder call is unavailable.
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = ::GetEnvironmentVariableW(L"USERPROFILE", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length != 0 && static_cast<std::size_t>(length) < buffer.size())
        return std::filesystem::path(std::wstring(buffer.data(), length)) / L"Documents";
    return {};
}

resolved_locations resolve_current_user() noexcept
{
    resolved_locations result;
    try
    {
        // The TEMP parent may legitimately be redirected: normalize that system-provided parent, not arbitrary
        // entries in the data itself. Disposable cache and walk-bot routes stay here.
        std::error_code error;
        const auto temporary = std::filesystem::temp_directory_path(error);
        if (error)
        {
            result.error = static_cast<std::uint32_t>(error.value());
            return result;
        }
        const auto parent = std::filesystem::weakly_canonical(temporary, error);
        if (error)
        {
            result.error = static_cast<std::uint32_t>(error.value());
            return result;
        }
        auto legacy = locations::under(parent / L"vesta");

        // User-facing data follows the loader's install folder. Creating these two directories here lets journal
        // startup and storage resolution see the same fallback before the full storage preparation runs.
        const auto documents = known_documents_directory();
        auto configs = documents.empty() ? std::filesystem::path{}
                                          : extended_path(documents / L"PASTA" / L"configs");
        auto logs = documents.empty() ? std::filesystem::path{}
                                      : extended_path(documents / L"PASTA" / L"logs");
        std::error_code configs_error;
        std::error_code logs_error;
        if (!configs.empty())
            std::filesystem::create_directories(configs, configs_error);
        if (!logs.empty())
            std::filesystem::create_directories(logs, logs_error);
        if (!configs.empty() && !logs.empty() && !configs_error && !logs_error)
        {
            auto destination = legacy;
            destination.configs = std::move(configs);
            destination.logs = std::move(logs);
            destination.research_logs = destination.logs / L"research";
            result.destination = std::move(destination);
            result.legacy = legacy.root;
            return result;
        }

        result.used_legacy = true;
        result.preferred_error = configs_error ? static_cast<std::uint32_t>(configs_error.value())
                                  : logs_error   ? static_cast<std::uint32_t>(logs_error.value())
                                                 : ERROR_PATH_NOT_FOUND;
        // The old layout remains usable when Documents is read-only or unavailable. Migration preparation still
        // creates the typed folders below it.
        result.destination = std::move(legacy);
        result.legacy.clear();
    }
    catch (...)
    {
        result.error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return result;
}
} // namespace platform::windows::data_storage
