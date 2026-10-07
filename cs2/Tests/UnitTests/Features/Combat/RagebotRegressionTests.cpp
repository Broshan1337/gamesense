#include <gtest/gtest.h>
#include <array>
#include <vector>
#include <utility>
#include <limits>
#include <optional>
#include <string_view>
#include <cstring>

#include <Features/Combat/AutoStop.h>
#include <Features/Combat/Autowall/Penetration.h>
#include <Features/Combat/HitboxGeometry.h>
#include <Features/Combat/ShotGeometry.h>
#include <Features/Combat/ShotWait.h>
#include <Features/Combat/SubtickShotWriter.h>
#include <GameClient/MultiPoint.h>
#include <GameClient/Entities/BaseWeapon.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <GameClient/Tracing/Tracing.h>

namespace {
Hitboxes::Entry capsule(float radius=2.0f) { return {0,0,{0,0,-2},{0,0,2},radius,false,false}; }
constexpr float identity[]{0,0,0,1};

struct Command {
    alignas(8) std::byte cmd[192]{}, base[160]{}, buttons[64]{}, angles[40]{};
    Command() {
        write(cmd,cs2::CUserCmd::kBaseMessageOffset,base);
        write(base,cs2::CUserCmd::BaseMessage::kButtonsPbOffset,buttons);
        write(base,cs2::CUserCmd::BaseMessage::kViewAnglesOffset,angles);
    }
    template<typename T> static void write(std::byte* p,int offset,T value) { std::memcpy(p+offset,&value,sizeof(value)); }
    template<typename T> static T read(std::byte* p,int offset) { T value{}; std::memcpy(&value,p+offset,sizeof(value)); return value; }
    cs2::CUserCmd* handle() { return reinterpret_cast<cs2::CUserCmd*>(cmd); }
    UserCmd view() { return UserCmd{handle()}; }
};

struct Weapon {
    float cone{0.02f};
    void updateAccuracyPenalty() const {}
    Optional<int> itemDefinitionIndex() const { return 7; }
    Optional<int> numBullets() const { return 1; }
    Optional<float> inaccuracy() const { return cone; }
    Optional<float> spread() const { return 0.0f; }
    Optional<float> recoilIndex() const { return 0.0f; }
};
struct Pawn { Weapon weapon; Weapon& getActiveWeapon() { return weapon; } };
struct NullDiagnostics { template<typename... Args> static void write(Args&&...) {} };
struct Context {
    struct Controller { Optional<int> tickBase() const { return 100; } };
    Controller localPlayerController() { return {}; }
    template<template<typename> typename T> auto make() { return T<Context>{*this}; }
};
using QuietWriter = SubtickShotWriter<Context, NullDiagnostics>;
}

TEST(RageSpread, ExactPredictionCannotMistakeUnavailableDataForZeroSpread)
{
    int context{};
    SpreadSolver<int> solver{context};
    SpreadSolver<int>::WeaponSpreadParams params{7,1,0.02f,0.01f,0};
    EXPECT_FALSE(solver.spreadOffset(123,params).hasValue());
    EXPECT_FALSE(solver.findSpreadCorrection({0,0,0},100,params).hasValue());
    params.inaccuracy=params.spread=0;
    EXPECT_TRUE(solver.spreadOffset(123,params).hasValue());
    EXPECT_TRUE(solver.findSpreadCorrection({0,0,0},100,params).hasValue());
    params.inaccuracy=std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(solver.spreadOffset(123,params).hasValue());
    EXPECT_FALSE(solver.estimatedSpreadOffset(123,params).hasValue());
}

TEST(RageSpread, EstimatedConeIsDeterministicBoundedAndNotIdenticallyZero)
{
    int context{}; SpreadSolver<int> solver{context};
    SpreadSolver<int>::WeaponSpreadParams params{7,1,0.1f,0.05f,0};
    float energy=0;
    for (int i=0;i<256;++i) {
        const auto a=solver.estimatedSpreadOffset(i,params);
        const auto b=solver.estimatedSpreadOffset(i,params);
        ASSERT_TRUE(a.hasValue()); ASSERT_TRUE(b.hasValue());
        EXPECT_FLOAT_EQ(a.value().x,b.value().x);
        EXPECT_FLOAT_EQ(a.value().y,b.value().y);
        const float length=a.value().x*a.value().x+a.value().y*a.value().y;
        EXPECT_LE(length,0.1501f*0.1501f);
        energy+=length;
    }
    EXPECT_GT(energy,0.5f);
    params.numBullets=8;
    EXPECT_FALSE(solver.estimatedSpreadOffset(1,params).hasValue());
}

TEST(RageSpread, IncreasingConeOrDistanceReducesMeasuredHitProbability)
{
    const auto shape=hitbox_geometry::from(capsule(),{100,0,0},identity);
    const auto hits=[&](float cone,float distance) {
        auto target=shape; target.origin.x=distance;
        int count=0;
        for (int i=0;i<256;++i) {
            const auto spread=spread_model::sample(i,cone,0);
            const auto ray=shot_geometry::normalized({1,spread.x,spread.y});
            count+=target.intersects({},ray);
        }
        return count;
    };
    EXPECT_GT(hits(0.005f,100),240);
    EXPECT_LT(hits(0.1f,100),80);
    EXPECT_GT(hits(0.02f,100),hits(0.02f,1000));
}

TEST(RageHitboxes, CapsuleHandlesTangentMissBehindEyeAndDegenerateSegment)
{
    auto shape=hitbox_geometry::from(capsule(),{10,0,0},identity);
    EXPECT_TRUE(shape.intersects({},{1,0,0}));
    EXPECT_TRUE(shape.intersects({0,2,0},{1,0,0}));
    EXPECT_FALSE(shape.intersects({0,2.1f,0},{1,0,0}));
    EXPECT_FALSE(shape.intersects({},{-1,0,0}));
    auto sphere=capsule(); sphere.mins=sphere.maxs={};
    shape=hitbox_geometry::from(sphere,{10,0,0},identity);
    EXPECT_TRUE(shape.intersects({},{1,0,0}));
    EXPECT_FALSE(shape.intersects({0,3,0},{1,0,0}));
}

TEST(RageHitboxes, OrientedBoxRespectsRotationAndTranslationOnlyFlag)
{
    Hitboxes::Entry box{4,0,{-4,-1,-1},{4,1,1},0,true,false};
    constexpr float quarterTurn[]{0,0,0.70710678f,0.70710678f};
    auto shape=hitbox_geometry::from(box,{10,0,0},quarterTurn);
    EXPECT_TRUE(shape.intersects({0,3,0},{1,0,0}));
    box.translationOnly=true;
    shape=hitbox_geometry::from(box,{10,0,0},quarterTurn);
    EXPECT_FALSE(shape.intersects({0,3,0},{1,0,0}));
    EXPECT_FALSE(shape.intersects({},{-1,0,0}));
}

TEST(RageMultipoint, UsesLocalHitboxCenterAndAlwaysIncludesCenterEvenAtZeroScale)
{
    auto entry=capsule(); entry.mins={4,0,-2}; entry.maxs={4,0,2};
    MultiPoint::Point points[MultiPoint::kMaxPoints];
    auto count=MultiPoint::generate(entry,{100,0,0},identity,0,{},0,false,points);
    ASSERT_EQ(count,1);
    EXPECT_TRUE(points[0].isCenter);
    EXPECT_FLOAT_EQ(points[0].position.x,104);
    const auto shape=hitbox_geometry::from(entry,{100,0,0},identity);
    count=MultiPoint::generate(entry,{100,0,0},identity,100,{},0,false,points);
    ASSERT_GT(count,1); ASSERT_LE(count,MultiPoint::kMaxPoints);
    for (int i=0;i<count;++i)
        EXPECT_TRUE(shape.intersects(points[i].position,{1,0,0}));
}

TEST(RageShotWait, DelaysEstimatedChanceButNeverOverridesDamageOrChance)
{
    shot_wait::State state;
    EXPECT_FALSE(state.ready(1,100,true,false,true,true,3));
    EXPECT_FALSE(state.ready(1,102,true,false,true,true,3));
    EXPECT_TRUE(state.ready(1,103,true,false,true,true,3));
    EXPECT_FALSE(state.ready(1,104,true,false,false,true,3));
    EXPECT_FALSE(state.ready(1,105,false,false,true,true,3));
    EXPECT_FALSE(state.ready(1,106,true,false,true,true,3));
    EXPECT_TRUE(state.ready(1,107,true,true,true,true,3));
}

TEST(RageShotWait, ResetsForTargetChangeAndClockRollback)
{
    shot_wait::State state;
    EXPECT_FALSE(state.ready(1,100,true,false,true,true,3));
    EXPECT_FALSE(state.ready(2,102,true,false,true,true,3));
    EXPECT_FALSE(state.ready(2,103,true,false,true,true,3));
    EXPECT_TRUE(state.ready(2,105,true,false,true,true,3));
    EXPECT_FALSE(state.ready(2,50,true,false,true,true,3));
    EXPECT_TRUE(state.ready(2,53,true,false,true,true,3));
    EXPECT_TRUE(state.ready(2,54,true,false,true,false,3));
}

TEST(RageAutoStop, ReplacesExistingMovementAndPreservesAttackInEveryBank)
{
    Command command;
    const auto attack=std::uint64_t{1};
    ASSERT_TRUE(command.view().pressButtonsBothBanks(auto_stop::forward|attack));
    command.view().setForwardMove(1);
    auto_stop::apply(command.view(),{200,0,0},0);
    EXPECT_NEAR(command.view().forwardMove().value(),-1,0.00001f);
    EXPECT_NEAR(command.view().leftMove().value(),0,0.00001f);
    for (const auto [buffer,offset]:std::array<std::pair<std::byte*,int>,4>{{
        {command.cmd,96},{command.cmd,104},{command.buttons,24},{command.buttons,32}}}) {
        const auto buttons=Command::read<std::uint64_t>(buffer,offset);
        EXPECT_EQ(buttons&auto_stop::mask,auto_stop::back);
        EXPECT_EQ(buttons&attack,attack);
    }
    auto_stop::apply(command.view(),{},0);
    EXPECT_EQ(command.view().buttonState1()&auto_stop::mask,0);
    EXPECT_FLOAT_EQ(command.view().forwardMove().value(),0);
}

TEST(RageAutoStop, CounterMovementUsesViewSpaceAndRejectsNonFiniteVelocity)
{
    Command command;
    auto_stop::apply(command.view(),{200,0,0},90);
    EXPECT_NEAR(command.view().leftMove().value(),1,0.0001f);
    EXPECT_NEAR(command.view().forwardMove().value(),0,0.0001f);
    auto_stop::apply(command.view(),{std::numeric_limits<float>::quiet_NaN(),0,0},0);
    EXPECT_NEAR(command.view().leftMove().value(),1,0.0001f);
}

namespace {
struct Slabs {
    std::vector<std::pair<float,float>> walls;
    Tracing::Result operator()(const cs2::Vector& start,const cs2::Vector& end,void*) const {
        Tracing::Result result{false,1,end,{},nullptr,true};
        const float direction=end.x-start.x;
        float nearest=1;
        for (auto [low,high]:walls) {
            if (start.x>low && start.x<high)
                return {true,0,start,{},nullptr,true};
            const float surface=direction>0 ? low : high;
            const float fraction=(surface-start.x)/direction;
            if (fraction>=0 && fraction<nearest) {
                nearest=fraction;
                result={true,fraction,{surface,0,0},{direction>0 ? -1.0f : 1.0f,0,0},nullptr,true};
            }
        }
        return result;
    }
};
}

TEST(RagePenetration, SeparatedWallsLoseDamagePerLayerWithoutCountingAirAsThickness)
{
    const auto separated=penetration::estimate({},{100,0,0},nullptr,nullptr,100,2,Slabs{{{10,12},{30,32}}});
    const auto solid=penetration::estimate({},{100,0,0},nullptr,nullptr,100,2,Slabs{{{10,32}}});
    ASSERT_TRUE(separated.hasValue()); ASSERT_TRUE(solid.hasValue());
    EXPECT_GT(separated.value(),solid.value());
    EXPECT_NEAR(separated.value(),((100-100*0.16f-5.625f-4.0f/24)*0.84f-5.625f-4.0f/24),0.001f);
}

TEST(RagePenetration, ThickWallsMissingTracesAndLowDamageFailClosed)
{
    EXPECT_FALSE(penetration::estimate({},{200,0,0},nullptr,nullptr,100,2,Slabs{{{10,150}}}).hasValue());
    EXPECT_FALSE(penetration::estimate({},{100,0,0},nullptr,nullptr,2,2,Slabs{{{10,12}}}).hasValue());
    EXPECT_FALSE(penetration::estimate({},{100,0,0},nullptr,nullptr,100,0,Slabs{}).hasValue());
    const auto invalid=[](auto&&,auto&&,void*) { return Tracing::Result{}; };
    EXPECT_FALSE(penetration::estimate({},{100,0,0},nullptr,nullptr,100,2,invalid).hasValue());
}

TEST(RageShotWriter, DisabledCompensationDoesNotClaimAnExactShot)
{
    Context context; Pawn pawn;
    Command command;
    const bool exact=QuietWriter{context}.run(command.handle(),pawn,10,20,0,0,0,false);
    EXPECT_FALSE(exact);
    EXPECT_FLOAT_EQ(command.view().viewPitch().value(),10);
    EXPECT_FLOAT_EQ(command.view().viewYaw().value(),20);
    pawn.weapon.cone=0;
    EXPECT_TRUE(QuietWriter{context}.run(command.handle(),pawn,10,20,0,0,0,false));
}

TEST(RageShotWriter, RejectsMalformedHistoryAndNonFiniteAnglesWithoutWriting) {
    Context context; Pawn pawn; Command command;
    command.view().setViewAngles(1,2);
    bool wrote=true;
    Command::write(command.cmd,cs2::CUserCmd::kInputHistorySizeOffset,33);
    EXPECT_FALSE(QuietWriter{context}.run(command.handle(),pawn,10,20,0,0,0,false,nullptr,nullptr,&wrote));
    EXPECT_FALSE(wrote);
    EXPECT_FLOAT_EQ(command.view().viewPitch().value(),1);
    Command::write(command.cmd,cs2::CUserCmd::kInputHistorySizeOffset,0);
    const float invalid=std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(QuietWriter{context}.run(command.handle(),pawn,invalid,20,0,0,0,false,nullptr,nullptr,&wrote));
    EXPECT_FALSE(wrote);
    EXPECT_FLOAT_EQ(command.view().viewYaw().value(),2);
}

TEST(RageShotWriter, NormalizesRecoilAdjustedAnglesAndReportsSuccessfulWrite) {
    Context context; Pawn pawn; Command command;
    bool wrote=false;
    EXPECT_FALSE(QuietWriter{context}.run(command.handle(),pawn,80,179,-20,-10,0,false,nullptr,nullptr,&wrote));
    EXPECT_TRUE(wrote);
    EXPECT_FLOAT_EQ(command.view().viewPitch().value(),89);
    EXPECT_NEAR(command.view().viewYaw().value(),-171,0.0001f);
}

namespace {
struct ReadySchema {
    bool available=true;
    std::optional<int> getFieldOffset(std::string_view, std::string_view name) const {
        if (!available) return {};
        if (name=="m_nNextPrimaryAttackTick") return 8;
        if (name=="m_flNextPrimaryAttackTickRatio") return 12;
        if (name=="m_bInReload") return 16;
        return {};
    }
};
struct ReadyContext { ReadySchema schema; ReadySchema& schemaSystem() { return schema; } };
}

TEST(RageWeaponReadiness, RespectsCooldownFractionReloadAndMissingSchema) {
    ReadyContext context;
    alignas(8) std::byte storage[32]{};
    BaseWeapon<ReadyContext> weapon{context,reinterpret_cast<cs2::C_CSWeaponBase*>(storage)};
    Command::write(storage,8,100);
    EXPECT_EQ(weapon.isReadyToFire(99),false);
    EXPECT_EQ(weapon.isReadyToFire(100),true);
    Command::write(storage,12,0.5f);
    EXPECT_EQ(weapon.isReadyToFire(100),false);
    EXPECT_EQ(weapon.isReadyToFire(101),true);
    Command::write(storage,16,true);
    EXPECT_EQ(weapon.isReadyToFire(101),false);
    context.schema.available=false;
    EXPECT_FALSE(weapon.isReadyToFire(101).hasValue());
}
