#pragma once

#include <Utils/Trig.h>

// Replicates CS2's bullet-spread computation so a shot's deflection can be PREDICTED before it is
// fired - the core of "hit while moving" for the triggerbot (fire only when the deflected bullet
// lands) and the aimbot (compensate the aim by the predicted offset).
//
// This is the exact math reverse-engineered from libclient's FX_FireBullets spread path (the cone
// generator sub_1ADCC00): for each bullet the game draws four uniform randoms from a seeded stream
// and turns them into a tangent-plane offset (x, y). The RNG is Valve's own ran1 (the tier0
// CUniformRandomStream the game imports as RandomSeed/RandomFloat), reproduced here so we advance OUR
// OWN state instead of perturbing the game's global stream.
//
// What is NOT modelled yet (standard weapons only, which is every normal gun): the revolver/negev
// per-weapon tweaks and the weapon_accuracy_nospread deterministic spray-pattern branch. Both are
// special cases layered on top of the path below. Also OUT of scope here is where the SEED comes from
// - the game derives it as hash(round(pitch,0.5deg), round(yaw,0.5deg), shotTick); this module takes
// the seed as an input.
namespace spread_prediction
{

// Valve's ran1 - the Numerical-Recipes Park-Miller minimal generator with a Bays-Durham shuffle - is
// the algorithm behind tier0's RandomSeed/RandomFloat. Reproduced exactly so the sequence matches the
// game's for a given seed. Not thread-safe and not meant to be: one lives on the stack per prediction.
class UniformRandomStream {
public:
    // Matches CUniformRandomStream::SetSeed: store the negated magnitude so the first draw reseeds.
    void setSeed(int seed) noexcept
    {
        idum = seed < 0 ? seed : -seed;
        iy = 0;
    }

    // Matches CUniformRandomStream::RandomFloat.
    [[nodiscard]] float randomFloat(float low, float high) noexcept
    {
        float value = kAM * static_cast<float>(generate());
        if (value > kRNMX)
            value = kRNMX;
        return value * (high - low) + low;
    }

private:
    // Matches CUniformRandomStream::GenerateRandomNumber (the ran1 core).
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

// Tangent-plane deflection of a bullet, in the same units the weapon's inaccuracy and spread are in
// (added to the aim's forward vector to get the bullet's actual direction).
struct SpreadOffset {
    float x;
    float y;
};

// The spread offset the game will apply to bullet `bulletIndex` (0-based) of a shot with the given
// seed, weapon inaccuracy and spread. Reproduces sub_1ADCC00's per-bullet loop exactly: the stream is
// seeded once and advanced four draws per bullet, so bullet N's offset depends on every bullet before
// it - which is why the whole prefix is walked rather than jumped to. For a single-bullet weapon pass
// bulletIndex = 0.
[[nodiscard]] inline SpreadOffset offsetForBullet(int seed, int bulletIndex, float inaccuracy, float spread) noexcept
{
    UniformRandomStream stream;
    stream.setSeed(seed);

    SpreadOffset offset{0.0f, 0.0f};
    for (int i = 0; i <= bulletIndex; ++i) {
        // Draw order is fixed by the game: inaccuracy fraction, inaccuracy angle, spread fraction,
        // spread angle. All four are drawn every bullet even though only the current one is kept, to
        // keep the stream in lockstep with the game's.
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
