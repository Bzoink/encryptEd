local mode = ui.choose('Comment lines:', {'Add', 'Remove'})
local prefix = ui.input('Comment prefix:', '//')
if prefix == '' or prefix:find('[\r\n]') then error('Enter a nonempty, single-line comment prefix.') end
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
for i, line in ipairs(lines) do
    local indent, content = line:match('^([ \t]*)(.*)$')
    if content ~= '' then
        if mode == 1 then lines[i] = indent .. prefix .. ' ' .. content
        elseif content:sub(1, #prefix) == prefix then
            content = content:sub(#prefix + 1)
            if content:sub(1, 1) == ' ' then content = content:sub(2) end
            lines[i] = indent .. content
        end
    end
end
text = table.concat(lines, '\n') .. (trailing and '\n' or '')
editor.replace(first, last, text)
editor.set_selection(first, first + #text)
