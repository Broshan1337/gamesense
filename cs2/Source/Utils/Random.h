#pragma once

#include <cstdint>







class Random {
public:
    
    [[nodiscard]] static float floating(float lo, float hi) noexcept
    {
        
        
        const float unit = static_cast<float>(next() >> 40) / static_cast<float>(1u << 24);
        return lo + (hi - lo) * unit;
    }

    
    
    
    [[nodiscard]] static float normalClamped(float mean, float stddev, float lo, float hi) noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < 12; ++i)
            sum += floating(0.0f, 1.0f);
        float value = mean + stddev * (sum - 6.0f);
        if (value < lo)
            value = lo;
        if (value > hi)
            value = hi;
        return value;
    }

private:
    [[nodiscard]] static std::uint64_t next() noexcept
    {
        if (state == 0) {
            std::uint64_t z = reinterpret_cast<std::uintptr_t>(&state) + 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            state = z ^ (z >> 31);
            if (state == 0)
                state = 0x9E3779B97F4A7C15ull;
        }
        std::uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 0x2545F4914F6CDD1Dull;
    }

    inline static std::uint64_t state{0};
};
