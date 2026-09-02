#pragma once

#include <Config/ConfigVariable.h>
#include "RadioParams.h"

namespace radio_vars
{

// Web radio (Radio tab). Volume percentage handed to ffplay. Play/Stop are one-shot actions (the
// `radio` command verb), not config vars - only the volume persists.
CONFIG_VARIABLE_RANGE(Volume, radio_params::kVolume);

// Mic broadcast: while enabled AND a station is playing, the game's microphone capture is moved
// (PulseAudio move-source-output on the host) to a virtual source fed by a second host-side
// ffmpeg writing the station's PCM. When the radio stops or the toggle goes off, the capture
// moves back to the real microphone and normal voice resumes.
CONFIG_VARIABLE(MicBroadcast, bool, false);

}
