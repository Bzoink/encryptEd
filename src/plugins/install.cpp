#include "plugins/plugins.h"

#include <system_error>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace plugins {
namespace fs = std::filesystem;

fs::path executable_path()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size && size < buffer.size()) return fs::path(std::wstring(buffer.data(), size));
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        return fs::canonical(fs::u8path(buffer.data()), ec);
    }
#else
    std::vector<char> buffer(65536);
    auto size = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (size > 0 && static_cast<std::size_t>(size) < buffer.size())
        return fs::u8path(std::string(buffer.data(), static_cast<std::size_t>(size)));
#endif
    return {};
}

fs::path default_directory(const fs::path &executable, const fs::path &installed)
{
    std::vector<fs::path> candidates;
    if (!executable.empty()) {
        auto directory = executable.parent_path();
#ifdef __APPLE__
        candidates.push_back(directory.parent_path() / "Resources/default-plugins");
#endif
        candidates.push_back(directory / "default-plugins");
        candidates.push_back(directory.parent_path() / "share/encryptEd/default-plugins");
    }
    // Accept resources from older packages when only the executable is upgraded.
    if (!executable.empty()) {
        auto directory = executable.parent_path();
#ifdef __APPLE__
        candidates.push_back(directory.parent_path() / "Resources/plugin-examples");
#endif
        candidates.push_back(directory / "plugin-examples");
        candidates.push_back(directory.parent_path() / "share/encryptEd/plugin-examples");
    }
    if (!installed.empty()) candidates.push_back(installed);
#ifdef ENCRYPTED_DEFAULT_PLUGIN_DIR
    else candidates.emplace_back(ENCRYPTED_DEFAULT_PLUGIN_DIR);
#endif
    for (const auto &candidate : candidates) {
        std::error_code ec;
        auto status = fs::symlink_status(candidate, ec);
        if (!ec && fs::is_directory(status) && !fs::is_symlink(status)) return candidate;
    }
    return {};
}

// Inspect each component before creating or copying anything through it.
static bool safe_directory(const fs::path &path, bool create, std::string &error)
{
    std::error_code ec;
    auto absolute = fs::absolute(path, ec);
    if (ec) { error = "Cannot resolve directory: " + path.u8string(); return false; }
    fs::path current = absolute.root_path();
    for (const auto &part : absolute.relative_path()) {
        if (part == ".") continue;
        if (part == "..") { error = "Directory contains traversal: " + path.u8string(); return false; }
        current /= part;
        auto status = fs::symlink_status(current, ec);
        if (status.type() == fs::file_type::not_found && create) {
            ec.clear();
            fs::create_directory(current, ec);
            if (!ec) status = fs::symlink_status(current, ec);
        }
        if (ec || !fs::is_directory(status) || fs::is_symlink(status)) {
            error = "Directory unavailable, conflicting, or a symlink: " + current.u8string();
            return false;
        }
    }
    return true;
}

InstallResult install_defaults(const fs::path &source, const fs::path &destination)
{
    InstallResult result;
    if (source.empty()) {
        result.errors.push_back("Default plugins were not found. Reinstall the application with its plugin examples.");
        return result;
    }
    if (destination.empty()) {
        result.errors.push_back("Could not determine the user plugin directory.");
        return result;
    }
    std::string error;
    if (!safe_directory(source, false, error)) { result.errors.push_back(error); return result; }
    auto catalog = discover(source);
    result.errors = catalog.errors;
    if (catalog.commands.empty()) {
        result.errors.push_back("No default Lua plugins were found in: " + source.u8string());
        return result;
    }
    if (!safe_directory(destination, true, error)) { result.errors.push_back(error); return result; }
    for (const auto &command : catalog.commands) {
        auto target = destination / command.path.lexically_relative(source);
        if (!safe_directory(command.path.parent_path(), false, error) ||
            !safe_directory(target.parent_path(), true, error)) {
            result.errors.push_back(error); continue;
        }
        std::error_code ec;
        auto status = fs::symlink_status(target, ec);
        if (status.type() != fs::file_type::not_found) {
            if (!ec && fs::is_regular_file(status) && !fs::is_symlink(status))
                result.skipped.push_back(target.u8string());
            else result.errors.push_back("Destination unavailable, conflicting, or a symlink: " + target.u8string());
            continue;
        }
        ec.clear();
        // copy_options::none never overwrites a file, even if it appeared after inspection.
        if (fs::copy_file(command.path, target, fs::copy_options::none, ec)) ++result.installed;
        else result.errors.push_back("Could not install " + target.u8string() + ": " + ec.message());
    }
    return result;
}
} // namespace plugins
