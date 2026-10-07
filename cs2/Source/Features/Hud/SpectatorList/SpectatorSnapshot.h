#pragma once

#include <cstdint>

#include <Utils/SpinLock.h>





namespace spectator_list
{

constexpr int kMaxNames = 8;

inline SpinLock lock;
inline char names[kMaxNames][40]{};
inline int nameCount = 0;


inline bool spectatingOthers = false;

inline void publish(const char (&newNames)[kMaxNames][40], int count, bool spectatingOthersNew) noexcept
{
    const std::lock_guard guard{lock};
    for (int i = 0; i < count; ++i) {
        std::size_t j = 0;
        for (; newNames[i][j] != '\0' && j < sizeof(names[0]) - 1; ++j)
            names[i][j] = newNames[i][j];
        names[i][j] = '\0';
    }
    nameCount = count;
    spectatingOthers = spectatingOthersNew;
}

struct Snapshot {
    char names[kMaxNames][40]{};
    int count = 0;
    bool spectatingOthers = false;
};

[[nodiscard]] inline Snapshot snapshot() noexcept
{
    const std::lock_guard guard{lock};
    Snapshot s;
    s.count = nameCount;
    s.spectatingOthers = spectatingOthers;
    for (int i = 0; i < nameCount; ++i) {
        std::size_t j = 0;
        for (; names[i][j] != '\0' && j < sizeof(s.names[0]) - 1; ++j)
            s.names[i][j] = names[i][j];
        s.names[i][j] = '\0';
    }
    return s;
}

}
