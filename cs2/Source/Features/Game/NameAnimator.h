#pragma once

#include <cstring>

#include <Features/Game/ChatTools.h>
#include <Features/Game/NameAnimatorConfigVariables.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/NetworkGameClientPointer.h>
#include <GameClient/NetMessageFactory.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Random.h>
#include <Utils/VerifyConsole.h>























template <typename HookContext>
class NameAnimator {
public:
    explicit NameAnimator(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() noexcept
    {
        if (!GET_CONFIG_VAR(name_animator_vars::Enabled))
            return reset();

        
        
        
        
        
        
        
        
        if (!hookContext.localPlayerController().pawn())
            return reset();

        
        
        
        
        
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return reset();

        
        
        
        
        const int ticksPerFrame = kSpeedMax - GET_CONFIG_VAR(name_animator_vars::Speed) + 1;
        if (tickCounter < ticksPerFrame) {
            ++tickCounter;
            return;
        }
        tickCounter = 0;

        
        if (!hasText && !loadText())
            return;

        char frame[chat_tools::kBufferTextSize]{};
        bool done = false;
        switch (GET_CONFIG_VAR(name_animator_vars::Mode)) {
        case 1:
            done = glitchAllFrame(frame);
            break;
        case 2:
            marqueeFrame(frame);
            break;
        case 3:
            done = scrambleFrame(frame);
            break;
        case 4:
            done = decodeFrame(kBinaryCharset, frame);
            break;
        case 5:
            flickerFrame(frame);
            break;
        case 6:
            done = backwardsFrame(frame);
            break;
        case 7:
            spongebobFrame(frame);
            break;
        case 8:
            pulseFrame(frame);
            break;
        case 9:
            strobeFrame(frame);
            break;
        case 10:
            waveFrame(frame);
            break;
        case 11:
            crawlerFrame(frame);
            break;
        case 12:
            stormFrame(frame);
            break;
        case 13:
            nystagmusFrame(frame);
            break;
        case 14:
            emojiStrobeFrame(frame);
            break;
        case 15:
            flashbangFrame(frame);
            break;
        case 16:
            twitchFrame(frame);
            break;
        case 17:
            faceStormFrame(frame);
            break;
        case 18:
            superWaveFrame(frame);
            break;
        case 19:
            rloFlipFrame(frame);
            break;
        case 20:
            vaporwaveFrame(frame);
            break;
        case 21:
            invisibleChaosFrame(frame);
            break;
        case 22:
            zalgo2Frame(frame);
            break;
        default:
            done = typewriterFrame(frame);
            break;
        }

        
        
        if (done)
            resetText();
        else
            applyFrame(frame);
    }

    void onUnload() const noexcept
    {
        reset();
    }

private:
    
    
    void buildSequences() noexcept
    {
        seqCount = 0;
        const auto length = std::strlen(text);
        for (std::size_t i = 0; i < length && seqCount < kMaxSequences;) {
            std::size_t span = 1;
            while (i + span < length && (static_cast<unsigned char>(text[i + span]) & 0xC0) == 0x80)
                ++span;
            seqOffset[seqCount] = i;
            seqLength[seqCount] = span;
            ++seqCount;
            i += span;
        }
        
        
        for (int i = 0; i < seqCount; ++i) {
            lockStep[i] = i;
            permDisplay[i] = i;
        }
        for (int i = seqCount - 1; i > 0; --i) {
            const auto pick = static_cast<int>(Random::floating(0.0f, static_cast<float>(i + 1)));
            const int swap = permDisplay[i];
            permDisplay[i] = permDisplay[pick];
            permDisplay[pick] = swap;
        }
    }

    [[nodiscard]] bool loadText() noexcept
    {
        char raw[chat_tools::kBufferTextSize];
        if (!chat_tools::readSidecar(hookContext, chat_tools::kAnimatorBuffer, raw, sizeof(raw)))
            return false;
        static_cast<void>(chat_tools::sanitizeInto(raw, text, sizeof(text)));
        if (text[0] == '\0')
            return false;
        buildSequences();
        position = 0;
        holdLeft = 0;
        
        
        
        
        hasText = seqCount > 0;
        return hasText;
    }

    void resetText() noexcept
    {
        hasText = false; 
        position = 0;
        holdLeft = 0;
        shrinking = false;
    }

    void reset() const noexcept
    {
        
        tickCounter = 0;
        hasText = false;
        position = 0;
        holdLeft = 0;
        shrinking = false;
    }

    void renderFullText(char (&frame)[chat_tools::kBufferTextSize]) const noexcept
    {
        std::memcpy(frame, text, std::strlen(text) + 1);
    }

    
    
    
    
    [[nodiscard]] int chunk() const noexcept
    {
        const auto step = seqCount / 4;
        return step > 0 ? step : 1;
    }

    
    
    
    
    
    
    
    [[nodiscard]] bool glitchAllFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        frame[0] = '\0';
        constexpr float kGlitchChance = 0.5f;
        for (int i = 0; i < seqCount; ++i) {
            char glyph[8]{};
            if (Random::floating(0.0f, 1.0f) >= kGlitchChance) {
                std::memcpy(glyph, text + seqOffset[i], seqLength[i]);
            } else {
                const auto pick = static_cast<int>(Random::floating(0.0f, static_cast<float>(kGlitchCharsetSize)));
                glyph[0] = kGlitchCharset[pick];
            }
            std::strcat(frame, glyph);
        }
        return false;
    }

    [[nodiscard]] bool decodeFrame(const char* charset, char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (holdLeft > 0) {
            --holdLeft;
            renderFullText(frame);
            return holdLeft == 0; 
        }
        if (position > seqCount) {
            holdLeft = kHoldUpdates;
            renderFullText(frame);
            return false;
        }
        frame[0] = '\0';
        const auto charsetSize = static_cast<int>(std::strlen(charset));
        for (int i = 0; i < seqCount; ++i) {
            char glyph[8]{};
            if (i < position) {
                std::memcpy(glyph, text + seqOffset[i], seqLength[i]);
            } else {
                const auto pick = static_cast<int>(Random::floating(0.0f, static_cast<float>(charsetSize)));
                glyph[0] = charset[pick];
            }
            std::strcat(frame, glyph);
        }
        ++position;
        return false;
    }

    
    
    [[nodiscard]] bool scrambleFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (holdLeft > 0) {
            --holdLeft;
            renderFullText(frame);
            return holdLeft == 0;
        }
        if (position > seqCount) {
            holdLeft = kHoldUpdates;
            renderFullText(frame);
            return false;
        }
        if (position > 0) {
            
            for (int locked = 0; locked < chunk(); ++locked) {
                for (int attempt = 0; attempt < seqCount; ++attempt) {
                    const auto pick = static_cast<int>(Random::floating(0.0f, static_cast<float>(seqCount)));
                    if (permDisplay[pick] != pick) {
                        permDisplay[pick] = pick;
                        break;
                    }
                }
            }
        }
        frame[0] = '\0';
        for (int i = 0; i < seqCount; ++i) {
            const auto source = permDisplay[i];
            std::strncat(frame, text + seqOffset[source], seqLength[source]);
        }
        ++position;
        return false;
    }

    
    
    void flickerFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        frame[0] = '\0';
        constexpr float kFlickerChance = 0.22f;
        for (int i = 0; i < seqCount; ++i) {
            char glyph[8]{};
            if (Random::floating(0.0f, 1.0f) >= kFlickerChance) {
                std::memcpy(glyph, text + seqOffset[i], seqLength[i]);
            } else {
                const auto pick = static_cast<int>(Random::floating(0.0f, static_cast<float>(kGlitchCharsetSize)));
                glyph[0] = kGlitchCharset[pick];
            }
            std::strcat(frame, glyph);
        }
    }

    
    
    
    
    [[nodiscard]] bool backwardsFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (shrinking) {
            position -= chunk();
            if (position <= 0)
                return true;
            frame[0] = '\0';
            for (int i = position - 1; i >= 0; --i)
                std::strncat(frame, text + seqOffset[i], seqLength[i]);
            return false;
        }
        if (holdLeft > 0) {
            if (--holdLeft == 0)
                shrinking = true;
            frame[0] = '\0';
            for (int i = seqCount - 1; i >= 0; --i)
                std::strncat(frame, text + seqOffset[i], seqLength[i]);
            return false;
        }
        if (position >= seqCount) {
            position = seqCount;
            holdLeft = kHoldUpdates;
            frame[0] = '\0';
            for (int i = seqCount - 1; i >= 0; --i)
                std::strncat(frame, text + seqOffset[i], seqLength[i]);
            return false;
        }
        position += chunk();
        frame[0] = '\0';
        for (int i = position - 1; i >= 0; --i)
            std::strncat(frame, text + seqOffset[i], seqLength[i]);
        return false;
    }

    void copySequencesRange(char (&out)[chat_tools::kBufferTextSize], int from, int to) const noexcept
    {
        std::size_t write = 0;
        for (int i = from; i < to && i < seqCount; ++i) {
            if (write + seqLength[i] >= sizeof(out))
                break;
            std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
            write += seqLength[i];
        }
        out[write] = '\0';
    }

    void copySequences(char (&out)[chat_tools::kBufferTextSize], int visible) const noexcept
    {
        copySequencesRange(out, 0, visible);
    }

    
    
    void marqueeFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        frame[0] = '\0';
        for (int k = 0; k < seqCount; ++k) {
            const int index = (position + k) % seqCount;
            std::strncat(frame, text + seqOffset[index], seqLength[index]);
        }
        std::strcat(frame, " |");
        position = (position + chunk()) % seqCount;
    }

    
    
    
    [[nodiscard]] bool typewriterFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (shrinking) {
            position -= chunk();
            if (position <= 0)
                return true; 
            copySequences(frame, position);
            return false;
        }
        if (holdLeft > 0) {
            if (--holdLeft == 0)
                shrinking = true;
            renderFullText(frame);
            return false;
        }
        if (position >= seqCount) {
            position = seqCount; 
            holdLeft = kHoldUpdates;
            renderFullText(frame);
            return false;
        }
        position += chunk();
        copySequences(frame, position);
        return false;
    }

    

    
    
    void spongebobFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        int letterIndex = 0;
        const bool flip = (static_cast<int>(animationTick()) & 1) != 0;
        for (int i = 0; i < seqCount && write + 8 < sizeof(out); ++i) {
            char c = text[seqOffset[i]];
            if ((c & 0x80) == 0 && std::isalpha(static_cast<unsigned char>(c))) {
                const bool upper = ((letterIndex & 1) == 0) != flip;
                c = upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                          : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                ++letterIndex;
            }
            out[write++] = c;        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    
    
    void upsideDownFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr char kFrom[] = "abcdefghijklmnopqrstuvwxyz.,123456790";
        constexpr char kTo[] = "ɐqɔpǝɟƃɥıɾʞlɯuodbɹsʇnʌʍxʎzˎ'ƖᄅƐㄣᄅ6ㄥ0Ɩ";
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        for (int i = seqCount - 1; i >= 0 && write + 8 < sizeof(out); --i) {
            const char c = text[seqOffset[i]];
            const auto hit = std::strchr(kFrom, c);
            if (hit) {
                
                const auto index = hit - kFrom;
                std::size_t walk = 0;
                for (int k = 0; k < index; ++k)
                    walk += sequenceSpan(kTo + walk);
                const auto span = sequenceSpan(kTo + walk);
                std::memcpy(out + write, kTo + walk, span);
                write += span;
            } else {
                
                std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
                write += seqLength[i];
            }
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    

    
    
    
    void pulseFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr int kSegLen = 5; 
        const int cycle = seqCount + kSegLen;
        const int head = position % cycle;
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        for (int i = 0; i < seqCount && write + 8 < sizeof(out); ++i) {
            char c = text[seqOffset[i]];
            if ((c & 0x80) == 0 && std::isalpha(static_cast<unsigned char>(c))) {
                const int distance = (i - head + cycle) % cycle;
                c = distance < kSegLen ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                                       : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            out[write++] = c;
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
        position += chunk() > 2 ? chunk() : 2; 
    }

    
    
    void strobeFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        const int step = static_cast<int>(animationTick() % 3);
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        for (int i = 0; i < seqCount && write + 4 < sizeof(out); ++i) {
            const char c = text[seqOffset[i]];
            if (step == 0) {
                out[write++] = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            } else if (step == 1) {
                out[write++] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            } else {
                
                out[write++] = c;
                if (i + 1 < seqCount && write + 4 < sizeof(out))
                    out[write++] = ' ';
            }
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    
    
    
    
    void waveFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr int kWavelength = 6;
        constexpr int kStep = 2;
        const int tick = animationTick() * 2;
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        int letterIndex = 0;
        for (int i = 0; i < seqCount && write + 8 < sizeof(out); ++i) {
            char c = text[seqOffset[i]];
            if ((c & 0x80) == 0 && std::isalpha(static_cast<unsigned char>(c))) {
                const int height = ((letterIndex - tick) % kWavelength + kWavelength) % kWavelength;
                c = height < kWavelength / 2 ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                                             : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                ++letterIndex;
            }
            out[write++] = c;
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    
    
    
    
    
    void crawlerFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr const char* kHead = "███";
        constexpr const char* kTrail1 = "▓▓";
        constexpr const char* kTrail2 = "▒▒";
        constexpr const char* kTrail3 = "░░";
        constexpr int kWidth = 3;
        const int cycle = seqCount + kWidth;
        const int head = (position * 2) % cycle;
        char out[chat_tools::kBufferTextSize]{};
        std::size_t write = 0;
        for (int i = 0; i < seqCount && write + 16 < sizeof(out); ++i) {
            
            
            const int rel = (i - head + cycle) % cycle;
            const char* bar = rel == 0 || rel == cycle - 1 ? kHead
                : rel == 1 || rel == cycle - 2 ? kTrail1
                : rel == 2 || rel == cycle - 3 ? kTrail2
                : rel == 3 || rel == cycle - 4 ? kTrail3
                : nullptr;
            if (bar) {
                const auto barLen = std::strlen(bar);
                std::memcpy(out + write, bar, barLen);
                write += barLen;
            }
            std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
            write += seqLength[i];
        }
        out[write < sizeof(out) - 1 ? write : sizeof(out) - 1] = '\0';
        std::memcpy(frame, out, write + 1);
        position += chunk() > 1 ? chunk() : 1;
    }

    
    
    void stormFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr char kHeavyCharset[] = "█▓▒░#@%&$";
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        constexpr float kChurn = 0.4f;
        for (int i = 0; i < seqCount && write + 8 < sizeof(out); ++i) {
            char c = text[seqOffset[i]];
            if (Random::floating(0.0f, 1.0f) < kChurn) {
                const int kind = static_cast<int>(Random::floating(0.0f, 3.0f));
                if (kind == 0 && (c & 0x80) == 0 && std::isalpha(static_cast<unsigned char>(c)))
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)) ^ 0x20); 
                else if (kind == 1)
                    c = kHeavyCharset[static_cast<int>(Random::floating(0.0f, 9.0f)) % 9];
                
            }
            out[write++] = c;
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }


    
    void nystagmusFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        frame[0] = '\0';
        const int jitter = static_cast<int>(Random::floating(0.0f, 7.0f)); 
        for (int k = 0; k < jitter / 2 && std::strlen(frame) + 1 < sizeof(chat_tools::kBufferTextSize); ++k)
            std::strcat(frame, " ");
        std::strncat(frame, text, sizeof(chat_tools::kBufferTextSize) - std::strlen(frame) - 1);
    }

    
    
    void emojiStrobeFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        
        static constexpr const char* kEmoji[] = {
            "\xF0\x9F\x94\xA5", 
            "\xE2\x9A\xA1",     
            "\xF0\x9F\x92\x80", 
            "\xF0\x9F\x8C\x88", 
            "\xE2\x9C\xA8",     
            "\xF0\x9F\x92\xA5", 
            "\xF0\x9F\x91\xA0", 
            "\xF0\x9F\x8E\xAF", 
        };
        constexpr int kEmojiCount = static_cast<int>(sizeof(kEmoji) / sizeof(kEmoji[0]));
        frame[0] = '\0';
        std::strcat(frame, kEmoji[position % kEmojiCount]);
        std::strcat(frame, " ");
        std::strncat(frame, text, sizeof(chat_tools::kBufferTextSize) - std::strlen(frame) - 1);
        position = (position + 1) % kEmojiCount;
    }

    
    
    void flashbangFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr const char* kWall = "\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88"; 
        if (animationTick() & 1) {
            renderFullText(frame);
            return;
        }
        frame[0] = '\0';
        for (int i = 0; i < seqCount && std::strlen(frame) + 3 < sizeof(chat_tools::kBufferTextSize); ++i)
            std::strcat(frame, kWall);
    }

    
    
    
    void twitchFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        constexpr const char* kNoise[] = {"\xE2\x96\x93", "\xE2\x96\x92", "\xE2\x96\x91"}; 
        constexpr float kSwapChance = 0.35f;
        for (int i = 0; i < seqCount && write + 8 < sizeof(out); ++i) {
            if (Random::floating(0.0f, 1.0f) < kSwapChance) {
                const char* n = kNoise[static_cast<int>(Random::floating(0.0f, 3.0f)) % 3];
                const auto len = std::strlen(n);
                std::memcpy(out + write, n, len);
                write += len;
            } else {
                std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
                write += seqLength[i];
            }
        }
        out[write] = '\0';
        
        if (Random::floating(0.0f, 1.0f) < 0.35f && write > 4) {
            const std::size_t drop = sequenceSpan(out);
            std::memmove(out, out + drop, write - drop);
            write -= drop;
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    
    
    
    void faceStormFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        static constexpr const char* kFaces[] = {
            "( \xCD\xA1\xC2\xB0 \xCD\x9C\xCA\x96 \xCD\xA1\xC2\xB0)", 
            "\xC2\xAF\x5C\x5F\x28\xE3\x83\x84\x29\x5F\x2F\xC2\xAF",  
            "(>\xE2\x96\xBF<)",                                      
            "\xE0\xB2\xA0_\xE0\xB2\xA0",                             
            "(\xE2\x95\xAF\xC2\xB0\xE2\x96\xA1\xC2\xB0)\xE2\x95\xAF", 
        };
        constexpr int kFaceCount = static_cast<int>(sizeof(kFaces) / sizeof(kFaces[0]));
        frame[0] = '\0';
        
        const int left = static_cast<int>(Random::floating(0.0f, static_cast<float>(kFaceCount)));
        const int right = static_cast<int>(Random::floating(0.0f, static_cast<float>(kFaceCount)));
        std::strncat(frame, kFaces[left], sizeof(chat_tools::kBufferTextSize) / 3);
        std::strncat(frame, text, sizeof(chat_tools::kBufferTextSize) - std::strlen(frame) - 1);
        std::strncat(frame, kFaces[right], (sizeof(chat_tools::kBufferTextSize) - std::strlen(frame)) / 2 - 1);
    }

    
    
    
    
    void superWaveFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr char kFrom[] = "abdeghijklmopqrstuvwy";
        
        static constexpr const char* kTo[] = {
            "\xE1\xB5\x83", "\xE1\xB5\x87", "\xE1\xB5\x96", "\xE1\xB5\x89",
            "\xCA\xB8", "\xCA\xB0", "\xE1\xB5\x8F", "\xCA\xB2", "\xCB\xA1",
            "\xE1\xB5\x90", "\xE1\xB5\x91", "\xCA\xB0", "\xCA\x80", "\xCB\xA2",
            "\xE1\xB5x97", "\xCA\x99", "\xCA\x9F", "\xE1\xB5\x8D",
        };
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        int letterIndex = 0;
        const int tick = animationTick();
        for (int i = 0; i < seqCount && write + 8 < sizeof(out); ++i) {
            char c = text[seqOffset[i]];
            const bool lower = std::islower(static_cast<unsigned char>(c)) != 0;
            const char lc = std::tolower(static_cast<unsigned char>(c));
            if ((c & 0x80) == 0 && lc >= 'a' && lc <= 'r') {
                
                const bool small = ((letterIndex + tick) % 6) < 2;
                const auto super = kTo[lc - 'a'];
                if (small) {
                    const auto len = std::strlen(super);
                    std::memcpy(out + write, super, len);
                    write += len;
                } else {
                    out[write++] = lower ? c : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                ++letterIndex;
            } else {
                std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
                write += seqLength[i];
            }
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    
    
    
    void rloFlipFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (animationTick() & 1) {
            renderFullText(frame);
            return;
        }
        frame[0] = '\0';
        std::strcat(frame, "\xE2\x80\xAE"); 
        std::strncat(frame, text, sizeof(chat_tools::kBufferTextSize) - std::strlen(frame) - 1);
    }

    
    
    
    void vaporwaveFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        int letterIndex = 0;
        const int tick = animationTick();
        for (int i = 0; i < seqCount && write + 8 < sizeof(out); ++i) {
            const char c = text[seqOffset[i]];
            const bool lower = std::islower(static_cast<unsigned char>(c)) != 0;
            const char uc = lower ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            if ((c & 0x80) == 0 && uc >= '!' && uc <= '~') {
                
                const bool wide = ((letterIndex + tick) % 6) < 3;
                if (wide) {
                    const unsigned code = uc + 0xFEE0;
                    out[write++] = static_cast<char>(0xE0 | (code >> 12));
                    out[write++] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out[write++] = static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    out[write++] = c;
                }
                ++letterIndex;
            } else {
                std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
                write += seqLength[i];
            }
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    
    
    
    
    void invisibleChaosFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        static constexpr const char* kZeroWidth[] = {
            "\xE2\x80\x8B", 
            "\xE2\x80\x8C", 
            "\xE2\x80\x8D", 
            "\xEF\xBB\xBF", 
        };
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        for (int i = 0; i < seqCount && write + 8 < sizeof(out); ++i) {
            if (Random::floating(0.0f, 1.0f) < 0.4f) {
                const char* zw = kZeroWidth[static_cast<int>(Random::floating(0.0f, 4.0f)) & 3];
                const auto len = std::strlen(zw);
                std::memcpy(out + write, zw, len);
                write += len;
            }
            std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
            write += seqLength[i];
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    
    
    
    
    void zalgo2Frame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        static constexpr const char* kMarks[] = {
            "\xCC\x80", "\xCC\x81", "\xCC\x89", "\xCC\x82", "\xCC\x83",
            "\xCC\x88", "\xCC\x8C", "\xCC\x93", "\xCC\x94", "\xCD\x82",
        };
        constexpr int kMarkCount = 10;
        constexpr int kPeak = 6; 
        const int cycle = static_cast<int>(position % (kPeak * 2));
        const int intensity = cycle <= kPeak ? cycle : kPeak * 2 - cycle;
        ++position;
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        for (int i = 0; i < seqCount && write + 40 < sizeof(out); ++i) {
            std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
            write += seqLength[i];
            for (int k = 0; k < intensity; ++k) {
                const char* mark = kMarks[static_cast<int>(Random::floating(0.0f, static_cast<float>(kMarkCount)))];
                const auto len = std::strlen(mark);
                std::memcpy(out + write, mark, len);
                write += len;
            }
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    void applyFrame(const char* frame) const noexcept
    {
        
        
        char safe[chat_tools::kBufferTextSize];
        static_cast<void>(chat_tools::sanitizeInto(frame, safe, sizeof(safe)));
        if (safe[0] == '\0')
            return;

        
        
        
        
        
        
        
        if (GET_CONFIG_VAR(name_animator_vars::DirectSend)) {
            const NetworkGameClientPointer clientPointer{};
            const char* reason = "no network client";
            const bool sent = clientPointer
                && net_messages::sendSingleSetConVar(clientPointer.get(), "name", safe, "Name Animator", &reason);
            if (sent)
                return;
            
            
            VerifyConsole::write(4.0f, "anim", "direct rename failed (%s) - setinfo fallback", reason);
        }

        
        
        
        
        
        if (!nameFlagsPatched) {
            nameFlagsPatched = hookContext.template make<CvarSystem>().patchUserInfoFlag("name");
            if (!nameFlagsPatched)
                return; 
        }

        char command[chat_tools::kBufferTextSize + 32]{"setinfo name \""};
        std::memcpy(command + 14, safe, std::strlen(safe) + 1);
        const auto length = std::strlen(command);
        command[length] = '"';
        command[length + 1] = '\0';
        hookContext.template make<EngineCommandExecutor>().execute(command);
    }

    
    [[nodiscard]] static std::size_t sequenceSpan(const char* s) noexcept
    {
        std::size_t span = 1;
        while ((static_cast<unsigned char>(s[span]) & 0xC0) == 0x80)
            ++span;
        return span;
    }

    
    
    [[nodiscard]] static int animationTick() noexcept
    {
        return ++frameParity;
    }

    static constexpr int kMaxSequences = 96;
    static constexpr int kMarqueeWidth = 12;
    static constexpr int kHoldUpdates = 8; 
    static constexpr char kGlitchCharset[] = "!<>-_\\/[]{}=+*^?#____";
    static constexpr char kBinaryCharset[] = "01";
    
    
    
    static constexpr int kSpeedMax = 64;
    static constexpr int kGlitchCharsetSize = static_cast<int>(sizeof(kGlitchCharset)) - 1;

    
    inline static int tickCounter{0};
    inline static int position{0};
    inline static int holdLeft{0};
    inline static bool shrinking{false};
    inline static bool hasText{false};
    inline static char text[chat_tools::kBufferTextSize]{};
    inline static std::size_t seqOffset[kMaxSequences]{};
    inline static std::size_t seqLength[kMaxSequences]{};
    inline static int lockStep[kMaxSequences]{};
    inline static int permDisplay[kMaxSequences]{}; 
    inline static int seqCount{0};
    inline static int frameParity{0};  
    inline static bool nameFlagsPatched{false}; 

    HookContext& hookContext;
};
