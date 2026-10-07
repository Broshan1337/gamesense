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
        
        
        
        
        if (showBox)
            SimpleMessageBox{}.showWarning(patternBrand, builder.cstring());

        assert(false && "Pattern needs to be updated!");
    }
};
