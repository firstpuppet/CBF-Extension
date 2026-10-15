/*
patches.cpp: raw memory patches (addresses in offsets.hpp)

- precision fix: keeps GD's frame timing from desyncing, so CBF's input timing lines up with the frames
- physics bypass: makes GD use CBF's step count instead of its own
- soft toggle: "Disable CBF" without restarting
*/

#include "cbf.hpp"
#include "offsets.hpp"

namespace {

// write a pointer into a byte buffer (for building machine code)
void writeAddr(geode::ByteVector& vec, size_t offset, const void* addr) {
	memcpy(vec.data() + offset, &addr, sizeof(addr));
}

// create a patch, or log the error and return nullptr. the patch's feature is then simply left off instead of crashing GD
Patch* createPatch(const char* name, void* addr, geode::ByteVector const& bytes) {
	auto res = Mod::get()->patch(addr, bytes);
	if (res.isErr()) {
		log::error("Failed to create {} patch at {}: {}", name, addr, res.unwrapErr());
		return nullptr;
	}
	log::info("{} patch: {} at {}", name, bytes, addr);
	return res.unwrap();
}

// returns false if the patch doesn't exist or couldn't be changed
bool setPatchEnabled(Patch* patch, bool enable) {
	if (!patch) return false;
	auto res = enable ? patch->enable() : patch->disable();
	if (res.isErr()) {
		log::error("Failed to {} patch: {}", enable ? "enable" : "disable", res.unwrapErr());
		return false;
	}
	return true;
}

/*
precision fix code cave: prevents frame timing from desyncing over time in CCApplication::run.
it runs in place of the 13 bytes at FRAME_TIME_SNAP (overwritten by the jump into the cave), recreates what they did
at the end, then jumps back to FRAME_TIME_SNAP_RETURN.

what it relies on at that point in CCApplication::run (GD 2.2081):
- rax: QueryPerformanceCounter time of the current frame
- xmm7: the animation interval (1 / fps cap) as a float
- [rsp+0x58]: CCApplication::run's local copy of the current frame time
- r12: used by the deltaTime calculation that the FRAME_DELTA_CALC patch rewrites
the cave doesn't write to itself, so its memory is made read-only once written
*/
void* createPrecisionFixCave() {
	uintptr_t base = geode::base::getCocos();
	void* retAddr = reinterpret_cast<void*>(base + offsets::FRAME_TIME_SNAP_RETURN);
	void* currentTimeAddr = reinterpret_cast<void*>(base + offsets::CURRENT_FRAME_TIME);

	geode::ByteVector cave = {
		0x9C,                                     // pushfq
		0x0F, 0x57, 0xC0,                         // xorps xmm0, xmm0
		0x0F, 0x57, 0xC9,                         // xorps xmm1, xmm1

		// grid snap currentTime based on the float version of animation interval
		0x48, 0x83, 0xC0, 0x0a,                   // add rax,10                               this fixes rounding errors, dont question it
		0x0F, 0x5A, 0xC7,                         // cvtps2pd xmm0,xmm7
		0x49, 0xB9, 0,0,0,0,0,0,0,0,              // movabs r9, &freq
		0xF2, 0x49, 0x0F, 0x2A, 0x09,             // cvtsi2sd xmm1,qword ptr [r9]
		0xF2, 0x0F, 0x59, 0xC1,                   // mulsd  xmm0,xmm1
		0xF2, 0x48, 0x0F, 0x2A, 0xC8,             // cvtsi2sd xmm1,rax
		0xF2, 0x0F, 0x5E, 0xC8,                   // divsd  xmm1,xmm0
		0xF2, 0x48, 0x0F, 0x2C, 0xC1,             // cvttsd2si rax,xmm1
		0xF2, 0x48, 0x0F, 0x2A, 0xC8,             // cvtsi2sd xmm1,rax
		0xF2, 0x0F, 0x59, 0xC8,                   // mulsd  xmm1,xmm0
		0xF2, 0x48, 0x0F, 0x2D, 0xC1,             // cvtsd2si rax,xmm1

		// fix precision issue in deltaTime calculation (supplemented by the FRAME_DELTA_CALC patch)
		0x4D, 0x8B, 0x21,                         // mov r12,QWORD PTR [r9]

		// move corrected currentTime value into relevant locations
		0x9D,                                     // popfq
		0x48, 0x89, 0x44, 0x24, 0x58,             // mov [rsp+0x58], rax
		0x48, 0xA3, 0,0,0,0,0,0,0,0,              // mov [currentTimeAddr], rax

		// restore previous state
		0x0F, 0x57, 0xC0,                         // xorps xmm0, xmm0
		0x0F, 0x57, 0xC9,                         // xorps xmm1, xmm1
		0xF2, 0x48, 0x0F, 0x2A, 0x44, 0x24, 0x68, // cvtsi2sd xmm0, [rsp+0x68]
		0x49, 0xB9, 0,0,0,0,0,0,0,0,              // movabs r9, retAddr
		0x41, 0xFF, 0xE1                          // jmp r9
	};

	// byte offsets of the placeholder addresses above
	writeAddr(cave, 16, &freq);
	writeAddr(cave, 72, currentTimeAddr);
	writeAddr(cave, 95, retAddr);

	void* caveAddr = VirtualAlloc(nullptr, cave.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!caveAddr) {
		log::error("Failed to allocate precision fix code: {}", GetLastError());
		return nullptr;
	}
	memcpy(caveAddr, cave.data(), cave.size());

	DWORD oldProtect;
	if (!VirtualProtect(caveAddr, cave.size(), PAGE_EXECUTE_READ, &oldProtect)) {
		log::error("Failed to make precision fix code executable: {}", GetLastError());
		VirtualFree(caveAddr, 0, MEM_RELEASE);
		return nullptr;
	}
	FlushInstructionCache(GetCurrentProcess(), caveAddr, cave.size());

	return caveAddr;
}

}

void togglePrecisionFix(bool enable) {
	static bool created = false;
	static Patch* snapPatch = nullptr; // jumps into the code cave
	static Patch* deltaPatch = nullptr;

	if (!created) {
		created = true;
		uintptr_t base = geode::base::getCocos();

		if (void* caveAddr = createPrecisionFixCave()) {
			geode::ByteVector entry = {
				0x49, 0xB9, 0,0,0,0,0,0,0,0,              // mov r9, caveAddr
				0x41, 0xFF, 0xE1                          // jmp r9
			};
			writeAddr(entry, 2, caveAddr);
			snapPatch = createPatch("Precision fix", reinterpret_cast<void*>(base + offsets::FRAME_TIME_SNAP), entry);
		}

		deltaPatch = createPatch("Delta fix", reinterpret_cast<void*>(base + offsets::FRAME_DELTA_CALC), {
			0x90, 0x90, 0x90,                         // nop x3
			0xF2, 0x0F, 0x5E, 0xD1,                   // divsd xmm2, xmm1
			0x90, 0x90, 0x90, 0x90                    // nop x4
		});
	}

	// the two patches only work together, so either both are on or both are off
	bool on = enable && setPatchEnabled(snapPatch, true) && setPatchEnabled(deltaPatch, true);
	if (!on) {
		setPatchEnabled(snapPatch, false);
		setPatchEnabled(deltaPatch, false);
		if (enable) log::warn("Precision fix unavailable, continuing without it");
	}

	precisionFix = on;
}

void togglePhysicsBypass(bool enable) {
	static Patch* pbPatch = [] {
		// mov rcx, &stepCount
		// mov r11d, dword ptr [rcx]
		geode::ByteVector bytes = { 0x48, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0, 0x44, 0x8b, 0x19 };
		writeAddr(bytes, 2, &stepCount);
		return createPatch("Physics bypass", reinterpret_cast<void*>(geode::base::get() + offsets::STEP_COUNT_LOAD), bytes);
	}();

	// physicsBypass must only be true while GD actually uses CBF's step count
	physicsBypass = setPatchEnabled(pbPatch, enable) && enable;
	if (enable && !physicsBypass) log::warn("Physics bypass unavailable, continuing without it");
}

void setSoftToggle(bool disable) {
	static Patch* modPatch = createPatch("Soft toggle", reinterpret_cast<void*>(geode::base::get() + offsets::SOFT_TOGGLE_CONSTANT), {
		0x29, 0x5c, 0x4f, 0x3f // 0.81f
	});

	setPatchEnabled(modPatch, !disable);
	softToggle = disable;

	// for mod menus that let you toggle cbf mid-attempt
	PlayLayer* pl = PlayLayer::get();
	if (pl) {
		if (!softToggle) {
			pl->m_clickBetweenSteps = false;
			pl->m_clickOnSteps = false;
		}
		else {
			GameManager* gm = GameManager::get();
			pl->m_clickBetweenSteps = gm->getGameVariable("0177");
			pl->m_clickOnSteps = gm->getGameVariable("0176");
		}
	}
}
