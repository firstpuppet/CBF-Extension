/*
main.cpp: startup. reads every mod setting, applies it, and keeps it updated when changed.
see cbf.hpp for an overview of the other files
*/

#include "cbf.hpp"

namespace {

// keep a variable in sync with a bool setting
void bindSetting(std::string const& key, bool& target) {
	target = Mod::get()->getSettingValue<bool>(key);
	listenForSettingChanges<bool>(key, [&target](bool value) { target = value; });
}

// run a function with a bool setting's value now and whenever it changes
void bindSetting(std::string const& key, void (*apply)(bool)) {
	apply(Mod::get()->getSettingValue<bool>(key));
	listenForSettingChanges<bool>(key, apply);
}

}

$on_mod(Loaded) {
	initTimer(); // first, everything else depends on the timer frequency

	bindSetting("soft-toggle", setSoftToggle);
	bindSetting("precision-fix", togglePrecisionFix);
	bindSetting("physics-bypass", togglePhysicsBypass);
	bindSetting("safe-mode", safeMode);
	bindSetting("click-on-steps", clickOnSteps);
	bindSetting("mouse-fix", mouseFix);

	installShipRotationHook();
}
