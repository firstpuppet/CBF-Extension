#pragma once

#include "windows.hpp"

using TimestampType = double;

inline TimestampType getCurrentTimestamp() {
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return static_cast<TimestampType>(t.QuadPart) / static_cast<TimestampType>(freq.QuadPart);
}
