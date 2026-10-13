#include <cstddef>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>
#include <UI/ImGui/SelfIntegrity.h>

// The chunked .text scan (SelfIntegrity.h tick) feeds the module .text through
// fnv1a in 512 KB slices and compares the wrapped hash against the baseline.
// That is only equivalent to the old single pass if FNV-1a resumes exactly:
// fnv1a(b, seed = fnv1a(a)) == fnv1a(a ++ b), at ANY split point. Pin it.
TEST(SelfIntegrityTest, Fnv1aResumesExactlyAtAnySplit)
{
    std::vector<std::byte> data(70'000);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<std::byte>((i * 31 + 7) & 0xFF);

    const auto whole = self_integrity::fnv1a(data.data(), data.size());
    ASSERT_NE(whole, 0xcbf29ce484222325ULL);

    for (const std::size_t split : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{13},
             std::size_t{512}, std::size_t{511}, std::size_t{65'000},
             std::size_t{69'999}, data.size()}) {
        const auto head = self_integrity::fnv1a(data.data(), split);
        const auto tail = self_integrity::fnv1a(data.data() + split, data.size() - split, head);
        EXPECT_EQ(tail, whole) << "resume broke at split " << split;
    }
}

TEST(SelfIntegrityTest, Fnv1aDetectsSingleByteDrift)
{
    std::vector<std::byte> data(4096);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<std::byte>(i & 0xFF);

    const auto baseline = self_integrity::fnv1a(data.data(), data.size());

    std::vector<std::byte> patched = data;
    patched[1234] ^= std::byte{0x01};

    const auto head = self_integrity::fnv1a(patched.data(), 1234);
    const auto chunkedDrift = self_integrity::fnv1a(patched.data() + 1234, patched.size() - 1234, head);

    EXPECT_NE(chunkedDrift, baseline);
    EXPECT_EQ(self_integrity::fnv1a(patched.data(), patched.size()), chunkedDrift);
}