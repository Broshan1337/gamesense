local bit = {}

function bit.band(a, b)
    local result = 0
    local bitval = 1
    while a > 0 or b > 0 do
        if (a % 2 == 1) and (b % 2 == 1) then
            result = result + bitval
        end
        a = math.floor(a / 2)
        b = math.floor(b / 2)
        bitval = bitval * 2
    end
    return result
end

function bit.bor(a, b)
    local result = 0
    local bitval = 1
    while a > 0 or b > 0 do
        if (a % 2 == 1) or (b % 2 == 1) then
            result = result + bitval
        end
        a = math.floor(a / 2)
        b = math.floor(b / 2)
        bitval = bitval * 2
    end
    return result
end

function bit.bxor(a, b)
    local result = 0
    local bitval = 1
    while a > 0 or b > 0 do
        if (a % 2 == 1) ~= (b % 2 == 1) then
            result = result + bitval
        end
        a = math.floor(a / 2)
        b = math.floor(b / 2)
        bitval = bitval * 2
    end
    return result
end

function bit.bnot(a)
    return bit.bxor(a, 0xFFFFFFFF)
end

function bit.lshift(a, n)
    return a * (2 ^ n)
end

function bit.rshift(a, n)
    return math.floor(a / (2 ^ n))
end

function bit.arshift(a, n)
    if a >= 0x80000000 then
        a = a - 0x100000000
    end
    return math.floor(a / (2 ^ n))
end

function bit.rol(a, n)
    n = n % 32
    return bit.bor(bit.lshift(a, n), bit.rshift(a, 32 - n))
end

function bit.ror(a, n)
    n = n % 32
    return bit.bor(bit.rshift(a, n), bit.lshift(a, 32 - n))
end

function bit.bswap(a)
    return bit.bor(
        bit.lshift(bit.band(a, 0xFF), 24),
        bit.lshift(bit.band(a, 0xFF00), 8),
        bit.rshift(bit.band(a, 0xFF0000), 8),
        bit.rshift(bit.band(a, 0xFF000000), 24)
    )
end

function bit.tobit(a)
    if a >= 0x80000000 then
        return a - 0x100000000
    end
    return a
end

function bit.tohex(a, n)
    n = n or 8
    local fmt = "%0" .. n .. "x"
    return string.format(fmt, bit.tobit(a))
end

return bit
