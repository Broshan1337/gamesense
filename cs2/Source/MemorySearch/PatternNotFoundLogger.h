#pragma once

#include <cassert>

#include <MemorySearch/BytePattern.h>
#include <Utils/NsStr.h>
#include <Utils/StringBuilder.h>
#include <Platform/SimpleMessageBox.h>

struct PatternNotFoundLogger {
    static void onPatternNotFound(BytePattern pattern) noexcept
    {
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
        SimpleMessageBox{}.showWarning(patternBrand, builder.cstring());

        assert(false && "Pattern needs to be updated!");
    }
};
