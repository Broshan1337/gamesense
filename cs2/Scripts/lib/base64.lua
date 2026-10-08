-- base64.lua - small base64 encode/decode for the Neversnooze lua framework (preloaded global).
--   base64.encode(data) / base64.decode(text)

local base64 = {}

local chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

local reverse = {}
for i = 1, #chars do
    reverse[chars:sub(i, i)] = i - 1
end

function base64.encode(data)
    local out = {}
    local len = #data
    for i = 1, len, 3 do
        local a, b, c = data:byte(i, i + 2)
        local group = (a or 0) * 65536 + (b or 0) * 256 + (c or 0)
        local quad = {}
        quad[1] = chars:sub(math.floor(group / 262144) % 64 + 1, math.floor(group / 262144) % 64 + 1)
        quad[2] = chars:sub(math.floor(group / 4096) % 64 + 1, math.floor(group / 4096) % 64 + 1)
        quad[3] = b and chars:sub(math.floor(group / 64) % 64 + 1, math.floor(group / 64) % 64 + 1) or "="
        quad[4] = c and chars:sub(group % 64 + 1, group % 64 + 1) or "="
        out[#out + 1] = quad[1] .. quad[2] .. quad[3] .. quad[4]
    end
    return table.concat(out)
end

function base64.decode(text)
    text = text:gsub("[^" .. chars .. "=]", "")
    text = text:gsub("=+$", "")
    local out = {}
    for i = 1, #text, 4 do
        local a = reverse[text:sub(i, i)] or 0
        local b = reverse[text:sub(i + 1, i + 1)] or 0
        local c = text:sub(i + 2, i + 2) ~= "" and reverse[text:sub(i + 2, i + 2)]
        local d = text:sub(i + 3, i + 3) ~= "" and reverse[text:sub(i + 3, i + 3)]
        local group = a * 262144 + b * 4096 + (c or 0) * 64 + (d or 0)
        out[#out + 1] = string.char(math.floor(group / 65536) % 256)
        if c then
            out[#out + 1] = string.char(math.floor(group / 256) % 256)
        end
        if d then
            out[#out + 1] = string.char(group % 256)
        end
    end
    return table.concat(out)
end

return base64
