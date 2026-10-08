




















#pragma once

#include <cstdint>

namespace valve::pb::raw { struct CBaseUserCmdPB; }

namespace fva::hooks
{

bool install_serialize_to_array_hook();
void uninstall_serialize_to_array_hook();



#if defined(_WIN32)
char __fastcall hook_body(void* self, void* stream, int max_size);
#else
char hook_body(void* self, void* stream, int max_size);
#endif

} 
