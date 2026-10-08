


































#pragma once

namespace valve::pb::raw { struct CUserCmd; }

namespace fva::view_angle_spoofer
{





bool init();



void set_target_angle(float pitch, float yaw, float roll) noexcept;


void clear_target_angle() noexcept;




void apply(valve::pb::raw::CUserCmd* raw) noexcept;

[[nodiscard]] bool ready() noexcept;

} 
