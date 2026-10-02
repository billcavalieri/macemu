/* Application services deliberately replaced by host-test fixtures.
 * These tests do not initialize the UI, preferences files, or timer thread. */
#include <stdint.h>
#include <time.h>
uint64_t GetTicks_usec()
{
	timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000u + t.tv_nsec / 1000;
}
bool PrefsFindBool(const char *) { return false; }
