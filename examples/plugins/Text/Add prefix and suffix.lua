local mode = ui.choose('Add prefix and suffix to:', {'Each line', 'Whole block'})
local prefix = ui.input('Prefix:', '')
local suffix = ui.input('Suffix:', '')
if prefix:find('[\r\n]') or suffix:find('[\r\n]') then
    error('Prefix and suffix must each be a single line.')
end
local first, last = editor.selection()
if mode == 1 and first then
    local first_line = editor.line_column(first)
    local last_line, column = editor.line_column(last)
    if column == 1 and last_line > first_line then last_line = last_line - 1 end
    first = editor.position(first_line, 1)
    last = editor.position(last_line, 1) + #editor.line(last_line)
elseif not first then first, last = 1, #editor.text() + 1 end
local text = editor.range(first, last)
if mode == 1 then
    local trailing = text:sub(-1) == '\n'
    local body = trailing and text:sub(1, -2) or text
    local lines = {}
    if text ~= '' then
        for line in (body .. '\n'):gmatch('(.-)\n') do
            lines[#lines + 1] = prefix .. line .. suffix
        end
    end
    text = table.concat(lines, '\n') .. (trailing and '\n' or '')
else text = prefix .. text .. suffix end
editor.replace(first, last, text)
editor.set_selection(first, first + #text)
