#pragma once

/*
state and functions shared between files. anything only used by one file lives in that file instead.

file overview:
- main.cpp        startup: reads the mod settings and applies them
- input.cpp       frame timing, mouse fix, turning queued inputs into a step queue
- game_layer.cpp  GJBaseGameLayer/PlayLayer hooks: step count per frame, Click on Steps, CBS/COS override
- player.cpp      PlayerObject hooks: splitting physics steps at the time of each input
- patches.cpp     raw memory patches (precision fix, physics bypass, soft toggle)
- results.cpp     safe mode, endscreen watermark, leaderboard submission
- offsets.hpp     every hardcoded GD/cocos address
- core/timing.hpp pure timing maths (unit tested in tests/)
*/

#include <Geode/Geode.hpp>

#include <deque>

using namespace geode::prelude;

// ---- settings (main.cpp / patches.cpp) ----

inline bool softToggle = false; // "Disable CBF": true -> CBF disabled (physics bypass keeps working)
inline bool safeMode = false; // no progress/stats on rated levels
inline bool clickOnSteps = false; // only register inputs on step boundaries (vanilla physics)
inline bool mouseFix = false; // "Reduce Mouse Lag"
inline bool precisionFix = false; // only true while the precision fix patch is applied
inline bool physicsBypass = false; // only true while the physics bypass patch is applied

// ---- frame timing (input.cpp) ----

inline LARGE_INTEGER freq; // QueryPerformanceCounter ticks per second
inline double lastFrameTime = 0.0; // seconds, start of the previous frame
inline double currentFrameTime = 0.0; // seconds, start of the current frame
inline bool firstFrame = true; // necessary to prevent accidental inputs at the start of the level or when unpausing
inline bool skipUpdate = true; // true -> dont split steps during PlayerObject::update()

void initTimer();

// ---- step queue (input.cpp) ----

// one entry of the step queue: simulate deltaFactor of a physics step, then apply input (unless endStep)
struct Step {
	PlayerButtonCommand input;
	double deltaFactor;
	bool endStep;
};

inline std::deque<Step> stepQueue;

void buildStepQueue(int stepCount);
Step popStepQueue(bool* inputApplied = nullptr);

// ---- physics steps (game_layer.cpp / player.cpp) ----

inline int stepCount = 0; // physics steps this frame. the physics bypass patch reads this variable directly
inline bool midStep = false; // true -> currently splitting a step in PlayerObject::update()
inline bool groupToggled = false; // set if a group was toggled mid-step (toggle orbs, touch triggers, etc.)

void installShipRotationHook(); // player.cpp

// ---- patches (patches.cpp) ----

void togglePrecisionFix(bool enable);
void togglePhysicsBypass(bool enable);
void setSoftToggle(bool disable);
