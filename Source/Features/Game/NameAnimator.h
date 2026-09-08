#pragma once

#include <cstring>

#include <Features/Game/ChatTools.h>
#include <Features/Game/NameAnimatorConfigVariables.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Random.h>

// NAME ANIMATOR: drives the LIVE in-game name frame by frame through the `setinfo` rename path
// (the same USERINFO-flag mechanism applyFakeName uses - the flags patch happens once per
// session, whichever feature touches it first, and is cached).
//
// The text comes from the user's own sidecar buffer (<configDir>/name_animator.txt, the
// "Animate Text" row) - anything they type is the animation source.
//
// Modes (all operate on UTF-8 character SEQUENCES, so multi-byte names never split mid-glyph):
//   Typewriter - reveals one character per frame, holds the full text, restarts.
//   Glitch     - hacker decode: unrevealed positions flicker through symbols while the real
//                characters lock in LEFT TO RIGHT.
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

        // MAP-CHANGE GUARD (crash fix): renames fired during scene teardown raced the engine's
        // DeleteSceneObject and took the game down (sceneobject_methods.cpp:1212 assert,
        // !IsDeleted double-delete). curtime resets on every map load, so this pauses the
        // animator through the entire teardown/rebuild window - the project's standard
        // schema-readiness idiom.
        if (const auto mapTime = hookContext.globalVars().curtime();
            !mapTime.hasValue() || mapTime.value() < schema_readiness::kMinMapTime)
            return reset();

        // The Speed slider reads as SPEED (high = fast): ticks-per-frame is the inverted value.
        const int ticksPerFrame = kSpeedMin + kSpeedMax - GET_CONFIG_VAR(name_animator_vars::Speed);
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
            done = decodeFrame(kGlitchCharset, frame);
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
        return seqCount > 0;
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

    // Progress per frame: ~10 frames per full cycle. The server coalesces rapid userinfo
    // updates, so tiny single-character deltas get lost between visible renames - big chunky
    // steps are what actually read as animation.
    [[nodiscard]] int chunk() const noexcept
    {
        const auto step = seqCount / 10;
        return step > 0 ? step : 1;
    }

    // Classic typewriter with the full cycle: types UP one chunk per frame, holds the full
    // text, then types back DOWN to nothing and restarts (h / he / hel / ... / hello / ...
    // / hell / he / h).
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

    // Hacker decode: unrevealed positions flicker the charset while real characters lock in
    // left to right.
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

    // Assembles from the END, holds, then collapses back toward the end (mirror of the
    // typewriter cycle).
    [[nodiscard]] bool backwardsFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        if (shrinking) {
            position -= chunk();
            if (position <= 0)
                return true;
            copySequencesRange(frame, seqCount - position, seqCount);
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
        copySequencesRange(frame, seqCount - position, seqCount);
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

    // Banner rotation: the whole name scrolls left one character per frame, wrapping around -
    // a space marks the seam where the end meets the start ("gamesense.pub" -> "amesense.pub g").
    void marqueeFrame(char (&frame)[chat_tools::kBufferTextSize]) noexcept
    {
        frame[0] = '\0';
        for (int k = 0; k < seqCount; ++k) {
            const int index = (position + k) % seqCount;
            if (index == 0 && k > 0)
                std::strcat(frame, " ");
            std::strncat(frame, text + seqOffset[index], seqLength[index]);
        }
        position = (position + 1) % seqCount;
    }

    void applyFrame(const char* frame) const noexcept
    {
        // the frame comes from the sanitized sidecar text (sanitizeInto already removed quotes
        // and line separators), so the quoting below cannot inject a second command
        char safe[chat_tools::kBufferTextSize];
        static_cast<void>(chat_tools::sanitizeInto(frame, safe, sizeof(safe)));
        if (safe[0] == '\0')
            return;

        char command[chat_tools::kBufferTextSize + 32]{"setinfo name \""};
        std::memcpy(command + 14, safe, std::strlen(safe) + 1);
        const auto length = std::strlen(command);
        command[length] = '"';
        command[length + 1] = '\0';
        hookContext.template make<EngineCommandExecutor>().execute(command);
    }

    static constexpr int kMaxSequences = 96;
    static constexpr int kMarqueeWidth = 12;
    static constexpr int kHoldUpdates = 8; // full-text frames before the loop restarts
    static constexpr char kGlitchCharset[] = "!<>-_\\/[]{}=+*^?#____";
    static constexpr char kBinaryCharset[] = "01";
    static constexpr int kSpeedMin = 2;
    static constexpr int kSpeedMax = 32;
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

    HookContext& hookContext;
};
