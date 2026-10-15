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

}

void togglePrecisionFix(bool enable) {
	static Patch* pfPatch = nullptr;
	static Patch* pfPatch2 = nullptr;

	if (!pfPatch) {
		uintptr_t base = geode::base::getCocos();
		void* patchAddr = reinterpret_cast<void*>(base + offsets::FRAME_TIME_SNAP);
		void* patchAddr2 = reinterpret_cast<void*>(base + offsets::FRAME_DELTA_CALC);
		void* retAddr = reinterpret_cast<void*>(base + offsets::FRAME_TIME_SNAP_RETURN);
		void* currentTimeAddr = reinterpret_cast<void*>(base + offsets::CURRENT_FRAME_TIME);

		// prevent frame timing from desyncing over time in ccapplication::run
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

			// fix precision issue in deltaTime calculation (supplemented by pfPatch2)
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

		writeAddr(cave, 16, &freq);
		writeAddr(cave, 72, currentTimeAddr);
		writeAddr(cave, 95, retAddr);

		void* caveAddr = VirtualAlloc(nullptr, cave.size(), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		memcpy(caveAddr, cave.data(), cave.size());

		geode::ByteVector entry = {
			0x49, 0xB9, 0,0,0,0,0,0,0,0,              // mov r9, caveAddr
			0x41, 0xFF, 0xE1                          // jmp r9
		};
		writeAddr(entry, 2, caveAddr);

		log::info("Precision fix patch: {} at {}", entry, patchAddr);
		pfPatch = Mod::get()->patch(patchAddr, entry).unwrap();

		geode::ByteVector deltaPatch = {
			0x90, 0x90, 0x90,
			0xF2, 0x0F, 0x5E, 0xD1,
			0x90, 0x90, 0x90, 0x90
		};

		log::info("Delta fix patch: {} at {}", deltaPatch, patchAddr2);
		pfPatch2 = Mod::get()->patch(patchAddr2, deltaPatch).unwrap();
	}

	if (enable) {
		(void) pfPatch->enable();
		(void) pfPatch2->enable();
	}
	else {
		(void) pfPatch->disable();
		(void) pfPatch2->disable();
	}

	precisionFix = enable;
}

void togglePhysicsBypass(bool enable) {
	void* addr = reinterpret_cast<void*>(geode::base::get() + offsets::STEP_COUNT_LOAD);

	static Patch* pbPatch = nullptr;
	if (!pbPatch) {
		// mov rcx, &stepCount
		// mov r11d, dword ptr [rcx]
		geode::ByteVector bytes = { 0x48, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0, 0x44, 0x8b, 0x19 };
		writeAddr(bytes, 2, &stepCount);
		log::info("Physics bypass patch: {} at {}", bytes, addr);
		pbPatch = Mod::get()->patch(addr, bytes).unwrap();
	}

	if (enable) (void) pbPatch->enable();
	else (void) pbPatch->disable();

	physicsBypass = enable;
}

void setSoftToggle(bool disable) {
	void* addr = reinterpret_cast<void*>(geode::base::get() + offsets::SOFT_TOGGLE_CONSTANT);

	static Patch* modPatch = nullptr;
	if (!modPatch) modPatch = Mod::get()->patch(addr, { 0x29, 0x5c, 0x4f, 0x3f }).unwrap();

	if (disable) (void) modPatch->disable();
	else (void) modPatch->enable();

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
