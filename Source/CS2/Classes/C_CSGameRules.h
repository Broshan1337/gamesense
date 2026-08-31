#pragma once

#include <CS2/Constants/RoundWinStatus.h>

namespace cs2
{

struct C_CSGameRules {
    using m_fRoundStartTime = float;
    using m_flRestartRoundTime = float;
    using m_iRoundTime = int;
    using m_iRoundWinStatus = RoundWinStatus;

    // m_bIsValveDS per the cs2-dumper schema dump of THIS Linux build (build 14178, dumped
    // 2026-08-26). The Windows reconstruction (FORFUTURETESTS/mytest) hardcodes 0xA4 for depot
    // 14169 - platform layouts differ, so the value is never copied across; re-dump instead of
    // trusting a stale number after a game update.
    static constexpr std::ptrdiff_t kIsValveDsOffset = 0x9C; // bool
};

}
