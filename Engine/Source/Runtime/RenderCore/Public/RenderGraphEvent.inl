// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

template <typename TScopeType>
FRDGScopeStack<TScopeType>::FRDGScopeStack(
	FRHICommandListImmediate& InRHICmdList,
	FPushFunction InPushFunction,
	FPopFunction InPopFunction)
	: RHICmdList(InRHICmdList)
	, MemStack(FMemStack::Get())
	, PushFunction(InPushFunction)
	, PopFunction(InPopFunction)
	, ScopeStack(MakeUniformStaticArray<const TScopeType*, kScopeStackDepthMax>(nullptr))
{}

template <typename TScopeType>
FRDGScopeStack<TScopeType>::~FRDGScopeStack()
{
	ClearScopes();
}

template <typename TScopeType>
template <typename... FScopeConstructArgs>
void FRDGScopeStack<TScopeType>::BeginScope(FScopeConstructArgs... ScopeConstructArgs)
{
	auto Scope = new(MemStack) TScopeType(CurrentScope, Forward<FScopeConstructArgs>(ScopeConstructArgs)...);
	Scopes.Add(Scope);
	CurrentScope = Scope;
}

template <typename TScopeType>
void FRDGScopeStack<TScopeType>::EndScope()
{
	checkf(CurrentScope != nullptr, TEXT("Current scope is null."));
	CurrentScope = CurrentScope->ParentScope;
}

template <typename TScopeType>
void FRDGScopeStack<TScopeType>::BeginExecute()
{
	checkf(CurrentScope == nullptr, TEXT("Render graph needs to have all scopes ended to execute."));
}

template <typename TScopeType>
void FRDGScopeStack<TScopeType>::BeginExecutePass(const TScopeType* ParentScope)
{
	// Find out how many scopes needs to be popped.
	TStaticArray<const TScopeType*, kScopeStackDepthMax> TraversedScopes;
	int32 CommonScopeId = -1;
	int32 TraversedScopeCount = 0;

	// Find common ancestor between current stack and requested scope.
	while (ParentScope)
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

		PopFunction(RHICmdList);
		ScopeStack[i] = nullptr;
	}

	// Push new scopes.
	for (int32 i = TraversedScopeCount - 1; i >= 0; i--)
	{
		PushFunction(RHICmdList, TraversedScopes[i]);
		CommonScopeId++;
		ScopeStack[CommonScopeId] = TraversedScopes[i];
	}
}

template <typename TScopeType>
void FRDGScopeStack<TScopeType>::EndExecute()
{
	for (uint32 ScopeIndex = 0; ScopeIndex < kScopeStackDepthMax; ++ScopeIndex)
	{
		if (!ScopeStack[ScopeIndex])
		{
			break;
		}

		PopFunction(RHICmdList);
	}
	ClearScopes();
}

template <typename TScopeType>
void FRDGScopeStack<TScopeType>::ClearScopes()
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