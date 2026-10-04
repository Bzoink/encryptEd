-- Transform the selection, or the whole document when nothing is selected.
local first, last = editor.selection()
local text = first and editor.range(first, last) or editor.text()
text = text:gsub('[ \t]+\n', '\n'):gsub('[ \t]+$', '')
if first then
    editor.replace(first, last, text)
    editor.set_selection(first, first + #text)
else
    editor.set_text(text)
end
