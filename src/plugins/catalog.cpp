#include "plugins/plugins.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace plugins {
namespace fs = std::filesystem;

fs::path user_directory()
{
#ifdef _WIN32
    if (const wchar_t *p = _wgetenv(L"APPDATA"))
        if (*p) return fs::path(p) / "encryptEd" / "plugins";
#elif defined(__APPLE__)
    if (const char *p = std::getenv("HOME"))
        if (*p) return fs::u8path(p) / "Library/Application Support/encryptEd/plugins";
#else
    if (const char *p = std::getenv("XDG_DATA_HOME"))
        if (*p && fs::u8path(p).is_absolute()) return fs::u8path(p) / "encryptEd/plugins";
    if (const char *p = std::getenv("HOME"))
        if (*p) return fs::u8path(p) / ".local/share/encryptEd/plugins";
#endif
    return {};
}

Catalog discover(const fs::path &root)
{
    Catalog result;
    if (root.empty()) {
        result.errors.push_back("Could not determine the user plugin directory.");
        return result;
    }
    std::error_code ec;
    auto status = fs::symlink_status(root, ec);
    if (status.type() == fs::file_type::not_found) return result;
    if (ec || !fs::is_directory(status) || fs::is_symlink(status)) {
        result.errors.push_back("Cannot scan plugin directory: " + root.u8string());
        return result;
    }
    // Walk each directory separately so one unreadable subtree cannot end the scan.
    std::vector<fs::path> pending{root};
    while (!pending.empty()) {
        fs::path dir = pending.back();
        pending.pop_back();
        fs::directory_iterator it(dir, ec), end;
        if (ec) {
            result.errors.push_back("Cannot read directory: " + dir.u8string());
            ec.clear();
            continue;
        }
        while (it != end) {
            auto entry = *it;
            auto s = entry.symlink_status(ec);
            if (ec) {
                result.errors.push_back("Cannot inspect: " + entry.path().u8string());
                ec.clear();
            } else if (!fs::is_symlink(s)) {
                if (fs::is_directory(s)) pending.push_back(entry.path());
                else if (fs::is_regular_file(s) && entry.path().extension() == ".lua") {
                    Command command;
                    command.path = entry.path();
                    command.label = entry.path().stem().u8string();
                    for (const auto &part : entry.path().parent_path().lexically_relative(root))
                        if (part != ".") command.folders.push_back(part.u8string());
                    result.commands.push_back(std::move(command));
                }
            }
            it.increment(ec);
            if (ec) {
                result.errors.push_back("Cannot continue reading: " + dir.u8string());
                ec.clear();
                break;
            }
        }
    }
    std::sort(result.commands.begin(), result.commands.end(), [](const Command &a, const Command &b) {
        if (a.folders != b.folders) return a.folders < b.folders;
        return a.label < b.label;
    });
    return result;
}

bool read_script(const fs::path &root, const Command &command, std::string &source,
                 std::string &error, const Limits &limits)
{
    std::error_code ec;
    auto relative = command.path.lexically_relative(root);
    if (root.empty() || relative.empty() || relative.is_absolute()) {
        error = "Invalid plugin path.";
        return false;
    }
    auto current = root;
    if (!fs::is_directory(fs::symlink_status(current, ec)) || ec) {
        error = "Plugin directory is unavailable or is a symlink.";
        return false;
    }
    for (const auto &part : relative) {
        if (part == ".." || part == ".") { error = "Invalid plugin path."; return false; }
        current /= part;
        if (fs::is_symlink(fs::symlink_status(current, ec)) || ec) {
            error = "Plugin path is unavailable or contains a symlink.";
            return false;
        }
    }
    if (!fs::is_regular_file(current, ec) || ec) {
        error = "Plugin is not a regular file.";
        return false;
    }
    auto size = fs::file_size(current, ec);
    if (ec || size > limits.script_bytes) {
        error = "Plugin is unreadable or exceeds the script size limit.";
        return false;
    }
    std::ifstream in(current, std::ios::binary);
    source.resize(static_cast<std::size_t>(size));
    if (!in || !in.read(source.data(), static_cast<std::streamsize>(size)) || in.peek() != EOF) {
        error = "Could not read plugin (it may have changed during loading).";
        source.clear();
        return false;
    }
    return true;
}
} // namespace plugins
