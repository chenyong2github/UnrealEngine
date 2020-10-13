// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGPUProfiler.h"
#include "HAL/IConsoleManager.h"

int32 GNiagaraGpuProfilingEnabled = 0;
static FAutoConsoleVariableRef CVarNiagaraGpuProfilingEnabled(
    TEXT("fx.NiagaraGpuProfilingEnabled"),
    GNiagaraGpuProfilingEnabled,
    TEXT("Used by the profiling tool in the system overview to enable or disable gathering of gpu stats.\n"),
    ECVF_Default
);

#if STATS
FNiagaraGPUProfiler::FNiagaraGPUProfiler()
{
	QueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime);
}

int32 FNiagaraGPUProfiler::StartTimer(uint64 ReporterHandle, TStatId StatId, FRHICommandList& RHICmdList)
{
	check(IsInRenderingThread());

	if (GNiagaraGpuProfilingEnabled == 0)
	{
		return -1;
	}

	int32 TimerIndex = ActiveTimers.AddZeroed();
	FTimerData& Timer = ActiveTimers[TimerIndex];
	Timer.ReporterHandle = ReporterHandle;
	Timer.StatId = StatId;
	Timer.StartQuery = QueryPool->AllocateQuery();
	Timer.EndQuery = QueryPool->AllocateQuery();
	Timer.FrameNumber = GFrameNumberRenderThread;

	RHICmdList.EndRenderQuery(Timer.StartQuery.GetQuery());
	return TimerIndex;
}

void FNiagaraGPUProfiler::EndTimer(int32 TimerHandle, FRHICommandList& RHICmdList)
{
	check(IsInRenderingThread());
	if (GNiagaraGpuProfilingEnabled == 0 || TimerHandle < 0)
	{
		return;
	}
	check(ActiveTimers.IsValidIndex(TimerHandle));
	
	RHICmdList.EndRenderQuery(ActiveTimers[TimerHandle].EndQuery.GetQuery());
}

void FNiagaraGPUProfiler::QueryTimingResults(FRHICommandList& RHICmdList, TArray<FNiagaraGPUTimingResult, TInlineAllocator<16>>& OutResults)
{
	check(IsInRenderingThread());
	
	if (ActiveTimers.Num() == 0 || !RHICmdList.IsImmediate())
	{
		return;
	}

	FRHICommandListImmediate* ImmediateCmdList = (FRHICommandListImmediate*)&RHICmdList;	
	for (int i = ActiveTimers.Num() - 1; i >= 0; i--)
	{
		FTimerData& Timer = ActiveTimers[i];
		if (Timer.FrameNumber != GFrameNumberRenderThread)
		{
			if (Timer.StartTime == 0 && !ImmediateCmdList->GetRenderQueryResult(Timer.StartQuery.GetQuery(), Timer.StartTime, false))
			{
				continue;
			}
			if (Timer.EndTime == 0 && !ImmediateCmdList->GetRenderQueryResult(Timer.EndQuery.GetQuery(), Timer.EndTime, false))
			{
				continue;
			}

			FNiagaraGPUTimingResult TimingResult;
			TimingResult.ReporterHandle = Timer.ReporterHandle;
			TimingResult.StatId = Timer.StatId;
			TimingResult.ElapsedMicroseconds = Timer.EndTime - Timer.StartTime;
			OutResults.Add(TimingResult);
			ActiveTimers.RemoveAtSwap(i);
		}
	}
}

bool FNiagaraGPUProfiler::IsProfilingEnabled() const
{
	return GNiagaraGpuProfilingEnabled > 0;
}

#endif
