// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

template <typename TScopeType>
TRDGScopeStack<TScopeType>::TRDGScopeStack(
	FRHIComputeCommandList& InRHICmdList,
	FPushFunction InPushFunction,
	FPopFunction InPopFunction)
	: RHICmdList(InRHICmdList)
	, MemStack(FMemStack::Get())
	, PushFunction(InPushFunction)
	, PopFunction(InPopFunction)
	, ScopeStack(MakeUniformStaticArray<const TScopeType*, kScopeStackDepthMax>(nullptr))
{}

template <typename TScopeType>
TRDGScopeStack<TScopeType>::~TRDGScopeStack()
{
	ClearScopes();
}

template <typename TScopeType>
template <typename... TScopeConstructArgs>
void TRDGScopeStack<TScopeType>::BeginScope(TScopeConstructArgs... ScopeConstructArgs)
{
	auto Scope = new(MemStack) TScopeType(CurrentScope, Forward<TScopeConstructArgs>(ScopeConstructArgs)...);
	Scopes.Add(Scope);
	CurrentScope = Scope;
}

template <typename TScopeType>
void TRDGScopeStack<TScopeType>::EndScope()
{
	checkf(CurrentScope != nullptr, TEXT("Current scope is null."));
	CurrentScope = CurrentScope->ParentScope;
}

template <typename TScopeType>
void TRDGScopeStack<TScopeType>::BeginExecute()
{
	checkf(CurrentScope == nullptr, TEXT("Render graph needs to have all scopes ended to execute."));
}

template <typename TScopeType>
void TRDGScopeStack<TScopeType>::BeginExecutePass(const TScopeType* ParentScope)
{
	// Find out how many scopes needs to be popped.
	TStaticArray<const TScopeType*, kScopeStackDepthMax> TraversedScopes;
	int32 CommonScopeId = -1;
	int32 TraversedScopeCount = 0;

	// Find common ancestor between current stack and requested scope.
	while (ParentScope && TraversedScopeCount < kScopeStackDepthMax)
	{
		TraversedScopes[TraversedScopeCount] = ParentScope;

		for (int32 i = 0; i < ScopeStack.Num(); i++)
		{
			if (ScopeStack[i] == ParentScope)
			{
				CommonScopeId = i;
				break;
			}
		}

		if (CommonScopeId != -1)
		{
			break;
		}

		TraversedScopeCount++;
		ParentScope = ParentScope->ParentScope;
	}

	// Pop no longer used scopes.
	for (int32 i = CommonScopeId + 1; i < kScopeStackDepthMax; i++)
	{
		if (!ScopeStack[i])
		{
			break;
		}

		PopFunction(RHICmdList, ScopeStack[i]);
		ScopeStack[i] = nullptr;
	}

	// Push new scopes.
	for (int32 i = TraversedScopeCount - 1; i >= 0 && CommonScopeId < static_cast<int32>(kScopeStackDepthMax); i--)
	{
		PushFunction(RHICmdList, TraversedScopes[i]);
		CommonScopeId++;
		ScopeStack[CommonScopeId] = TraversedScopes[i];
	}
}

template <typename TScopeType>
void TRDGScopeStack<TScopeType>::EndExecute()
{
	for (uint32 ScopeIndex = 0; ScopeIndex < kScopeStackDepthMax; ++ScopeIndex)
	{
		if (!ScopeStack[ScopeIndex])
		{
			break;
		}

		PopFunction(RHICmdList, ScopeStack[ScopeIndex]);
	}
	ClearScopes();
}

template <typename TScopeType>
void TRDGScopeStack<TScopeType>::ClearScopes()
{
	for (int32 Index = Scopes.Num() - 1; Index >= 0; --Index)
	{
		Scopes[Index]->~TScopeType();
	}
	Scopes.Empty();
}

inline FRDGEventName::~FRDGEventName()
{
#if RDG_EVENTS == RDG_EVENTS_STRING_REF || RDG_EVENTS == RDG_EVENTS_STRING_COPY
	EventFormat = nullptr;
#endif
}

#if RDG_EVENTS != RDG_EVENTS_STRING_COPY
inline FRDGEventName::FRDGEventName(const TCHAR* InEventFormat, ...)
#if RDG_EVENTS == RDG_EVENTS_STRING_REF
	: EventFormat(InEventFormat)
#endif
{}
#endif

inline FRDGEventName::FRDGEventName(const FRDGEventName& Other)
{
	*this = Other;
}

inline FRDGEventName::FRDGEventName(FRDGEventName&& Other)
{
	*this = Forward<FRDGEventName>(Other);
}

inline FRDGEventName& FRDGEventName::operator=(const FRDGEventName& Other)
{
#if RDG_EVENTS == RDG_EVENTS_STRING_REF
	EventFormat = Other.EventFormat;
#elif RDG_EVENTS == RDG_EVENTS_STRING_COPY
	EventFormat = Other.EventFormat;
	FormatedEventName = Other.FormatedEventName;
#endif
	return *this;
}

inline FRDGEventName& FRDGEventName::operator=(FRDGEventName&& Other)
{
#if RDG_EVENTS == RDG_EVENTS_STRING_REF
	EventFormat = Other.EventFormat;
	Other.EventFormat = nullptr;
#elif RDG_EVENTS == RDG_EVENTS_STRING_COPY
	EventFormat = Other.EventFormat;
	Other.EventFormat = nullptr;
	FormatedEventName = MoveTemp(Other.FormatedEventName);
#endif
	return *this;
}

inline const TCHAR* FRDGEventName::GetTCHAR() const
{
#if RDG_EVENTS == RDG_EVENTS_STRING_REF || RDG_EVENTS == RDG_EVENTS_STRING_COPY
	#if RDG_EVENTS == RDG_EVENTS_STRING_COPY
		if (!FormatedEventName.IsEmpty())
		{
			return *FormatedEventName;
		}
	#endif

	// The event has not been formated, at least return the event format to have
	// error messages that give some clue when GetEmitRDGEvents() == false.
	return EventFormat;
#else
	// Render graph draw events have been completely compiled for CPU performance reasons.
	return TEXT("!!!Unavailable RDG event name: need RDG_EVENTS>=0 and r.RDG.EmitWarnings=1 or -rdgdebug!!!");
#endif
}

#if RDG_GPU_SCOPES

inline FRDGGPUScopeStacks::FRDGGPUScopeStacks(FRHIComputeCommandList& RHICmdList)
	: Event(RHICmdList)
	, Stat(RHICmdList)
{}

inline void FRDGGPUScopeStacks::BeginExecute()
{
	Event.BeginExecute();
	Stat.BeginExecute();
}

inline void FRDGGPUScopeStacks::BeginExecutePass(const FRDGPass* Pass)
{
	Event.BeginExecutePass(Pass);
	Stat.BeginExecutePass(Pass);
}

inline void FRDGGPUScopeStacks::EndExecutePass()
{
	Event.EndExecutePass();
}

inline void FRDGGPUScopeStacks::EndExecute()
{
	Event.EndExecute();
	Stat.EndExecute();
}

inline FRDGGPUScopes FRDGGPUScopeStacks::GetCurrentScopes() const
{
	FRDGGPUScopes Scopes;
	Scopes.Event = Event.GetCurrentScope();
	Scopes.Stat = Stat.GetCurrentScope();
	return Scopes;
}

inline FRDGGPUScopeStacksByPipeline::FRDGGPUScopeStacksByPipeline(FRHICommandListImmediate& RHICmdListGraphics, FRHIComputeCommandList& RHICmdListAsyncCompute)
	: Graphics(RHICmdListGraphics)
	, AsyncCompute(RHICmdListAsyncCompute)
{}

inline void FRDGGPUScopeStacksByPipeline::BeginEventScope(FRDGEventName&& ScopeName)
{
	FRDGEventName ScopeNameCopy = ScopeName;
	Graphics.Event.BeginScope(MoveTemp(ScopeNameCopy));
	AsyncCompute.Event.BeginScope(MoveTemp(ScopeName));
}

inline void FRDGGPUScopeStacksByPipeline::EndEventScope()
{
	Graphics.Event.EndScope();
	AsyncCompute.Event.EndScope();
}

inline void FRDGGPUScopeStacksByPipeline::BeginStatScope(const FName& Name, const FName& StatName, int32 (*DrawCallCounter)[MAX_NUM_GPUS])
{
	Graphics.Stat.BeginScope(Name, StatName, DrawCallCounter);
	AsyncCompute.Stat.BeginScope(Name, StatName, DrawCallCounter);
}

inline void FRDGGPUScopeStacksByPipeline::EndStatScope()
{
	Graphics.Stat.EndScope();
	AsyncCompute.Stat.EndScope();
}

inline void FRDGGPUScopeStacksByPipeline::BeginExecute()
{
	Graphics.BeginExecute();
	AsyncCompute.BeginExecute();
}

inline void FRDGGPUScopeStacksByPipeline::EndExecute()
{
	Graphics.EndExecute();
	AsyncCompute.EndExecute();
}

inline const FRDGGPUScopeStacks& FRDGGPUScopeStacksByPipeline::GetScopeStacks(ERHIPipeline Pipeline) const
{
	switch (Pipeline)
	{
	case ERHIPipeline::Graphics:
		return Graphics;
	case ERHIPipeline::AsyncCompute:
		return AsyncCompute;
	default:
		checkNoEntry();
		return Graphics;
	}
}

inline FRDGGPUScopeStacks& FRDGGPUScopeStacksByPipeline::GetScopeStacks(ERHIPipeline Pipeline)
{
	switch (Pipeline)
	{
	case ERHIPipeline::Graphics:
		return Graphics;
	case ERHIPipeline::AsyncCompute:
		return AsyncCompute;
	default:
		checkNoEntry();
		return Graphics;
	}
}

inline FRDGGPUScopes FRDGGPUScopeStacksByPipeline::GetCurrentScopes(ERHIPipeline Pipeline) const
{
	return GetScopeStacks(Pipeline).GetCurrentScopes();
}

#endif

//////////////////////////////////////////////////////////////////////////
// CPU Scopes
//////////////////////////////////////////////////////////////////////////

#if RDG_CPU_SCOPES

inline FRDGCPUScopeStacks::FRDGCPUScopeStacks(FRHIComputeCommandList& RHICmdList, const char* UnaccountedCSVStat)
	: CSV(RHICmdList, UnaccountedCSVStat)
{}

inline void FRDGCPUScopeStacks::BeginExecute()
{
	CSV.BeginExecute();
}

inline void FRDGCPUScopeStacks::BeginExecutePass(const FRDGPass* Pass)
{
	CSV.BeginExecutePass(Pass);
}

inline void FRDGCPUScopeStacks::EndExecute()
{
	CSV.EndExecute();
}

inline FRDGCPUScopes FRDGCPUScopeStacks::GetCurrentScopes() const
{
	FRDGCPUScopes Scopes;
	Scopes.CSV = CSV.GetCurrentScope();
	return Scopes;
}

#endif