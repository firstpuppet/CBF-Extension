/*
input.cpp: frame timing and the step queue

- timestamps the start of every frame (or reads GD's own timestamp when the precision fix is on)
- resets CBF whenever it shouldn't be splitting steps (paused, endscreen, unfocused, ...)
- "Reduce Mouse Lag": drops excess mouse movement messages
- turns GD's queued inputs into a queue of (partial) physics steps, see buildStepQueue()
*/

#include "cbf.hpp"
#include "core/timing.hpp"
#include "offsets.hpp"

#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/CCScheduler.hpp>

namespace {

constexpr PlayerButtonCommand EMPTY_INPUT = {
	.m_button = PlayerButton::Jump,
	.m_isPush = false,
	.m_isPlayer2 = false,
	.m_timestamp = 0
};

std::vector<PlayerButtonCommand> inputVector; // inputs not yet put in the step queue (sorted by timestamp)
PlayerButtonCommand nextInput = EMPTY_INPUT; // input to apply before simulating the next popped step

double getCurrentTimestamp() {
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}

void onFrameStart() {
	if (!precisionFix) currentFrameTime = getCurrentTimestamp();

	PlayLayer* playLayer = PlayLayer::get();
	CCNode* par;

	if (softToggle // CBF disabled
		|| !GetFocus() // GD is minimized
		|| !playLayer // not in level
		|| !(par = playLayer->getParent()) // must be a real playLayer with a parent (for compatibility with mods that use a fake playLayer)
		|| par->getChildByType<PauseLayer>(0) // if paused
		|| playLayer->getChildByType<EndLevelLayer>(0)) // if on endscreen
	{
		firstFrame = true;
		skipUpdate = true;
		inputVector.clear();
	}

	if (mouseFix && !skipUpdate) { // reduce lag with high polling rate mice by limiting the number of mouse movements per frame to 1
		MSG msg;
		int index = 1;
		while (PeekMessage(&msg, NULL, WM_MOUSEFIRST + index, WM_MOUSELAST, PM_NOREMOVE)) { // check for mouse inputs in the queue
			if (msg.message == WM_MOUSEMOVE || msg.message == WM_NCMOUSEMOVE) {
				PeekMessage(&msg, NULL, WM_MOUSEFIRST + index, WM_MOUSELAST, PM_REMOVE); // remove mouse movements from queue
			}
			else index++;
		}
	}
}

}

void initTimer() {
	QueryPerformanceFrequency(&freq);
}

// start of every frame, right before input is polled
class $modify(CCEGLView) {
	void pollEvents() {
		onFrameStart();
		CCEGLView::pollEvents();
	}
};

// with the precision fix, use the (grid snapped) frame timestamp GD itself uses
class $modify(CCScheduler) {
	void update(float dt) {
		if (precisionFix) {
			static LARGE_INTEGER* cur = reinterpret_cast<LARGE_INTEGER*>(geode::base::getCocos() + offsets::CURRENT_FRAME_TIME);
			currentFrameTime = static_cast<double>(cur->QuadPart) / static_cast<double>(freq.QuadPart);
		}

		CCScheduler::update(dt);
	}
};

/*
take GD's queued inputs and build the step queue for this frame,
based on when each input happened relative to the start of the frame (see cbf::timing::planSteps())
*/
void buildStepQueue(int stepCount) {
	PlayLayer* playLayer = PlayLayer::get();
	nextInput = EMPTY_INPUT;
	stepQueue.clear(); // shouldnt be necessary, but just in case

	skipUpdate = false;
	if (firstFrame) {
		skipUpdate = true;
		firstFrame = false;
		lastFrameTime = currentFrameTime;
		inputVector.clear();
		return;
	}

	// take the inputs from GD so it doesn't process them itself
	inputVector.insert(inputVector.end(), playLayer->m_queuedButtons.begin(), playLayer->m_queuedButtons.end());
	playLayer->m_queuedButtons.clear();

	std::vector<double> timestamps;
	timestamps.reserve(inputVector.size());
	for (auto const& input : inputVector) timestamps.push_back(input.m_timestamp);

	auto plan = cbf::timing::planSteps(timestamps, lastFrameTime, currentFrameTime, stepCount);
	for (auto const& step : plan.steps) {
		stepQueue.push_back({ step.endStep() ? EMPTY_INPUT : inputVector[step.input], step.deltaFactor, step.endStep() });
	}

	lastFrameTime = currentFrameTime;
	inputVector.erase(inputVector.begin(), inputVector.begin() + plan.inputsUsed); // keep inputs with timestamps later than currentFrameTime
}

/*
return the first step in the queue,
also check if an input happened on the previous step, if so run handleButton.
inputApplied is set to true if handleButton was run.
*/
Step popStepQueue(bool* inputApplied) {
	if (inputApplied) *inputApplied = false;
	if (stepQueue.empty()) return { EMPTY_INPUT, 1.0, true };

	Step front = stepQueue.front();

	if (nextInput.m_timestamp != 0) {
		PlayLayer::get()->handleButton(nextInput.m_isPush, static_cast<int>(nextInput.m_button), !nextInput.m_isPlayer2);
		if (inputApplied) *inputApplied = true;
	}

	nextInput = front.input;
	stepQueue.pop_front();
	return front;
}
