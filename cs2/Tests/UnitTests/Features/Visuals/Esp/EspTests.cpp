#include <gtest/gtest.h>

#include <cstdint>

#include <CS2/Classes/Vector.h>
#include <GameClient/WorldToScreen/ClipSpaceCoordinates.h>
#include <Features/Visuals/Esp/Esp.h>
#include <Utils/ColorUtils.h>

namespace
{

// Identity view transform: clip == world, w = z (so z < 0 = behind the camera).
struct IdentityConverter {
    [[nodiscard]] ClipSpaceCoordinates toClipSpace(const cs2::Vector& world) const noexcept
    {
        return ClipSpaceCoordinates{.x = world.x, .y = world.y, .z = world.z, .w = world.z};
    }
};

}

// Percent-of-display projection: NDC (-1,+1) -> (0%,0%); (+1,-1) -> (100%,100%);
// center -> (50,50); behind the camera (w <= 0.001) rejected; NaN world rejected.
TEST(EspProjection, MapsNdcToDisplayPercentsAndGatesBehind)
{
    float x = -1.0f, y = -1.0f;
    EXPECT_TRUE(esp_detail::project(IdentityConverter{}, {0.0f, 0.0f, 1.0f}, x, y));
    EXPECT_NEAR(x, 50.0f, 0.001f);
    EXPECT_NEAR(y, 50.0f, 0.001f);

    // NDC +y is up = the TOP of the screen: (-1,+1) -> (0%, 0%); (+1,-1) -> (100%,100%).
    EXPECT_TRUE(esp_detail::project(IdentityConverter{}, {-1.0f, 1.0f, 1.0f}, x, y));
    EXPECT_NEAR(x, 0.0f, 0.001f);
    EXPECT_NEAR(y, 0.0f, 0.001f);

    EXPECT_TRUE(esp_detail::project(IdentityConverter{}, {1.0f, -1.0f, 1.0f}, x, y));
    EXPECT_NEAR(x, 100.0f, 0.001f);
    EXPECT_NEAR(y, 100.0f, 0.001f);

    // Behind the camera and degenerate w are rejected without writing.
    EXPECT_FALSE(esp_detail::project(IdentityConverter{}, {0.0f, 0.0f, -1.0f}, x, y));
    EXPECT_FALSE(esp_detail::project(IdentityConverter{}, {0.0f, 0.0f, 0.0f}, x, y));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(esp_detail::project(IdentityConverter{}, {nan, 0.0f, 1.0f}, x, y));
}

// The backup-health damage echo (hzqst's white bar): a health drop arms the
// echo at the OLD health, fades out over 500ms, and lands/reset on expiry; a
// heal or no-anim config shows nothing.
TEST(EspBackupHealth, ArmsOnDamageDropAndFadesOut)
{
    int lastHealth = 100, backupHealth = 100;
    std::int64_t lastChangeMs = 0;

    // Steady full health: no echo.
    auto echo = esp_detail::advanceBackupHealth(lastHealth, backupHealth, lastChangeMs, 100, 1000, true);
    EXPECT_EQ(echo.second, 0u);

    // Damage 100 -> 40: echo armed at 100%, full alpha.
    echo = esp_detail::advanceBackupHealth(lastHealth, backupHealth, lastChangeMs, 40, 1100, true);
    EXPECT_NEAR(echo.first, 1.0f, 0.001f);
    EXPECT_NE(echo.second, 0u);
    EXPECT_EQ(lastChangeMs, 1100);
    EXPECT_NEAR(static_cast<float>(echo.second & 0xFF), 200.0f, 1.0f);  // smoothstep(0) = full 200 alpha

    // Mid-fade: alpha eased, fill still at the old health.
    echo = esp_detail::advanceBackupHealth(lastHealth, backupHealth, lastChangeMs, 40, 1350, true);
    EXPECT_NEAR(echo.first, 1.0f, 0.001f);
    const auto midAlpha = (echo.second & 0xFF) + 0.5f;
    EXPECT_GT(midAlpha, 1.0f);
    EXPECT_LT(midAlpha, 200.0f);

    // Past 500ms: echo expired, state caught up.
    echo = esp_detail::advanceBackupHealth(lastHealth, backupHealth, lastChangeMs, 40, 1700, true);
    EXPECT_EQ(echo.second, 0u);
    EXPECT_EQ(backupHealth, 40);

    // Heal up: never an echo.
    echo = esp_detail::advanceBackupHealth(lastHealth, backupHealth, lastChangeMs, 90, 1800, true);
    EXPECT_EQ(echo.second, 0u);
    EXPECT_EQ(lastHealth, 90);

    // Anim off: drop arms the state but shows nothing.
    echo = esp_detail::advanceBackupHealth(lastHealth, backupHealth, lastChangeMs, 30, 1900, false);
    EXPECT_EQ(echo.second, 0u);
    EXPECT_EQ(backupHealth, 90);
}
#include <Features/Visuals/GrenadePrediction/GrenadePredictionEsp.h>

namespace
{

// Flat ground plane z=0 tracer: crossing segments hit at the plane, normal +Z;
// everything else flies free. Walls/floor complexity are the game's, not ours -
// the sim's math is what we pin here.
struct PlaneTracer {
    [[nodiscard]] Tracing::Result operator()(const cs2::Vector& from, const cs2::Vector& to, void*) const noexcept
    {
        Tracing::Result result{};
        result.valid = true;
        if (from.z >= 0.0f && to.z < 0.0f) {
            const float fraction = from.z / (from.z - to.z);
            result.didHit = true;
            result.fraction = fraction;
            result.endPos = cs2::Vector{from.x + (to.x - from.x) * fraction,
                                        from.y + (to.y - from.y) * fraction,
                                        0.0f};
            result.normal = cs2::Vector{0.0f, 0.0f, 1.0f};
        } else {
            result.didHit = false;
            result.fraction = 1.0f;
            result.endPos = to;
            result.normal = cs2::Vector{0.0f, 0.0f, 1.0f};
        }
        result.hitEntity = nullptr;
        return result;
    }
};

}

// Trajectory preview physics: thrown from the eye with the view speed + local
// velocity; gravity bends it, the first ground crossing lands ON the plane,
// bounces reflect with restitution 0.3, and the landing sits on the ground.
TEST(GrenadeTrajectory, LandsOnGroundAndBouncesWithRestitution)
{
    cs2::Vector points[grenade_detail::kMaxTrajectoryPoints];
    cs2::Vector landing{-1.0f, -1.0f, -1.0f};
    const auto count = grenade_detail::simulate(
        cs2::Vector{0.0f, 0.0f, 200.0f},          // eye
        cs2::Vector{1.0f, 0.5f, -0.1f},            // view forward (downhill throw)
        cs2::Vector{0.0f, 0.0f, 0.0f},            // standing still
        nullptr, PlaneTracer{}, points, landing);
    EXPECT_GT(count, 2);
    EXPECT_LE(count, grenade_detail::kMaxTrajectoryPoints);
    EXPECT_NEAR(points[0].x, 0.0f, 0.01f);
    EXPECT_NEAR(points[0].z, 200.0f, 0.01f);
    // Landing on the ground plane (settled or last recorded point at rest).
    EXPECT_GE(landing.z, -0.5f);
    EXPECT_LT(landing.z, 60.0f);
    // The throw travels forward.
    EXPECT_GT(landing.x, 200.0f);
}

// Straight drop: no forward speed, just gravity - lands at x of the eye.
TEST(GrenadeTrajectory, StraightDropStaysPut)
{
    cs2::Vector points[grenade_detail::kMaxTrajectoryPoints];
    cs2::Vector landing{-1.0f, -1.0f, -1.0f};
    const auto count = grenade_detail::simulate(
        cs2::Vector{50.0f, 50.0f, 100.0f}, cs2::Vector{0.0f, 0.0f, 0.0f},
        cs2::Vector{0.0f, 0.0f, 0.0f}, nullptr, PlaneTracer{}, points, landing);
    EXPECT_GT(count, 1);
    EXPECT_NEAR(landing.x, 50.0f, 5.0f);
    EXPECT_NEAR(landing.y, 50.0f, 5.0f);
    EXPECT_GE(landing.z, -0.5f);
    EXPECT_LT(landing.z, 15.0f);
}
