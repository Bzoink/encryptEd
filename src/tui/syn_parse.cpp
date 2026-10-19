#include "tui/syn_parse.h"

#include <cctype>
#include <cstring>

namespace {

// ---- Keyword tables --------------------------------------------------------
// Each table is a null-terminated array of C-strings.

static const char *const kKwC[] = {
    "_Bool", "_Complex", "_Generic", "_Imaginary", "_Static_assert",
    "auto", "bool", "break", "case", "char", "const", "continue", "default",
    "do", "double", "else", "enum", "extern", "false", "float", "for", "goto",
    "if", "inline", "int", "long", "nullptr", "register", "restrict", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "true", "union", "unsigned", "void", "volatile", "while",
    nullptr,
};

static const char *const kKwCpp[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto",
    "bitand", "bitor", "bool", "break",
    "case", "catch", "char", "char8_t", "char16_t", "char32_t",
    "class", "compl", "concept", "const", "consteval", "constexpr",
    "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield",
    "decltype", "default", "delete", "do", "double", "dynamic_cast",
    "else", "enum", "explicit", "export", "extern",
    "false", "final", "float", "for", "friend",
    "goto",
    "if", "inline", "int",
    "long",
    "mutable",
    "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
    "operator", "or", "or_eq", "override",
    "private", "protected", "public",
    "register", "reinterpret_cast", "requires", "return",
    "short", "signed", "sizeof", "static", "static_assert", "static_cast",
    "struct", "switch",
    "template", "this", "thread_local", "throw", "true", "try",
    "typedef", "typeid", "typename",
    "union", "unsigned", "using",
    "virtual", "void", "volatile",
    "wchar_t", "while",
    "xor", "xor_eq",
    nullptr,
};

static const char *const kKwPy[] = {
    "False", "None", "True",
    "and", "as", "assert", "async", "await",
    "break", "class", "continue",
    "def", "del",
    "elif", "else", "except",
    "finally", "for", "from",
    "global",
    "if", "import", "in", "is",
    "lambda",
    "nonlocal", "not",
    "or",
    "pass",
    "raise", "return",
    "try",
    "while", "with",
    "yield",
    nullptr,
};

static const char *const kKwSh[] = {
    "break", "case", "continue", "declare", "do", "done",
    "elif", "else", "esac", "eval", "exit", "export",
    "fi", "for", "function",
    "if", "in",
    "local",
    "return",
    "select", "set", "shift", "source",
    "then", "time",
    "unset", "until",
    "while",
    nullptr,
};

static const char *const kKwCMake[] = {
    "add_custom_command", "add_custom_target", "add_definitions",
    "add_executable", "add_library", "add_subdirectory", "add_test",
    "break", "cmake_minimum_required",
    "else", "elseif", "enable_testing",
    "endif", "endforeach", "endfunction", "endmacro", "endwhile",
    "file", "find_library", "find_package", "find_path",
    "foreach", "function",
    "get_property", "get_target_property",
    "if", "include", "install",
    "list",
    "macro", "math", "message",
    "option",
    "project",
    "return",
    "set", "set_property", "set_target_properties", "string",
    "target_compile_definitions", "target_compile_options",
    "target_include_directories", "target_link_libraries",
    "while",
    nullptr,
};

static const char *const kKwRust[] = {
    "Self", "String",
    "as", "async", "await",
    "bool", "break",
    "char", "const", "continue", "crate",
    "dyn",
    "else", "enum", "extern",
    "f32", "f64", "false", "fn", "for",
    "i8", "i16", "i32", "i64", "i128", "if", "impl", "in", "isize",
    "let", "loop",
    "match", "mod", "move", "mut",
    "pub",
    "ref", "return",
    "self", "static", "str", "struct", "super",
    "trait", "true", "type",
    "u8", "u16", "u32", "u64", "u128", "union", "unsafe", "use", "usize",
    "where", "while",
    nullptr,
};

static const char *const kKwJs[] = {
    "abstract", "any",
    "async", "await",
    "boolean", "break",
    "case", "catch", "class", "const", "continue",
    "debugger", "declare", "default", "delete", "do",
    "else", "enum", "export", "extends",
    "false", "finally", "for", "from", "function",
    "if", "implements", "import", "in", "instanceof", "interface",
    "let",
    "module",
    "namespace", "never", "new", "null", "number",
    "of",
    "private", "protected", "public",
    "readonly", "return",
    "static", "string", "super", "switch", "symbol",
    "this", "throw", "true", "try", "type", "typeof",
    "undefined",
    "var", "void",
    "while",
    "yield",
    nullptr,
};

static const char *const kKwHtml[] = {
    "a", "abbr", "address", "area", "article", "aside", "audio",
    "b", "base", "bdi", "bdo", "blockquote", "body", "br", "button",
    "canvas", "caption", "cite", "code", "col", "colgroup",
    "data", "datalist", "dd", "del", "details", "dfn", "dialog", "div", "dl", "dt",
    "em", "embed",
    "fieldset", "figcaption", "figure", "footer", "form",
    "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hgroup", "hr", "html",
    "i", "iframe", "img", "input", "ins",
    "kbd", "keygen",
    "label", "legend", "li", "link",
    "main", "map", "mark", "menu", "menuitem", "meta", "meter",
    "nav", "noindex", "noscript",
    "object", "ol", "optgroup", "option",
    "p", "param", "picture", "pre", "progress",
    "q",
    "rp", "rt", "rtc", "ruby",
    "s", "samp", "script", "section", "select", "shadow", "slot", "small",
    "source", "span", "strong", "style", "sub", "summary", "sup",
    "table", "tbody", "td", "template", "textarea", "tfoot", "th", "thead", "time",
    "title", "tr", "track",
    "u", "ul",
    "var", "video",
    "wbr",
    nullptr,
};

static const char *const kKwCss[] = {
    "absolute", "active", "after", "all", "and", "any", "auto",
    "before", "black", "block", "blue", "bold", "both", "bottom",
    "center", "clear", "color", "content",
    "default", "display",
    "ease", "end",
    "false", "fill", "flex", "float", "focus", "font",
    "green",
    "height", "hidden", "hover",
    "important", "inline", "inherit",
    "left",
    "margin", "max-height", "max-width", "min-height", "min-width",
    "none", "not", "nth-child", "nth-of-type",
    "opacity", "or", "overflow",
    "padding", "position",
    "red", "relative", "repeat", "right",
    "solid", "space", "static",
    "text", "top", "transparent",
    "underline",
    "vertical-align", "visible",
    "white", "width",
    "yellow", "z-index",
    nullptr,
};

static const char *const kKwPowerShell[] = {
    "begin", "break", "catch", "class", "continue",
    "data", "define", "do", "dynamicparam",
    "else", "elseif", "end", "exit",
    "filter", "finally", "for", "foreach", "from", "function",
    "if", "in",
    "param", "process",
    "return",
    "switch",
    "throw", "trap", "try",
    "until",
    "using",
    "while",
    nullptr,
};

static const char *const kKwGo[] = {
    "break", "case", "chan", "const", "continue",
    "default", "defer", "else", "fallthrough", "for", "func",
    "go", "goto",
    "if", "import", "interface",
    "map",
    "package",
    "range", "return",
    "select", "struct", "switch",
    "type",
    "var",
    nullptr,
};

static const char *const kKwPhp[] = {
    "abstract", "and", "array", "as",
    "break",
    "case", "catch", "class", "clone", "const", "continue",
    "declare", "default", "die", "do",
    "echo", "else", "elseif", "empty", "enddeclare", "endfor", "endforeach",
    "endif", "endswitch", "endwhile", "eval", "exit", "extends",
    "final", "finally", "fn", "for", "foreach", "from", "function",
    "global", "goto",
    "if", "implements", "include", "include_once", "instanceof", "insteadof",
    "interface", "isset",
    "list",
    "match",
    "namespace", "new", "not",
    "or",
    "print", "private", "protected", "public",
    "readonly", "require", "require_once", "return",
    "static", "switch",
    "throw", "trait", "try",
    "unset", "use",
    "var",
    "while",
    "xor",
    "yield",
    nullptr,
};

static const char *const kKwSql[] = {
    "absolute", "add", "after", "all", "alter", "and", "any", "as", "asc",
    "before", "begin", "between", "by",
    "case", "cascade", "check", "collate", "column", "commit", "constraint",
    "create", "cross", "current", "current_date", "current_time",
    "delete", "desc", "distinct", "drop",
    "else", "end", "except", "exists",
    "false", "fetch", "for", "foreign", "from", "full",
    "grant", "group",
    "having",
    "in", "index", "inner", "insert", "into", "intersect", "is",
    "join",
    "key",
    "left", "like", "limit",
    "match",
    "natural", "not", "null",
    "offset", "on", "or", "order", "outer",
    "primary",
    "references", "restrict", "revoke", "right",
    "select", "set", "some",
    "table", "then", "to", "transaction", "true",
    "union", "unique", "update", "using",
    "values",
    "where", "with",
    nullptr,
};

static const char *const kKwDocker[] = {
    "add", "arg", "cmd", "copy",
    "entrypoint", "env", "expose",
    "from",
    "healthcheck",
    "label",
    "maintainer",
    "onbuild",
    "run",
    "shell", "stopsignal",
    "user", "usermod",
    "volume",
    "workdir",
    nullptr,
};

static const char *const kKwYaml[] = {
    "true", "false", "yes", "no", "on", "off",
    nullptr,
};

static const char *const kKwXml[] = {
    nullptr,
};

static const char *const kKwJava[] = {
    "abstract", "assert",
    "boolean", "break", "byte",
    "case", "catch", "char", "class", "const", "continue",
    "default", "do", "double",
    "else", "enum", "extends",
    "false", "final", "finally", "float", "for",
    "goto",
    "if", "implements", "import", "instanceof", "int", "interface",
    "long",
    "native", "new", "null",
    "package", "private", "protected", "public",
    "return",
    "short", "static", "strictfp", "super", "switch", "synchronized",
    "this", "throw", "throws", "transient", "true", "try",
    "var", "void", "volatile",
    "while",
    nullptr,
};

static const char *const kKwCSharp[] = {
    "abstract", "as", "async", "await",
    "base", "bool", "break", "byte",
    "case", "catch", "char", "checked", "class", "const", "continue",
    "decimal", "default", "delegate", "do", "double",
    "else", "enum", "event", "explicit", "extern",
    "false", "finally", "fixed", "float", "for", "foreach",
    "goto",
    "if", "implicit", "in", "int", "interface", "internal", "is",
    "lock", "long",
    "namespace", "new", "not", "null", "object",
    "operator", "out", "override",
    "params", "private", "protected", "public",
    "readonly", "record", "ref", "return",
    "sbyte", "sealed", "short", "sizeof", "stackalloc", "static",
    "string", "struct", "switch",
    "this", "throw", "true", "try", "typeof",
    "uint", "ulong", "unchecked", "unsafe", "ushort", "using",
    "var", "virtual", "void", "volatile",
    "when", "where", "while",
    "yield",
    nullptr,
};

// Markdown: highlight ATX heading markers and fence/delimiter keywords.
// The parser handles these structurally; the keyword table is empty
// (headings/fences are detected in syn_parse_line directly for Markdown).
static const char *const kKwMarkdown[] = {
    nullptr,
};

// Makefile: built-in functions and special targets.
static const char *const kKwMakefile[] = {
    "abspath", "addprefix", "addsuffix", "all", "and",
    "basename",
    "call", "clean",
    "define", "dir",
    "else", "endef", "endif", "error", "eval", "export",
    "file", "filter", "filter-out", "findstring", "firstword", "flavor", "foreach",
    "if", "ifdef", "ifeq", "ifndef", "ifneq", "include", "info",
    "join",
    "lastword",
    "notdir",
    "or", "origin", "override",
    "patsubst",
    "sort", "strip", "subst", "suffix",
    "unexport", "undefine",
    "value",
    "warning", "wildcard", "word", "wordlist", "words",
    nullptr,
};

// Batch/CMD: Windows batch keywords.
static const char *const kKwBatch[] = {
    "assoc",
    "call", "cd", "chdir", "cls", "cmd", "color", "copy",
    "date", "del", "dir", "do", "dpath",
    "echo", "else", "endlocal", "erase", "exit", "exist",
    "for", "ftype",
    "goto",
    "if",
    "md", "mkdir", "move",
    "not",
    "path", "pause", "popd", "prompt", "pushd",
    "rd", "rem", "ren", "rename", "rmdir",
    "set", "setlocal", "shift", "start",
    "time", "title", "type",
    nullptr,
};

static const char *const kKwRuby[] = {
    "BEGIN", "END",
    "__ENCODING__", "__FILE__", "__LINE__",
    "alias", "and",
    "begin", "break",
    "case", "class",
    "def", "defined?", "do",
    "else", "elsif", "end", "ensure",
    "false", "for",
    "if", "in",
    "module",
    "next", "nil", "not",
    "or",
    "redo", "rescue", "retry", "return",
    "self", "super",
    "then", "true",
    "undef", "unless", "until",
    "when", "while",
    "yield",
    nullptr,
};

static const char *const kKwPerl[] = {
    "BEGIN", "END",
    "STDIN", "STDOUT", "STDERR",
    "and",
    "cmp", "continue",
    "do",
    "else", "elsif", "eq",
    "for", "foreach",
    "ge", "gt",
    "if", "in",
    "last", "le", "local", "lt",
    "my",
    "ne", "next", "not",
    "or", "our",
    "package",
    "ref", "return",
    "scalar", "sub",
    "undef", "unless", "until", "use",
    "while",
    "x",
    nullptr,
};

// TOML: built-in values; keys are not keywords in TOML.
static const char *const kKwToml[] = {
    "false", "inf", "nan", "true",
    nullptr,
};

// INI/Properties: no true keywords; section headers handled structurally.
static const char *const kKwIni[] = {
    nullptr,
};

static const char *const kKwR[] = {
    "FALSE", "Inf", "NA", "NA_character_", "NA_complex_", "NA_integer_",
    "NA_real_", "NaN", "NULL", "TRUE",
    "break",
    "else",
    "for", "function",
    "if", "in",
    "next",
    "repeat", "return",
    "while",
    nullptr,
};

static const char *const kKwSwift[] = {
    "Any", "AnyObject",
    "Self", "String",
    "as", "associatedtype", "associativity",
    "break",
    "case", "catch", "class", "continue", "convenience",
    "default", "defer", "deinit", "didSet", "do", "dynamic",
    "else", "enum", "extension",
    "fallthrough", "false", "fileprivate", "final", "for", "func",
    "get", "guard",
    "if", "import", "in", "indirect", "infix", "init", "inout", "internal", "is",
    "lazy", "left", "let",
    "mutating",
    "nil", "none", "nonmutating",
    "open", "operator", "optional", "override",
    "postfix", "precedencegroup", "prefix", "private", "protocol", "public",
    "repeat", "required", "rethrows", "return", "right",
    "self", "set", "some", "static", "struct", "subscript", "super", "switch",
    "throws", "true", "try", "type", "typealias",
    "unowned", "var",
    "weak", "where", "while", "willSet",
    nullptr,
};

static const char *const kKwKotlin[] = {
    "abstract", "actual", "annotation", "as",
    "break", "by",
    "catch", "class", "companion", "const", "constructor", "continue",
    "crossinline",
    "data", "delegate", "do", "dynamic",
    "else", "enum", "expect", "external",
    "false", "field", "file", "final", "finally", "for", "fun",
    "get",
    "if", "import", "in", "infix", "init", "inline", "inner", "interface",
    "internal", "is",
    "it",
    "lateinit",
    "noinline", "null",
    "object", "open", "operator", "out", "override",
    "package", "param", "private", "property", "protected", "public",
    "receiver", "reified", "return",
    "sealed", "set", "setparam", "super", "suspend",
    "tailrec", "this", "throw", "true", "try", "typealias",
    "val", "var", "vararg",
    "when", "where", "while",
    nullptr,
};

static const char *const kKwLua[] = {
    "and", "break", "do",
    "else", "elseif", "end",
    "false", "for", "function",
    "goto",
    "if", "in",
    "local",
    "nil", "not",
    "or",
    "repeat", "return",
    "then", "true",
    "until",
    "while",
    nullptr,
};

// Assembly: common x86/x86-64 mnemonics and directives.
static const char *const kKwAsm[] = {
    // Data directives
    "db", "dd", "dq", "dt", "dw",
    "equ",
    "global",
    // Sections
    "bss",
    "data",
    "extern",
    "section", "segment",
    "text",
    // Common mnemonics (lowercase; assembler is usually case-insensitive)
    "adc", "add", "and",
    "call", "cbw", "cdq", "cld", "cli", "cmp", "cmps", "cmpsb",
    "cwd",
    "dec", "div",
    "hlt",
    "idiv", "imul", "inc", "int",
    "ja", "jae", "jb", "jbe", "jc", "je", "jg", "jge", "jl", "jle",
    "jmp", "jna", "jnae", "jnb", "jnbe", "jnc", "jne", "jng", "jnge",
    "jnl", "jnle", "jno", "jnp", "jns", "jnz", "jo", "jp", "js", "jz",
    "lea", "leave", "lods", "lodsb", "loop",
    "mov", "movs", "movsb", "movsx", "movzx", "mul",
    "neg", "nop", "not",
    "or",
    "pop", "popa", "popad", "popf", "push", "pusha", "pushad", "pushf",
    "rcl", "rcr", "rep", "repe", "repne", "ret", "rol", "ror",
    "sal", "sar", "sbb", "shl", "shr", "stc", "std", "sti", "stos", "stosb",
    "sub",
    "test",
    "xchg", "xor",
    nullptr,
};

// JSON: literals only — strings, numbers and punctuation carry the rest.
static const char *const kKwJson[] = {
    "false",
    "null",
    "true",
    nullptr,
};

// ---- Helpers ----------------------------------------------------------------

const char *const *kw_table(SynLang lang) {
    switch (lang) {
        case SynLang::C:           return kKwC;
        case SynLang::Cpp:         return kKwCpp;
        case SynLang::Python:      return kKwPy;
        case SynLang::Shell:       return kKwSh;
        case SynLang::CMake:       return kKwCMake;
        case SynLang::Rust:        return kKwRust;
        case SynLang::JavaScript:  return kKwJs;
        case SynLang::HTML:        return kKwHtml;
        case SynLang::CSS:         return kKwCss;
        case SynLang::PowerShell:  return kKwPowerShell;
        case SynLang::Go:          return kKwGo;
        case SynLang::PHP:         return kKwPhp;
        case SynLang::SQL:         return kKwSql;
        case SynLang::Dockerfile:  return kKwDocker;
        case SynLang::YAML:        return kKwYaml;
        case SynLang::XML:         return kKwXml;
        case SynLang::Java:        return kKwJava;
        case SynLang::CSharp:      return kKwCSharp;
        case SynLang::Markdown:    return kKwMarkdown;
        case SynLang::Makefile:    return kKwMakefile;
        case SynLang::Batch:       return kKwBatch;
        case SynLang::Ruby:        return kKwRuby;
        case SynLang::Perl:        return kKwPerl;
        case SynLang::TOML:        return kKwToml;
        case SynLang::INI:         return kKwIni;
        case SynLang::R:           return kKwR;
        case SynLang::Swift:       return kKwSwift;
        case SynLang::Kotlin:      return kKwKotlin;
        case SynLang::Lua:         return kKwLua;
        case SynLang::Asm:         return kKwAsm;
        case SynLang::JSON:        return kKwJson;
        default:                   return nullptr;
    }
}

bool is_keyword(const char *word, std::size_t len, const char *const *table) {
    for (; *table; ++table)
        if (strlen(*table) == len && memcmp(*table, word, len) == 0)
            return true;
    return false;
}

inline bool is_ident_start(unsigned char c) {
    return std::isalpha(c) || c == '_';
}
inline bool is_ident(unsigned char c) {
    return std::isalnum(c) || c == '_';
}
inline bool is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

inline void push_run(std::vector<SynRun> &out,
                     std::size_t start, std::size_t end, SynStyle style) {
    if (end > start)
        out.push_back({start, end - start, style});
}

// Scan a quoted string starting at `i` (opening quote already identified).
// Handles backslash escapes. Returns index past the closing quote (or n).
std::size_t scan_quoted(const std::string &s, std::size_t i, char q) {
    ++i;  // skip opening quote
    while (i < s.size()) {
        char c = s[i];
        if (c == '\\') { i += 2; continue; }
        if (c == q)    { return i + 1; }
        ++i;
    }
    return s.size();
}

}  // namespace

// ---- Public API -------------------------------------------------------------

SynLang syn_detect(const std::string &filename) {
    auto sep = filename.find_last_of("/\\");
    const std::string base = sep == std::string::npos
                           ? filename : filename.substr(sep + 1);

    if (base == "CMakeLists.txt") return SynLang::CMake;
    if (base == "Dockerfile")     return SynLang::Dockerfile;
    if (base == "Makefile" || base == "GNUmakefile") return SynLang::Makefile;

    auto dot = base.rfind('.');
    if (dot == std::string::npos) return SynLang::None;

    std::string ext = base.substr(dot + 1);
    for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == "c" || ext == "h")                               return SynLang::C;
    if (ext == "cpp" || ext == "cc" || ext == "cxx" ||
        ext == "hpp" || ext == "hh" || ext == "hxx")            return SynLang::Cpp;
    if (ext == "py")                                             return SynLang::Python;
    if (ext == "sh" || ext == "bash")                           return SynLang::Shell;
    if (ext == "cmake")                                          return SynLang::CMake;
    if (ext == "rs")                                             return SynLang::Rust;
    if (ext == "js" || ext == "mjs" || ext == "cjs" ||
        ext == "ts" || ext == "tsx" || ext == "jsx")            return SynLang::JavaScript;
    if (ext == "html" || ext == "htm" || ext == "xhtml")       return SynLang::HTML;
    if (ext == "css" || ext == "scss" || ext == "less")        return SynLang::CSS;
    if (ext == "ps1" || ext == "psm1" || ext == "psd1")        return SynLang::PowerShell;
    if (ext == "go")                                             return SynLang::Go;
    if (ext == "php" || ext == "php3" || ext == "php4" ||
        ext == "php5" || ext == "php7" || ext == "php8")        return SynLang::PHP;
    if (ext == "sql")                                            return SynLang::SQL;
    if (ext == "json")                                           return SynLang::JSON;
    if (ext == "yaml" || ext == "yml")                          return SynLang::YAML;
    if (ext == "xml" || ext == "svg" || ext == "xsd")           return SynLang::XML;
    if (ext == "java")                                           return SynLang::Java;
    if (ext == "cs")                                             return SynLang::CSharp;
    if (ext == "md" || ext == "markdown")                        return SynLang::Markdown;
    if (ext == "mk" || ext == "mak" || ext == "make")           return SynLang::Makefile;
    if (ext == "bat" || ext == "cmd")                            return SynLang::Batch;
    if (ext == "rb" || ext == "rake" || ext == "gemspec")        return SynLang::Ruby;
    if (ext == "pl" || ext == "pm" || ext == "t")                return SynLang::Perl;
    if (ext == "toml")                                            return SynLang::TOML;
    if (ext == "ini" || ext == "cfg" || ext == "conf" ||
        ext == "properties")                                      return SynLang::INI;
    if (ext == "r" || ext == "rmd")                              return SynLang::R;
    if (ext == "swift")                                           return SynLang::Swift;
    if (ext == "kt" || ext == "kts")                             return SynLang::Kotlin;
    if (ext == "lua")                                             return SynLang::Lua;
    if (ext == "asm" || ext == "s" || ext == "nasm")             return SynLang::Asm;

    return SynLang::None;
}

std::vector<SynRun> syn_parse_line(const std::string &s, SynState &st, SynLang lang) {
    std::vector<SynRun> runs;
    if (lang == SynLang::None) return runs;

    const std::size_t n = s.size();
    std::size_t i = 0;

    // --- Python triple-quote continuation from previous line ---
    if (st.in_multiline_str) {
        char q = st.ml_str_char;
        const std::string close{q, q, q};
        auto end = s.find(close);
        if (end == std::string::npos) {
            if (n) runs.push_back({0, n, SynStyle::String});
            return runs;
        }
        push_run(runs, 0, end + 3, SynStyle::String);
        st.in_multiline_str = false;
        i = end + 3;
    }

    // --- Block comment continuation from previous line ---
    // Ruby (=begin/=end) and Perl (=pod/=cut) handle their own block state above.
    if (st.in_block_comment &&
        lang != SynLang::Ruby && lang != SynLang::Perl) {
        const char *close_seq = (lang == SynLang::PowerShell) ? "#>"
                              : (lang == SynLang::Lua)        ? "]]"
                                                              : "*/";
        auto end = s.find(close_seq, i);
        if (end == std::string::npos) {
            if (n > i) push_run(runs, i, n, SynStyle::Comment);
            return runs;
        }
        push_run(runs, i, end + 2, SynStyle::Comment);
        st.in_block_comment = false;
        i = end + 2;
    }

    const bool c_style  = lang == SynLang::C   || lang == SynLang::Cpp  ||
                          lang == SynLang::Rust || lang == SynLang::JavaScript ||
                          lang == SynLang::Go || lang == SynLang::PHP ||
                          lang == SynLang::Java || lang == SynLang::CSharp ||
                          lang == SynLang::Swift || lang == SynLang::Kotlin;
    const bool hash_cmt = lang == SynLang::Python || lang == SynLang::Shell ||
                          lang == SynLang::CMake || lang == SynLang::PowerShell ||
                          lang == SynLang::YAML || lang == SynLang::PHP ||
                          lang == SynLang::Makefile || lang == SynLang::Ruby ||
                          lang == SynLang::Perl || lang == SynLang::TOML ||
                          lang == SynLang::INI || lang == SynLang::R;
    const bool preproc  = lang == SynLang::C || lang == SynLang::Cpp;
    const bool html_cmt = lang == SynLang::HTML || lang == SynLang::XML;
    const bool sql_dash = lang == SynLang::SQL;

    // --- Markdown: ATX headings and fenced code lines ---
    if (lang == SynLang::Markdown) {
        std::size_t j = 0;
        while (j < n && s[j] == '#') ++j;
        if (j > 0 && j <= 6 && (j == n || s[j] == ' ')) {
            runs.push_back({0, n, SynStyle::Preproc});
            return runs;
        }
        if (n >= 3 && s[0] == '`' && s[1] == '`' && s[2] == '`') {
            runs.push_back({0, n, SynStyle::Preproc});
            return runs;
        }
        // Inline code: `...`
        std::size_t k = 0;
        while (k < n) {
            if (s[k] == '`') {
                auto end = s.find('`', k + 1);
                std::size_t past = (end == std::string::npos) ? n : end + 1;
                push_run(runs, k, past, SynStyle::String);
                k = past;
            } else {
                ++k;
            }
        }
        return runs;
    }

    // --- Ruby =begin/=end block comments ---
    if (lang == SynLang::Ruby) {
        if (st.in_block_comment) {
            if (n >= 4 && s.substr(0, 4) == "=end") st.in_block_comment = false;
            push_run(runs, 0, n, SynStyle::Comment);
            return runs;
        }
        if (n >= 6 && s.substr(0, 6) == "=begin") {
            st.in_block_comment = true;
            push_run(runs, 0, n, SynStyle::Comment);
            return runs;
        }
    }

    // --- Perl =pod/=cut block comments ---
    if (lang == SynLang::Perl) {
        if (st.in_block_comment) {
            if (n >= 4 && s.substr(0, 4) == "=cut") st.in_block_comment = false;
            push_run(runs, 0, n, SynStyle::Comment);
            return runs;
        }
        if (n >= 4 && s[0] == '=' && std::isalpha(static_cast<unsigned char>(s[1]))) {
            st.in_block_comment = true;
            push_run(runs, 0, n, SynStyle::Comment);
            return runs;
        }
    }

    // --- INI/Properties: section headers and value strings ---
    if (lang == SynLang::INI) {
        if (n > 0 && s[0] == '[') {
            runs.push_back({0, n, SynStyle::Preproc});
            return runs;
        }
        // Value after = or : highlighted as string
        auto eq = s.find_first_of("=:");
        if (eq != std::string::npos && eq + 1 < n) {
            push_run(runs, eq + 1, n, SynStyle::String);
        }
        return runs;
    }

    // --- Batch/CMD: :: line comment ---
    if (lang == SynLang::Batch && i + 1 < n && s[i] == ':' && s[i + 1] == ':') {
        push_run(runs, i, n, SynStyle::Comment);
        return runs;
    }

    // --- Preprocessor line (C/C++): # must be first non-whitespace ---
    if (preproc) {
        std::size_t j = 0;
        while (j < n && (s[j] == ' ' || s[j] == '\t')) ++j;
        if (j < n && s[j] == '#') {
            runs.push_back({0, n, SynStyle::Preproc});
            return runs;
        }
    }

    const char *const *kw = kw_table(lang);

    // --- Main scan ---
    while (i < n) {
        const unsigned char uc = static_cast<unsigned char>(s[i]);
        const char c = s[i];

        // Line comment: // (C-style langs) or # (hash-comment langs)
        if (c_style && c == '/' && i + 1 < n && s[i + 1] == '/') {
            push_run(runs, i, n, SynStyle::Comment);
            return runs;
        }
        if (hash_cmt && c == '#') {
            push_run(runs, i, n, SynStyle::Comment);
            return runs;
        }
        // SQL/Lua line comment: --
        // (Lua block --[[ ]] handled below under block comments)
        if ((sql_dash || lang == SynLang::Lua) && c == '-' && i + 1 < n && s[i + 1] == '-') {
            // Lua: --[[ starts a block comment
            if (lang == SynLang::Lua && i + 3 < n && s[i + 2] == '[' && s[i + 3] == '[') {
                auto end = s.find("]]", i + 4);
                if (end == std::string::npos) {
                    push_run(runs, i, n, SynStyle::Comment);
                    st.in_block_comment = true;
                    return runs;
                }
                push_run(runs, i, end + 2, SynStyle::Comment);
                i = end + 2;
                continue;
            }
            push_run(runs, i, n, SynStyle::Comment);
            return runs;
        }
        // Assembly: ; line comment
        if (lang == SynLang::Asm && c == ';') {
            push_run(runs, i, n, SynStyle::Comment);
            return runs;
        }
        // Assembly: . directives (.section, .global, .data, ...)
        if (lang == SynLang::Asm && c == '.') {
            std::size_t j = i + 1;
            while (j < n && is_ident(static_cast<unsigned char>(s[j]))) ++j;
            if (j > i + 1) push_run(runs, i, j, SynStyle::Preproc);
            i = j;
            continue;
        }
        // Batch/CMD :: comment (at line start already handled above; skip mid-line)
        if (lang == SynLang::Batch && c == ':' && i + 1 < n && s[i + 1] == ':') {
            push_run(runs, i, n, SynStyle::Comment);
            return runs;
        }
        // HTML/XML comment: <!-- ... -->
        if (html_cmt && c == '<' && i + 3 < n && s[i + 1] == '!' &&
            s[i + 2] == '-' && s[i + 3] == '-') {
            auto end = s.find("-->", i + 4);
            if (end == std::string::npos) {
                push_run(runs, i, n, SynStyle::Comment);
                return runs;
            }
            push_run(runs, i, end + 3, SynStyle::Comment);
            i = end + 3;
            continue;
        }
        // XML tag names: <tag  </tag  <?pi  — highlight the name as Keyword
        if (lang == SynLang::XML && c == '<') {
            std::size_t j = i + 1;
            if (j < n && (s[j] == '/' || s[j] == '?')) ++j;
            if (j < n && is_ident_start(static_cast<unsigned char>(s[j]))) {
                std::size_t k = j;
                while (k < n && (is_ident(static_cast<unsigned char>(s[k])) ||
                                  s[k] == ':' || s[k] == '-' || s[k] == '.'))
                    ++k;
                push_run(runs, j, k, SynStyle::Keyword);
                i = k;
                continue;
            }
            ++i;
            continue;
        }
        // PowerShell block comment: <# ... #>
        if (lang == SynLang::PowerShell && c == '<' && i + 1 < n && s[i + 1] == '#') {
            auto end = s.find("#>", i + 2);
            if (end == std::string::npos) {
                push_run(runs, i, n, SynStyle::Comment);
                st.in_block_comment = true;
                return runs;
            }
            push_run(runs, i, end + 2, SynStyle::Comment);
            i = end + 2;
            continue;
        }

        // Block comment: /* ... */
        if (c_style && c == '/' && i + 1 < n && s[i + 1] == '*') {
            auto end = s.find("*/", i + 2);
            if (end == std::string::npos) {
                push_run(runs, i, n, SynStyle::Comment);
                st.in_block_comment = true;
                return runs;
            }
            push_run(runs, i, end + 2, SynStyle::Comment);
            i = end + 2;
            continue;
        }

        // Python triple-quoted strings: """ or '''
        if (lang == SynLang::Python && (c == '"' || c == '\'') &&
            i + 2 < n && s[i + 1] == c && s[i + 2] == c) {
            const std::string close{c, c, c};
            auto end = s.find(close, i + 3);
            if (end == std::string::npos) {
                push_run(runs, i, n, SynStyle::String);
                st.in_multiline_str = true;
                st.ml_str_char = c;
                return runs;
            }
            push_run(runs, i, end + 3, SynStyle::String);
            i = end + 3;
            continue;
        }

        // Regular strings / char literals
        if (c == '"' || c == '\'') {
            // Shell single-quote: no escape sequences inside
            if (lang == SynLang::Shell && c == '\'') {
                auto end = s.find('\'', i + 1);
                std::size_t past = (end == std::string::npos) ? n : end + 1;
                push_run(runs, i, past, SynStyle::String);
                i = past;
                continue;
            }
            std::size_t end = scan_quoted(s, i, c);
            push_run(runs, i, end, SynStyle::String);
            i = end;
            continue;
        }

        // JavaScript / TypeScript template literals `...`
        if (lang == SynLang::JavaScript && c == '`') {
            auto end = s.find('`', i + 1);
            std::size_t past = (end == std::string::npos) ? n : end + 1;
            push_run(runs, i, past, SynStyle::String);
            i = past;
            continue;
        }

        // Numbers: integer, hex, binary, float
        if (is_digit(uc) || (c == '.' && i + 1 < n && is_digit(static_cast<unsigned char>(s[i + 1])))) {
            std::size_t j = i;
            if (c == '0' && j + 1 < n && (s[j + 1] == 'x' || s[j + 1] == 'X' ||
                                            s[j + 1] == 'b' || s[j + 1] == 'B')) {
                j += 2;
                while (j < n && (std::isxdigit(static_cast<unsigned char>(s[j])) || s[j] == '_'))
                    ++j;
            } else {
                while (j < n && (is_digit(static_cast<unsigned char>(s[j])) ||
                                  s[j] == '.' || s[j] == '_'))
                    ++j;
                // Exponent: 1e10, 1.5E-3 (JSON numbers and most languages).
                if (j < n && (s[j] == 'e' || s[j] == 'E')) {
                    std::size_t k = j + 1;
                    if (k < n && (s[k] == '+' || s[k] == '-')) ++k;
                    if (k < n && is_digit(static_cast<unsigned char>(s[k]))) {
                        ++k;
                        while (k < n && (is_digit(static_cast<unsigned char>(s[k])) ||
                                          s[k] == '_'))
                            ++k;
                        j = k;
                    }
                }
            }
            push_run(runs, i, j, SynStyle::Number);
            i = j;
            continue;
        }

        // Identifiers and keywords
        if (is_ident_start(uc)) {
            std::size_t j = i + 1;
            while (j < n && is_ident(static_cast<unsigned char>(s[j]))) ++j;
            if (kw && is_keyword(s.c_str() + i, j - i, kw))
                push_run(runs, i, j, SynStyle::Keyword);
            i = j;
            continue;
        }

        ++i;
    }

    return runs;
}
