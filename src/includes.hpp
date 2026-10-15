#pragma once

#include <queue>
#include <mutex>

#include <Geode/Geode.hpp>

using namespace geode::prelude;

#include "timestamp.hpp"

struct Step {
	PlayerButtonCommand input;
	double deltaFactor;
	bool endStep;
};

extern std::vector<struct PlayerButtonCommand> inputVector;

extern bool softToggle; // true -> cbf disabled
