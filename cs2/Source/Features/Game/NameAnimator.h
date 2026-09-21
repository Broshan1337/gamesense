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

// NAME ANIMATOR: drives the LIVE in-game name frame by frame through the `setinfo` rename path
// (the same USERINFO-flag mechanism applyFakeName uses - applyFrame patches the name cvar's
// flags itself once per session, like ChatTools::ensureNameFlagsPatched; the original header
// claimed this patch existed here but it NEVER did - the reason every mode read as frozen).
//
// The text comes from the user's own sidecar buffer (<configDir>/name_animator.txt, the
// "Animate Text" row) - anything they type is the animation source.
//
// Modes (all operate on UTF-8 character SEQUENCES, so multi-byte names never split mid-glyph):
//   Typewriter - reveals one character per frame, holds the full text, restarts.
//   Glitch     - the WHOLE name glitches at once: every glyph independently flips between the
//                real character and a glitch symbol every frame, never stable, never crawling.
//   Marquee    - the text scrolls through a fixed-width window, looping forever.
//   Binary     - decode with a 0/1 noise charset - pure matrix flavor.
//   Flicker    - the full text renders but random characters keep swapping to glitch chars -
//                never stable, always alive.
//   Backwards  - reveals from the END of the name backwards.
//
// Cadence: one frame every `Speed` game ticks. Every frame is a REAL mid-match rename (userinfo
// update) - the default ~8 ticks keeps that at ~8 renames/sec, well within what a server
// accepts. The rename is session-only: a reconnect re-reads the Steam persona, and re-enabling
// the animator takes over again.
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

        // SESSION GATE (crash fix, 2026-09-12): BOTH map-join crashes of the day decode into
        // run() (the 20:19 one directly: glibc malloc-arena lock with a garbage arena pointer
        // under our return address, CreateMove thread, map join). The curtime guard below
        // cannot see this window: curtime is still the OLD map's large value while the client
        // session underneath is mid-teardown, so the animator kept driving `setinfo` through
        // an engine command buffer being rebuilt (the 2026-09-07 DeleteSceneObject crash
        // class, second appearance). A resolvable local player controller pawn is the
        // readiness signal that cannot lie: it is null exactly when there is no live session.
        if (!hookContext.localPlayerController().pawn())
            return reset();

        // MAP-CHANGE GUARD (crash fix): renames fired during scene teardown raced the engine's
        // DeleteSceneObject and took the game down (sceneobject_methods.cpp:1212 assert,
        // !IsDeleted double-delete). curtime resets on every map load, so this pauses the
        // animator through the entire teardown/rebuild window - the project's standard
        // schema-readiness idiom.
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return reset();

        // The Speed slider reads as SPEED (high = fast): ticks-per-frame is the inverted value.
        // CRITICAL (2026-09-12 lesson): the scale must be (kSpeedMax - Speed), NOT
        // (kSpeedMin + kSpeedMax - Speed) - the +kSpeedMin offset silently shifted the whole
        // range when kSpeedMax was raised (Speed 62 became ~1 rename/sec = "nothing animates").
        const int ticksPerFrame = kSpeedMax - GET_CONFIG_VAR(name_animator_vars::Speed) + 1;
        if (tickCounter < ticksPerFrame) {
            ++tickCounter;
            return;
        }
        tickCounter = 0;

        // Reload the text once per animation loop so sidecar edits are picked up live.
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

        // done = the hold just finished; the next tick reloads the text and restarts. No frame
        // is applied on this tick, so the name simply stays at its last state.
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
    // Splits the text into UTF-8 sequences (a sequence starts at a non-continuation byte and
    // spans its continuation bytes), so slicing never cuts a multi-byte glyph in half.
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
        // Sequential lock order for the left-to-right decodes; a Fisher-Yates shuffle for the
        // scramble's starting arrangement (real letters, wrong places).
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
        // THE 2026-09-12 GLITCH-FREEZE BUG: this flag was never set true, so run() reloaded the
        // text EVERY animation frame - which resets position/holdLeft first, so the decode modes
        // could never progress past their first glyph (each frame re-locked from scratch, reading
        // as "stops / forgets the rest of the name").
        hasText = seqCount > 0;
        return hasText;
    }

    void resetText() noexcept
    {
        hasText = false; // reload + restart the animation on the next tick
        position = 0;
        holdLeft = 0;
        shrinking = false;
    }

    void reset() const noexcept
    {
        // Reset is const-context (called from the enabled-gate); the mutable state is static.
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

    // Progress per frame: a QUARTER of the name per frame. The server coalesces rapid
    // userinfo updates, so the visible rename rate is only a few per second - a 1-2 letter
    // step per frame is invisible (the reason pulse/marquee/backwards/typewriter read as
    // frozen in v1/v2). Big fraction-of-name jumps are what actually read as animation.
    [[nodiscard]] int chunk() const noexcept
    {
        const auto step = seqCount / 4;
        return step > 0 ? step : 1;
    }

    // Hacker decode: unrevealed positions flicker the charset while real characters lock in
    // left to right.
    // GLITCH: the full name glitches simultaneously - each glyph flips between the real
    // character and a glitch symbol every frame (user verdict 2026-09-19: the old left-to-right
    // decode crawl read as "crawling the name"; the effect should cover the whole name at once).
    // The per-frame delta is large by construction, so the server's rename coalescing cannot
    // eat it. Loops forever - never "done".
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
            return holdLeft == 0; // done after the last hold frame
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

    // Scramble: the name's OWN letters shown in a shuffled order that sorts back into place,
    // one slot per frame. No noise characters - it reads as an anagram resolving into the name.
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
            // snap chunk() random not-yet-correct slots to their real letters per frame
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

    // The full name, but every character has a chance to be swapped for a glitch glyph THIS
    // frame - never stable, loops forever.
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

    // BACKWARDS: the name typed in REVERSE - "hello" grows as "o", "oh", "olle", "olleh".
    // A mirrored build is unmistakably different from the plain name at every step (the v2
    // version assembled the forward name from its tail, which is indistinguishable from the
    // plain name for repetitive text like "lolololololol" - the reason it read as dead).
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

    // MARQUEE: the name spins - the whole name slides left by a QUARTER of its length each
    // frame, and a " |" marks the wrap seam so rotation is visible even on repetitive text.
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

    // TYPEWRITER: types UP in big chunk steps, holds, then erases back down and restarts -
    // the chunk is a QUARTER of the name so even 1 visible rename/sec shows 4 distinct
    // typed stages ("l", "lolo", "lololol", "lololololo...").
    [[nodiscard]] bool typewriterFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (shrinking) {
            position -= chunk();
            if (position <= 0)
                return true; // fully collapsed - restart from nothing
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
            position = seqCount; // clamp chunk overshoot
            holdLeft = kHoldUpdates;
            renderFullText(frame);
            return false;
        }
        position += chunk();
        copySequences(frame, position);
        return false;
    }

    // ---- annoying/chaotic modes (2026-09-12 batch) -------------------------------------

    // Mocking Spongebob: aLtErNaTiNg CaPsE, the parity flips every frame so the case pattern
    // keeps sliding - reads as relentless "sArCaSm".
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

    // Upside down: per-glyph flip map for ASCII, multi-byte glyphs become their vibe kaomoji.
    // The map is lossy on purpose - it is the joke.
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
                // map into the parallel string (kTo holds multi-byte glyphs)
                const auto index = hit - kFrom;
                std::size_t walk = 0;
                for (int k = 0; k < index; ++k)
                    walk += sequenceSpan(kTo + walk);
                const auto span = sequenceSpan(kTo + walk);
                std::memcpy(out + write, kTo + walk, span);
                write += span;
            } else {
                // multi-byte or unmapped: copy as-is (sequence spans its continuations)
                std::memcpy(out + write, text + seqOffset[i], seqLength[i]);
                write += seqLength[i];
            }
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    // ---- REAL ANIMATIONS (2026-09-12 v2 batch - continuous motion, never a static name) --

    // PULSE: a bright "caps segment" JUMPS around the name - a quarter of the name per frame
    // so even at the server's visible rename rate the highlight visibly teleports. (Moving
    // 2 letters/frame was swallowed by the server's rename coalescing - the v2 bug.)
    void pulseFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr int kSegLen = 5; // letters lit at once
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
        position += chunk() > 2 ? chunk() : 2; // the segment jumps at least 2 letters per frame
    }

    // STROBE: hard cuts between three completely different renderings of the name - SHOUTING
    // / whisper / spaced out. Maximum annoyance through pure contrast, zero subtle.
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
                // spaced: every letter gets breathing room
                out[write++] = c;
                if (i + 1 < seqCount && write + 4 < sizeof(out))
                    out[write++] = ' ';
            }
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    // WAVE: letters rise and fall through case height in a traveling sine - lowercase is the
    // trough, uppercase the crest, so a smooth hump sweeps the name forever. The tick is
    // snapshotted ONCE per frame (calling the clock per letter advanced it seqCount times and
    // froze the wave shape - the v1 bug) and steps 2 letters per frame for visibility.
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

    // CRAWLER: a bright scanner head with a fading trail sweeps through the name - the head
    // is "███", the trail "▓▓" → "▒▒" → "░░" (block-glyph gradient), and the whole assembly
    // JUMPS a quarter of the name per frame so the sweep is visible even at the server's
    // rename coalescing rate. No dead frames: the cycle is seqCount + bar width, so the bar
    // re-enters left the moment it exits right.
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
            // the assembly covers virtual positions head, head-1 (trail1), head-2 (trail2),
            // head-3 (trail3) - all wrapping
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

    // STORM: heavy random churn - every frame ~40% of letters swap between case + glitch
    // symbols + fullwidth block glyphs. The name is never readable for two consecutive frames.
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
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)) ^ 0x20); // flip case
                else if (kind == 1)
                    c = kHeavyCharset[static_cast<int>(Random::floating(0.0f, 9.0f)) % 9];
                // kind == 2: keep the letter but it will likely differ next frame anyway
            }
            out[write++] = c;
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

// NYSTAGMUS: the whole name jitters left/right by 0-3 spaces EVERY frame - the eye keeps
    // trying to lock focus on it and cannot. This is the genuinely eye-scratching one.
    void nystagmusFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        frame[0] = '\0';
        const int jitter = static_cast<int>(Random::floating(0.0f, 7.0f)); // 0..3 spaces (2 = one space char)
        for (int k = 0; k < jitter / 2 && std::strlen(frame) + 1 < sizeof(chat_tools::kBufferTextSize); ++k)
            std::strcat(frame, " ");
        std::strncat(frame, text, sizeof(chat_tools::kBufferTextSize) - std::strlen(frame) - 1);
    }

    // EMOJI STROBE: a big bright emoji prefix slams through a cycle every frame - the name
    // gets flanked by a different loud glyph each rename. The scoreboard color-band effect.
    void emojiStrobeFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        // single-codepoint emoji, 4-byte UTF-8 each - render reliably in CS2 names
        static constexpr const char* kEmoji[] = {
            "\xF0\x9F\x94\xA5", // fire
            "\xE2\x9A\xA1",     // lightning (3 bytes)
            "\xF0\x9F\x92\x80", // skull
            "\xF0\x9F\x8C\x88", // rainbow
            "\xE2\x9C\xA8",     // sparkles
            "\xF0\x9F\x92\xA5", // collision
            "\xF0\x9F\x91\xA0", // eye
            "\xF0\x9F\x8E\xAF", // dart
        };
        constexpr int kEmojiCount = static_cast<int>(sizeof(kEmoji) / sizeof(kEmoji[0]));
        frame[0] = '\0';
        std::strcat(frame, kEmoji[position % kEmojiCount]);
        std::strcat(frame, " ");
        std::strncat(frame, text, sizeof(chat_tools::kBufferTextSize) - std::strlen(frame) - 1);
        position = (position + 1) % kEmojiCount;
    }

    // FLASHBANG: hard alternation between the name and a full solid block bar - the
    // scoreboard slot strobes between "text" and "wall". Zero between-frames.
    void flashbangFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr const char* kWall = "\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88"; // █████
        if (animationTick() & 1) {
            renderFullText(frame);
            return;
        }
        frame[0] = '\0';
        for (int i = 0; i < seqCount && std::strlen(frame) + 3 < sizeof(chat_tools::kBufferTextSize); ++i)
            std::strcat(frame, kWall);
    }

    // TWITCH: the name keeps CONTRACTING - a random 1-3 glyphs are swapped for noise blocks
    // AND the whole string randomly shifts left by a character, so it keeps "trying" to hold
    // still and failing.
    void twitchFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        char out[chat_tools::kBufferTextSize];
        std::size_t write = 0;
        constexpr const char* kNoise[] = {"\xE2\x96\x93", "\xE2\x96\x92", "\xE2\x96\x91"}; // ▓ ▒ ░
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
        // the shift: with 35% odds drop the leading glyph (the name twitches left)
        if (Random::floating(0.0f, 1.0f) < 0.35f && write > 4) {
            const std::size_t drop = sequenceSpan(out);
            std::memmove(out, out + drop, write - drop);
            write -= drop;
        }
        out[write] = '\0';
        std::memcpy(frame, out, write + 1);
    }

    // FACE STORM: kaomoji faces spawn around the name every frame - the face set and their
    // positions are re-rolled each frame, so the name is permanently surrounded by a swarm
    // of staring ( ͡° ͜ʖ ͡°)-style creatures that never hold still.
    void faceStormFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        static constexpr const char* kFaces[] = {
            "( \xCD\xA1\xC2\xB0 \xCD\x9C\xCA\x96 \xCD\xA1\xC2\xB0)", // lenny
            "\xC2\xAF\x5C\x5F\x28\xE3\x83\x84\x29\x5F\x2F\xC2\xAF",  // ¯\_(ツ)_/¯
            "(>\xE2\x96\xBF<)",                                      // (>𐌏<)
            "\xE0\xB2\xA0_\xE0\xB2\xA0",                             // ಠ_ಠ
            "(\xE2\x95\xAF\xC2\xB0\xE2\x96\xA1\xC2\xB0)\xE2\x95\xAF", // (╯°□°)╯
        };
        constexpr int kFaceCount = static_cast<int>(sizeof(kFaces) / sizeof(kFaces[0]));
        frame[0] = '\0';
        // left face + name + right face, re-rolled every frame; guard the buffer (faces are long)
        const int left = static_cast<int>(Random::floating(0.0f, static_cast<float>(kFaceCount)));
        const int right = static_cast<int>(Random::floating(0.0f, static_cast<float>(kFaceCount)));
        std::strncat(frame, kFaces[left], sizeof(chat_tools::kBufferTextSize) / 3);
        std::strncat(frame, text, sizeof(chat_tools::kBufferTextSize) - std::strlen(frame) - 1);
        std::strncat(frame, kFaces[right], (sizeof(chat_tools::kBufferTextSize) - std::strlen(frame)) / 2 - 1);
    }

    // SUPER WAVE: each letter cycles through SUPERSCRIPT forms (ˡᵒˡ - tiny glyphs) with a
    // traveling phase - a "size wave" of small letters rolling through the name. Chars
    // without a superscript form (most) stay normal. Charset sourced from the MC addon's
    // EconValues unicode taxonomy (superscript No-category glyphs).
    void superWaveFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        constexpr char kFrom[] = "abdeghijklmopqrstuvwy";
        // lowercase superscripts (No category - the MC list proved these are filter-safe)
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
                // wave: 2 of every 6 letters render superscript, the phase travels
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

    // RLO FLIP: alternates the plain name and the name prefixed with U+202E (RIGHT-TO-LEFT
    // OVERRIDE, from the MC addon's invisible-character class) - everything after the mark
    // renders MIRRORED, so the name flips direction every rename.
    void rloFlipFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (animationTick() & 1) {
            renderFullText(frame);
            return;
        }
        frame[0] = '\0';
        std::strcat(frame, "\xE2\x80\xAE"); // U+202E
        std::strncat(frame, text, sizeof(chat_tools::kBufferTextSize) - std::strlen(frame) - 1);
    }

    // VAPORWAVE WAVE: letters morph between NORMAL and FULLWIDTH forms (ＮＯＲＭＡＬ -
    // the aesthetic) with a traveling phase, sourced from the MC addon's fullwidth class
    // (EconValues UNICODE_DIGITS / CompletionCrash "ａ"). A wide/tall wave sweeps the name.
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
                // fullwidth form = ASCII + 0xFEE0 (Latin-1 Supplement fullwidth block)
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

    // INVISIBLE CHAOS: zero-width characters (U+200B/200C/200D + BOM, from the MC addon's
    // NastyText ZERO_WIDTH class) get injected at random positions every frame. The name
    // LOOKS identical to the eye but the string is different every rename - invisible
    // flicker; chat parsers that compare names see chaos.
    void invisibleChaosFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        static constexpr const char* kZeroWidth[] = {
            "\xE2\x80\x8B", // U+200B zero-width space
            "\xE2\x80\x8C", // U+200C ZWNJ
            "\xE2\x80\x8D", // U+200D ZWJ
            "\xEF\xBB\xBF", // U+FEFF BOM
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

    // ZALGO PROPER: port of the MC addon's NastyText COMBINING recipe - a base letter every
    // 8th position followed by dense combining-mark stacks (my earlier Zalgo failed because
    // it stacked marks onto EVERY glyph with the wrong mark set; the base-letter spacing is
    // what makes the corruption actually render). Intensity breathes via a sawtooth.
    void zalgo2Frame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        static constexpr const char* kMarks[] = {
            "\xCC\x80", "\xCC\x81", "\xCC\x89", "\xCC\x82", "\xCC\x83",
            "\xCC\x88", "\xCC\x8C", "\xCC\x93", "\xCC\x94", "\xCD\x82",
        };
        constexpr int kMarkCount = 10;
        constexpr int kPeak = 6; // marks per letter at the peak
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
        // the frame comes from the sanitized sidecar text (sanitizeInto already removed quotes
        // and line separators), so the quoting below cannot inject a second command
        char safe[chat_tools::kBufferTextSize];
        static_cast<void>(chat_tools::sanitizeInto(frame, safe, sizeof(safe)));
        if (safe[0] == '\0')
            return;

        // DIRECT RENAME MODE: each frame goes out as a genuine CNETMsg_SetConVar through the
        // game's own net channel (queue -> commit -> transmit in one call) - no local cvar set,
        // no setinfo. This bypasses the engine's userinfo-change machinery, which coalesces
        // console-driven renames: exactly the force behind the "frozen modes" saga. On ANY
        // failure the frame FALLS THROUGH to the setinfo path below - the animation keeps
        // running at the engine's coalesced rate instead of dying with the direct path (the
        // 2026-09-13 lesson: a persistently closed send window made direct-only mode useless).
        if (GET_CONFIG_VAR(name_animator_vars::DirectSend)) {
            const NetworkGameClientPointer clientPointer{};
            const char* reason = "no network client";
            const bool sent = clientPointer
                && net_messages::sendSingleSetConVar(clientPointer.get(), "name", safe, "Name Animator", &reason);
            if (sent)
                return;
            // throttled by VerifyConsole per-tag; "send window closed"/"no net channel" happen
            // around connects and under degraded connections, factory stages mean drift
            VerifyConsole::write(4.0f, "anim", "direct rename failed (%s) - setinfo fallback", reason);
        }

        // THE MISSING PATCH (root cause of "nothing animates" across every mode): CS2
        // registers `name` WITHOUT FCVAR_USERINFO, so a raw `setinfo name` is silently
        // rejected client-side and never reaches the server. ChatTools patches the flags once
        // per session before its own renames; this animator never did - every frame of every
        // mode was a no-op regardless of speed. Mirror ChatTools::ensureNameFlagsPatched here.
        if (!nameFlagsPatched) {
            nameFlagsPatched = hookContext.template make<CvarSystem>().patchUserInfoFlag("name");
            if (!nameFlagsPatched)
                return; // offset unverifiable this session - fail closed like ChatTools
        }

        char command[chat_tools::kBufferTextSize + 32]{"setinfo name \""};
        std::memcpy(command + 14, safe, std::strlen(safe) + 1);
        const auto length = std::strlen(command);
        command[length] = '"';
        command[length + 1] = '\0';
        hookContext.template make<EngineCommandExecutor>().execute(command);
    }

    // UTF-8 continuation-aware span of the sequence starting at `s` (1 for ASCII).
    [[nodiscard]] static std::size_t sequenceSpan(const char* s) noexcept
    {
        std::size_t span = 1;
        while ((static_cast<unsigned char>(s[span]) & 0xC0) == 0x80)
            ++span;
        return span;
    }

    // A monotonic-ish frame counter for parity flips (Random has no state; the tick counter
    // is already 0..ticksPerFrame so it cannot serve - use a dedicated static).
    [[nodiscard]] static int animationTick() noexcept
    {
        return ++frameParity;
    }

    static constexpr int kMaxSequences = 96;
    static constexpr int kMarqueeWidth = 12;
    static constexpr int kHoldUpdates = 8; // full-text frames before the loop restarts
    static constexpr char kGlitchCharset[] = "!<>-_\\/[]{}=+*^?#____";
    static constexpr char kBinaryCharset[] = "01";
    // Slider maps Speed -> ticksPerFrame = kSpeedMax - Speed + 1, so Speed 64 = one frame every
    // tick (~64 renames/sec, the engine's visible ceiling - the userinfo flood's Heavy Values
    // experiment proved the server coalesces anything faster anyway).
    static constexpr int kSpeedMax = 64;
    static constexpr int kGlitchCharsetSize = static_cast<int>(sizeof(kGlitchCharset)) - 1;

    // Feature objects are rebuilt per call - all cross-tick state is static.
    inline static int tickCounter{0};
    inline static int position{0};
    inline static int holdLeft{0};
    inline static bool shrinking{false};
    inline static bool hasText{false};
    inline static char text[chat_tools::kBufferTextSize]{};
    inline static std::size_t seqOffset[kMaxSequences]{};
    inline static std::size_t seqLength[kMaxSequences]{};
    inline static int lockStep[kMaxSequences]{};
    inline static int permDisplay[kMaxSequences]{}; // scramble: display slot i shows text[permDisplay[i]]
    inline static int seqCount{0};
    inline static int frameParity{0};  // strobe/wave: frame clock
    inline static bool nameFlagsPatched{false}; // name cvar USERINFO flag - once per session

    HookContext& hookContext;
};
