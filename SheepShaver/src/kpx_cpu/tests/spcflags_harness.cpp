/* Cross-thread CPU return/interrupt regression, independent of guest media. */
#include "sysdeps.h"
#include "cpu/spcflags.hpp"
#include <atomic>
#include <thread>
#include <stdio.h>

int main()
{
	basic_spcflags flags;
	std::atomic<unsigned> submitted(0), completed(0);
	const unsigned rounds = 100000;
	unsigned resurrected = 0, lost = 0;
	std::thread timer([&] {
		for (unsigned n = 1; n <= rounds; ++n) {
			while (submitted.load(std::memory_order_acquire) != n)
				std::this_thread::yield();
			flags.set(SPCFLAG_CPU_TRIGGER_INTERRUPT);
			completed.store(n, std::memory_order_release);
		}
	});
	for (unsigned n = 1; n <= rounds; ++n) {
		flags.init(SPCFLAG_CPU_EXEC_RETURN);
		submitted.store(n, std::memory_order_release);
		// Returning from nested emulation must not be undone by the timer's
		// independent interrupt update. These operations intentionally race.
		flags.clear(SPCFLAG_CPU_EXEC_RETURN);
		(void)flags.empty();
		(void)flags.get();
		while (completed.load(std::memory_order_acquire) != n)
			std::this_thread::yield();
		resurrected += flags.test(SPCFLAG_CPU_EXEC_RETURN);
		lost += !flags.test(SPCFLAG_CPU_TRIGGER_INTERRUPT);
	}
	timer.join();
	basic_spcflags copy(flags), assigned;
	assigned = flags;
	const bool copied = copy.get() == flags.get() && assigned.get() == flags.get();
	flags.clear(SPCFLAG_CPU_TRIGGER_INTERRUPT);
	const bool independent = flags.empty() && copy.test(SPCFLAG_CPU_TRIGGER_INTERRUPT);
	// The same missing ARM64 lock implementation also protected legacy
	// interrupt read/modify/write helpers. Verify exclusion and publication.
	spinlock_t lock = SPIN_LOCK_UNLOCKED;
	unsigned counter = 0;
	auto add = [&] {
		for (unsigned n = 0; n < rounds; ++n) {
			spin_lock(&lock); ++counter; spin_unlock(&lock);
		}
	};
	std::thread first(add), second(add);
	first.join(); second.join();
	const bool locked = counter == rounds * 2;
	printf("CPU special flags: %u concurrent updates, %u resurrected returns, "
	       "%u lost interrupts, copy=%s\n", rounds, resurrected, lost,
	       copied && independent ? "pass" : "fail");
	printf("Host spin lock: %u/%u protected updates\n", counter, rounds * 2);
	return resurrected || lost || !copied || !independent || !locked;
}
