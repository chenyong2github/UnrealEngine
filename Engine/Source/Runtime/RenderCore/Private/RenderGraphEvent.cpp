// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphEvent.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphPrivate.h"
#include "RenderGraphPass.h"

RENDERCORE_API bool GetEmitRDGEvents()
{
	check(IsInRenderingThread());
#if RDG_EVENTS != RDG_EVENTS_NONE
	return GetEmitDrawEvents() || GRDGDebug;
#else
	return false;
#endif
}

#if RDG_EVENTS == RDG_EVENTS_STRING_COPY

FRDGEventName::FRDGEventName(const TCHAR* InEventFormat, ...)
	: EventFormat(InEventFormat)
{
	check(InEventFormat);

	if (GetEmitRDGEvents())
	{
		va_list VAList;
		va_start(VAList, InEventFormat);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, UE_ARRAY_COUNT(TempStr), InEventFormat, VAList);
		va_end(VAList);

		FormatedEventName = TempStr;
	}
}

#endif

#if RDG_GPU_SCOPES

static void GetEventScopePathRecursive(const FRDGEventScope* Root, FString& String)
{
	if (Root->ParentScope)
	{
		GetEventScopePathRecursive(Root->ParentScope, String);
	}

	if (!String.IsEmpty())
	{
		String += TEXT(".");
	}

	String += Root->Name.GetTCHAR();
}

FString FRDGEventScope::GetPath(const FRDGEventName& Event) const
{
	FString Path;
	GetEventScopePathRecursive(this, Path);
	Path += TEXT(".");
	Path += Event.GetTCHAR();
	return MoveTemp(Path);
}

FRDGEventScopeGuard::FRDGEventScopeGuard(FRDGBuilder& InGraphBuilder, FRDGEventName&& ScopeName, bool InbCondition)
	: GraphBuilder(InGraphBuilder)
	, bCondition(InbCondition)
{
	if (bCondition)
	{
		GraphBuilder.GPUScopeStacks.BeginEventScope(MoveTemp(ScopeName));
	}
}

FRDGEventScopeGuard::~FRDGEventScopeGuard()
{
	if (bCondition)
	{
		GraphBuilder.GPUScopeStacks.EndEventScope();
	}
}

static void OnPushEvent(FRHIComputeCommandList& RHICmdList, const FRDGEventScope* Scope)
{
	SCOPED_GPU_MASK(RHICmdList, Scope->GPUMask);
	RHICmdList.PushEvent(Scope->Name.GetTCHAR(), FColor(0));
}

static void OnPopEvent(FRHIComputeCommandList& RHICmdList, const FRDGEventScope* Scope)
{
	SCOPED_GPU_MASK(RHICmdList, Scope->GPUMask);
	RHICmdList.PopEvent();
}

bool FRDGEventScopeStack::IsEnabled()
{
#if RDG_EVENTS
	return GetEmitRDGEvents();
#else
	return false;
#endif
}

FRDGEventScopeStack::FRDGEventScopeStack(FRHIComputeCommandList& InRHICmdList)
	: ScopeStack(InRHICmdList, &OnPushEvent, &OnPopEvent)
{}

void FRDGEventScopeStack::BeginScope(FRDGEventName&& EventName)
{
	if (IsEnabled())
	{
		ScopeStack.BeginScope(Forward<FRDGEventName&&>(EventName), ScopeStack.RHICmdList.GetGPUMask());
	}
}

void FRDGEventScopeStack::EndScope()
{
	if (IsEnabled())
	{
		ScopeStack.EndScope();
	}
}

void FRDGEventScopeStack::BeginExecute()
{
	if (IsEnabled())
	{
		ScopeStack.BeginExecute();
	}
}

void FRDGEventScopeStack::BeginExecutePass(const FRDGPass* Pass)
{
	if (IsEnabled())
	{
		ScopeStack.BeginExecutePass(Pass->GetGPUScopes().Event);

		// Skip empty strings.
		const TCHAR* Name = Pass->GetEventName().GetTCHAR();

		if (Name && *Name)
		{
			FColor Color(255, 255, 255);
			ScopeStack.RHICmdList.PushEvent(Name, Color);
			bEventPushed = true;
		}
	}
}

void FRDGEventScopeStack::EndExecutePass()
{
	if (IsEnabled() && bEventPushed)
	{
		ScopeStack.RHICmdList.PopEvent();
		bEventPushed = false;
	}
}

void FRDGEventScopeStack::EndExecute()
{
	if (IsEnabled())
	{
		ScopeStack.EndExecute();
	}
}

FRDGGPUStatScopeGuard::FRDGGPUStatScopeGuard(FRDGBuilder& InGraphBuilder, const FName& Name, const FName& StatName, int32(*NumDrawCallsPtr)[MAX_NUM_GPUS])
	: GraphBuilder(InGraphBuilder)
{
	GraphBuilder.GPUScopeStacks.BeginStatScope(Name, StatName, NumDrawCallsPtr);
}

FRDGGPUStatScopeGuard::~FRDGGPUStatScopeGuard()
{
	GraphBuilder.GPUScopeStacks.EndStatScope();
}

static void OnPushGPUStat(FRHIComputeCommandList& RHICmdList, const FRDGGPUStatScope* Scope)
{
#if HAS_GPU_STATS
	// GPU stats are currently only supported on the immediate command list.
	if (RHICmdList.IsImmediate())
	{
		FRealtimeGPUProfiler::Get()->PushStat(static_cast<FRHICommandListImmediate&>(RHICmdList), Scope->Name, Scope->StatName, Scope->DrawCallCounter);

		if (Scope->DrawCallCounter != nullptr)
		{
			static_cast<FRHICommandListImmediate&>(RHICmdList).EnqueueLambda(
				[DrawCallCounter = Scope->DrawCallCounter](FRHICommandListImmediate&)
			{
				GCurrentNumDrawCallsRHIPtr = DrawCallCounter;
			});
		}

	}
#endif
}

static void OnPopGPUStat(FRHIComputeCommandList& RHICmdList, const FRDGGPUStatScope* Scope)
{
#if HAS_GPU_STATS
	// GPU stats are currently only supported on the immediate command list.
	if (RHICmdList.IsImmediate())
	{
		FRealtimeGPUProfiler::Get()->PopStat(static_cast<FRHICommandListImmediate&>(RHICmdList), Scope->DrawCallCounter);

		if (Scope->DrawCallCounter != nullptr)
		{
			static_cast<FRHICommandListImmediate&>(RHICmdList).EnqueueLambda(
				[](FRHICommandListImmediate&)
			{
				GCurrentNumDrawCallsRHIPtr = &GCurrentNumDrawCallsRHI;
			});
		}
	}
#endif
}

bool FRDGGPUStatScopeStack::IsEnabled()
{
#if HAS_GPU_STATS
	return AreGPUStatsEnabled();
#else
	return false;
#endif
}

FRDGGPUStatScopeStack::FRDGGPUStatScopeStack(FRHIComputeCommandList& InRHICmdList)
	: ScopeStack(InRHICmdList, &OnPushGPUStat, &OnPopGPUStat)
{}

void FRDGGPUStatScopeStack::BeginScope(const FName& Name, const FName& StatName, int32 (*DrawCallCounter)[MAX_NUM_GPUS])
{
	if (IsEnabled())
	{
		check(DrawCallCounter != nullptr);
		ScopeStack.BeginScope(Name, StatName, DrawCallCounter);
	}
}

void FRDGGPUStatScopeStack::EndScope()
{
	if (IsEnabled())
	{
		ScopeStack.EndScope();
	}
}

void FRDGGPUStatScopeStack::BeginExecute()
{
	if (IsEnabled())
	{
		ScopeStack.BeginExecute();
	}
}

void FRDGGPUStatScopeStack::BeginExecutePass(const FRDGPass* Pass)
{
	if (IsEnabled())
	{
		ScopeStack.BeginExecutePass(Pass->GetGPUScopes().Stat);
	}
}

void FRDGGPUStatScopeStack::EndExecute()
{
	if (IsEnabled())
	{
		ScopeStack.EndExecute();
	}
}

void FRDGGPUScopeStacksByPipeline::BeginExecutePass(const FRDGPass* Pass)
{
	ERHIPipeline Pipeline = Pass->GetPipeline();

	/**TODO(RDG): This currently crashes certain platforms. */
	if (GRDGAsyncCompute == RDG_ASYNC_COMPUTE_FORCE_ENABLED)
	{
		Pipeline = ERHIPipeline::Graphics;
	}

	GetScopeStacks(Pipeline).BeginExecutePass(Pass);
}

void FRDGGPUScopeStacksByPipeline::EndExecutePass(const FRDGPass* Pass)
{
	ERHIPipeline Pipeline = Pass->GetPipeline();

	/**TODO(RDG): This currently crashes certain platforms. */
	if (GRDGAsyncCompute == RDG_ASYNC_COMPUTE_FORCE_ENABLED)
	{
		Pipeline = ERHIPipeline::Graphics;
	}

	GetScopeStacks(Pipeline).EndExecutePass();
}

#endif

//////////////////////////////////////////////////////////////////////////
// CPU Scopes
//////////////////////////////////////////////////////////////////////////

#if RDG_CPU_SCOPES

#if CSV_PROFILER

FRDGScopedCsvStatExclusive::FRDGScopedCsvStatExclusive(FRDGBuilder& InGraphBuilder, const char* InStatName)
	: FScopedCsvStatExclusive(InStatName)
	, GraphBuilder(InGraphBuilder)
{
	GraphBuilder.CPUScopeStacks.CSV.BeginScope(InStatName);
}

FRDGScopedCsvStatExclusive::~FRDGScopedCsvStatExclusive()
{
	GraphBuilder.CPUScopeStacks.CSV.EndScope();
}

FRDGScopedCsvStatExclusiveConditional::FRDGScopedCsvStatExclusiveConditional(FRDGBuilder& InGraphBuilder, const char* InStatName, bool bInCondition)
	: FScopedCsvStatExclusiveConditional(InStatName, bInCondition)
	, GraphBuilder(InGraphBuilder)
{
	if (bCondition)
	{
		GraphBuilder.CPUScopeStacks.CSV.BeginScope(InStatName);
	}
}

FRDGScopedCsvStatExclusiveConditional::~FRDGScopedCsvStatExclusiveConditional()
{
	if (bCondition)
	{
		GraphBuilder.CPUScopeStacks.CSV.EndScope();
	}
}

#endif

static void OnPushCSVStat(FRHIComputeCommandList&, const FRDGCSVStatScope* Scope)
{
#if CSV_PROFILER
	FCsvProfiler::BeginExclusiveStat(Scope->StatName);
#if CSV_EXCLUSIVE_TIMING_STATS_EMIT_NAMED_EVENTS
	FPlatformMisc::BeginNamedEvent(FColor(255, 128, 128), Scope->StatName);
#endif
#endif
}

static void OnPopCSVStat(FRHIComputeCommandList&, const FRDGCSVStatScope* Scope)
{
#if CSV_PROFILER
#if CSV_EXCLUSIVE_TIMING_STATS_EMIT_NAMED_EVENTS
	FPlatformMisc::EndNamedEvent();
#endif
	FCsvProfiler::EndExclusiveStat(Scope->StatName);
#endif
}

FRDGCSVStatScopeStack::FRDGCSVStatScopeStack(FRHIComputeCommandList& InRHICmdList, const char* InUnaccountedStatName)
	: ScopeStack(InRHICmdList, &OnPushCSVStat, &OnPopCSVStat)
	, UnaccountedStatName(InUnaccountedStatName)
{
	BeginScope(UnaccountedStatName);
}

bool FRDGCSVStatScopeStack::IsEnabled()
{
#if CSV_PROFILER
	return true;
#else
	return false;
#endif
}

void FRDGCSVStatScopeStack::BeginScope(const char* StatName)
{
	if (IsEnabled())
	{
		ScopeStack.BeginScope(StatName);
	}
}

void FRDGCSVStatScopeStack::EndScope()
{
	if (IsEnabled())
	{
		ScopeStack.EndScope();
	}
}

void FRDGCSVStatScopeStack::BeginExecute()
{
	if (IsEnabled())
	{
		EndScope();
		ScopeStack.BeginExecute();
	}
}

void FRDGCSVStatScopeStack::BeginExecutePass(const FRDGPass* Pass)
{
	if (IsEnabled())
	{
		ScopeStack.BeginExecutePass(Pass->GetCPUScopes().CSV);
	}
}

void FRDGCSVStatScopeStack::EndExecute()
{
	if (IsEnabled())
	{
		ScopeStack.EndExecute();
	}
}

#endif