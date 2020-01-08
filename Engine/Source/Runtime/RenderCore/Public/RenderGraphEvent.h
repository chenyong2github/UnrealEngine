// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"


/** Returns whether the current frame is emitting render graph events. */
extern RENDERCORE_API bool GetEmitRDGEvents();


/** A helper profiler class for tracking and evaluating hierarchical scopes in the context of render graph. */
template <typename TScopeType>
class FRDGScopeStack final
{
	static constexpr uint32 kScopeStackDepthMax = 8;
public:
	using FPushFunction = void(*)(FRHICommandListImmediate&, const TScopeType*);
	using FPopFunction = void(*)(FRHICommandListImmediate&);

	FRDGScopeStack(FRHICommandListImmediate& InRHICmdList, FPushFunction InPushFunction, FPopFunction InPopFunction);
	~FRDGScopeStack();

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph setup phase.

	/** Call to begin recording a scope. */
	template <typename... FScopeConstructArgs>
	void BeginScope(FScopeConstructArgs... ScopeConstructArgs);

	/** Call to end recording a scope. */
	void EndScope();

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph execute phase.

	/** Call prior to executing the graph. */
	void BeginExecute();

	/** Call prior to executing a pass in the graph. */
	void BeginExecutePass(const TScopeType* ParentScope);

	/** Call after executing the graph. */
	void EndExecute();

	//////////////////////////////////////////////////////////////////////////

	const TScopeType* GetCurrentScope() const
	{
		return CurrentScope;
	}

	FRHICommandListImmediate& RHICmdList;
	FMemStackBase& MemStack;

private:
	void ClearScopes();

	FPushFunction PushFunction;
	FPopFunction PopFunction;

	/** The top of the scope stack during setup. */
	const TScopeType* CurrentScope = nullptr;

	/** Tracks scopes allocated through MemStack for destruction. */
	TArray<TScopeType*, SceneRenderingAllocator> Scopes;

	/** Stacks of scopes pushed to the RHI command list during execution. */
	TStaticArray<const TScopeType*, kScopeStackDepthMax> ScopeStack;
};

class FRDGPass;
class FRDGBuilder;

//////////////////////////////////////////////////////////////////////////
//
// GPU Events - Named hierarchical events emitted to external profiling tools.
//
//////////////////////////////////////////////////////////////////////////

/** Stores a GPU event name for the render graph. Draw events can be compiled out entirely from
 *  a release build for performance.
 */
class RENDERCORE_API FRDGEventName final
{
public:
	FRDGEventName() = default;

	explicit FRDGEventName(const TCHAR* EventFormat, ...);

	~FRDGEventName();

	FRDGEventName(const FRDGEventName& Other);
	FRDGEventName(FRDGEventName&& Other);
	FRDGEventName& operator=(const FRDGEventName& Other);
	FRDGEventName& operator=(FRDGEventName&& Other);

	const TCHAR* GetTCHAR() const;

private:
#if RDG_EVENTS == RDG_EVENTS_STRING_REF || RDG_EVENTS == RDG_EVENTS_STRING_COPY
	// Event format kept arround to still have a clue what error might be causing the problem in error messages.
	const TCHAR* EventFormat;

#if RDG_EVENTS == RDG_EVENTS_STRING_COPY
	// Formated event name if GetEmitRDGEvents() == true.
	FString FormatedEventName;
#endif
#endif
};

class FRDGEventScope final
{
public:
	FRDGEventScope(const FRDGEventScope* InParentScope, FRDGEventName&& InName)
		: ParentScope(InParentScope)
		, Name(Forward<FRDGEventName&&>(InName))
	{}

	const FRDGEventScope* const ParentScope;
	const FRDGEventName Name;
};

/** Manages a stack of event scopes. Scopes are recorded ahead of time in a hierarchical fashion
 *  and later executed topologically during pass execution.
 */
class RENDERCORE_API FRDGEventScopeStack final
{
public:
	FRDGEventScopeStack(FRHICommandListImmediate& RHICmdList);

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph setup phase.

	/** Call to begin recording an event scope. */
	void BeginScope(FRDGEventName&& EventName);

	/** Call to end recording an event scope. */
	void EndScope();

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph execute phase.

	/** Call prior to executing the graph. */
	void BeginExecute();

	/** Call prior to executing a pass in the graph. */
	void BeginExecutePass(const FRDGPass* Pass);

	/** Call after executing a pass in the graph. */
	void EndExecutePass();

	/** Call after executing the graph. */
	void EndExecute();

	//////////////////////////////////////////////////////////////////////////

	const FRDGEventScope* GetCurrentScope() const
	{
		return ScopeStack.GetCurrentScope();
	}

private:
	static bool IsEnabled();
	FRDGScopeStack<FRDGEventScope> ScopeStack;
};

/** RAII class for begin / end of an event scope through the graph builder. */
class RENDERCORE_API FRDGEventScopeGuard final
{
public:
	FRDGEventScopeGuard(FRDGBuilder& InGraphBuilder, FRDGEventName&& ScopeName, bool bCondition = true);
	FRDGEventScopeGuard(const FRDGEventScopeGuard&) = delete;
	~FRDGEventScopeGuard();

private:
	FRDGBuilder& GraphBuilder;
	bool bCondition = true;
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
	#define RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Condition, Format, ...) FRDGEventScopeGuard PREPROCESSOR_JOIN(__RDG_ScopeRef_,__LINE__) ((GraphBuilder), RDG_EVENT_NAME(Format, ##__VA_ARGS__), Condition)
#elif RDG_EVENTS == RDG_EVENTS_NONE
	#define RDG_EVENT_NAME(Format, ...) FRDGEventName()
	#define RDG_EVENT_SCOPE(GraphBuilder, Format, ...) 
	#define RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Condition, Format, ...)
#else
	#error "RDG_EVENTS is not a valid value."
#endif

//////////////////////////////////////////////////////////////////////////
//
// GPU Stats - Aggregated counters emitted to the runtime 'stat GPU' profiler.
//
//////////////////////////////////////////////////////////////////////////

class FRDGStatScope final
{
public:
	FRDGStatScope(const FRDGStatScope* InParentScope, const FName& InName, const FName& InStatName)
		: ParentScope(InParentScope)
		, Name(InName)
		, StatName(InStatName)
	{}

	const FRDGStatScope* const ParentScope;
	const FName Name;
	const FName StatName;
};

class RENDERCORE_API FRDGStatScopeStack final
{
public:
	FRDGStatScopeStack(FRHICommandListImmediate& RHICmdList);

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph setup phase.

	/** Call to begin recording a stat scope. */
	void BeginScope(const FName& Name, const FName& StatName);

	/** Call to end recording a stat scope. */
	void EndScope();

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph execute phase.

	/** Call prior to executing the graph. */
	void BeginExecute();

	/** Call prior to executing a pass in the graph. */
	void BeginExecutePass(const FRDGPass* Pass);

	/** Call after executing the graph. */
	void EndExecute();

	//////////////////////////////////////////////////////////////////////////

	const FRDGStatScope* GetCurrentScope() const
	{
		return ScopeStack.GetCurrentScope();
	}

private:
	static bool IsEnabled();
	FRDGScopeStack<FRDGStatScope> ScopeStack;
};

/** RAII class for begin / end of a stat scope through the graph builder. */
class RENDERCORE_API FRDGStatScopeGuard final
{
public:
	FRDGStatScopeGuard(FRDGBuilder& InGraphBuilder, const FName& Name, const FName& StatName);
	FRDGStatScopeGuard(const FRDGStatScopeGuard&) = delete;
	~FRDGStatScopeGuard();

private:
	FRDGBuilder& GraphBuilder;
};

#if HAS_GPU_STATS
	#if STATS
		#define RDG_GPU_STAT_SCOPE(GraphBuilder, StatName) FRDGStatScopeGuard PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__) ((GraphBuilder), CSV_STAT_FNAME(StatName), GET_STATID(Stat_GPU_##StatName).GetName());
	#else
		#define RDG_GPU_STAT_SCOPE(GraphBuilder, StatName) FRDGStatScopeGuard PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__) ((GraphBuilder), CSV_STAT_FNAME(StatName), FName());
	#endif
#else
	#define RDG_GPU_STAT_SCOPE(GraphBuilder, StatName)
#endif

#include "RenderGraphEvent.inl"