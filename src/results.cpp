/*
results.cpp: what happens to progress when CBF or physics bypass is used

- safe mode: no completions, new bests or stats
- endscreen watermark ("CBF", "CBF+PB" or "PB")
- leaderboard: CBF/PB runs on rated levels aren't submitted as valid
*/

#include "cbf.hpp"

#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/PlayLayer.hpp>

namespace {

bool safeModeActive() {
	return safeMode && !softToggle;
}

}

class $modify(PlayLayer) {
	// disable progress in safe mode by pretending the run is a test run
	void levelComplete() {
		bool testMode = this->m_isTestMode;
		if (safeModeActive()) this->m_isTestMode = true;

		PlayLayer::levelComplete();

		this->m_isTestMode = testMode;
	}

	// disable new best popup in safe mode
	void showNewBest(bool p0, int p1, int p2, bool p3, bool p4, bool p5) {
		if (!safeModeActive()) PlayLayer::showNewBest(p0, p1, p2, p3, p4, p5);
	}
};

class $modify(EndLevelLayer) {
	void customSetup() {
		EndLevelLayer::customSetup();

		const char* text = nullptr;
		if (physicsBypass) text = (softToggle || clickOnSteps) ? "PB" : "CBF+PB";
		else if (!softToggle && !clickOnSteps) text = "CBF";
		if (!text) return; // vanilla physics, no watermark

		CCSize size = CCDirector::sharedDirector()->getWinSize();
		CCLabelBMFont* indicator = CCLabelBMFont::create(text, "bigFont.fnt");

		indicator->setPosition({ size.width, size.height });
		indicator->setAnchorPoint({ 1.0f, 1.0f });
		indicator->setOpacity(30);
		indicator->setScale(0.2f);

		this->addChild(indicator);
	}
};

class $modify(GJGameLevel) {
	// only runs with vanilla physics (CBF off or Click on Steps, and no physics bypass) count as valid on rated levels
	void savePercentage(int percent, bool p1, int clicks, int attempts, bool valid) {
		const bool vanillaPhysics = (softToggle || clickOnSteps) && !physicsBypass;
		valid = vanillaPhysics || this->m_stars == 0;

		if (!safeModeActive()) GJGameLevel::savePercentage(percent, p1, clicks, attempts, valid);
	}
};
