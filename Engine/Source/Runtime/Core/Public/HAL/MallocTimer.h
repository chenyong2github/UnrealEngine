// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreTypes.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"

#ifndef UE_TIME_VIRTUALMALLOC
#define UE_TIME_VIRTUALMALLOC 0
#endif

#if UE_TIME_VIRTUALMALLOC
struct CORE_API FScopedVirtualMallocTimer
{
	enum IndexType
	{
		Reserve,
		Commit,
		Combined,
		DeCommit,
		Free,

		Max
	};
	static uint64 GTotalCycles[IndexType::Max];

	int32 Index;
	uint64 Cycles;

	FORCEINLINE FScopedVirtualMallocTimer(int32 InIndex = 0)
		: Index(InIndex)
		, Cycles(FPlatformTime::Cycles64())
	{
	}
	FORCEINLINE ~FScopedVirtualMallocTimer()
	{
		uint64 Add = uint64(FPlatformTime::Cycles64() - Cycles);
		FPlatformAtomics::InterlockedAdd((volatile int64*)&GTotalCycles[Index], Add);
	}

	static void UpdateStats();
};
#else	//UE_TIME_VIRTUALMALLOC
struct CORE_API FScopedVirtualMallocTimer
{
	enum IndexType
	{
		Reserve,
		Commit,
		Combined,
		DeCommit,
		Free,

		Max
	};
	FORCEINLINE FScopedVirtualMallocTimer(int32 InIndex = 0)
	{
	}
	FORCEINLINE ~FScopedVirtualMallocTimer()
	{
	}

	static FORCEINLINE void UpdateStats()
	{
	}
};
#endif //UE_TIME_VIRTUALMALLOC
