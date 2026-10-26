# EncryptEd's Lua changes

The bundled Lua 5.5.1 has a local native-work callback extension. It is compiled
statically together with the plugin host; its internal state layout is not ABI
compatible with an unmodified Lua build.

`lua_setworkf` installs an optional host callback and context for a Lua state.
`lua_checkwork` charges work to that callback, which may raise a Lua error. A new
state has no callback. Only use checkpoints inside protected execution; never
add them to error-reporting or cleanup code, which must work after exhaustion.

The string library checks pattern backtracking, character-class and balanced
pattern scans, backreferences, repeated comparisons in literal searches, and
bulk transformations. The table library checks element movement, concatenation,
and sorting (including the cost of long string comparisons). The UTF-8 library
checks scans, including malformed runs of continuation bytes. These checks
complement the VM instruction hook and allocation limit. They do not provide
OS isolation or a preemptive real-time deadline.

When updating Lua, retain the callback initialization and API in `lua.h`,
`lstate.h`, `lstate.c`, and `lapi.c`, and reapply/review the checkpoints in
`lstrlib.c`, `ltablib.c`, and `lutf8lib.c`. Run `plugin_tests` and
`default_plugin_tests`, including their adversarial pattern/table and rollback
cases. New native functions exposed to plugins must have bounded work or
equivalent checkpoints.
