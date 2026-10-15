#pragma once

/*
every hardcoded address the mod uses, as offsets from the module base. all of them are for GD 2.2081 (Windows).
when GD updates, these are what needs to be found again
*/

#include <cstdint>

namespace offsets {

// ---- GeometryDash.exe (geode::base::get()) ----

// inside GJBaseGameLayer::update (0x237850): where the physics step count is loaded into r11d.
// physics bypass replaces it with a load from the mod's stepCount variable
constexpr uintptr_t STEP_COUNT_LOAD = 0x237a91;

// 4-byte constant that CBF overrides with 0.81f (bytes 29 5c 4f 3f) while enabled (inherited from upstream CBF)
constexpr uintptr_t SOFT_TOGGLE_CONSTANT = 0x622bd0;

// Slerp2D(float, float, float), used by PlayerObject::updateShipRotation
constexpr uintptr_t SLERP_2D = 0x71ef0;

// ---- libcocos2d.dll (geode::base::getCocos()) ----

// QueryPerformanceCounter value for the start of the current frame, written by CCApplication::run
constexpr uintptr_t CURRENT_FRAME_TIME = 0x1a84d8;

// CCApplication::run: where the precision fix jumps to its code cave, and where the cave jumps back to
constexpr uintptr_t FRAME_TIME_SNAP = 0x736f6;
constexpr uintptr_t FRAME_TIME_SNAP_RETURN = 0x73703;

// CCApplication::run: deltaTime calculation, patched by the precision fix
constexpr uintptr_t FRAME_DELTA_CALC = 0x73716;

}
