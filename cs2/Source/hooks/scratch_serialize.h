



















#pragma once

#include <string>

namespace fva::scratch_serialize
{



bool init();




bool serialize(std::string& out);




[[nodiscard]] bool ready() noexcept;





void* get_arena_string_set() noexcept;

} 
