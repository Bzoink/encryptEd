local scope = ui.choose('Process which text?', {
    'Selection, or whole document if nothing is selected', 'Whole document'
})
local first, last = editor.selection()
local selected = scope == 1 and first ~= nil
local text = selected and editor.range(first, last) or editor.text()
local original_bytes = #text
local action = ui.choose('Choose a transformation:', {
    'Uppercase (ASCII)', 'Lowercase (ASCII)', 'Collapse spaces and tabs', 'Number each line'
})
if action == 1 then
    text = text:upper()
elseif action == 2 then
    text = text:lower()
elseif action == 3 then
    text = text:gsub('[ \t]+', ' ')
else
    -- Preserve a final newline without adding a numbered phantom line.
    local lines = {}
    local trailing = text:sub(-1) == '\n'
    local body = trailing and text:sub(1, -2) or text
    if text ~= '' then
        for line in (body .. '\n'):gmatch('(.-)\n') do
            lines[#lines + 1] = tostring(#lines + 1) .. ': ' .. line
        end
    end
    text = table.concat(lines, '\n') .. (trailing and '\n' or '')
end
local prefix = ui.input('Prefix for the processed text:', '[DEMO] ')
local suffix = ui.input('Suffix for the processed text:', ' [/DEMO]')
text = prefix .. text .. suffix
if not ui.confirm('Apply this change?\n\nOriginal: ' .. original_bytes
    .. ' bytes\nResult: ' .. #text .. ' bytes, ' .. utf8.len(text) .. ' Unicode code points') then
    editor.cancel()
end
local start = selected and first or 1
if selected then editor.replace(first, last, text) else editor.set_text(text) end
editor.set_selection(start, start + #text)
editor.set_cursor(start + #text)
ui.message('Done! The processed text is selected.\nUse Undo to restore it.')
