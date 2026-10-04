local mode = ui.choose('Convert names (one per line) to:', {'snake_case', 'camelCase', 'PascalCase'})
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
    local indent, name, tail = line:match('^([ \t]*)(.-)([ \t]*)$')
    if name ~= '' then
        if name:find('[^A-Za-z0-9 _\t-]') then
            error('Line ' .. i .. ': use ASCII names, not source code; only letters, digits, spaces, underscores, and hyphens are supported.')
        end
        name = name:gsub('([A-Z])([A-Z][a-z])', '%1 %2'):gsub('([a-z0-9])([A-Z])', '%1 %2')
        local words = {}
        for word in name:gmatch('[A-Za-z0-9]+') do words[#words + 1] = word:lower() end
        if #words == 0 or not words[1]:match('^[a-z]') then
            error('Line ' .. i .. ': the converted name must begin with a letter.')
        end
        if mode ~= 1 then
            for j, word in ipairs(words) do
                if mode == 3 or j > 1 then words[j] = word:sub(1, 1):upper() .. word:sub(2) end
            end
        end
        lines[i] = indent .. table.concat(words, mode == 1 and '_' or '') .. tail
    end
end
text = table.concat(lines, '\n') .. (trailing and '\n' or '')
editor.replace(first, last, text)
editor.set_selection(first, first + #text)
