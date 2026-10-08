-- json.lua - vendored rxi/json.lua (MIT, https://github.com/rxi/json.lua), lightly trimmed.
-- Preloaded by the framework as the global `json` before every script body.
--   json.encode(value) / json.decode(string[, pos]) / json.null

local json = { _version = "0.1.2" }

local encode
local escape_char_map = {
  [ "\\" ] = "\\", [ '"' ] = '"', [ "\b" ] = "b", [ "\f" ] = "f",
  [ "\n" ] = "n", [ "\r" ] = "r", [ "\t" ] = "t",
}
local escape_char_map_inv = { [ "/" ] = "/" }
for k, v in pairs(escape_char_map) do escape_char_map_inv[v] = k end

local function escape_char(c)
  return "\\" .. (escape_char_map[c] or string.format("u%04x", c:byte()))
end

local function encode_nil() return "null" end
local function encode_number(n)
  if n ~= n or n == math.huge or n == -math.huge then return "null" end
  if n <= 2147483647 and n >= -2147483648 then return string.format("%.14g", n) end
  return string.format("%.17g", n)
end

local function encode_string(s)
  local res = {}
  res[#res+1] = '"'
  s = s:gsub('[%z\1-\31\\"]', escape_char)
  res[#res+1] = s
  res[#res+1] = '"'
  return table.concat(res)
end

local function encode_table(val, stack)
  local res = {}
  stack = stack or {}

  if stack[val] then error("circular reference") end
  stack[val] = true

  if rawget(val, 1) ~= nil or next(val) == nil then
    -- array
    local n = 0
    for k in pairs(val) do
      if type(k) ~= "number" then error("invalid table: mixed or invalid key types") end
      n = n + 1
    end
    for i = 1, #val do
      res[#res+1] = encode(val[i], stack)
    end
    stack[val] = nil
    return "[" .. table.concat(res, ",") .. "]"
  end

  -- object
  for k, v in pairs(val) do
    if type(k) ~= "string" then error("invalid table: mixed or invalid key types") end
    res[#res+1] = encode_string(k) .. ":" .. encode(v, stack)
  end
  stack[val] = nil
  return "{" .. table.concat(res, ",") .. "}"
end

local function encode_function() error("cannot encode function") end

encode = function(val, stack)
  local t = type(val)
  if t == "nil" then return encode_nil()
  elseif t == "number" then return encode_number(val)
  elseif t == "string" then return encode_string(val)
  elseif t == "boolean" then return val and "true" or "false"
  elseif t == "table" then return encode_table(val, stack)
  elseif t == "function" then return encode_function()
  end
  error("unexpected type '" .. t .. "'")
end

local escape_set = {}
for k in pairs(escape_char_map_inv) do escape_set[k] = true end

local function decode_unicode(surrogate1, surrogate2)
  local cp = tonumber(surrogate2, 16)
  if not surrogate2 then
    return utf8 and utf8.char(tonumber(surrogate1, 16)) or ""
  end
  local code = 0x10000 + (tonumber(surrogate1, 16) - 0xD800) * 0x400 + (tonumber(surrogate2, 16) - 0xDC00)
  return utf8 and utf8.char(code) or ""
end

local function decode_string(s)
  local res = {}
  local idx = 1
  while idx <= #s do
    local c = s:sub(idx, idx)
    if c == '"' then break end
    if c == "\\" then
      local nextc = s:sub(idx+1, idx+1)
      if escape_char_map_inv[nextc] then
        res[#res+1] = escape_char_map_inv[nextc]
        idx = idx + 2
      elseif nextc == "u" then
        local u1 = s:match("u%x%x%x%x", idx+1)
        local u2
        if u1 and tonumber(u1, 16) >= 0xD800 and tonumber(u1, 16) <= 0xDBFF then
          u2 = s:match("\\u%x%x%x%x", idx+6)
          res[#res+1] = decode_unicode(u1, u2)
          idx = idx + (u2 and 11 or 6)
        else
          res[#res+1] = s:sub(idx+1, idx+5)
          idx = idx + 5
        end
      else
        res[#res+1] = escape_char_map[nextc]
        idx = idx + 1
      end
    else
      res[#res+1] = c
      idx = idx + 1
    end
  end
  return table.concat(res)
end

local function expect(str, pos, delim)
    if str:sub(pos, pos) ~= delim then
        error("expected " .. delim .. " at " .. pos)
    end
    return pos + 1
end

local decode
local function decode_number(str, pos)
    local num = str:match("^-?%d+%.?%d*[eE]?[-+]?%d*", pos)
    if not num then error("invalid number") end
    return tonumber(num), pos + #num
end

decode = function(str, pos)
    pos = pos or 1
    if pos > #str then error("unexpected end of string") end
    local c = str:sub(pos, pos)

    if c == "{" then
        pos = expect(str, pos, "{")
        local obj = {}
        if str:sub(pos, pos) == "}" then return obj, pos + 1 end
        while true do
            local key
            key, pos = decode(str, pos)
            pos = expect(str, pos, ":")
            local val
            val, pos = decode(str, pos)
            obj[key] = val
            if str:sub(pos, pos) == "," then
                pos = pos + 1
            else
                pos = expect(str, pos, "}")
                return obj, pos
            end
        end
    elseif c == "[" then
        pos = expect(str, pos, "[")
        local arr = {}
        if str:sub(pos, pos) == "]" then return arr, pos + 1 end
        while true do
            local val
            val, pos = decode(str, pos)
            arr[#arr + 1] = val
            if str:sub(pos, pos) == "," then
                pos = pos + 1
            else
                pos = expect(str, pos, "]")
                return arr, pos
            end
        end
    elseif c == '"' then
        pos = pos + 1
        local start = pos
        while true do
            local sc = str:sub(pos, pos)
            if sc == "" then error("unterminated string") end
            if sc == "\\" then pos = pos + 1 end
            if sc == '"' then break end
            pos = pos + 1
        end
        local val = decode_string(str:sub(start, pos - 1))
        return val, pos + 1
    elseif str:sub(pos, pos + 3) == "true" then
        return true, pos + 4
    elseif str:sub(pos, pos + 4) == "false" then
        return false, pos + 5
    elseif str:sub(pos, pos + 3) == "null" then
        return json.null, pos + 5
    else
        return decode_number(str, pos)
    end
end

function json.encode(val) return ( encode(val) ) end
function json.decode(str) return decode(str) end
json.null = setmetatable({}, { __tostring = function() return "null" end })

return json
