/*
game_layer.cpp: GJBaseGameLayer/PlayLayer hooks

- works out the physics step count for each frame (with physics bypass: from CBF's own frame timing) and builds the step queue
- picks 2.1 or 2.2 physics bypass per level
- Click on Steps: applies inputs on step boundaries
- tracks group toggles caused by inputs (used by player.cpp)
- overrides the vanilla CBS/COS options while CBF is enabled
*/

#include "cbf.hpp"
#include "core/timing.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

namespace {

bool legacyBypass = false; // true -> current level uses 2.1 physics, picked in PlayLayer::init()
double averageDelta = 0.0; // smoothed frame time for 2.2 physics bypass

int calculateStepCount(double delta, float timewarp, bool forceVanilla) {
	using cbf::timing::StepMode;

	StepMode mode = StepMode::Vanilla;
	if (physicsBypass && !forceVanilla) mode = legacyBypass ? StepMode::Legacy : StepMode::Bypass;

	return cbf::timing::stepCount(delta, timewarp, mode, CCDirector::sharedDirector()->getAnimationInterval(), averageDelta);
}

}

class $modify(PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		legacyBypass = level && cbf::timing::usesLegacyPhysics(level->m_gameVersion, level->m_timestamp);
		if (physicsBypass) log::info("Physics bypass using {} physics (game version {})", legacyBypass ? "2.1" : "2.2", level ? level->m_gameVersion : 0);

		bool result = PlayLayer::init(level, useReplay, dontCreateObjects);
		if (!softToggle) {
			this->m_clickBetweenSteps = false;
			this->m_clickOnSteps = false;
		}
		return result;
	}
};

class $modify(GJBaseGameLayer) {
	static void onModify(auto& self) {
		(void) self.setHookPriority("GJBaseGameLayer::getModifiedDelta", Priority::VeryEarly);
	}

	// GD's frame delta, rounded to whole steps. with physics bypass, CBF's own frame timing is used instead.
	// kept as a float on purpose: that's the precision the step count has always been calculated with
	double getModifiedDelta(float delta) {
		float modifiedDelta = GJBaseGameLayer::getModifiedDelta(delta);

		PlayLayer* pl = PlayLayer::get();
		if (!pl) {
			if (physicsBypass) stepCount = calculateStepCount(modifiedDelta, this->m_gameState.m_timeWarp, true); // disable physics bypass outside levels
			return modifiedDelta;
		}

		const float timewarp = pl->m_gameState.m_timeWarp;
		if (physicsBypass) {
			if (softToggle) modifiedDelta = CCDirector::sharedDirector()->getActualDeltaTime() * timewarp;
			else if (!firstFrame) modifiedDelta = (currentFrameTime - lastFrameTime) * timewarp;
		}

		stepCount = calculateStepCount(modifiedDelta, timewarp, false);

		if (pl->m_playerDied || GameManager::sharedState()->getEditorLayer() || softToggle) {
			skipUpdate = true;
			firstFrame = true;
		}
		else if (modifiedDelta > 0.0) buildStepQueue(stepCount);
		else skipUpdate = true;

		return modifiedDelta;
	}

	// Click on Steps: apply this step's inputs before the step runs (process 1 step, or more if theres an input)
	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		if (clickOnSteps && !stepQueue.empty()) {
			Step step;
			do step = popStepQueue(); while (!stepQueue.empty() && !step.endStep);
		}
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
	}

	// track group toggles caused by inputs so objects they enable (portals, pads, etc.) can be collided with at the time of the input.
	// spawn triggers don't need their own hook: if a spawned toggle runs immediately it goes through toggleGroup(),
	// and delayed ones run on a later step anyway
	void toggleGroup(int id, bool activate) {
		if (midStep) groupToggled = true;
		GJBaseGameLayer::toggleGroup(id, activate);
	}

	void toggleGroupTriggered(int group, bool activate, gd::vector<int> const& remapKeys, int triggerID, int controlID) {
		if (midStep) groupToggled = true;
		GJBaseGameLayer::toggleGroupTriggered(group, activate, remapKeys, triggerID, controlID);
	}
};
