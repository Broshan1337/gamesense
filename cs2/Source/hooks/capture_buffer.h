








#pragma once

#include "../schema/cbaseusercmdpb_layout.h"

namespace fva::capture_buffer
{



extern valve::pb::raw::CBaseUserCmdPB g_scratch;




void publish();


unsigned long long generation() noexcept;





void cache_vtable_from(void* src) noexcept;

} 
