// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/Timespan.h"
#include "Misc/AssertionMacros.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"

class FWindowsSemaphore
{
public:
	UE_NONCOPYABLE(FWindowsSemaphore);

	FWindowsSemaphore(int32 InitialCount, int32 MaxCount)
		: Semaphore(CreateSemaphore(nullptr, InitialCount, MaxCount, nullptr))
	{
		checkfSlow(Semaphore, TEXT("CreateSemaphore failed: %d"), GetLastError());
	}

	~FWindowsSemaphore()
	{
		CloseHandle(Semaphore);
	}

	void Acquire()
	{
		HRESULT hRes = WaitForSingleObject(Semaphore, INFINITE);
		checkfSlow(hRes == WAIT_OBJECT_0, TEXT("Acquiring semaphore failed: %d (%d)"), hRes, GetLastError());
	}

	bool TryAcquire(FTimespan Timeout = FTimespan::Zero())
	{
		HRESULT hRes = WaitForSingleObject(Semaphore, (DWORD)Timeout.GetTotalMilliseconds());
		checkfSlow(hRes == WAIT_OBJECT_0 || hRes == WAIT_TIMEOUT, TEXT("Acquiring semaphore failed: %d (%d)"), hRes, GetLastError());
		return hRes == WAIT_OBJECT_0;
	}

	void Release(int32 Count = 1)
	{
		checkfSlow(Count > 0, TEXT("Releasing semaphore with Count = %d, that should be greater than 0"), Count);
		bool bRes = ReleaseSemaphore(Semaphore, Count, nullptr);
		checkfSlow(bRes, TEXT("Releasing semaphore for %d failed: %d"), Count, GetLastError());
	}

private:
	HANDLE Semaphore;
};

using FSemaphore = FWindowsSemaphore;

#include "Windows/HideWindowsPlatformTypes.h"
