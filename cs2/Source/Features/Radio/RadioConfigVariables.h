#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

#include "RadioParams.h"

namespace radio_params
{
inline constexpr auto kVoiceKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
}

namespace radio_vars
{

// Web radio (Radio tab). Volume percentage handed to ffplay. Play/Stop are one-shot actions (the
// `radio` command verb), not config vars - only the volume persists.
CONFIG_VARIABLE_RANGE(Volume, radio_params::kVolume);

// Mic broadcast: while enabled AND a station is playing, the game's microphone capture is moved
// (PulseAudio move-source-output on the host) to a virtual source fed by a second host-side
// ffmpeg writing the station's PCM. Transmission is driven by a SYNTHETIC push-to-talk press
// (see VoiceKeyBind) - voice_vox / voice_threshold experiments empirically failed: VOX's gate
// kept closing (both-side icon drop) and +voicerecord never transmitted at all, while a held
// synthetic PTT key is exactly what CS2's gate polls.
CONFIG_VARIABLE(MicBroadcast, bool, false);

// The CS2 "Use Voice" key (Bind.h encoding), driven synthetically while broadcasting or while an
// airhorn clip plays. Optional manual override - leave Off and the broadcast auto-binds a spare
// key (F9) for hands-free transmit instead. Mouse binds can't drive PTT either way.
CONFIG_VARIABLE_RANGE(VoiceKeyBind, radio_params::kVoiceKey);

// Airhorn: event-triggered gag clips into voice chat. Same virtual-source routing as the mic
// broadcast, but engaged ONLY for the duration of a clip: a game event (first blood / local
// headshot kill / round win) routes the capture to the pipe, plays
// <configDir>/sounds/airhorn.wav through it with voice_vox forced on, then hands the mic back.
// Between gags normal push-to-talk voice is untouched.
CONFIG_VARIABLE(AirhornEnabled, bool, false);
CONFIG_VARIABLE(AirhornFirstBlood, bool, true);
CONFIG_VARIABLE(AirhornHeadshot, bool, false);
CONFIG_VARIABLE(AirhornRoundWin, bool, false);

// The bottom-left "NOW PLAYING" HUD box (drawNowPlayingWindow): station + track while a
// web-radio station plays. Track titles come from the host-side ICY burst probe
// (ns_radio_meta.py, ~2KB per minute) and simply stay empty for stations that publish none.
CONFIG_VARIABLE(ShowNowPlaying, bool, true);

// While NO station plays, the same box instead mirrors the host desktop's MPRIS media players
// (Spotify, Firefox, mpv...) via a `playerctl` burst through the launch-client - so the element
// is never dead weight. Radio playback always wins the box when it is active.
CONFIG_VARIABLE(ShowMediaPlayers, bool, true);

// Box position offsets from its default bottom-left anchor (above the COMBAT box). Float range
// so the box can live anywhere on modern screens - the box is mouse-draggable in game and the
// drag writes the same offsets (the status-chips pattern). Min is negative (BindsList
// kOffsetRange precedent) precisely so the box can be dragged DOWN below its anchor: with a
// 0-min range the clamp pins it 422px above the bottom edge and "move it to the bottom" is
// impossible - reported live 2026-10-06.
inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<float>{.min = -4096.0f, .max = 4096.0f, .def = 0.0f};
CONFIG_VARIABLE_RANGE(NowPlayingOffsetX, kOffsetRange);
CONFIG_VARIABLE_RANGE(NowPlayingOffsetY, kOffsetRange);

}
