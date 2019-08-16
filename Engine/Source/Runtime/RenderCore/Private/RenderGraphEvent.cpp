// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderGraphEvent.h"
#include "RenderGraphBuilder.h"

extern bool GetEmitRDGEvents();

#if RDG_EVENTS == RDG_EVENTS_STRING_COPY

FRDGEventName::FRDGEventName(const TCHAR* EventFormat, ...)
{
	if (GetEmitRDGEvents())
	{
		va_list VAList;
		va_start(VAList, EventFormat);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, ARRAY_COUNT(TempStr), EventFormat, VAList);
		va_end(VAList);

		EventNameStorage = TempStr;
		EventName = *EventNameStorage;
	}
	else
	{
		EventName = TEXT("!!!Unavailable RDG event name: try r.RDG.EmitWarnings=1 or -rdgdebug!!!");
	}
}

#endif

FRDGEventScopeGuard::FRDGEventScopeGuard(FRDGBuilder& InGraphBuilder, FRDGEventName&& ScopeName)
	: GraphBuilder(InGraphBuilder)
{
	GraphBuilder.BeginEventScope(MoveTemp(ScopeName));
}

FRDGEventScopeGuard::~FRDGEventScopeGuard()
{
	GraphBuilder.EndEventScope();
}

static void OnPushEvent(FRHICommandListImmediate& RHICmdList, const FRDGEventScope* Scope)
{
	RHICmdList.PushEvent(Scope->Name.GetTCHAR(), FColor(0));
}

static void OnPopEvent(FRHICommandListImmediate& RHICmdList)
{
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

FRDGEventScopeStack::FRDGEventScopeStack(FRHICommandListImmediate& InRHICmdList)
	: ScopeStack(InRHICmdList, &OnPushEvent, &OnPopEvent)
{}

void FRDGEventScopeStack::BeginScope(FRDGEventName&& EventName)
{
	if (IsEnabled())
	{
		ScopeStack.BeginScope(Forward<FRDGEventName&&>(EventName));
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
		ScopeStack.BeginExecutePass(Pass->GetEventScope());

		// Push the pass event with some color.
		FColor Color(0, 0, 0);

		if (Pass->IsCompute())
		{
			// Green for compute.
			Color = FColor(128, 255, 128);
		}
		else
		{
			// Red for rasterizer.
			Color = FColor(255, 128, 128);
		}

		ScopeStack.RHICmdList.PushEvent(Pass->GetName(), Color);
	}
}

void FRDGEventScopeStack::EndExecutePass()
{
	if (IsEnabled())
	{
		ScopeStack.RHICmdList.PopEvent();
	}
}

void FRDGEventScopeStack::EndExecute()
{
	if (IsEnabled())
	{
		ScopeStack.EndExecute();
	}
}

FRDGStatScopeGuard::FRDGStatScopeGuard(FRDGBuilder& InGraphBuilder, const FName& Name, const FName& StatName)
	: GraphBuilder(InGraphBuilder)
{
	GraphBuilder.BeginStatScope(Name, StatName);
}

FRDGStatScopeGuard::~FRDGStatScopeGuard()
{
	GraphBuilder.EndStatScope();
}

static void OnPushStat(FRHICommandListImmediate& RHICmdList, const FRDGStatScope* Scope)
{
#if HAS_GPU_STATS
	FRealtimeGPUProfiler::Get()->PushEvent(RHICmdList, Scope->Name, Scope->StatName);
#endif
}

static void OnPopStat(FRHICommandListImmediate& RHICmdList)
{
#if HAS_GPU_STATS
	FRealtimeGPUProfiler::Get()->PopEvent(RHICmdList);
#endif
}

bool FRDGStatScopeStack::IsEnabled()
{
#if HAS_GPU_STATS
	return AreGPUStatsEnabled();
#else
	return false;
#endif
}

FRDGStatScopeStack::FRDGStatScopeStack(FRHICommandListImmediate& InRHICmdList)
	: ScopeStack(InRHICmdList, &OnPushStat, &OnPopStat)
{}

void FRDGStatScopeStack::BeginScope(const FName& Name, const FName& StatName)
{
	if (IsEnabled())
	{
		ScopeStack.BeginScope(Name, StatName);
	}
}

void FRDGStatScopeStack::EndScope()
{
	if (IsEnabled())
	{
		ScopeStack.EndScope();
	}
}

void FRDGStatScopeStack::BeginExecute()
{
	if (IsEnabled())
	{
		ScopeStack.BeginExecute();
	}
}

void FRDGStatScopeStack::BeginExecutePass(const FRDGPass* Pass)
{
	if (IsEnabled())
	{
		ScopeStack.BeginExecutePass(Pass->GetStatScope());
	}
}

void FRDGStatScopeStack::EndExecute()
{
	if (IsEnabled())
	{
		ScopeStack.EndExecute();
	}
}