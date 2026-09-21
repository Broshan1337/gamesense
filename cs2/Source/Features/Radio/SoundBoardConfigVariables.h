#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

#include <cstdint>

namespace soundboard_params
{
// VoiceDataFormat_t for the injected voice messages: STEAM=0 / ENGINE=1 / OPUS=2 (decoded from
// the embedded netmessages.proto descriptor). The correct value for raw opus frames is 2; the
// var exists so a silent session can cycle all three without a rebuild.
inline constexpr auto kVoiceFormat = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 2, .def = 2};

// Index into the scanned <configDir>/sounds/*.wav list (the Clip dropdown). 0 = the first
// file alphabetically; the scan re-sorts by readdir order - if the folder changes, the index
// may point elsewhere. The dropdown renders the CURRENT list so the user always sees the match.
inline constexpr auto kClipIndex = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 63, .def = 0};
// Press-to-play keybind: fires the selected clip once per press (edge-triggered, not held).
inline constexpr auto kSoundKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
}

namespace soundboard_vars
{

// SOUND BOARD (in-process voice injection): clips from <configDir>/sounds/*.wav are decoded to
// 48kHz mono PCM, opus-encoded with the GAME'S OWN libopus (dlsym'd from libsoundsystem.so) and
// injected as paced CCLCMsg_VoiceData messages through the game's net channel (the Server
// Lagger's pipeline at normal voice cadence). Replaces the old host-script airhorn pipeline
// entirely: no pactl/ffmpeg/fifo, no device switches, no host spawns.
//
// Master gate = radio_vars::AirhornEnabled (the "Soundboard" toggle, shared with the event
// triggers below). Clip = soundboard_vars::ClipIndex into the scanned sounds folder; Sound
// Key = press-to-play keybind; the Airhorn event toggles (First Blood/Headshot/Round Win)
// trigger the selected clip as well.
CONFIG_VARIABLE_RANGE(ClipIndex, soundboard_params::kClipIndex);
CONFIG_VARIABLE_RANGE(VoiceFormat, soundboard_params::kVoiceFormat);
CONFIG_VARIABLE_RANGE(SoundKeyBind, soundboard_params::kSoundKeyBind);

}