// Copyright 2011-2019 Molecular Matters GmbH, all rights reserved.

#pragma once

#include "Windows/MinimalWindowsApi.h"

class Semaphore
{
public:
	Semaphore(unsigned int initialValue, unsigned int maximumValue);
	~Semaphore(void);

	// Signals the semaphore.
	void Signal(void);

	// Waits until the semaphore becomes signaled, blocking.
	bool Wait(void);

	// Waits until the semaphore becomes signaled, blocking until the timeout is reached.
	// Returns whether the semaphore was signaled.
	bool WaitTimeout(unsigned int milliSeconds);

	// Returns whether the semaphore was signaled, non-blocking.
	bool TryWait(void);

private:
	Windows::HANDLE m_sema;
};
