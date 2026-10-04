local mode = ui.choose('Escape HTML for:', {'Text', 'Attribute value'})
local first, last = editor.selection()
if not first then first, last = 1, #editor.text() + 1 end
local text = editor.range(first, last)
text = text:gsub('&', '&amp;'):gsub('<', '&lt;'):gsub('>', '&gt;')
if mode == 2 then text = text:gsub('"', '&quot;'):gsub("'", '&#39;') end
editor.replace(first, last, text)
editor.set_selection(first, first + #text)
