#include "includes.hpp"

LARGE_INTEGER freq;

void windowsSetup() {
	QueryPerformanceFrequency(&freq);
}
