#include "plugins/plugins.h"

#include <cstdio>
#include <fstream>
#include <iterator>

using namespace plugins;
namespace fs = std::filesystem;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

struct Dialogs : DialogHost {
    std::vector<int> choices{1};
    std::vector<std::string> inputs;
    std::size_t choice = 0, input = 0;
    int calls = 0, cancel_at = 0;
    bool confirm = true;
    std::string message;
    DialogReply show(const DialogRequest &request) override {
        DialogReply reply;
        reply.accepted = ++calls != cancel_at;
        reply.confirmed = confirm;
        if (request.kind == DialogKind::Choose && choice < choices.size()) reply.choice = choices[choice++];
        if (request.kind == DialogKind::Input) reply.text = input < inputs.size() ? inputs[input++] : request.initial;
        if (request.kind == DialogKind::Message) message = request.prompt;
        return reply;
    }
};

static Result execute(const std::string &plugin, const Document &document, Dialogs &dialogs)
{
    auto path = fs::path(ENCRYPTED_SOURCE_DIR) / "examples/plugins" / (plugin + ".lua");
    std::ifstream in(path);
    CHECK(in.good());
    std::string source(std::istreambuf_iterator<char>(in), {});
    return run(source, plugin, document, dialogs);
}

static void transform(const std::string &plugin, const std::string &text,
                      const std::string &expected, int mode = 1,
                      std::vector<std::string> inputs = {},
                      std::size_t start = 0, std::size_t end = 0)
{
    Document d; d.text = text; d.selection_start = start; d.selection_end = end;
    Dialogs ui; ui.choices = {mode}; ui.inputs = std::move(inputs);
    auto r = execute(plugin, d, ui);
    if (r.status != Status::Success || r.document.text != expected) {
        std::fprintf(stderr, "%s: status %d, error [%s], got [%s], expected [%s]\n",
            plugin.c_str(), static_cast<int>(r.status), r.error.c_str(), r.document.text.c_str(), expected.c_str());
        ++failures;
    }
    CHECK(d.text == text && d.selection_start == start && d.selection_end == end);
}

static void invalid(const std::string &plugin, const std::string &text,
                    std::vector<std::string> inputs = {})
{
    Document d; d.text = text;
    Dialogs ui; ui.inputs = std::move(inputs);
    auto r = execute(plugin, d, ui);
    CHECK(r.status == Status::Error && !r.error.empty());
    CHECK(d.text == text);
}

static void text_tests()
{
    const std::string affix = "Text/Add prefix and suffix";
    transform(affix, "a\n\nb\n", "%[a]%\n%[]%\n%[b]%\n", 1, {"%[", "]%"});
    transform(affix, "a\n\nb", "[a]\n[]\n[b]", 1, {"[", "]"});
    transform(affix, "", "", 1, {"[", "]"});
    transform(affix, "", "[]", 2, {"[", "]"});
    transform(affix, "猫 a\nb\n", "猫 [a]\nb\n", 2, {"[", "]"}, 4, 5);
    transform(affix, "first\nsecond\nlast", "[first]\nsecond\nlast", 1, {"[", "]"}, 2, 6);
    transform(affix, "a\n", "a\n", 1, {"", ""});
    invalid(affix, "a", {"a\nb", ""});

    const std::string sort = "Lines/Sort lines";
    transform(sort, "z\nA\na\na\n\n", "\nA\na\na\nz\n");
    transform(sort, "z\nA\na\na\n", "z\na\na\nA\n", 2);
    transform(sort, "keep\nz\na\nend", "keep\na\nz\nend", 1, {}, 6, 9);
    transform(sort, "猫\né\na", "a\né\n猫");
    transform(sort, "", "");
    transform(sort, "\n", "\n");

    const std::string duplicates = "Lines/Remove duplicate lines";
    transform(duplicates, "b\na\nb\n\n\n猫\n猫\n", "b\na\n\n猫\n");
    transform(duplicates, "A\na\nA", "A\na");
    transform(duplicates, "keep\nx\nx\nend", "keep\nx\nend", 1, {}, 5, 9);
    transform(duplicates, "", "");
    Document d; d.text = "a\na"; Dialogs ui;
    CHECK(execute(duplicates, d, ui).status == Status::Success);
    CHECK(ui.message == "Removed 1 duplicate lines.");
}

static void code_tests()
{
    const std::string comment = "Code/Comment lines";
    transform(comment, "  a\n\tb\n \t\n猫\n", "  // a\n\t// b\n \t\n// 猫\n");
    transform(comment, "  // a\n\t//b\n//  c\n# d\n", "  a\n\tb\n c\n# d\n", 2, {"//"});
    transform(comment, "a\nb", "-- a\n-- b", 1, {"--"});
    transform(comment, "a", "% a", 1, {"%"});
    transform(comment, "a\nb\nc", "a\n# b\nc", 1, {"#"}, 2, 4);
    transform(comment, "", "");
    invalid(comment, "a", {""});
    invalid(comment, "a", {"x\ny"});

    const std::string identifier = "Code/Convert identifier style";
    transform(identifier, "  HTTPServer \ncustomerAccount\nFOO_bar-2\n\n", "  http_server \ncustomer_account\nfoo_bar_2\n\n");
    transform(identifier, "HTTPServer\ncustomer account", "httpServer\ncustomerAccount", 2);
    transform(identifier, "HTTPServer\ncustomer account", "HttpServer\nCustomerAccount", 3);
    transform(identifier, "outside();\nhello_world\nend();", "outside();\nhelloWorld\nend();", 2, {}, 13, 23);
    transform(identifier, " \t\n", " \t\n");
    transform(identifier, "", "");
    invalid(identifier, "goodName\nbad();");
    invalid(identifier, "猫");
    invalid(identifier, "2words");
    invalid(identifier, "---");
}

static void html_tests()
{
    const std::string escape = "HTML/Escape HTML";
    transform(escape, "<&>\"' 猫", "&lt;&amp;&gt;\"' 猫");
    transform(escape, "<&>\"'", "&lt;&amp;&gt;&quot;&#39;", 2);
    transform(escape, "&amp;", "&amp;amp;");
    transform(escape, "a<b>c", "a&lt;b&gt;c", 1, {}, 1, 4);
    transform(escape, "", "");

    const std::string wrap = "HTML/Wrap in tag";
    transform(wrap, "猫\n", "<div>猫\n</div>");
    transform(wrap, "a b c", "a <my-tag>b</my-tag> c", 1, {"my-tag"}, 2, 3);
    transform(wrap, "", "<div></div>");
    for (const auto &tag : {"", "a class='x'", "1div", "a_b", "IMG", "br", "猫"}) invalid(wrap, "x", {tag});

    const std::string compact = "HTML/Compact HTML";
    transform(compact, "<span>Hello</span> \n <span>world</span>", "<span>Hello</span><span>world</span>");
    transform(compact, "<div title='a > b'> \t <b> 猫 </b> \n </div>", "<div title='a > b'><b> 猫 </b></div>");
    transform(compact, "<div> text <b>x</b> </div>", "<div> text <b>x</b></div>");
    transform(compact, "<a></a> <!-- <b>  </b> --> <c></c>", "<a></a> <!-- <b>  </b> --> <c></c>");
    for (const auto &tag : {"pre", "PRE", "textarea", "script", "style"}) {
        std::string protected_text = "<" + std::string(tag) + "><b> \n </b></" + tag + ">";
        transform(compact, "<div> \n " + protected_text + " \n </div>", "<div>" + protected_text + "</div>");
        transform(compact, protected_text, protected_text, 1, {}, std::string(tag).size() + 2, protected_text.size());
    }
    transform(compact, "<pre><pre> \n </pre> \n </pre> \n <b>x</b>", "<pre><pre> \n </pre> \n </pre><b>x</b>");
    transform(compact, "<script>if (a < b) x = '<div>  </div>';</script> \n <b>x</b>", "<script>if (a < b) x = '<div>  </div>';</script><b>x</b>");
    transform(compact, "<a> \n </a> \n <b> \n </b>", "<a></a> \n <b> \n </b>", 1, {}, 0, 10);
    transform(compact, "a < 5", "a < 5");
    transform(compact, "", "");
    for (const auto &bad : {"<div", "<div x='abc>", "<!-- missing", "<pre>abc", "<script>abc", "<textarea>abc", "<style>abc"}) invalid(compact, bad);
    Document d; d.text = "<a> </a>"; Dialogs ui; ui.confirm = false;
    CHECK(execute(compact, d, ui).status == Status::Cancelled);
    CHECK(d.text == "<a> </a>");
}

static void cancellation_tests()
{
    const std::vector<std::pair<std::string, int>> commands{
        {"Text/Add prefix and suffix", 3}, {"HTML/Compact HTML", 1},
        {"HTML/Escape HTML", 1}, {"HTML/Wrap in tag", 1}, {"Lines/Sort lines", 1},
        {"Lines/Remove duplicate lines", 1}, {"Code/Comment lines", 2},
        {"Code/Convert identifier style", 1}};
    for (const auto &command : commands) {
        for (int dialog = 1; dialog <= command.second; ++dialog) {
            Document d; d.text = "helloWorld\nhelloWorld"; d.cursor = 2;
            d.selection_start = 1; d.selection_end = 9;
            Dialogs ui; ui.cancel_at = dialog;
            auto r = execute(command.first, d, ui);
            CHECK(r.status == Status::Cancelled);
            CHECK(d.text == "helloWorld\nhelloWorld" && d.cursor == 2 &&
                  d.selection_start == 1 && d.selection_end == 9);
        }
    }
}

int main()
{
    text_tests(); code_tests(); html_tests(); cancellation_tests();
    return failures ? 1 : 0;
}
