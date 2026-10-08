#pragma once

#include <Utils/Trig.h>
















namespace spread_prediction
{




class UniformRandomStream {
public:
    
    void setSeed(int seed) noexcept
    {
        idum = seed < 0 ? seed : -seed;
        iy = 0;
    }

    
    [[nodiscard]] float randomFloat(float low, float high) noexcept
    {
        float value = kAM * static_cast<float>(generate());
        if (value > kRNMX)
            value = kRNMX;
        return value * (high - low) + low;
    }

private:
    
    [[nodiscard]] int generate() noexcept
    {
        if (idum <= 0 || iy == 0) {
            if (-idum < 1)
                idum = 1;
            else
                idum = -idum;
            for (int j = kNTAB + 7; j >= 0; --j) {
                const int k = idum / kIQ;
                idum = kIA * (idum - k * kIQ) - kIR * k;
                if (idum < 0)
                    idum += kIM;
                if (j < kNTAB)
                    iv[j] = idum;
            }
            iy = iv[0];
        }
        const int k = idum / kIQ;
        idum = kIA * (idum - k * kIQ) - kIR * k;
        if (idum < 0)
            idum += kIM;
        const int j = iy / kNDIV;
        iy = iv[j];
        iv[j] = idum;
        return iy;
    }

    static constexpr int kNTAB = 32;
    static constexpr int kIA = 16807;
    static constexpr int kIM = 2147483647;
    static constexpr int kIQ = 127773;
    static constexpr int kIR = 2836;
    static constexpr int kNDIV = 1 + (kIM - 1) / kNTAB;
    static constexpr float kAM = 1.0f / static_cast<float>(kIM);
    static constexpr float kRNMX = 1.0f - 1.2e-7f;

    int idum{0};
    int iy{0};
    int iv[kNTAB]{};
};



struct SpreadOffset {
    float x;
    float y;
};






[[nodiscard]] inline SpreadOffset offsetForBullet(int seed, int bulletIndex, float inaccuracy, float spread) noexcept
{
    UniformRandomStream stream;
    stream.setSeed(seed);

    SpreadOffset offset{0.0f, 0.0f};
    for (int i = 0; i <= bulletIndex; ++i) {
        
        
        
        const float inaccuracyFraction = stream.randomFloat(0.0f, 1.0f);
        const float inaccuracyAngle = stream.randomFloat(0.0f, trig::kTwoPi);
        const float spreadFraction = stream.randomFloat(0.0f, 1.0f);
        const float spreadAngle = stream.randomFloat(0.0f, trig::kTwoPi);

        if (i == bulletIndex) {
            const float inaccuracyRadius = inaccuracy * inaccuracyFraction;
            const float spreadRadius = spread * spreadFraction;
            offset.x = trig::cosine(spreadAngle) * spreadRadius + trig::cosine(inaccuracyAngle) * inaccuracyRadius;
            offset.y = trig::sine(spreadAngle) * spreadRadius + trig::sine(inaccuracyAngle) * inaccuracyRadius;
        }
    }
    return offset;
}

}
