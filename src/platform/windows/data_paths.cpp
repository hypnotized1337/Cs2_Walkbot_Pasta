#include <platform/windows/data_paths.hpp>
#include <windows.h>
#include <shlobj.h>
#include <foundation/scope_exit.hpp>

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

resolved_locations resolve_current_user() noexcept
{
    resolved_locations result;
    try
    {
        // Data is temporary by design and lives in %TEMP%\vesta_uitest. The TEMP parent may legitimately be
        // redirected: normalize that system-provided parent, not arbitrary entries in the data itself.
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
        const auto root = parent / L"vesta_uitest";
        const auto canonical = std::filesystem::weakly_canonical(root, error);
        if (error)
        {
            result.error = static_cast<std::uint32_t>(error.value());
            return result;
        }
        // Do not let a pre-existing junction redirect the test root into production data.
        if (::CompareStringOrdinal(root.c_str(), -1, canonical.c_str(), -1, TRUE) != CSTR_EQUAL)
        {
            result.error = ERROR_ACCESS_DENIED;
            return result;
        }
        result.destination = locations::under(root);
    }
    catch (...)
    {
        result.error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return result;
}
bool is_test_data_path(const std::filesystem::path& path) noexcept
{
    try
    {
        if (!path.is_absolute())
            return false;
        const auto locations = resolve_current_user();
        if (locations.error || locations.destination.root.empty())
            return false;
        std::error_code error;
        const auto root = plain_path(std::filesystem::weakly_canonical(locations.destination.root, error));
        if (error)
            return false;
        const auto candidate = plain_path(std::filesystem::weakly_canonical(path, error));
        if (error)
            return false;
        auto part = candidate.begin();
        for (const auto& expected : root)
        {
            if (part == candidate.end() ||
                ::CompareStringOrdinal(part->c_str(), -1, expected.c_str(), -1, TRUE) != CSTR_EQUAL)
                return false;
            ++part;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace platform::windows::data_storage
