#include "tui/plugin_buffer.h"
#include <cstdio>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)
struct Dialogs : plugins::DialogHost {
    bool cancel = false;
    plugins::DialogReply show(const plugins::DialogRequest &) override {
        plugins::DialogReply reply; reply.accepted = !cancel; reply.text = "hello"; return reply;
    }
};
static plugins::Status execute(EditBuffer &buffer, const std::string &source, Dialogs &dialogs) {
    auto document = plugin_document(buffer);
    auto result = plugins::run(source, "test", document, dialogs);
    if (result.status == plugins::Status::Success) apply_plugin_document(buffer, result.document);
    return result.status;
}

static void positions() {
    EditBuffer buffer; buffer.set_text("aé\n猫🔒\n");
    const Position positions[] = {{0,0}, {0,1}, {0,2}, {1,0}, {1,1}, {1,2}, {2,0}};
    const std::size_t bytes[] = {0,1,3,4,7,11,12};
    for (std::size_t i = 0; i < 7; ++i) {
        CHECK(buffer.byte_position(positions[i]) == bytes[i]);
        CHECK(buffer.position_from_byte(bytes[i]) == positions[i]);
    }
    buffer.set_selection({0,1}, {1,1});
    buffer.set_cursor({2,0});
    auto document = plugin_document(buffer);
    CHECK(document.text == "aé\n猫🔒\n" && document.cursor == 12);
    CHECK(document.selection_start == 1 && document.selection_end == 7);
    CHECK(buffer.selected_text() == "é\n猫");
    Dialogs ui;
    CHECK(execute(buffer, "assert(editor.selected_text() == 'é\\n猫'); editor.replace_selection('Å'); editor.set_selection(1, 2); editor.set_cursor(#editor.text()+1)", ui) == plugins::Status::Success);
    CHECK(buffer.text() == "aÅ🔒\n" && buffer.cursor() == (Position{1,0}));
    CHECK(buffer.selected_text() == "a");
    buffer.undo();
    CHECK(buffer.text() == "aé\n猫🔒\n" && buffer.cursor() == (Position{2,0}));
    CHECK(buffer.selected_text() == "é\n猫");
    buffer.redo();
    CHECK(buffer.text() == "aÅ🔒\n" && buffer.selected_text() == "a");
}

static void history() {
    EditBuffer buffer;
    buffer.insert_text("a");
    Dialogs ui;
    CHECK(execute(buffer, "editor.set_text('AA'); editor.set_selection(nil)", ui) == plugins::Status::Success);
    buffer.insert_text("z");
    CHECK(buffer.text() == "AAz");
    buffer.undo(); CHECK(buffer.text() == "AA");
    buffer.undo(); CHECK(buffer.text() == "a");
    buffer.undo(); CHECK(buffer.text().empty());
    buffer.redo(); CHECK(buffer.text() == "a");
    buffer.redo(); CHECK(buffer.text() == "AA");
    buffer.redo(); CHECK(buffer.text() == "AAz");
    buffer.undo();
    CHECK(execute(buffer, "editor.set_text(editor.text()); editor.set_cursor(1); editor.set_selection(1,2)", ui) == plugins::Status::Success);
    CHECK(buffer.can_redo() && buffer.selected_text() == "A" && buffer.cursor() == (Position{0,0}));
    buffer.redo(); CHECK(buffer.text() == "AAz");
    EditBuffer empty;
    CHECK(execute(empty, "editor.set_text(''); editor.set_cursor(1)", ui) == plugins::Status::Success);
    CHECK(!empty.can_undo());
    buffer.set_text("a");
    CHECK(execute(buffer, "editor.set_text('b'); editor.set_text('c'); editor.insert(2,'d')", ui) == plugins::Status::Success);
    CHECK(buffer.text() == "cd");
    buffer.undo(); CHECK(buffer.text() == "a" && !buffer.can_undo());
}

static void rollback() {
    EditBuffer buffer; buffer.insert_text("é猫");
    buffer.set_selection({0,0}, {0,1}); buffer.set_cursor({0,2});
    Dialogs ui; ui.cancel = true;
    CHECK(execute(buffer, "editor.set_text('discarded'); ui.input('Cancel')", ui) == plugins::Status::Cancelled);
    CHECK(buffer.text() == "é猫" && buffer.selected_text() == "é" && buffer.cursor() == (Position{0,2}));
    CHECK(execute(buffer, "editor.set_text('discarded'); error('failure')", ui) == plugins::Status::Error);
    CHECK(execute(buffer, "editor.set_text('discarded'); while true do end", ui) == plugins::Status::Error);
    CHECK(buffer.text() == "é猫" && buffer.selected_text() == "é");
    buffer.undo(); CHECK(buffer.text().empty());
}
int main() { positions(); history(); rollback(); return failures ? 1 : 0; }
