// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/MallocTimer.h"

#if UE_TIME_VIRTUALMALLOC
#include "ProfilingDebugging/CsvProfiler.h"
#if CSV_PROFILER
CSV_DEFINE_CATEGORY_MODULE(CORE_API, VirtualMemory, true);
#endif
uint64 FScopedVirtualMallocTimer::GTotalCycles[FScopedVirtualMallocTimer::IndexType::Max] = { 0 };

void FScopedVirtualMallocTimer::UpdateStats()
{
	static uint64 GLastTotalCycles[IndexType::Max] = { 0 };
	static uint64 GLastFrame = 0;

	uint64 Frames = GFrameCounter - GLastFrame;
	if (Frames)
	{
		GLastFrame = GFrameCounter;
		// not atomic; we assume the error is minor
		uint64 TotalCycles[IndexType::Max] = { 0 };
		float TotalSeconds = 0.0f;
		for (int32 Comp = 0; Comp < IndexType::Max; Comp++)
		{
			TotalCycles[Comp] = GTotalCycles[Comp] - GLastTotalCycles[Comp];
			//TotalCycles[Comp] = GTotalCycles[Comp];
			GLastTotalCycles[Comp] = GTotalCycles[Comp];
			TotalSeconds += 1000.0f * float(FPlatformTime::GetSecondsPerCycle64()) * float(TotalCycles[Comp]);

		}
#if CSV_PROFILER
#if 0	// extra detail
		CSV_CUSTOM_STAT(VirtualMemory, Reserve, 1000.0f * float(FPlatformTime::GetSecondsPerCycle64()) * float(TotalCycles[0]), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(VirtualMemory, Commit, 1000.0f * float(FPlatformTime::GetSecondsPerCycle64()) * float(TotalCycles[1]), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(VirtualMemory, Combined, 1000.0f * float(FPlatformTime::GetSecondsPerCycle64()) * float(TotalCycles[2]), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(VirtualMemory, DeCommit, 1000.0f * float(FPlatformTime::GetSecondsPerCycle64()) * float(TotalCycles[3]), ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(VirtualMemory, Free, 1000.0f * float(FPlatformTime::GetSecondsPerCycle64()) * float(TotalCycles[4]), ECsvCustomStatOp::Set);
#else
		CSV_CUSTOM_STAT(VirtualMemory, TotalInSeconds, TotalSeconds, ECsvCustomStatOp::Set);
#endif
#endif	// CSV_PROFILER
	}
}
#endif
