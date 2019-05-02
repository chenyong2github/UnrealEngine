// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphEvent.h"
#include "RenderGraphBuilder.h"

extern bool GetEmitRDGEvents();

class FRDGEventScope
{
public:
	FRDGEventScope(const FRDGEventScope* InParentScope, FRDGEventName&& InName)
		: ParentScope(InParentScope)
		, Name(InName)
	{}

	// Pointer towards this one is contained in.
	const FRDGEventScope* const ParentScope;

	// Name of the event.
	const FRDGEventName Name;
};

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

FRDGEventScopeStack::FRDGEventScopeStack(FRHICommandListImmediate& InRHICmdList)
	: RHICmdList(InRHICmdList)
	, MemStack(FMemStack::Get())
	, ScopeStack(MakeUniformStaticArray<const FRDGEventScope*, kScopeStackDepthMax>(nullptr))
{}

void FRDGEventScopeStack::BeginEventScope(FRDGEventName&& EventName)
{
	if (RDG_EVENTS)
	{
		auto Scope = new(MemStack) FRDGEventScope(CurrentScope, Forward<FRDGEventName>(EventName));
		EventScopes.Add(Scope);
		CurrentScope = Scope;
	}
}

void FRDGEventScopeStack::EndEventScope()
{
	if (RDG_EVENTS)
	{
		checkf(CurrentScope != nullptr, TEXT("Current scope is null."));
		CurrentScope = CurrentScope->ParentScope;
	}
}

void FRDGEventScopeStack::BeginExecute()
{
	/** The usage need RDG_EVENT_SCOPE() needs to happen in inner scope of the one containing FRDGBuilder because of
	 *  FStackRDGEventScopeRef's destructor modifying this FRDGBuilder instance.
	 *
	 *
	 *  FRDGBuilder GraphBuilder(RHICmdList);
	 *  {
	 *  	RDG_EVENT_SCOPE(GraphBuilder, "MyEventScope");
	 *  	// ...
	 *  }
	 *  GraphBuilder.Execute();
	 */
	checkf(CurrentScope == nullptr, TEXT("Render graph needs to have all scopes ended to execute."));
}

void FRDGEventScopeStack::BeginExecutePass(const FRDGPass* Pass)
{
	if (!GetEmitRDGEvents())
	{
		return;
	}

	// Push the scope event.
	{
		// Find out how many scope events needs to be popped.
		TStaticArray<const FRDGEventScope*, kScopeStackDepthMax> TraversedScopes;
		int32 CommonScopeId = -1;
		int32 TraversedScopeCount = 0;

		const FRDGEventScope* PassParentScope = Pass->GetParentScope();

		while (PassParentScope)
		{
			TraversedScopes[TraversedScopeCount] = PassParentScope;

			for (int32 i = 0; i < ScopeStack.Num(); i++)
			{
				if (ScopeStack[i] == PassParentScope)
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
			PassParentScope = PassParentScope->ParentScope;
		}

		// Pop no longer used scopes
		for (int32 i = CommonScopeId + 1; i < kScopeStackDepthMax; i++)
		{
			if (!ScopeStack[i])
				break;

			RHICmdList.PopEvent();
			ScopeStack[i] = nullptr;
		}

		// Push new scopes
		const FColor ScopeColor(0);
		for (int32 i = TraversedScopeCount - 1; i >= 0; i--)
		{
			RHICmdList.PushEvent(TraversedScopes[i]->Name.GetTCHAR(), ScopeColor);
			CommonScopeId++;
			ScopeStack[CommonScopeId] = TraversedScopes[i];
		}
	}

	// Push the pass's event with some color.
	{
		FColor Color(0, 0, 0);

		if (Pass->IsCompute())
		{
			// Green for compute.
			Color = FColor(128, 255, 128);
		}
		else
		{
			// Ref for rasterizer.
			Color = FColor(255, 128, 128);
		}

		RHICmdList.PushEvent(Pass->GetName(), Color);
	}
}

void FRDGEventScopeStack::EndExecutePass()
{
	if (GetEmitRDGEvents())
	{
		RHICmdList.PopEvent();
	}
}

void FRDGEventScopeStack::EndExecute()
{
	// Pops remaining scopes
	if (GetEmitRDGEvents())
	{
		for (uint32 ScopeIndex = 0; ScopeIndex < kScopeStackDepthMax; ++ScopeIndex)
		{
			if (!ScopeStack[ScopeIndex])
			{
				break;
			}

			RHICmdList.PopEvent();
		}
	}

#if RDG_EVENTS
	// Event scopes are allocated on FMemStack, so need to call their destructor because have a FString within them.
	for (int32 Index = EventScopes.Num() - 1; Index >= 0; --Index)
	{
		EventScopes[Index]->~FRDGEventScope();
	}
	EventScopes.Empty();
#endif
}