// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "CoreTypes.h"
#include "Windows/MinimalWindowsApi.h"

// named/unnamed event.
// acts process-wide if given a name.
class Event
{
public:
	struct Type
	{
		enum Enum
		{
			MANUAL_RESET,
			AUTO_RESET
		};
	};

	Event(const wchar_t* name, Type::Enum type);
	~Event(void);

	// Resets the event.
	void Reset(void);

	// Signals the event.
	void Signal(void);

	// Waits until the event becomes signaled, blocking.
	bool Wait(void);

	// Waits until the event becomes signaled, blocking until the timeout is reached.
	// Returns whether the event was signaled.
	bool WaitTimeout(unsigned int milliSeconds);

	// Returns whether the event was signaled, non-blocking.
	bool TryWait(void);

private:
	Windows::HANDLE m_event;
};
