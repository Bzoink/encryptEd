local prefix = ui.input('Prefix to add to each selected line:', '> ')
local first, last = editor.selection()
-- Expand a selection to complete lines, excluding an untouched next line.
if first then
    local first_line = editor.line_column(first)
    local last_line, column = editor.line_column(last)
    if column == 1 and last_line > first_line then last_line = last_line - 1 end
    first = editor.position(first_line, 1)
    last = editor.position(last_line, 1) + #editor.line(last_line)
else
    first, last = 1, #editor.text() + 1
end
local text = editor.range(first, last)
if text == '' then return end
-- A callback treats percent signs in the prefix literally.
local trailing_newline = text:sub(-1) == '\n'
text = prefix .. text:gsub('\n', function() return '\n' .. prefix end)
if trailing_newline and #prefix > 0 then text = text:sub(1, #text - #prefix) end
editor.replace(first, last, text)
editor.set_selection(first, first + #text)
