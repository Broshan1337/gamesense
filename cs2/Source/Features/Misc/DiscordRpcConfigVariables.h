#pragma once

#include <Config/ConfigVariable.h>

namespace discord_rpc_vars
{

// Discord Rich Presence (Misc tab). While enabled, a host-side python relay (written and spawned
// by DiscordRpc.h) keeps a connection to the Discord desktop client's local IPC socket and
// republishes the activity JSON the feature drops into /tmp/ns_discord_rpc.json. The templates
// for the two presence lines are user-editable in the menu and persist in
// <configDir>/discord_rpc.txt.
CONFIG_VARIABLE(Enabled, bool, false);

}