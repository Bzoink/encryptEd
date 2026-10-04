local tag = ui.input('HTML tag name (without brackets or attributes):', 'div')
if not tag:match('^[A-Za-z][A-Za-z0-9-]*$') then
    error('Enter a tag name beginning with an ASCII letter, using only letters, digits, and hyphens.')
end
local void = {area=true, base=true, br=true, col=true, embed=true, hr=true,
    img=true, input=true, link=true, meta=true, param=true, source=true, track=true, wbr=true}
if void[tag:lower()] then error('Void elements cannot wrap text.') end
local first, last = editor.selection()
if not first then first, last = 1, #editor.text() + 1 end
local text = editor.range(first, last)
text = '<' .. tag .. '>' .. text .. '</' .. tag .. '>'
editor.replace(first, last, text)
editor.set_selection(first, first + #text)
