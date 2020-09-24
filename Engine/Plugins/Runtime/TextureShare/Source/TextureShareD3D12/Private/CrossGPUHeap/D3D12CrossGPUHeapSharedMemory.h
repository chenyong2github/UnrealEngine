// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

class FCrossGPUSharedMemory
{
public:
	static constexpr auto Memory_GUID = TEXT("{a59128cc-cee2-4279-8190-93ef04ba5993}");	
	static constexpr auto Memory_MutexName = TEXT("TextureShareD3D12CrossGPUMutex");

protected:
	/** Lock that guards access to the memory region */
	static FPlatformProcess::FSemaphore* ProcessMutex;

	/** Low-level memory region */
	static FPlatformMemory::FSharedMemoryRegion* ProcessMemory;
public:
	virtual ~FCrossGPUSharedMemory()
	{
	}

	bool IsValid() const
	{ return ProcessMemory && ProcessMutex;	}

	static bool InitializeProcessMemory(SIZE_T TotalSize, const void* pSecurityAttributes);
	static void ReleaseProcessMemory();

	bool WriteData(const void* SrcDataPtr, SIZE_T DataOffset, SIZE_T DataSize, uint32 MaxMillisecondsToWait)
	{
		check(ProcessMutex);
		check(ProcessMemory);

		check(SrcDataPtr);
		check(DataSize);

		// acquire
		if (!MaxMillisecondsToWait)
		{
			ProcessMutex->Lock();
		}
		else
		{
			if (!ProcessMutex->TryLock(MaxMillisecondsToWait * 1000000ULL))	// 1ms = 10^6 ns
			{
				return false;
			}
		}

		// we have exclusive ownership now!
		FMemory::Memcpy(((char*)ProcessMemory->GetAddress()) + DataOffset, SrcDataPtr, DataSize);

		// relinquish
		ProcessMutex->Unlock();

		return true;
	}

	bool ReadData(void* DstDataPtr, SIZE_T DataOffset, SIZE_T DataSize, uint32 MaxMillisecondsToWait)
	{
		check(ProcessMutex);
		check(ProcessMemory);

		check(DstDataPtr);
		check(DataSize);

		// acquire
		if (!MaxMillisecondsToWait)
		{
			ProcessMutex->Lock();
		}
		else
		{
			if (!ProcessMutex->TryLock(MaxMillisecondsToWait * 1000000ULL))	// 1ms = 10^6 ns
			{
				return false;
			}
		}

		// we have exclusive ownership now!
		FMemory::Memcpy(DstDataPtr, ((char*)ProcessMemory->GetAddress()) + DataOffset, DataSize);

		// relinquish
		ProcessMutex->Unlock();

		return true;
	}
};

