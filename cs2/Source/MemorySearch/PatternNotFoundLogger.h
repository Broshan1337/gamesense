#pragma once

#include <atomic>
#include <cassert>

#include <MemorySearch/BytePattern.h>
#include <Utils/NsStr.h>
#include <Utils/StringBuilder.h>
#include <Platform/SimpleMessageBox.h>

struct PatternNotFoundLogger {
    static void onPatternNotFound(BytePattern pattern) noexcept
    {
        // The box is modal-shelled out to zenity by SDL3, and a sealed pattern vault (or a
        // broken pattern set) fails lookups repeatedly - the 2026-10-04 session popped one
        // dialog per failed lookup for minutes. Show the FIRST failure's box, stay silent
        // after that (the assert below still fires in debug builds).
        static std::atomic<bool> boxShown{false};
        const bool showBox = !boxShown.exchange(true);
        StringBuilderStorage<500> storage;
        auto builder = storage.builder();

        builder.put("Failed to find pattern ");

        bool printedFirst = false;
        const auto wildcardChar{pattern.getWildcardChar()};
        for (const auto byte : pattern.raw()) {
            if (printedFirst)
                builder.put(' ');
            if (byte != wildcardChar) {
                if ((byte & 0xF0) == 0)
                    builder.put('0');
                builder.putHex(static_cast<unsigned char>(byte));
            } else {
                builder.put(byte);
            }

            printedFirst = true;
        }

        builder.put('\n');

        NS_STR(patternBrand, "Neversnooze");
        // The message box (with the pattern bytes) must show BEFORE the debug assert aborts -
        // assert only stringifies its expression, so the runtime pattern bytes would never
        // reach the journal otherwise (2026-09-25 update spent a session finding WHICH of
        // 162 patterns died because the assert fired first).
        if (showBox)
            SimpleMessageBox{}.showWarning(patternBrand, builder.cstring());

        assert(false && "Pattern needs to be updated!");
    }
};
