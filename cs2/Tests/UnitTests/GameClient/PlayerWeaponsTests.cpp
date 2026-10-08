#include <gtest/gtest.h>

#include <type_traits>
#include <GameClient/Entities/PlayerWeapons.h>

namespace {

template <typename> struct TestWeapon {
    bool present{};
    explicit operator bool() const noexcept { return present; }
};

struct InventoryContext {
    int resolvedHandles{};
    struct FakeEntitySystem {
        InventoryContext& context;
        cs2::CEntityInstance* getEntityFromHandle(cs2::CEntityHandle handle) const noexcept
        {
            ++context.resolvedHandles;
            return handle.value == 1 ? reinterpret_cast<cs2::CEntityInstance*>(1) : nullptr;
        }
    };
    struct FakeEntity {
        bool present;
        explicit operator bool() const noexcept { return present; }
        template <template <typename...> typename T>
        auto cast() const noexcept { return TestWeapon<InventoryContext>{present}; }
    };
    template <template <typename...> typename T, typename... Args>
    auto make(Args... args)
    {
        if constexpr (std::is_same_v<T<InventoryContext>, EntitySystem<InventoryContext>>)
            return FakeEntitySystem{*this};
        else
            return FakeEntity{((args != nullptr) && ...)};
    }
    template <typename T>
    auto make(std::nullptr_t) { return TestWeapon<InventoryContext>{false}; }
};

} // namespace

TEST(PlayerWeaponsTest, RejectsMalformedInventoryBeforeResolvingHandles)
{
    cs2::CEntityHandle handle{1};
    for (const int count : {-1, 0, 65, 1000000}) {
        InventoryContext context;
        cs2::CUtlVector<cs2::CEntityHandle> vector{count, &handle, 0, 0};
        const PlayerWeapons inventory{context, &vector};
        EXPECT_FALSE(inventory.has<TestWeapon>());
        EXPECT_FALSE(static_cast<bool>(inventory.get<TestWeapon>()));
        inventory.forEach([](auto&&) { FAIL() << "Invalid inventory was visited"; });
        EXPECT_EQ(context.resolvedHandles, 0);
    }
}

TEST(PlayerWeaponsTest, NullStorageDoesNotReportABomb)
{
    InventoryContext context;
    cs2::CUtlVector<cs2::CEntityHandle> vector{3, nullptr, 0, 0};
    EXPECT_FALSE((PlayerWeapons{context, &vector}.has<TestWeapon>()));
    EXPECT_EQ(context.resolvedHandles, 0);
}

TEST(PlayerWeaponsTest, EmptySlotsDoNotReportAWeaponAndValidInventoryDoes)
{
    InventoryContext context;
    cs2::CEntityHandle handles[]{{0}, {0}};
    cs2::CUtlVector<cs2::CEntityHandle> vector{2, handles, 0, 0};
    const PlayerWeapons inventory{context, &vector};
    EXPECT_FALSE(inventory.has<TestWeapon>());
    handles[1] = cs2::CEntityHandle{1};
    EXPECT_TRUE(inventory.has<TestWeapon>());
    EXPECT_TRUE(static_cast<bool>(inventory.get<TestWeapon>()));
}
