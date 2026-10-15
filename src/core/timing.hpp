#pragma once

/*
pure timing maths used by CBF, kept free of Geode/GD types so it can be unit tested on any machine (see tests/)

- planSteps(): splits a frame's physics steps at the time each input happened
- stepCount(): how many physics steps a frame gets (vanilla, 2.1 bypass, 2.2 bypass)
- usesLegacyPhysics(): whether a level should get 2.1 physics with physics bypass
*/

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace cbf::timing {

// smallest step fraction ever simulated. a step of exactly 0 makes GD divide by zero in a few places
constexpr double SMALLEST_FLOAT = std::numeric_limits<float>::min();

// one entry of a frame's step plan
struct PlannedStep {
	int input; // index into the frame's input list, or NO_INPUT for the end of a physics step
	double deltaFactor; // fraction of a physics step to simulate before this entry

	static constexpr int NO_INPUT = -1;
	bool endStep() const { return input == NO_INPUT; }
};

struct StepPlan {
	std::vector<PlannedStep> steps;
	size_t inputsUsed = 0; // inputs after this index happened after the frame, keep them for the next frame
};

/*
build the step plan for one frame.
timestamps must be sorted, lastFrameTime/currentFrameTime are when the previous/current frame started.

every physics step ends with an end entry. an input that happened partway through step i is placed in step i,
splitting it: e.g. one input 30% into a step gives { input 0.3, end 0.7 }.
*/
inline StepPlan planSteps(std::span<const double> timestamps, double lastFrameTime, double currentFrameTime, int stepCount) {
	StepPlan plan;
	if (stepCount <= 0) return plan;

	const double stepDelta = (currentFrameTime - lastFrameTime) / stepCount;

	for (int i = 0; i < stepCount; i++) {
		double elapsed = 0.0; // fraction of this step already planned

		// while loop to account for multiple inputs on the same step
		while (plan.inputsUsed < timestamps.size()) {
			const double sinceFrameStart = timestamps[plan.inputsUsed] - lastFrameTime;
			if (sinceFrameStart >= stepDelta * (i + 1)) break; // input belongs to a later step (or the next frame)

			// proportion of the step elapsed when the input happened.
			// an input from before the frame started counts as happening at the start of the step
			// (otherwise the fraction goes negative and the step gets simulated for longer than a step)
			const double inputTime = std::max(0.0, std::fmod(sinceFrameStart, stepDelta) / stepDelta);
			plan.steps.push_back({ static_cast<int>(plan.inputsUsed), std::clamp(inputTime - elapsed, SMALLEST_FLOAT, 1.0) });
			elapsed = inputTime;
			plan.inputsUsed++;
		}

		plan.steps.push_back({ PlannedStep::NO_INPUT, std::max(SMALLEST_FLOAT, 1.0 - elapsed) });
	}

	return plan;
}

enum class StepMode {
	Vanilla, // vanilla 2.2 (physics bypass off, or outside of levels)
	Legacy, // 2.1 physics bypass: same as vanilla 2.1
	Bypass, // 2.2 physics bypass: as few steps as possible, minimum 240 per second
};

/*
number of physics steps for a frame. this rewrites GD's formula because reading its step count would need inline assembly.
delta: frame time in seconds, animationInterval: target frame time (1 / fps cap),
averageDelta: smoothed frame time, kept between calls (only used by Bypass)
*/
inline int stepCount(double delta, float timewarp, StepMode mode, double animationInterval, double& averageDelta) {
	const float slowdown = std::min(1.0f, timewarp); // timewarp below 1 needs more steps, above 1 doesn't change anything

	switch (mode) {
	case StepMode::Vanilla:
		// not sure if this is different from `(delta * 240) / timewarp` bc of float precision
		return std::round(std::max(1.0, ((delta * 60.0) / slowdown) * 4.0));

	case StepMode::Legacy:
		return std::round(std::max(4.0, delta * 240.0) / slowdown);

	case StepMode::Bypass: {
		// exponential moving average to detect lag/external fps caps, capped so it can recover quickly
		averageDelta = std::min((0.05 * delta) + (0.95 * averageDelta), animationInterval * 10);

		const bool laggingOneFrame = animationInterval < delta - (1.0 / 240.0); // more than 1 step of lag on a single frame
		const bool laggingManyFrames = averageDelta - animationInterval > 0.0005; // average lag is >0.5ms

		double steps;
		if (laggingOneFrame) steps = std::ceil(delta * 240.0); // need to catch up badly
		else if (laggingManyFrames) steps = std::ceil(averageDelta * 240.0); // consistently low fps
		else steps = std::ceil((animationInterval * 240.0) - 0.0001); // no stepcount variance when not lagging

		return std::round(steps / slowdown);
	}
	}

	return 1;
}

/*
levels uploaded before 2.2 were built around 2.1 physics, so physics bypass emulates 2.1 on those
- levelTimestamp (GJGameLevel::m_timestamp) is only recorded when a level is verified in 2.2, so if it's set the level is definitely a 2.2 level
- gameVersion is the version the level was last uploaded with (21 = 2.1), 0 for local/unknown levels which are treated as 2.2
*/
inline bool usesLegacyPhysics(int gameVersion, int levelTimestamp) {
	if (levelTimestamp > 0) return false;
	return gameVersion > 0 && gameVersion < 22;
}

}
