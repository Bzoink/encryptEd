-- Scan the full document to retain protected-element context for selections.
if not ui.confirm('Remove whitespace between HTML tags?\n\nThis also removes spaces between inline elements and may join rendered words.\nContents of pre, textarea, script, and style are preserved.') then
    editor.cancel()
end
local document = editor.text()
local lower = document:lower()
local first, last = editor.selection()
if not first then first, last = 1, #document + 1 end
local gaps = {}
local position, previous_end, pre_depth = 1, nil, 0
local raw = {script=true, style=true, textarea=true}

local function tag_end(start)
    local quote
    for i = start + 1, #document do
        local ch = document:sub(i, i)
        if quote then
            if ch == quote then quote = nil end
        elseif ch == '"' or ch == "'" then quote = ch
        elseif ch == '>' then return i end
    end
    error('Unterminated HTML tag or quoted attribute at byte ' .. start .. '.')
end

while position <= #document do
    local start = document:find('<', position, true)
    if not start then break end
    if document:sub(start, start + 3) == '<!--' then
        local close = document:find('-->', start + 4, true)
        if not close then error('Unterminated HTML comment at byte ' .. start .. '.') end
        position, previous_end = close + 3, nil
    elseif document:sub(start, start + 1):match('^<[/!?A-Za-z]') then
        local finish = tag_end(start)
        local token = lower:sub(start, finish)
        local closing = token:match('^</') ~= nil
        local name = token:match('^</?([a-z][a-z0-9:-]*)')
        if previous_end and pre_depth == 0 then
            local gap_start = previous_end + 1
            if gap_start < start and gap_start >= first and start <= last
                and document:sub(gap_start, start - 1):match('^%s+$') then
                gaps[#gaps + 1] = {gap_start, start}
            end
        end
        if name == 'pre' then
            if closing then pre_depth = math.max(0, pre_depth - 1)
            else pre_depth = pre_depth + 1 end
        end
        if raw[name] and not closing then
            -- Raw text ends at its HTML closing tag, even inside JS/CSS strings.
            local close = lower:find('</' .. name .. '[ \t\r\n/>]', finish + 1)
            if not close then error('Unterminated protected HTML element: ' .. name .. '.') end
            finish = tag_end(close)
            if not lower:sub(close, finish):match('^</' .. name .. '%s*>$') then
                error('Invalid closing tag for protected HTML element: ' .. name .. '.')
            end
        end
        previous_end, position = finish, finish + 1
    else
        position = start + 1
    end
end
if pre_depth > 0 then error('Unterminated protected HTML element: pre.') end
local parts, cursor = {}, first
for _, gap in ipairs(gaps) do
    parts[#parts + 1] = document:sub(cursor, gap[1] - 1)
    cursor = gap[2]
end
parts[#parts + 1] = document:sub(cursor, last - 1)
local text = table.concat(parts)
editor.replace(first, last, text)
editor.set_selection(first, first + #text)
