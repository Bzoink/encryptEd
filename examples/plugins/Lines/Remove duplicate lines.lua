local first, last = editor.selection()
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
local trailing = text:sub(-1) == '\n'
local body = trailing and text:sub(1, -2) or text
local lines = {}
if text ~= '' then
    for line in (body .. '\n'):gmatch('(.-)\n') do lines[#lines + 1] = line end
end
local seen, unique, removed = {}, {}, 0
for _, line in ipairs(lines) do
    if seen[line] then removed = removed + 1
    else seen[line] = true; unique[#unique + 1] = line end
end
lines = unique
text = table.concat(lines, '\n') .. (trailing and '\n' or '')
editor.replace(first, last, text)
editor.set_selection(first, first + #text)
ui.message('Removed ' .. removed .. ' duplicate lines.')
