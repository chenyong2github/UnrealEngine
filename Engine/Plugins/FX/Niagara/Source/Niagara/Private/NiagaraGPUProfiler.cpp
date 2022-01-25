// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGPUProfiler.h"
#include "NiagaraGPUSystemTick.h"
#include "NiagaraSimStageData.h"
#include "HAL/IConsoleManager.h"
#include "Misc/StringBuilder.h"

bool GNiagaraGpuProfilingEnabled = true;
static FAutoConsoleVariableRef CVarNiagaraGpuProfilingEnabled(
	TEXT("fx.Niagara.GpuProfiling.Enabled"),
	GNiagaraGpuProfilingEnabled,
	TEXT("Master control to allow Niagara to use GPU profiling or not.\n"),
	ECVF_Default
);

#if WITH_NIAGARA_GPU_PROFILER

FNiagaraGPUProfiler::FNiagaraGPUProfiler(uintptr_t InOwnerContext)
	: OwnerContext(InOwnerContext)
{
	QueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime);
}

FNiagaraGPUProfiler::~FNiagaraGPUProfiler()
{
	for (FGpuFrameData& Frame : GpuFrames)
	{
		Frame.EndQuery.ReleaseQuery();
		for (FGpuStageTimer& StageTimer : Frame.StageTimers)
		{
			StageTimer.StartQuery.ReleaseQuery();
			StageTimer.EndQuery.ReleaseQuery();
		}

		for (FGpuDispatchTimer& DispatchTimer : Frame.DispatchTimers)
		{
			DispatchTimer.StartQuery.ReleaseQuery();
			DispatchTimer.EndQuery.ReleaseQuery();
		}
	}
}

void FNiagaraGPUProfiler::BeginFrame(FRHICommandListImmediate& RHICmdList)
{
	// Process all frames until we run out
	while (FGpuFrameData* ReadFrame = GetReadFrame())
	{
		// Attempt to process, might not be read
		if ( !ProcessFrame(RHICmdList, *ReadFrame) )
		{
			break;
		}

		// Frame was processed
		CurrentReadFrame = (CurrentReadFrame + 1) % NumBufferFrames;
	}

	// If we are not enabled nothing to profile
	static const auto CSVStatsEnabledCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.DetailedCSVStats"));
	const bool bCsvCollecting = CSVStatsEnabledCVar && CSVStatsEnabledCVar->GetBool();
	if ( !GNiagaraGpuProfilingEnabled || ((NumReaders == 0) && !bCsvCollecting) )
	{
		return;
	}

	// Get frame to write into
	ActiveWriteFrame = GetWriteFrame();
}

void FNiagaraGPUProfiler::EndFrame(FRHICommandList& RHICmdList)
{
	if (ActiveWriteFrame == nullptr)
	{
		return;
	}

	// Inject end marker so we know if all dispatches are complete
	ActiveWriteFrame->EndQuery = QueryPool->AllocateQuery();
	RHICmdList.EndRenderQuery(ActiveWriteFrame->EndQuery.GetQuery());

	CurrentWriteFrame = (CurrentWriteFrame + 1) % NumBufferFrames;
}

void FNiagaraGPUProfiler::BeginStage(FRHICommandList& RHICmdList, ENiagaraGpuComputeTickStage::Type TickStage, int32 NumDispatchGroups)
{
	if (ActiveWriteFrame == nullptr)
	{
		return;
	}

	FGpuStageTimer& StageTimer = ActiveWriteFrame->StageTimers[TickStage];
	StageTimer.NumDispatchGroups = NumDispatchGroups;
	StageTimer.StartQuery = QueryPool->AllocateQuery();
	RHICmdList.EndRenderQuery(StageTimer.StartQuery.GetQuery());
}

void FNiagaraGPUProfiler::EndStage(FRHICommandList& RHICmdList, ENiagaraGpuComputeTickStage::Type TickStage, int32 NumDispatches)
{
	if (ActiveWriteFrame == nullptr)
	{
		return;
	}

	FGpuStageTimer& StageTimer = ActiveWriteFrame->StageTimers[TickStage];
	StageTimer.NumDispatches = NumDispatches;
	StageTimer.EndQuery = QueryPool->AllocateQuery();
	RHICmdList.EndRenderQuery(StageTimer.EndQuery.GetQuery());
}

void FNiagaraGPUProfiler::BeginDispatch(FRHICommandList& RHICmdList, const FNiagaraGpuDispatchInstance& DispatchInstance)
{
	if (ActiveWriteFrame == nullptr)
	{
		return;
	}
	check(bDispatchRecursionGuard == false);
	bDispatchRecursionGuard = true;

	FGpuDispatchTimer& DispatchTimer = ActiveWriteFrame->DispatchTimers.AddDefaulted_GetRef();

	DispatchTimer.bUniqueInstance = DispatchInstance.SimStageData.bSetDataToRender && (&DispatchInstance.InstanceData == &DispatchInstance.Tick.GetInstances()[0]);
	DispatchTimer.OwnerComponent = DispatchInstance.InstanceData.Context->ProfilingComponentPtr;
	DispatchTimer.OwnerEmitter = DispatchInstance.InstanceData.Context->ProfilingEmitterPtr;
	DispatchTimer.StageName = DispatchInstance.SimStageData.StageMetaData->SimulationStageName;
	DispatchTimer.StartQuery = QueryPool->AllocateQuery();
	RHICmdList.EndRenderQuery(DispatchTimer.StartQuery.GetQuery());
}

void FNiagaraGPUProfiler::BeginDispatch(FRHICommandList& RHICmdList, const struct FNiagaraComputeInstanceData& InstanceData, FName StageName)
{
	if (ActiveWriteFrame == nullptr)
	{
		return;
	}
	check(bDispatchRecursionGuard == false);
	bDispatchRecursionGuard = true;

	FGpuDispatchTimer& DispatchTimer = ActiveWriteFrame->DispatchTimers.AddDefaulted_GetRef();
	DispatchTimer.bUniqueInstance = false;
	DispatchTimer.OwnerComponent = InstanceData.Context->ProfilingComponentPtr;
	DispatchTimer.OwnerEmitter = InstanceData.Context->ProfilingEmitterPtr;
	DispatchTimer.StageName = StageName;
	DispatchTimer.StartQuery = QueryPool->AllocateQuery();
	RHICmdList.EndRenderQuery(DispatchTimer.StartQuery.GetQuery());
}

void FNiagaraGPUProfiler::BeginDispatch(FRHICommandList& RHICmdList, FName StageName)
{
	if (ActiveWriteFrame == nullptr)
	{
		return;
	}
	check(bDispatchRecursionGuard == false);
	bDispatchRecursionGuard = true;

	FGpuDispatchTimer& DispatchTimer = ActiveWriteFrame->DispatchTimers.AddDefaulted_GetRef();
	DispatchTimer.bUniqueInstance = false;
	DispatchTimer.OwnerComponent = nullptr;
	DispatchTimer.OwnerEmitter = nullptr;
	DispatchTimer.StageName = StageName;
	DispatchTimer.StartQuery = QueryPool->AllocateQuery();
	RHICmdList.EndRenderQuery(DispatchTimer.StartQuery.GetQuery());
}

void FNiagaraGPUProfiler::EndDispatch(FRHICommandList& RHICmdList)
{
	if (ActiveWriteFrame == nullptr)
	{
		return;
	}
	check(bDispatchRecursionGuard == true);
	bDispatchRecursionGuard = false;

	FGpuDispatchTimer& DispatchTimer = ActiveWriteFrame->DispatchTimers.Last();
	DispatchTimer.EndQuery = QueryPool->AllocateQuery();
	RHICmdList.EndRenderQuery(DispatchTimer.EndQuery.GetQuery());
}

bool FNiagaraGPUProfiler::ProcessFrame(FRHICommandListImmediate& RHICmdList, FGpuFrameData& ReadFrame)
{
	// Frame ready to process?
	//-OPT: We can just look at the last write stage end timer here, but that relies on the batcher always executing
	uint64 DummyEndTime;
	if (!RHICmdList.GetRenderQueryResult(ReadFrame.EndQuery.GetQuery(), DummyEndTime, false))
	{
		return false;
	}
	ReadFrame.EndQuery.ReleaseQuery();

	//-OPT: Potentially pool these
	FNiagaraGpuFrameResultsPtr FrameResults = MakeShared<FNiagaraGpuFrameResults, ESPMode::ThreadSafe>();
	FrameResults->OwnerContext = OwnerContext;
	FrameResults->DispatchResults.Reserve(ReadFrame.DispatchTimers.Num());

	// Process results
	for (int32 i = 0; i < ENiagaraGpuComputeTickStage::Max; ++i)
	{
		FGpuStageTimer& StageTimer = ReadFrame.StageTimers[i];
		auto& StageResults = FrameResults->StageResults[i];
		StageResults.NumDispatches = StageTimer.NumDispatches;
		StageResults.NumDispatchGroups = StageTimer.NumDispatchGroups;
		StageResults.DurationMicroseconds = 0;
		if ( StageTimer.StartQuery.GetQuery() != nullptr )
		{
			uint64 StartMicroseconds;
			uint64 EndMicroseconds;
			ensure(RHICmdList.GetRenderQueryResult(StageTimer.StartQuery.GetQuery(), StartMicroseconds, false));
			ensure(RHICmdList.GetRenderQueryResult(StageTimer.EndQuery.GetQuery(), EndMicroseconds, false));
			StageResults.DurationMicroseconds = EndMicroseconds - StartMicroseconds;
			StageTimer.StartQuery.ReleaseQuery();
			StageTimer.EndQuery.ReleaseQuery();
		}
		StageTimer.NumDispatches = 0;
		StageTimer.NumDispatchGroups = 0;
	}

	for (FGpuDispatchTimer& DispatchTimer : ReadFrame.DispatchTimers)
	{
		auto& DispatchResults = FrameResults->DispatchResults.AddDefaulted_GetRef();
			
		uint64 StartMicroseconds;
		uint64 EndMicroseconds;
		ensure(RHICmdList.GetRenderQueryResult(DispatchTimer.StartQuery.GetQuery(), StartMicroseconds, false));
		ensure(RHICmdList.GetRenderQueryResult(DispatchTimer.EndQuery.GetQuery(), EndMicroseconds, false));
		DispatchTimer.StartQuery.ReleaseQuery();
		DispatchTimer.EndQuery.ReleaseQuery();

		DispatchResults.bUniqueInstance = DispatchTimer.bUniqueInstance;
		DispatchResults.OwnerComponent = DispatchTimer.OwnerComponent;
		DispatchResults.OwnerEmitter = DispatchTimer.OwnerEmitter;
		DispatchResults.StageName = DispatchTimer.StageName;
		DispatchResults.DurationMicroseconds = EndMicroseconds - StartMicroseconds;
	}

	ReadFrame.DispatchTimers.Empty();

	// Post Results
	PostResults(FrameResults);

	return true;
}

#endif
