























#pragma once

#include "../schema/cbaseusercmdpb_layout.h"

namespace fva::hooks::input_scratch_mirror
{




bool init();

[[nodiscard]] bool ready() noexcept;







[[nodiscard]] bool used_heap_fallback() noexcept;




void apply(const valve::pb::raw::CUserCmd* raw_cmd) noexcept;



[[nodiscard]] const valve::pb::raw::CInButtonStatePB* buttons_cache() noexcept;
[[nodiscard]] const valve::pb::raw::CMsgQAngle*       angles_cache()  noexcept;

} 
