/*
player.cpp: splitting physics steps at the time of each input (the core of CBF)

on a step with inputs, PlayerObject::update() for P1 runs the whole step for both players itself:
for every input it simulates up to the time of the input, checks collisions, applies the input, then continues.
also fixes rotation/ship rotation for split steps and physics bypass.
*/

#include "cbf.hpp"
#include "core/timing.hpp"
#include "offsets.hpp"

#include <Geode/modify/PlayerObject.hpp>
#include <tulip/TulipHook.hpp>

using cbf::timing::SMALLEST_FLOAT;

namespace {

// per-player state for the step currently being split, indexed by player (0 = P1, 1 = P2)
bool playerSplit[2] = { false, false }; // updateRotation() finishes the split step's rotation for these players
CCPoint stepStartPos[2]; // positions at the start of the step, restored as m_lastPosition at the end

float rotationDelta; // delta of the last substep, used to finish the rotation in updateRotation()
float shipRotDelta = 0.0f; // nonzero only while inside updateShipRotation(), read by the Slerp2D hook
bool inputThisStep = false;

struct SplitPlayer {
	PlayerObject* player = nullptr;
	bool split = false; // false -> update the player normally at the end of the step
	bool startedOnGround = false;
	bool buffering = false; // airborne in a gamemode where clicks get buffered until landing (cube, ball, robot, spider)
	bool checkHazards = false; // check hazards at the time of the input instead of only at the end of the step
};

bool touchingTeleportOrb(PlayerObject* p) {
	for (GameObject* ring : CCArrayExt<GameObject*>(p->m_touchingRings)) {
		if (ring->m_objectType == GameObjectType::SpiderOrb || ring->m_objectType == GameObjectType::TeleportOrb) return true;
	}
	return false;
}

SplitPlayer prepareSplit(PlayerObject* p, bool split) {
	SplitPlayer sp;
	sp.player = p;
	sp.split = split;
	sp.startedOnGround = p->m_isOnGround;
	sp.buffering = !(p->m_isOnGround
		|| p->m_touchingRings->count()
		|| p->m_isDashing
		|| p->m_isDart || p->m_isBird || p->m_isShip || p->m_isSwing);

	// spider teleports are instant, so unless hazards are checked at the time of the input,
	// whether you survive only depends on which step the teleport happened on
	sp.checkHazards = p->m_isSpider || touchingTeleportOrb(p);
	return sp;
}

void substepCollision(PlayLayer* pl, PlayerObject* p, float stepDelta, bool ignoreDamage) {
	if (!p->m_isOnSlope || p->m_isDart) pl->checkCollisions(p, 0.0f, ignoreDamage); // moving platforms will launch u really high if this is anything other than 0.0, idk why
	else pl->checkCollisions(p, stepDelta, ignoreDamage); // slopes will launch you really high if the 2nd argument is lower than like 0.01, idk why
}

// PlayerObject::resetCollisionLog() rewritten since it's inlined on Windows
void resetCollisionLog(PlayerObject* p) {
	p->m_collisionLogTop->removeAllObjects();
	p->m_collisionLogBottom->removeAllObjects();
	p->m_collisionLogLeft->removeAllObjects();
	p->m_collisionLogRight->removeAllObjects();
	p->m_lastCollisionLeft = -1;
	p->m_lastCollisionRight = -1;
	p->m_lastCollisionBottom = -1;
	p->m_lastCollisionTop = -1;
}

// 0 for P1, 1 for P2, -1 for anything else (e.g. players of other mods)
int playerIndex(PlayLayer* pl, PlayerObject* p) {
	if (!pl) return -1;
	if (p == pl->m_player1) return 0;
	if (p == pl->m_player2) return 1;
	return -1;
}

}

class $modify(PlayerObject) {
	// split a single step based on the entries in stepQueue
	void update(float stepDelta) {
		PlayLayer* pl = PlayLayer::get();

		if (pl && this != pl->m_player1 || midStep) { // do all of the logic during the P1 update for simplicity
			if (midStep || !inputThisStep || this != pl->m_player2) PlayerObject::update(stepDelta);
			return;
		}

		inputThisStep = !stepQueue.empty() && !stepQueue.front().endStep;
		if (!stepQueue.empty() && !inputThisStep && !clickOnSteps) stepQueue.pop_front(); // no input this step, just skip its entry

		if (skipUpdate || !pl || !inputThisStep || clickOnSteps) {
			playerSplit[0] = playerSplit[1] = false;
			inputThisStep = false;
			PlayerObject::update(stepDelta);
			return;
		}

		/*
		every player with an input on this step gets split, including airborne ones (P2 only in dual mode).
		buffering players are kept from landing mid-step though (see below), so buffer clicks behave like vanilla.
		*/
		SplitPlayer players[2] = {
			prepareSplit(this, true),
			prepareSplit(pl->m_player2, pl->m_gameState.m_isDualMode)
		};

		for (int i = 0; i < 2; i++) {
			stepStartPos[i] = players[i].player->getPosition(); // save for later to prevent desync with move triggers & some other issues
			playerSplit[i] = players[i].split;
		}

		// P1 has to go through the original function directly since we're inside its hook
		auto updatePlayer = [&](PlayerObject* p, float dt) {
			if (p == this) PlayerObject::update(dt);
			else p->update(dt);
		};

		auto updatePlayerRotation = [&](PlayerObject* p, float dt) {
			if (p == this) PlayerObject::updateRotation(dt);
			else p->updateRotation(dt);
		};

		Step step;
		bool firstLoop = true;
		bool died = false;
		midStep = true;

		do {
			CCPoint before[2] = { players[0].player->getPosition(), players[1].player->getPosition() };
			bool inputApplied = false;
			groupToggled = false;
			step = popStepQueue(&inputApplied);
			const float substepDelta = stepDelta * step.deltaFactor;
			rotationDelta = substepDelta;

			/*
			let the input take effect at the exact time it happened, then check collisions again if it did something instant:
			- spider/teleport orb teleports need a hazard check at the new position
			- groups toggled by the input (toggle orbs, touch triggers) can enable portals/pads the player is already inside of
			otherwise all of that would only be processed at the end of the step
			*/
			if (inputApplied && !died) {
				for (SplitPlayer& sp : players) {
					PlayerObject* p = sp.player;
					if (sp.split && (p->m_isSpider || p->m_touchingRings->count())) updatePlayer(p, stepDelta * SMALLEST_FLOAT);
				}

				for (int i = 0; i < 2; i++) {
					SplitPlayer& sp = players[i];
					PlayerObject* p = sp.player;
					if (!sp.split) continue;

					const bool teleported = before[i].getDistance(p->getPosition()) > 1.0f;
					if (!teleported && !groupToggled) continue;

					substepCollision(pl, p, stepDelta, !teleported && !sp.checkHazards);
					if (sp.buffering && !teleported) p->m_isOnGround = false;
					resetCollisionLog(p);
				}

				if (pl->m_playerDied) died = true;
			}

			for (SplitPlayer& sp : players) {
				PlayerObject* p = sp.player;

				if (!sp.split) {
					if (step.endStep && !died) updatePlayer(p, stepDelta); // not split: update normally (only P2 outside of dual mode)
					continue;
				}

				if (died) continue; // freeze the player where it died

				updatePlayer(p, substepDelta);
				if (step.endStep) continue; // the rest of the step (collisions, rotation) is done by GD itself

				if (firstLoop && ((p->m_yVelocity < 0) ^ p->m_isUpsideDown)) p->m_isOnGround = sp.startedOnGround; // this fixes delayed inputs on platforms moving down for some reason
				substepCollision(pl, p, stepDelta, !sp.checkHazards);

				// a click made after landing mid-step would jump instantly, but outside of CBF it's buffered until the next step,
				// so dont let buffering players land until the end of the step (to match vanilla buffer clicks)
				if (sp.buffering) p->m_isOnGround = false;

				updatePlayerRotation(p, substepDelta);
				resetCollisionLog(p); // necessary for wave

				if (pl->m_playerDied) died = true;
			}

			firstLoop = false;
		} while (!step.endStep);

		midStep = false;
	}

	// this function was chosen to update m_lastPosition in just because it's called right at the end of the vanilla physics step loop
	void updateRotation(float t) {
		PlayLayer* pl = PlayLayer::get();
		const int idx = playerIndex(pl, this);

		if (idx != -1 && playerSplit[idx] && !midStep) {
			PlayerObject::updateRotation(rotationDelta); // perform the remaining rotation that was left incomplete in the PlayerObject::update() hook
			this->m_lastPosition = stepStartPos[idx]; // move triggers & spider get confused without this (iirc)
		}
		else PlayerObject::updateRotation(t);

		if (physicsBypass && pl && !midStep) { // fix percent calculation with physics bypass on 2.2 levels
			pl->m_gameState.m_currentProgress = static_cast<int>(pl->m_gameState.m_levelTime * 240.0);
		}
	}

	void updateShipRotation(float t) {
		PlayLayer* pl = PlayLayer::get();

		if (playerIndex(pl, this) != -1 && (physicsBypass || inputThisStep)) {
			shipRotDelta = t;
			PlayerObject::updateShipRotation(1.0 / 1024); // necessary to use a really small deltatime to get around an oversight in rob's math
			shipRotDelta = 0.0f;
		}
		else PlayerObject::updateShipRotation(t);
	}
};

namespace {

// only changes anything when called from inside updateShipRotation() (see above): swaps in the real rotation delta
float Slerp2D(float p0, float p1, float p2) {
	static auto orig = reinterpret_cast<float (*)(float, float, float)>(geode::base::get() + offsets::SLERP_2D);
	if (shipRotDelta != 0.0f) {
		shipRotDelta *= p2 * 1024; // p2 is 1/1024 scaled by a constant factor, we need to multiply shipRotDelta by that factor
		return orig(p0, p1, shipRotDelta);
	}
	return orig(p0, p1, p2);
}

}

void installShipRotationHook() {
	(void) Mod::get()->hook(
		reinterpret_cast<void*>(geode::base::get() + offsets::SLERP_2D),
		Slerp2D,
		"Slerp2D",
		tulip::hook::TulipConvention::Default
	);
}
