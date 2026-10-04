#include "plugins/plugins.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "core/utf8.h"
#include "platform/secure_mem.h"
}

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>
#include <stdexcept>

namespace plugins {
namespace {
void wipe(std::string &s) { if (!s.empty()) txx_secure_zero(s.data(), s.size()); s.clear(); }

struct Context {
    Document document;
    DialogHost *dialogs = nullptr;
    Limits limits;
    std::size_t allocated = 0;
    std::size_t remaining = 0;
    unsigned dialog_count = 0;
    bool cancelled = false;
    std::string scratch;
    char error[512]{};
    DialogRequest request{};
    DialogReply reply;
    ~Context() { wipe(scratch); txx_secure_zero(error, sizeof(error)); }
};

void *allocate(void *ud, void *ptr, std::size_t old_size, std::size_t new_size)
{
    auto &c = *static_cast<Context *>(ud);
    if (!ptr) old_size = 0; // Lua passes a type tag for new allocations.
    if (!new_size) {
        if (ptr) { txx_secure_zero(ptr, old_size); std::free(ptr); }
        c.allocated -= old_size;
        return nullptr;
    }
    // Lua requires shrinking an allocation to succeed.
    if (ptr && new_size <= old_size) {
        txx_secure_zero(static_cast<char *>(ptr) + new_size, old_size - new_size);
        c.allocated -= old_size - new_size;
        return ptr;
    }
    if (new_size > c.limits.memory_bytes - (c.allocated - old_size)) return nullptr;
    void *next = std::malloc(new_size);
    if (!next) return nullptr;
    if (ptr) {
        std::memcpy(next, ptr, std::min(old_size, new_size));
        txx_secure_zero(ptr, old_size);
        std::free(ptr);
    }
    c.allocated = c.allocated - old_size + new_size;
    return next;
}

Context &context(lua_State *L)
{
    void *ud = nullptr;
    lua_getallocf(L, &ud);
    return *static_cast<Context *>(ud);
}

void instruction_hook(lua_State *L, lua_Debug *)
{
    auto &c = context(L);
    if (c.remaining <= 1000) luaL_error(L, "Plugin instruction limit exceeded.");
    c.remaining -= 1000;
}

bool boundary(const std::string &s, std::size_t p)
{
    return p <= s.size() && (p == s.size() || (static_cast<unsigned char>(s[p]) & 0xc0) != 0x80);
}

std::size_t position(lua_State *L, int arg)
{
    lua_Integer p = luaL_checkinteger(L, arg);
    const auto &s = context(L).document.text;
    if (p < 1 || static_cast<lua_Unsigned>(p - 1) > s.size() || !boundary(s, static_cast<std::size_t>(p - 1)))
        luaL_error(L, "Position must be a UTF-8 character boundary in 1..#text+1.");
    return static_cast<std::size_t>(p - 1);
}

const char *string_arg(lua_State *L, int arg, std::size_t *size)
{
    luaL_checktype(L, arg, LUA_TSTRING);
    return lua_tolstring(L, arg, size);
}

void validate(const char *s, std::size_t n)
{
    if (std::memchr(s, 0, n) || !txx_utf8_validate(reinterpret_cast<const uint8_t *>(s), n))
        throw std::runtime_error("Text must be valid UTF-8 without NUL bytes.");
}

// No Lua API calls in these helpers: native exceptions unwind C++ owners normally.
void replace(Context &c, std::size_t a, std::size_t b, const char *s, std::size_t n)
{
    if (a > b) throw std::runtime_error("Range start exceeds its end.");
    if (n > c.limits.document_bytes) throw std::runtime_error("Document size limit exceeded.");
    validate(s, n);
    wipe(c.scratch);
    c.scratch.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (s[i] == '\r') {
            c.scratch += '\n';
            if (i + 1 < n && s[i + 1] == '\n') ++i;
        } else c.scratch += s[i];
    }
    n = c.scratch.size();
    if (n > c.limits.document_bytes - (c.document.text.size() - (b - a)))
        throw std::runtime_error("Document size limit exceeded.");
    // Build separately, then wipe the old storage rather than reallocating plaintext.
    Document next;
    next.text.reserve(c.document.text.size() - (b - a) + n);
    next.text.append(c.document.text, 0, a);
    next.text += c.scratch;
    next.text.append(c.document.text, b, std::string::npos);
    c.document.text.swap(next.text);
    auto remap = [=](std::size_t p) { return p < a ? p : p <= b ? a + n : p - (b - a) + n; };
    c.document.cursor = remap(c.document.cursor);
    c.document.selection_start = remap(c.document.selection_start);
    c.document.selection_end = remap(c.document.selection_end);
    wipe(c.scratch);
}

enum Operation {
    Text, SetText, Range, Replace, Insert, Erase, LineCount, Line, ToPosition, FromPosition,
    Cursor, SetCursor, Selection, SetSelection, SelectedText, ReplaceSelection, Indentation, Cancel,
    Input, Choose, Confirm, Message
};

int dispatch(lua_State *L)
{
    auto &c = context(L);
    auto &d = c.document;
    auto op = static_cast<Operation>(lua_tointeger(L, lua_upvalueindex(1)));
    std::size_t a = 0, b = d.text.size(), n = 0;
    const char *s = nullptr;
    switch (op) {
    case Text: lua_pushlstring(L, d.text.data(), d.text.size()); return 1;
    case Range:
        a = position(L, 1); b = position(L, 2);
        if (a > b) return luaL_error(L, "Range start exceeds its end.");
        lua_pushlstring(L, d.text.data() + a, b - a); return 1;
    case SetText: s = string_arg(L, 1, &n); break;
    case Replace: a = position(L, 1); b = position(L, 2); s = string_arg(L, 3, &n); break;
    case Insert: a = b = position(L, 1); s = string_arg(L, 2, &n); break;
    case Erase: a = position(L, 1); b = position(L, 2); s = ""; break;
    case ReplaceSelection:
        a = d.selection_start; b = d.selection_end;
        if (a == b) a = b = d.cursor;
        s = string_arg(L, 1, &n); break;
    case LineCount:
        lua_pushinteger(L, 1 + std::count(d.text.begin(), d.text.end(), '\n')); return 1;
    case Line:
    case ToPosition: {
        lua_Integer line = luaL_checkinteger(L, 1);
        lua_Integer col = op == ToPosition ? luaL_checkinteger(L, 2) : 1;
        if (line < 1 || col < 1) return luaL_error(L, "Line and column must be positive.");
        for (lua_Integer i = 1; i < line; ++i) {
            a = d.text.find('\n', a);
            if (a == std::string::npos) return luaL_error(L, "Line is out of range.");
            ++a;
        }
        b = d.text.find('\n', a);
        if (b == std::string::npos) b = d.text.size();
        if (op == Line) { lua_pushlstring(L, d.text.data() + a, b - a); return 1; }
        for (lua_Integer i = 1; i < col; ++i) {
            if (a == b) return luaL_error(L, "Column is out of range.");
            do { ++a; } while (!boundary(d.text, a));
        }
        lua_pushinteger(L, static_cast<lua_Integer>(a + 1)); return 1;
    }
    case FromPosition: {
        a = position(L, 1);
        lua_Integer line = 1, col = 1;
        for (std::size_t i = 0; i < a; ++i) {
            if (d.text[i] == '\n') { ++line; col = 1; }
            else if (boundary(d.text, i)) ++col;
        }
        lua_pushinteger(L, line); lua_pushinteger(L, col); return 2;
    }
    case Cursor: lua_pushinteger(L, static_cast<lua_Integer>(d.cursor + 1)); return 1;
    case SetCursor: d.cursor = position(L, 1); return 0;
    case Selection:
        if (d.selection_start == d.selection_end) { lua_pushnil(L); return 1; }
        lua_pushinteger(L, static_cast<lua_Integer>(d.selection_start + 1));
        lua_pushinteger(L, static_cast<lua_Integer>(d.selection_end + 1)); return 2;
    case SetSelection:
        if (lua_isnoneornil(L, 1)) { d.selection_start = d.selection_end = d.cursor; return 0; }
        a = position(L, 1); b = position(L, 2);
        if (a > b) return luaL_error(L, "Range start exceeds its end.");
        d.selection_start = a; d.selection_end = b; return 0;
    case SelectedText:
        lua_pushlstring(L, d.text.data() + d.selection_start, d.selection_end - d.selection_start); return 1;
    case Indentation:
        lua_pushinteger(L, d.indent_width); lua_pushboolean(L, d.indent_spaces); return 2;
    case Cancel: c.cancelled = true; return luaL_error(L, "Plugin cancelled.");
    case Input: case Choose: case Confirm: case Message:
        if (++c.dialog_count > 100) return luaL_error(L, "Plugin dialog limit exceeded.");
        s = string_arg(L, 1, &n);
        if (n > 65536) return luaL_error(L, "Dialog text is too long.");
        validate(s, n);
        wipe(c.request.prompt); wipe(c.request.initial);
        for (auto &choice : c.request.choices) wipe(choice);
        c.request.choices.clear();
        c.request.prompt.assign(s, n);
        c.request.kind = op == Input ? DialogKind::Input : op == Choose ? DialogKind::Choose
                          : op == Confirm ? DialogKind::Confirm : DialogKind::Message;
        if (op == Input && !lua_isnoneornil(L, 2)) {
            s = string_arg(L, 2, &n);
            if (n > 65536) return luaL_error(L, "Dialog text is too long.");
            validate(s, n); c.request.initial.assign(s, n);
        }
        if (op == Choose) {
            luaL_checktype(L, 2, LUA_TTABLE);
            std::size_t count = lua_rawlen(L, 2), total = 0;
            if (!count || count > 256) return luaL_error(L, "Expected 1..256 choices.");
            for (std::size_t i = 1; i <= count; ++i) {
                lua_rawgeti(L, 2, static_cast<lua_Integer>(i));
                s = string_arg(L, -1, &n);
                if (n > 65536 - total) return luaL_error(L, "Choice text is too long.");
                validate(s, n); total += n;
                c.request.choices.emplace_back(s, n);
                lua_pop(L, 1);
            }
        }
        wipe(c.reply.text);
        c.reply = c.dialogs->show(c.request);
        if (!c.reply.accepted) { c.cancelled = true; return luaL_error(L, "Plugin cancelled."); }
        if (op == Input) {
            if (c.reply.text.size() > 65536) return luaL_error(L, "Dialog response is too long.");
            validate(c.reply.text.data(), c.reply.text.size());
            lua_pushlstring(L, c.reply.text.data(), c.reply.text.size()); return 1;
        }
        if (op == Choose) {
            if (c.reply.choice < 1 || static_cast<std::size_t>(c.reply.choice) > c.request.choices.size())
                return luaL_error(L, "Invalid choice response.");
            lua_pushinteger(L, c.reply.choice); return 1;
        }
        if (op == Confirm) { lua_pushboolean(L, c.reply.confirmed); return 1; }
        return 0;
    }
    replace(c, a, b, s, n);
    return 0;
}

// Lua uses longjmp: never keep nontrivial C++ stack owners across a Lua API call
// that can raise. Temporary strings and dialog data belong to Context instead.
int guarded_dispatch(lua_State *L)
{
    auto &c = context(L);
    try { return dispatch(L); }
    catch (const std::exception &e) { std::snprintf(c.error, sizeof(c.error), "%s", e.what()); }
    catch (...) { std::snprintf(c.error, sizeof(c.error), "%s", "Plugin host operation failed."); }
    return luaL_error(L, "%s", c.error);
}

void function(lua_State *L, const char *name, Operation operation)
{
    lua_pushinteger(L, operation);
    lua_pushcclosure(L, guarded_dispatch, 1);
    lua_setfield(L, -2, name);
}

int initialize(lua_State *L)
{
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_newtable(L);
    const char *allowed[] = {"assert", "error", "ipairs", "pairs", "next", "select", "tonumber", "tostring", "type", "rawequal", "_VERSION"};
    for (const char *name : allowed) { lua_getfield(L, -2, name); lua_setfield(L, -2, name); }
    lua_pushvalue(L, -1); lua_setfield(L, -2, "_G");
    lua_pushvalue(L, -1); lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_pop(L, 2);
    luaL_requiref(L, "string", luaopen_string, 1);
    lua_pushnil(L); lua_setfield(L, -2, "dump"); lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1); lua_pop(L, 1);
    lua_newtable(L);
    lua_pushinteger(L, 1); lua_setfield(L, -2, "api_version");
    function(L, "text", Text); function(L, "set_text", SetText);
    function(L, "range", Range); function(L, "replace", Replace);
    function(L, "insert", Insert); function(L, "delete", Erase);
    function(L, "line_count", LineCount); function(L, "line", Line);
    function(L, "position", ToPosition); function(L, "line_column", FromPosition);
    function(L, "cursor", Cursor); function(L, "set_cursor", SetCursor);
    function(L, "selection", Selection); function(L, "set_selection", SetSelection);
    function(L, "selected_text", SelectedText); function(L, "replace_selection", ReplaceSelection);
    function(L, "indentation", Indentation); function(L, "cancel", Cancel);
    lua_setglobal(L, "editor");
    lua_newtable(L);
    function(L, "input", Input); function(L, "choose", Choose);
    function(L, "confirm", Confirm); function(L, "message", Message);
    lua_setglobal(L, "ui");
    return 0;
}
} // namespace

Document::~Document() { wipe(text); }
DialogRequest::~DialogRequest() { wipe(prompt); wipe(initial); for (auto &s : choices) wipe(s); }
DialogReply::~DialogReply() { wipe(text); }
Result::~Result() { wipe(error); }

Result run(const std::string &source, const std::string &name, const Document &document,
           DialogHost &dialogs, const Limits &limits)
{
    Result result;
    Context c;
    c.limits = limits; c.remaining = limits.instructions; c.dialogs = &dialogs;
    lua_State *L = nullptr;
    try {
        if (source.size() > limits.script_bytes) throw std::runtime_error("Plugin script size limit exceeded.");
        if (document.text.size() > limits.document_bytes) throw std::runtime_error("Document size limit exceeded.");
        validate(document.text.data(), document.text.size());
        if (!boundary(document.text, document.cursor) || !boundary(document.text, document.selection_start) ||
            !boundary(document.text, document.selection_end) || document.selection_start > document.selection_end)
            throw std::runtime_error("Invalid document cursor or selection.");
        c.document = document;
        L = lua_newstate(allocate, &c);
        if (!L) throw std::runtime_error("Could not allocate Lua runtime.");
        // A C function with no upvalues needs no allocation before protected setup.
        lua_pushcfunction(L, initialize);
        int status = lua_pcall(L, 0, 0, 0);
        if (status == LUA_OK) {
            lua_sethook(L, instruction_hook, LUA_MASKCOUNT, 1000);
            status = luaL_loadbufferx(L, source.data(), source.size(), name.c_str(), "t");
            if (status == LUA_OK) status = lua_pcall(L, 0, 0, 0);
        }
        if (status == LUA_OK) {
            result.document = c.document;
            result.status = Status::Success;
        } else if (c.cancelled) result.status = Status::Cancelled;
        else {
            // Do not coerce arbitrary error values outside a protected Lua call.
            const char *message = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
            result.error = message ? std::string(message, std::min<std::size_t>(lua_rawlen(L, -1), 4096))
                                   : "Plugin failed with a non-string error.";
        }
    } catch (const std::exception &e) { result.error = e.what(); }
    if (L) lua_close(L);
    return result;
}
} // namespace plugins
