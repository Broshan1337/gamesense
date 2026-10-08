-- vector.lua - minimal 2D/3D vector + angle helpers for the Neversnooze lua framework.
-- Preloaded as the global `vec3` (constructor) and `angle` (angle math helpers).
--   vec3.new(x, y, z) / vec3(x, y, z) - lightweight userdata-free vectors

local vec3 = {}
vec3.__index = vec3

function vec3.new(x, y, z)
    return setmetatable({ x = x or 0.0, y = y or 0.0, z = z or 0.0 }, vec3)
end

setmetatable(vec3, {
    __call = function(_, x, y, z) return vec3.new(x, y, z) end,
})

function vec3.__add(a, b) return vec3.new(a.x + b.x, a.y + b.y, a.z + b.z) end
function vec3.__sub(a, b) return vec3.new(a.x - b.x, a.y - b.y, a.z - b.z) end
function vec3.__mul(a, b)
    if type(a) == "number" then return vec3.new(a * b.x, a * b.y, a * b.z) end
    if type(b) == "number" then return vec3.new(a.x * b, a.y * b, a.z * b) end
    return vec3.new(a.x * b.x, a.y * b.y, a.z * b.z)
end
function vec3.__unm(a) return vec3.new(-a.x, -a.y, -a.z) end
function vec3.__tostring(v) return string.format("(%.2f, %.2f, %.2f)", v.x, v.y, v.z) end

function vec3:length()
    return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)
end

function vec3:length2d()
    return math.sqrt(self.x * self.x + self.y * self.y)
end

function vec3:normalized()
    local len = self:length()
    if len < 1e-6 then return vec3.new() end
    return vec3.new(self.x / len, self.y / len, self.z / len)
end

function vec3:dot(o) return self.x * o.x + self.y * o.y + self.z * o.z end
function vec3:cross(o)
    return vec3.new(self.y * o.z - self.z * o.y, self.z * o.x - self.x * o.z, self.x * o.y - self.y * o.x)
end

function vec3:distance(o) return (self - o):length() end
function vec3:distance2d(o)
    local dx, dy = self.x - o.x, self.y - o.y
    return math.sqrt(dx * dx + dy * dy)
end

function vec3:lerp(o, t) return self + (o - self) * o end
function vec3.lerpTo(a, b, t)
    return vec3.new(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t)
end

-- angle helpers ---------------------------------------------------------------

local angle = {}

-- degrees -> radians and back (kept for readability in scripts)
local function rad(deg) return deg * math.pi / 180.0 end
local function deg(rad_) return rad_ * 180.0 / math.pi end

-- yaw angle (degrees) needed to look from `from` to `to`
function angle.yawTo(from, to)
    local dx = to.x - from.x
    local dy = to.y - from.y
    return deg(math.atan2(dy, dx))
end

-- pitch/yaw to aim `from` at `to` (degrees, CS convention: pitch negative = up)
function angle.to(from, to)
    local dx = to.x - from.x
    local dy = to.y - from.y
    local dz = to.z - from.z
    local hyp = math.sqrt(dx * dx + dy * dy)
    return deg(math.atan2(-dz, hyp)), deg(math.atan2(dy, dx))
end

-- direction vector for pitch/yaw angles in degrees
function angle.forward(pitch, yaw)
    local p, y = rad(pitch), rad(yaw)
    local cp = math.cos(p)
    return vec3.new(cp * math.cos(y), cp * math.sin(y), -math.sin(p))
end

-- shortest signed difference between two yaw angles (degrees)
function angle.normalizeDelta(delta)
    delta = delta % 360.0
    if delta > 180.0 then delta = delta - 360.0 end
    if delta < -180.0 then delta = delta + 360.0 end
    return delta
end

angle.rad = rad
angle.deg = deg

vec3.angle = angle
-- also expose as a standalone global (the preload names this file's return value `vec3`;
-- scripts that only need angle math can use `angle.*` directly)
angle_global_alias = angle

return vec3
