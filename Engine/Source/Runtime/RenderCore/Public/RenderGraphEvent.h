// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"

class FRDGPass;
class FRDGBuilder;
class FRDGEventScope;

/** Stores a GPU event name for the render graph. Draw events can be compiled out entirely from
 *  a release build for performance.
 */
class RENDERCORE_API FRDGEventName final
{
public:
	FRDGEventName() = default;

	~FRDGEventName()
	{
#if RDG_EVENTS != RDG_EVENTS_NONE
		EventName = nullptr;
#endif
	}

#if RDG_EVENTS == RDG_EVENTS_STRING_COPY
	explicit FRDGEventName(const TCHAR* EventFormat, ...);
#else
	explicit FRDGEventName(const TCHAR* EventFormat, ...)
#if RDG_EVENTS == RDG_EVENTS_STRING_REF
		: EventName(EventFormat)
#endif
	{}
#endif

	FRDGEventName(const FRDGEventName& Other)
	{
		*this = Other;
	}

	FRDGEventName(FRDGEventName&& Other)
	{
		*this = Forward<FRDGEventName>(Other);
	}

	FRDGEventName& operator=(const FRDGEventName& Other)
	{
#if RDG_EVENTS == RDG_EVENTS_STRING_REF
		EventName = Other.EventName;
#elif RDG_EVENTS == RDG_EVENTS_STRING_COPY
		EventNameStorage = Other.EventNameStorage;
		EventName = *EventNameStorage;
#endif
		return *this;
	}

	FRDGEventName& operator=(FRDGEventName&& Other)
	{
#if RDG_EVENTS == RDG_EVENTS_STRING_REF
		EventName = Other.EventName;
		Other.EventName = nullptr;
#elif RDG_EVENTS == RDG_EVENTS_STRING_COPY
		EventNameStorage = MoveTemp(Other.EventNameStorage);
		EventName = *EventNameStorage;
		Other.EventName = nullptr;
#endif
		return *this;
	}

	const TCHAR* GetTCHAR() const
	{
#if RDG_EVENTS == RDG_EVENTS_STRING_REF || RDG_EVENTS == RDG_EVENTS_STRING_COPY
		return EventName;
#else
		// Render graph draw events have been completely compiled for CPU performance reasons.
		return TEXT("!!!Unavailable RDG event name: need RDG_EVENTS>=0 and r.RDG.EmitWarnings=1 or -rdgdebug!!!");
#endif
	}

private:
#if RDG_EVENTS == RDG_EVENTS_STRING_REF || RDG_EVENTS == RDG_EVENTS_STRING_COPY
	const TCHAR* EventName;
#if RDG_EVENTS == RDG_EVENTS_STRING_COPY
	FString EventNameStorage;
#endif
#endif
};

/** Manages a stack of event scopes. Scopes are recorded ahead of time in a hierarchical fashion
 *  and later executed topologically during pass execution.
 */
class RENDERCORE_API FRDGEventScopeStack final
{
public:
	FRDGEventScopeStack(FRHICommandListImmediate& RHICmdList);
	~FRDGEventScopeStack();

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph setup phase.

	void BeginEventScope(FRDGEventName&& EventName);

	void EndEventScope();

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph execute phase.

	void BeginExecute();

	void BeginExecutePass(const FRDGPass* Pass);

	void EndExecutePass();

	void EndExecute();

	//////////////////////////////////////////////////////////////////////////

	const FRDGEventScope* GetCurrentScope() const
	{
		return CurrentScope;
	}

private:
	static constexpr uint32 kScopeStackDepthMax = 8;

	FRHICommandListImmediate& RHICmdList;
	FMemStackBase& MemStack;

	/** The top of the scope stack during setup. */
	const FRDGEventScope* CurrentScope = nullptr;

	/** Tracks scopes allocated through MemStack for destruction. */
	TArray<FRDGEventScope*, SceneRenderingAllocator> EventScopes;

	/** Stacks of scopes pushed to the RHI command list during execution. */
	TStaticArray<const FRDGEventScope*, kScopeStackDepthMax> ScopeStack;

	/** Used to validate Begin / End symmetry and that setup and execute are mutually exclusive. */
	bool bIsExecuting = false;
};

/** RAII class for begin / end of an event scope through the graph builder. */
class RENDERCORE_API FRDGEventScopeGuard final
{
public:
	FRDGEventScopeGuard(FRDGBuilder& InGraphBuilder, FRDGEventName&& ScopeName);
	FRDGEventScopeGuard(const FRDGEventScopeGuard&) = delete;
	~FRDGEventScopeGuard();

private:
	FRDGBuilder& GraphBuilder;
};

/** Macros for create render graph event names and scopes.
 *
 *  FRDGEventName Name = RDG_EVENT_NAME("MyPass %sx%s", ViewRect.Width(), ViewRect.Height());
 *
 *  RDG_EVENT_SCOPE(GraphBuilder, "MyProcessing %sx%s", ViewRect.Width(), ViewRect.Height());
 */
#if RDG_EVENTS == RDG_EVENTS_STRING_REF || RDG_EVENTS == RDG_EVENTS_STRING_COPY
	#define RDG_EVENT_NAME(Format, ...) FRDGEventName(TEXT(Format), ##__VA_ARGS__)
	#define RDG_EVENT_SCOPE(GraphBuilder, Format, ...) FRDGEventScopeGuard PREPROCESSOR_JOIN(__RDG_ScopeRef_,__LINE__) ((GraphBuilder), RDG_EVENT_NAME(Format, ##__VA_ARGS__))
#elif RDG_EVENTS == RDG_EVENTS_NONE
	#define RDG_EVENT_NAME(Format, ...) FRDGEventName()
	#define RDG_EVENT_SCOPE(GraphBuilder, Format, ...) 
#else
	#error "RDG_EVENTS is not a valid value."
#endif