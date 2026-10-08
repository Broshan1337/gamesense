#pragma once
#include <optional>
namespace convar_override {
template<typename T> struct State {
    std::optional<T> original;
    template<typename Read, typename Write>
    void update(bool enabled, T value, Read&& read, Write&& write) noexcept {
        if (!enabled) { restore(write); return; }
        if (!original) original = read();
        if (original) write(value);
    }
    template<typename Write> void restore(Write&& write) noexcept {
        if (original && write(*original)) original.reset();
    }
};
}
