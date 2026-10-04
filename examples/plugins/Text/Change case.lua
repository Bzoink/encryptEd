-- Lua's upper/lower functions provide ASCII case conversion here.
local action = ui.choose('Convert text to:', {'Lowercase (ASCII)', 'Uppercase (ASCII)'})
local first, last = editor.selection()
local text = first and editor.range(first, last) or editor.text()
text = action == 1 and text:lower() or text:upper()
if first then
    editor.replace(first, last, text)
    editor.set_selection(first, first + #text)
else
    editor.set_text(text)
end
