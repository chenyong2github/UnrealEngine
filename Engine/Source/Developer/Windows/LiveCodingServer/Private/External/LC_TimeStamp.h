// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "LC_Platform.h"

namespace timeStamp
{
	void Startup(void);
	void Shutdown(void);


	// Returns the current value of the high-resolution performance counter.
	// This is not the same as actual CPU cycles.
	uint64_t Get(void);

	// Converts counts returned by Get() into seconds.
	double ToSeconds(uint64_t count);

	// Converts counts returned by Get() into milliseconds.
	double ToMilliSeconds(uint64_t count);

	// Converts counts returned by Get() into microseconds.
	double ToMicroSeconds(uint64_t count);
}
