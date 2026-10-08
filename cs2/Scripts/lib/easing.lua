-- easing.lua - Penner easing functions for the Neversnooze lua framework (preloaded global).
-- All functions map t in [0, 1] to an eased [0, 1]. Usage: easing.outCubic(t) etc.
-- in/out/inOut variants for: linear quad cubic quart quint sine expo circ back elastic bounce.

local pi = math.pi

local baseIn = {
    linear = function(t) return t end,
    quad = function(t) return t * t end,
    cubic = function(t) return t * t * t end,
    quart = function(t) return t * t * t * t end,
    quint = function(t) return t * t * t * t * t end,
    sine = function(t) return 1 - math.cos(t * pi * 0.5) end,
    expo = function(t) return t == 0 and 0 or math.pow(2, 10 * (t - 1)) end,
    circ = function(t) return 1 - math.sqrt(math.max(0.0, 1 - t * t)) end,
    back = function(t)
        local c1, c3 = 1.70158, 2.70158
        return c3 * t * t * t - c1 * t * t
    end,
    elastic = function(t)
        if t == 0 or t == 1 then return t end
        return -math.pow(2, 10 * (t - 1)) * math.sin((t * 10 - 10.75) * (2 * pi) / 3)
    end,
    bounce = function(t)
        local n1, d1 = 7.5625, 2.75
        if t < 1 / d1 then return n1 * t * t end
        if t < 2 / d1 then
            t = t - 1.5 / d1
            return n1 * t * t + 0.75
        end
        if t < 2.5 / d1 then
            t = t - 2.25 / d1
            return n1 * t * t + 0.9375
        end
        t = t - 2.625 / d1
        return n1 * t * t + 0.984375
    end,
}

local easing = {}

for name, fn in pairs(baseIn) do
    if name == "bounce" then
        -- the classic bounce formula IS the out variant
        easing.outBounce = fn
        easing.inBounce = function(t) return 1.0 - fn(1.0 - t) end
        easing.inOutBounce = function(t)
            if t < 0.5 then return (1.0 - fn(1.0 - t * 2.0)) * 0.5 end
            return fn(t * 2.0 - 1.0) * 0.5 + 0.5
        end
    else
        easing[name] = fn
        local out = function(t) return 1.0 - fn(1.0 - t) end
        local inOut = function(t)
            if t < 0.5 then return fn(t * 2.0) * 0.5 end
            return 1.0 - fn(2.0 - t * 2.0) * 0.5
        end
        easing["out" .. name:sub(1, 1):upper() .. name:sub(2)] = out
        easing["inOut" .. name:sub(1, 1):upper() .. name:sub(2)] = inOut
    end
end

return easing
