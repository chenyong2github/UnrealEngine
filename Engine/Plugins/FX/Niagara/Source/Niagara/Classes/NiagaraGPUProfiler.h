// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "HAL/Platform.h"
#include "Misc/Build.h"

#if STATS
#include "Stats/Stats2.h"

struct FNiagaraGPUTimingResult
{
	uint64 ReporterHandle;
	TStatId StatId;
	uint64 ElapsedMicroseconds;
};

/** Helper class to time gpu runtime cost of dispatches */
class FNiagaraGPUProfiler
{
public:
	FNiagaraGPUProfiler();

	/** Starts a new gpu timer. Use this directly before a dispatch call. The returned value must be used with the EndTimer method. */
	int32 StartTimer(uint64 ReporterHandle, TStatId StatId, FRHICommandList& RHICmdList);

	/** Ends a gpu timer. Use this directly after a dispatch call. The result will not be available immediately and must be queried every frame with QueryTimingResults. */
	void EndTimer(int32 TimerHandle, FRHICommandList& RHICmdList);

	/** Checks if any of the timing results are ready to be processed. They are usually available with one frame delay. */
	void QueryTimingResults(FRHICommandList& RHICmdList, TArray<FNiagaraGPUTimingResult, TInlineAllocator<16>>& OutResults);

	bool IsProfilingEnabled() const;

private:

	struct FTimerData
	{
		uint64 ReporterHandle;
		TStatId StatId;
		uint64 StartTime = 0;
		uint64 EndTime = 0;
		uint32 FrameNumber;
		FRHIPooledRenderQuery StartQuery;
		FRHIPooledRenderQuery EndQuery;
	};

	FRenderQueryPoolRHIRef QueryPool;
	TArray<FTimerData> ActiveTimers;
};
#endif