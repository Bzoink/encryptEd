#ifndef ENCRYPTED_PLUGINS_H
#define ENCRYPTED_PLUGINS_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace plugins {

// Host offsets are zero-based UTF-8 bytes; Lua offsets are one-based.
struct Document {
    std::string text;
    std::size_t cursor = 0;
    std::size_t selection_start = 0;
    std::size_t selection_end = 0;
    int indent_width = 4;
    bool indent_spaces = false;
    ~Document();
};

struct Limits {
    std::size_t memory_bytes = 64 * 1024 * 1024;
    std::size_t document_bytes = 16 * 1024 * 1024;
    std::size_t script_bytes = 1024 * 1024;
    std::size_t instructions = 10000000;
};

enum class DialogKind { Input, Choose, Confirm, Message };
struct DialogRequest {
    DialogKind kind;
    std::string prompt;
    std::string initial;
    std::vector<std::string> choices;
    ~DialogRequest();
};
struct DialogReply {
    bool accepted = false;
    std::string text;
    int choice = 1; // one-based
    bool confirmed = false;
    ~DialogReply();
};
class DialogHost {
public:
    virtual ~DialogHost() = default;
    virtual DialogReply show(const DialogRequest &request) = 0;
};

enum class Status { Success, Cancelled, Error };
struct Result {
    Status status = Status::Error;
    Document document; // populated only on success
    std::string error;
    ~Result();
};

Result run(const std::string &source, const std::string &name,
           const Document &document, DialogHost &dialogs, const Limits &limits = {});

struct Command {
    std::filesystem::path path;
    std::vector<std::string> folders;
    std::string label;
};
struct Catalog {
    std::vector<Command> commands;
    std::vector<std::string> errors;
};

std::filesystem::path user_directory();
struct InstallResult {
    std::size_t installed = 0;
    std::vector<std::string> skipped;
    std::vector<std::string> errors;
};
std::filesystem::path default_directory(const std::filesystem::path &executable,
                                        const std::filesystem::path &installed = {});
std::filesystem::path executable_path();
InstallResult install_defaults(const std::filesystem::path &source,
                               const std::filesystem::path &destination);

Catalog discover(const std::filesystem::path &root);
// Reject symlinks (including parent directories), traversal, and oversized files.
bool read_script(const std::filesystem::path &root, const Command &command,
                 std::string &source, std::string &error, const Limits &limits = {});

} // namespace plugins
#endif
