// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphEvent.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphPrivate.h"
#include "RenderGraphPass.h"

RENDERCORE_API bool GetEmitRDGEvents()
{
#if RDG_EVENTS != RDG_EVENTS_NONE
	bool bRDGChannelEnabled = false;
#if RDG_ENABLE_TRACE
	bRDGChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(RDGChannel);
#endif // RDG_ENABLE_TRACE
	return GRDGEmitEvents != 0 || GRDGDebug != 0 || bRDGChannelEnabled != 0;
#else
	return false;
#endif
}

#if RDG_EVENTS == RDG_EVENTS_STRING_COPY

FRDGEventName::FRDGEventName(const TCHAR* InEventFormat, ...)
	: EventFormat(InEventFormat)
{
	check(InEventFormat);

	{
		va_list VAList;
		va_start(VAList, InEventFormat);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, UE_ARRAY_COUNT(TempStr), InEventFormat, VAList);
		va_end(VAList);

		FormattedEventName = TempStr;
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
		GraphBuilder.GPUScopeStacks.BeginEventScope(MoveTemp(ScopeName), GraphBuilder.RHICmdList.GetGPUMask());
	}
}

FRDGEventScopeGuard::~FRDGEventScopeGuard()
{
	if (bCondition)
	{
		GraphBuilder.GPUScopeStacks.EndEventScope();
	}
}

static void OnPushEvent(FRHIComputeCommandList& RHICmdList, const FRDGEventScope* Scope, bool bRDGEvents)
{
#if RHI_WANT_BREADCRUMB_EVENTS
	RHICmdList.PushBreadcrumb(Scope->Name.GetTCHAR());
#endif

	if (bRDGEvents)
	{
		SCOPED_GPU_MASK(RHICmdList, Scope->GPUMask);
		RHICmdList.PushEvent(Scope->Name.GetTCHAR(), FColor(0));
	}
}

static void OnPopEvent(FRHIComputeCommandList& RHICmdList, const FRDGEventScope* Scope, bool bRDGEvents)
{
	if (bRDGEvents)
	{
		SCOPED_GPU_MASK(RHICmdList, Scope->GPUMask);
		RHICmdList.PopEvent();
	}

#if RHI_WANT_BREADCRUMB_EVENTS
	RHICmdList.PopBreadcrumb();
#endif
}

void FRDGEventScopeOpArray::Execute(FRHIComputeCommandList& RHICmdList)
{
	for (int32 Index = 0; Index < Ops.Num(); ++Index)
	{
		FRDGEventScopeOp Op = Ops[Index];

		if (Op.IsScope())
		{
			if (Op.IsPush())
			{
				OnPushEvent(RHICmdList, Op.Scope, bRDGEvents);
			}
			else
			{
				OnPopEvent(RHICmdList, Op.Scope, bRDGEvents);
			}
		}
		else
		{
			if (Op.IsPush())
			{
				RHICmdList.PushEvent(Op.Name, FColor(255, 255, 255));
			}
			else
			{
				RHICmdList.PopEvent();
			}
		}
	}
}

#if RHI_WANT_BREADCRUMB_EVENTS

void FRDGEventScopeOpArray::Execute(FRDGBreadcrumbState& State)
{
	for (int32 Index = 0; Index < Ops.Num(); ++Index)
	{
		FRDGEventScopeOp Op = Ops[Index];

		if (Op.IsScope())
		{
			if (Op.IsPush())
			{
				State.PushBreadcrumb(Op.Scope->Name.GetTCHAR());
				State.Version++;
			}
			else
			{
				State.PopBreadcrumb();
				State.Version++;
			}
		}
	}
}

#endif

FRDGEventScopeOpArray FRDGEventScopeStack::CompilePassPrologue(const FRDGPass* Pass)
{
	FRDGEventScopeOpArray Ops(bRDGEvents);
	if (IsEnabled())
	{
		Ops.Ops = ScopeStack.CompilePassPrologue(Pass->GetGPUScopes().Event, GetEmitRDGEvents() ? Pass->GetEventName().GetTCHAR() : nullptr);
	}
	return MoveTemp(Ops);
}

FRDGEventScopeOpArray FRDGEventScopeStack::CompilePassEpilogue()
{
	FRDGEventScopeOpArray Ops(bRDGEvents);
	if (IsEnabled())
	{
		Ops.Ops = ScopeStack.CompilePassEpilogue();
	}
	return MoveTemp(Ops);
}

FRDGGPUStatScopeGuard::FRDGGPUStatScopeGuard(FRDGBuilder& InGraphBuilder, const FName& Name, const FName& StatName, const TCHAR* Description, int32(*NumDrawCallsPtr)[MAX_NUM_GPUS])
	: GraphBuilder(InGraphBuilder)
{
	GraphBuilder.GPUScopeStacks.BeginStatScope(Name, StatName, Description, NumDrawCallsPtr);
}

FRDGGPUStatScopeGuard::~FRDGGPUStatScopeGuard()
{
	GraphBuilder.GPUScopeStacks.EndStatScope();
}

FRDGGPUStatScopeOpArray::FRDGGPUStatScopeOpArray(TRDGScopeOpArray<FRDGGPUStatScopeOp> InOps, FRHIGPUMask GPUMask)
	: Ops(InOps)
	, Type(EType::Prologue)
{
#if HAS_GPU_STATS
	for (int32 Index = 0; Index < Ops.Num(); ++Index)
	{
		FRDGGPUStatScopeOp& Op = Ops[Index];

		if (Op.IsPush())
		{
			Op.Query = FRealtimeGPUProfiler::Get()->PushEvent(GPUMask, Op.Scope->Name, Op.Scope->StatName, *Op.Scope->Description);
		}
		else
		{
			Op.Query = FRealtimeGPUProfiler::Get()->PopEvent();
		}
	}
#endif
}

void FRDGGPUStatScopeOpArray::Execute(FRHIComputeCommandList& RHICmdListCompute)
{
#if HAS_GPU_STATS
	if (!RHICmdListCompute.IsGraphics())
	{
		return;
	}

	FRHICommandList& RHICmdList = static_cast<FRHICommandList&>(RHICmdListCompute);

	for (int32 Index = 0; Index < Ops.Num(); ++Index)
	{
		Ops[Index].Query.Submit(RHICmdList);
	}

	if (OverrideEventIndex != kInvalidEventIndex)
	{
		if (Type == EType::Prologue)
		{
			FRealtimeGPUProfiler::Get()->PushEventOverride(OverrideEventIndex);
		}
		else
		{
			FRealtimeGPUProfiler::Get()->PopEventOverride();
		}
	}

	for (int32 Index = Ops.Num() - 1; Index >= 0; --Index)
	{
		const FRDGGPUStatScopeOp Op = Ops[Index];
		const FRDGGPUStatScope* Scope = Op.Scope;

		if (Scope->DrawCallCounter != nullptr && (**Scope->DrawCallCounter) != -1)
		{
			RHICmdList.EnqueueLambda(
				[DrawCallCounter = Scope->DrawCallCounter, bPush = Op.IsPush()](auto&)
			{
				GCurrentNumDrawCallsRHIPtr = bPush ? DrawCallCounter : &GCurrentNumDrawCallsRHI;
			});
			break;
		}
	}
#endif
}

FRDGGPUStatScopeOpArray FRDGGPUStatScopeStack::CompilePassPrologue(const FRDGPass* Pass, FRHIGPUMask GPUMask)
{
#if HAS_GPU_STATS
	if (IsEnabled() && Pass->GetPipeline() == ERHIPipeline::Graphics)
	{
		FRDGGPUStatScopeOpArray Ops(ScopeStack.CompilePassPrologue(Pass->GetGPUScopes().Stat), GPUMask);
		if (!Pass->IsParallelExecuteAllowed())
		{
			OverrideEventIndex = FRealtimeGPUProfiler::Get()->GetCurrentEventIndex();
			Ops.OverrideEventIndex = OverrideEventIndex;
		}
		return MoveTemp(Ops);
	}
#endif
	return {};
}

FRDGGPUStatScopeOpArray FRDGGPUStatScopeStack::CompilePassEpilogue()
{
#if HAS_GPU_STATS
	if (OverrideEventIndex != FRDGGPUStatScopeOpArray::kInvalidEventIndex)
	{
		FRDGGPUStatScopeOpArray Ops;
		Ops.OverrideEventIndex = OverrideEventIndex;
		OverrideEventIndex = FRDGGPUStatScopeOpArray::kInvalidEventIndex;
		return MoveTemp(Ops);
	}
#endif
	return {};
}

FRDGGPUScopeOpArrays FRDGGPUScopeStacksByPipeline::CompilePassPrologue(const FRDGPass* Pass, FRHIGPUMask GPUMask)
{
	return GetScopeStacks(Pass->GetPipeline()).CompilePassPrologue(Pass, GPUMask);
}

FRDGGPUScopeOpArrays FRDGGPUScopeStacksByPipeline::CompilePassEpilogue(const FRDGPass* Pass)
{
	return GetScopeStacks(Pass->GetPipeline()).CompilePassEpilogue();
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

inline void OnPushCSVStat(const FRDGCSVStatScope* Scope)
{
#if CSV_PROFILER
	FCsvProfiler::BeginExclusiveStat(Scope->StatName);
#endif
}

inline void OnPopCSVStat(const FRDGCSVStatScope* Scope)
{
#if CSV_PROFILER
	FCsvProfiler::EndExclusiveStat(Scope->StatName);
#endif
}

void FRDGCSVStatScopeOpArray::Execute()
{
	for (int32 Index = 0; Index < Ops.Num(); ++Index)
	{
		FRDGCSVStatScopeOp Op = Ops[Index];

		if (Op.IsPush())
		{
			OnPushCSVStat(Op.Scope);
		}
		else
		{
			OnPopCSVStat(Op.Scope);
		}
	}
}

FRDGCSVStatScopeOpArray FRDGCSVStatScopeStack::CompilePassPrologue(const FRDGPass* Pass)
{
	if (IsEnabled())
	{
		return ScopeStack.CompilePassPrologue(Pass->GetCPUScopes().CSV);
	}
	return {};
}

#endif
