#include "plugins/plugins.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <chrono>

using namespace plugins;
namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

struct MockDialogs : DialogHost {
    std::vector<DialogKind> seen;
    bool cancel = false;
    DialogReply show(const DialogRequest &request) override {
        seen.push_back(request.kind);
        DialogReply reply;
        reply.accepted = !cancel;
        reply.text = "prefix: ";
        reply.choice = 2;
        reply.confirmed = false;
        return reply;
    }
};

static void runtime_tests()
{
    MockDialogs ui;
    Document d;
    d.text = "aé\n猫\n";
    d.cursor = 3; d.selection_start = 1; d.selection_end = 3;
    auto r = run(R"(
        assert(editor.api_version == 1)
        assert(editor.line_count() == 3)
        assert(editor.line(1) == 'aé' and editor.line(3) == '')
        assert(editor.position(1, 3) == 4)
        assert(editor.position(2, 2) == 8)
        local line, col = editor.line_column(8)
        assert(line == 2 and col == 2)
        local a, b = editor.selection()
        assert(a == 2 and b == 4 and editor.selected_text() == 'é')
        assert(editor.range(a, b) == 'é')
        editor.replace_selection('XYZ')
        assert(editor.cursor() == 5)
        assert(editor.selection() == nil)
        editor.insert(1, '!')
        editor.delete(2, 3)
        assert(editor.text() == '!XYZ\n猫\n')
        editor.replace(1, 2, '?')
        editor.set_cursor(2)
        editor.set_selection(2, 5)
        local width, spaces = editor.indentation()
        assert(width == 4 and spaces == false)
    )", "unicode", d, ui);
    CHECK(r.status == Status::Success);
    if (r.status != Status::Success) std::fprintf(stderr, "%s\n", r.error.c_str());
    CHECK(r.document.text == "?XYZ\n猫\n");
    CHECK(r.document.cursor == 1 && r.document.selection_start == 1 && r.document.selection_end == 4);
    CHECK(d.text == "aé\n猫\n");

    for (const char *script : {
        "editor.insert(3, 'x')", "editor.delete(4, 2)", "editor.set_cursor(0)",
        "editor.set_selection(1, 500)", "editor.line(4)", "editor.position(1, 4)",
        "editor.set_text(string.char(255))", "editor.set_text('a' .. string.char(0))",
        "editor.set_text('changed'); error('failure')", "this is not Lua",
        "error({})", "ui.choose('x', {})", "ui.message(string.char(0))"
    }) {
        auto failed = run(script, "invalid", d, ui);
        CHECK(failed.status == Status::Error);
        CHECK(failed.document.text.empty());
        CHECK(!failed.error.empty());
    }
    auto newline = run("editor.set_text('a\\r\\nb\\rc\\n')", "newline", d, ui);
    CHECK(newline.status == Status::Success && newline.document.text == "a\nb\nc\n");

    auto restricted = run(R"(
        assert(io == nil and os == nil and package == nil and require == nil)
        assert(debug == nil and load == nil and loadfile == nil and dofile == nil)
        assert(print == nil and warn == nil and collectgarbage == nil)
        assert(pcall == nil and xpcall == nil and coroutine == nil)
        assert(getmetatable == nil and setmetatable == nil and string.dump == nil)
        assert(_G.editor == editor and utf8.len('猫') == 1)
        assert(('a  '):gsub(' +$', '') == 'a')
        assert(math.floor(1.2) == 1 and table.concat({'a', 'b'}) == 'ab')
        secret = 'must not persist'
    )", "restricted", d, ui);
    CHECK(restricted.status == Status::Success);
    CHECK(run("assert(secret == nil)", "fresh", d, ui).status == Status::Success);
    CHECK(run(std::string("\x1bLua\x54", 5), "bytecode", d, ui).status == Status::Error);
    auto cancelled = run("editor.set_text('changed'); editor.cancel()", "cancel", d, ui);
    CHECK(cancelled.status == Status::Cancelled && cancelled.document.text.empty());

    auto dialogs = run(R"(
        assert(ui.input('Prefix', '') == 'prefix: ')
        assert(ui.choose('Action', {'One', 'Two'}) == 2)
        assert(ui.confirm('Continue?') == false)
        ui.message('Done')
    )", "dialogs", d, ui);
    CHECK(dialogs.status == Status::Success && ui.seen.size() == 4);
    ui.cancel = true;
    auto cancel_dialog = run("editor.delete(1, 2); ui.input('Cancel me')", "dialog-cancel", d, ui);
    CHECK(cancel_dialog.status == Status::Cancelled && cancel_dialog.document.text.empty());
    ui.cancel = false;

    Limits limits;
    limits.instructions = 10000;
    CHECK(run("while true do end", "loop", d, ui, limits).status == Status::Error);
    CHECK(run("local function f() return f() end; f()", "tail-loop", d, ui, limits).status == Status::Error);
    limits.memory_bytes = 128 * 1024;
    CHECK(run("local t = {}; while true do t[#t+1] = string.rep('x', 4096) end", "memory", d, ui, limits).status == Status::Error);
    limits.memory_bytes = 1;
    CHECK(run("", "init-memory", d, ui, limits).status == Status::Error);
    // Exercise allocation failures during library setup as well as script execution.
    for (std::size_t budget = 1024; budget < 40000; budget += 1024) {
        limits.memory_bytes = budget;
        auto limited = run("editor.set_text(string.rep('z', 40000))", "allocation", d, ui, limits);
        CHECK(limited.status == Status::Error && limited.document.text.empty());
    }
    limits = Limits{}; limits.document_bytes = d.text.size();
    CHECK(run("editor.insert(1, 'extra')", "size", d, ui, limits).status == Status::Error);
    limits.script_bytes = 2;
    CHECK(run("return", "script-size", d, ui, limits).status == Status::Error);
    CHECK(run("for i=1,101 do ui.message('Again') end", "dialogs-limit", d, ui).status == Status::Error);
    Document empty;
    CHECK(run("assert(editor.line_count() == 1 and editor.line(1) == ''); editor.replace_selection('x')", "empty", empty, ui).document.text == "x");
}

static void write(const fs::path &p, const std::string &text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

static void catalog_tests()
{
    auto root = fs::temp_directory_path() / ("encrypted-plugin-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    write(root / "B/Deep/same.lua", "error('not executed by discovery')");
    write(root / "A/same.lua", "editor.set_text('a')");
    write(root / "A/A & B.lua", "not valid Lua");
    write(root / "root.lua", "return");
    write(root / "ignored.txt", "return");
    std::error_code ec;
    fs::create_directory_symlink(root / "A", root / "link", ec);
    ec.clear(); fs::create_symlink(root / "root.lua", root / "linked.lua", ec);
    auto catalog = discover(root);
    CHECK(catalog.errors.empty() && catalog.commands.size() == 4);
    CHECK(catalog.commands[0].label == "root");
    CHECK(catalog.commands[1].folders == std::vector<std::string>{"A"});
    CHECK(catalog.commands[1].label == "A & B");
    CHECK(catalog.commands[3].folders == (std::vector<std::string>{"B", "Deep"}));
    std::string source, error;
    CHECK(read_script(root, catalog.commands[0], source, error) && source == "return");
    write(root / "root.lua", "-- reloaded");
    CHECK(read_script(root, catalog.commands[0], source, error) && source == "-- reloaded");
    Limits limits; limits.script_bytes = 1;
    CHECK(!read_script(root, catalog.commands[0], source, error, limits));
    Command outside{root / "../outside.lua", {}, "outside"};
    CHECK(!read_script(root, outside, source, error));
    fs::remove(root / "root.lua");
    CHECK(!read_script(root, catalog.commands[0], source, error));
    CHECK(discover(root).commands.size() == 3);
    CHECK(discover(root / "missing").commands.empty());
#ifndef _WIN32
    CHECK(!read_script(root, Command{root / "linked.lua", {}, "linked"}, source, error));
    fs::create_directories(root / "unreadable");
    fs::permissions(root / "unreadable", fs::perms::none);
    CHECK(!discover(root).errors.empty());
    fs::permissions(root / "unreadable", fs::perms::owner_all);
#endif
    fs::remove_all(root);
}

static void user_directory_tests()
{
#if !defined(_WIN32) && !defined(__APPLE__)
    const char *original_data = std::getenv("XDG_DATA_HOME");
    const char *original_home = std::getenv("HOME");
    bool had_data = original_data != nullptr, had_home = original_home != nullptr;
    std::string data = original_data ? original_data : "";
    std::string home = original_home ? original_home : "";
    setenv("HOME", "/tmp/plugin-user", 1);
    setenv("XDG_DATA_HOME", "/tmp/plugin-data", 1);
    CHECK(user_directory() == fs::path("/tmp/plugin-data/encryptEd/plugins"));
    setenv("XDG_DATA_HOME", "relative", 1);
    CHECK(user_directory() == fs::path("/tmp/plugin-user/.local/share/encryptEd/plugins"));
    unsetenv("XDG_DATA_HOME");
    CHECK(user_directory() == fs::path("/tmp/plugin-user/.local/share/encryptEd/plugins"));
    if (had_data) setenv("XDG_DATA_HOME", data.c_str(), 1); else unsetenv("XDG_DATA_HOME");
    if (had_home) setenv("HOME", home.c_str(), 1); else unsetenv("HOME");
#endif
}

static void example_tests()
{
    const auto root = fs::path(ENCRYPTED_SOURCE_DIR) / "examples/plugins";
    auto catalog = discover(root);
    CHECK(catalog.errors.empty() && catalog.commands.size() == 12);
    MockDialogs ui;
    for (const auto &command : catalog.commands) {
        if (command.label != "Trim trailing spaces" && command.label != "Prefix lines" && command.label != "Change case") continue;
        std::string source, error;
        CHECK(read_script(root, command, source, error));
        Document document; document.text = "one  \ntwo\t\nthree  ";
        auto result = run(source, command.label, document, ui);
        CHECK(result.status == Status::Success);
        if (command.label == "Trim trailing spaces") CHECK(result.document.text == "one\ntwo\nthree");
        if (command.label == "Prefix lines") CHECK(result.document.text == "prefix: one  \nprefix: two\t\nprefix: three  ");
        if (command.label == "Change case") CHECK(result.document.text == "ONE  \nTWO\t\nTHREE  ");
        if (command.label == "Prefix lines") {
            document.text = "a\n\nb\n";
            result = run(source, command.label, document, ui);
            CHECK(result.document.text == "prefix: a\nprefix: \nprefix: b\n");
        }
    }
}

static std::string contents(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

static void install_tests()
{
    auto root = fs::temp_directory_path() / ("encrypted-install-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto source = root / "source", dest = root / "user";
    write(source / "Nested/Deep/test.lua", "return");
    write(source / "second.lua", "return");
    write(source / "ignored.txt", "ignored");
    CHECK(install_defaults({}, dest).errors.size() == 1);
    CHECK(!fs::exists(dest));
    CHECK(!install_defaults(root / "missing", dest).errors.empty());
    CHECK(!fs::exists(dest));
    auto result = install_defaults(source, dest);
    CHECK(result.installed == 2 && result.errors.empty() && result.skipped.empty());
    CHECK(contents(dest / "Nested/Deep/test.lua") == "return");
    CHECK(!fs::exists(dest / "ignored.txt"));
    write(dest / "second.lua", "user edits");
    write(dest / "custom.lua", "custom");
    result = install_defaults(source, dest);
    CHECK(result.installed == 0 && result.skipped.size() == 2 && result.errors.empty());
    CHECK(contents(dest / "second.lua") == "user edits");
    CHECK(contents(dest / "custom.lua") == "custom");
    fs::remove_all(dest / "Nested");
    write(dest / "Nested", "directory conflict");
    write(source / "third.lua", "third");
    result = install_defaults(source, dest);
    CHECK(result.installed == 1 && !result.errors.empty());
    CHECK(contents(dest / "Nested") == "directory conflict");
    fs::remove(dest / "Nested");
#ifndef _WIN32
    fs::create_directories(root / "outside");
    fs::create_directory_symlink(root / "outside", dest / "Nested");
    result = install_defaults(source, dest);
    CHECK(!result.errors.empty() && !fs::exists(root / "outside/Deep"));
    fs::remove(dest / "Nested");
    fs::remove(dest / "second.lua");
    write(root / "outside.lua", "untouched");
    fs::create_symlink(root / "outside.lua", dest / "second.lua");
    CHECK(!install_defaults(source, dest).errors.empty());
    CHECK(contents(root / "outside.lua") == "untouched");
    fs::create_directory_symlink(source, root / "source-link");
    CHECK(!install_defaults(root / "source-link", dest).errors.empty());
    fs::permissions(source / "Nested", fs::perms::none);
    CHECK(!install_defaults(source, dest).errors.empty());
    fs::permissions(source / "Nested", fs::perms::owner_all);
#endif
    auto bundled = install_defaults(fs::path(ENCRYPTED_SOURCE_DIR) / "examples/plugins", root / "bundled");
    CHECK(bundled.installed == 12 && bundled.errors.empty());
    CHECK(discover(root / "bundled").commands.size() == 12);
    write(root / "bundled/Text/Change case.lua", "-- edited default");
    write(root / "bundled/My custom.lua", "-- custom plugin");
    fs::remove(root / "bundled/HTML/Compact HTML.lua");
    bundled = install_defaults(fs::path(ENCRYPTED_SOURCE_DIR) / "examples/plugins", root / "bundled");
    CHECK(bundled.installed == 1 && bundled.skipped.size() == 11 && bundled.errors.empty());
    CHECK(contents(root / "bundled/Text/Change case.lua") == "-- edited default");
    CHECK(contents(root / "bundled/My custom.lua") == "-- custom plugin");
    auto executable = root / "package/bin/encryptEd";
    auto data = root / "package/share/encryptEd/default-plugins";
    fs::create_directories(data);
    CHECK(default_directory(executable, root / "missing") == data);
    auto adjacent = executable.parent_path() / "default-plugins";
    fs::create_directories(adjacent);
    CHECK(default_directory(executable, data) == adjacent);
    CHECK(default_directory({}, data) == data);
    CHECK(default_directory({}, root / "missing").empty());
    fs::remove_all(adjacent);
    fs::remove_all(data);
    auto legacy = executable.parent_path() / "plugin-examples";
    fs::create_directories(legacy);
    CHECK(default_directory(executable, root / "missing") == legacy);
    CHECK(!executable_path().empty());
    fs::remove_all(root);
}

struct PlaygroundDialogs : DialogHost {
    int scope = 1, action = 1, inputs = 0, calls = 0, cancel_at = 0;
    bool confirm = true;
    std::string prefix, suffix;
    DialogReply show(const DialogRequest &request) override {
        DialogReply reply;
        reply.accepted = ++calls != cancel_at;
        reply.confirmed = confirm;
        if (request.kind == DialogKind::Choose) reply.choice = calls == 1 ? scope : action;
        if (request.kind == DialogKind::Input) reply.text = inputs++ == 0 ? prefix : suffix;
        return reply;
    }
};

static void playground_tests()
{
    auto path = fs::path(ENCRYPTED_SOURCE_DIR) / "examples/plugins/Demo/Text Playground.lua";
    auto source = contents(path);
    const std::vector<std::string> expected{"A  B\nÉ猫\n", "a  b\nÉ猫\n", "a B\nÉ猫\n", "1: a  B\n2: É猫\n"};
    for (int action = 1; action <= 4; ++action) {
        Document d; d.text = "a  B\nÉ猫\n";
        PlaygroundDialogs ui; ui.action = action;
        auto r = run(source, "playground", d, ui);
        CHECK(r.status == Status::Success && r.document.text == expected[action - 1]);
        CHECK(r.document.selection_start == 0 && r.document.selection_end == r.document.text.size());
        CHECK(r.document.cursor == r.document.text.size());
    }
    Document d; d.text = "one two three"; d.selection_start = 4; d.selection_end = 7;
    PlaygroundDialogs ui; ui.prefix = "%["; ui.suffix = "]%";
    auto r = run(source, "playground", d, ui);
    CHECK(r.status == Status::Success && r.document.text == "one %[TWO]% three");
    ui = PlaygroundDialogs{}; ui.scope = 2;
    r = run(source, "playground", d, ui);
    CHECK(r.document.text == "ONE TWO THREE");
    ui = PlaygroundDialogs{}; ui.confirm = false;
    CHECK(run(source, "playground", d, ui).status == Status::Cancelled);
    for (int dialog = 1; dialog <= 6; ++dialog) {
        ui = PlaygroundDialogs{}; ui.cancel_at = dialog;
        CHECK(run(source, "playground", d, ui).status == Status::Cancelled);
        CHECK(d.text == "one two three" && d.selection_start == 4 && d.selection_end == 7);
    }
    for (const auto &text : {"", "\n", "a\n\nb\n"}) {
        Document empty; empty.text = text;
        ui = PlaygroundDialogs{}; ui.action = 4;
        r = run(source, "playground", empty, ui);
        CHECK(r.status == Status::Success);
        if (empty.text.empty()) CHECK(r.document.text.empty());
        else if (empty.text == "\n") CHECK(r.document.text == "1: \n");
        else CHECK(r.document.text == "1: a\n2: \n3: b\n");
    }
}

int main()
{
    runtime_tests(); catalog_tests(); user_directory_tests(); example_tests(); install_tests(); playground_tests();
    if (!failures) std::puts("Plugin tests passed.");
    return failures ? 1 : 0;
}
