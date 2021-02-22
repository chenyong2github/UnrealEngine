// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

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

#if HAS_GPU_STATS
	#if STATS
		#define RDG_GPU_STAT_SCOPE(GraphBuilder, StatName) FRDGGPUStatScopeGuard PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__) ((GraphBuilder), CSV_STAT_FNAME(StatName), GET_STATID(Stat_GPU_##StatName).GetName(), &DrawcallCountCategory_##StatName.Counters);
	#else
		#define RDG_GPU_STAT_SCOPE(GraphBuilder, StatName) FRDGGPUStatScopeGuard PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__) ((GraphBuilder), CSV_STAT_FNAME(StatName), FName(), &DrawcallCountCategory_##StatName.Counters);
	#endif
#else
	#define RDG_GPU_STAT_SCOPE(GraphBuilder, StatName)
#endif

#if CSV_PROFILER
	#define RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, StatName) FRDGScopedCsvStatExclusive RDGScopedCsvStatExclusive ## StatName (GraphBuilder, #StatName)
	#define RDG_CSV_STAT_EXCLUSIVE_SCOPE_CONDITIONAL(GraphBuilder, StatName, bCondition) FRDGScopedCsvStatExclusiveConditional RDGScopedCsvStatExclusiveConditional ## StatName (GraphBuilder, #StatName, bCondition)
#else
	#define RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, StatName)
	#define RDG_CSV_STAT_EXCLUSIVE_SCOPE_CONDITIONAL(GraphBuilder, StatName, bCondition)
#endif

/** Returns whether the current frame is emitting render graph events. */
RENDERCORE_API bool GetEmitRDGEvents();

/** A helper profiler class for tracking and evaluating hierarchical scopes in the context of render graph. */
template <typename TScopeType>
class TRDGScopeStack final
{
	static constexpr uint32 kScopeStackDepthMax = 8;
public:
	using FPushFunction = void(*)(FRHIComputeCommandList&, const TScopeType*);
	using FPopFunction = void(*)(FRHIComputeCommandList&, const TScopeType*);

	TRDGScopeStack(FRHIComputeCommandList& InRHICmdList, FPushFunction InPushFunction, FPopFunction InPopFunction);
	~TRDGScopeStack();

	//////////////////////////////////////////////////////////////////////////
	//! Called during graph setup phase.

	/** Call to begin recording a scope. */
	template <typename... TScopeConstructArgs>
	void BeginScope(TScopeConstructArgs... ScopeConstructArgs);

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

	FRHIComputeCommandList& RHICmdList;
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
	// Event format kept around to still have a clue what error might be causing the problem in error messages.
	const TCHAR* EventFormat = TEXT("");

#if RDG_EVENTS == RDG_EVENTS_STRING_COPY
	// Formated event name if GetEmitRDGEvents() == true.
	FString FormatedEventName;
#endif
#endif
};

#if RDG_GPU_SCOPES

class FRDGEventScope final
{
public:
	FRDGEventScope(const FRDGEventScope* InParentScope, FRDGEventName&& InName, FRHIGPUMask InGPUMask)
		: ParentScope(InParentScope)
		, Name(Forward<FRDGEventName&&>(InName))
#if WITH_MGPU
		, GPUMask(InGPUMask)
#endif
	{}

	/** Returns a formatted path for debugging. */
	FString GetPath(const FRDGEventName& Event) const;

	const FRDGEventScope* const ParentScope;
	const FRDGEventName Name;
#if WITH_MGPU
	const FRHIGPUMask GPUMask;
#endif
};

/** Manages a stack of event scopes. Scopes are recorded ahead of time in a hierarchical fashion
 *  and later executed topologically during pass execution.
 */
class RENDERCORE_API FRDGEventScopeStack final
{
public:
	FRDGEventScopeStack(FRHIComputeCommandList& RHICmdList);

	void BeginScope(FRDGEventName&& EventName);

	void EndScope();

	void BeginExecute();

	void BeginExecutePass(const FRDGPass* Pass);

	void EndExecutePass();

	void EndExecute();

	const FRDGEventScope* GetCurrentScope() const
	{
		return ScopeStack.GetCurrentScope();
	}

private:
	static bool IsEnabled();
	TRDGScopeStack<FRDGEventScope> ScopeStack;
	bool bEventPushed = false;
};

RENDERCORE_API FString GetRDGEventPath(const FRDGEventScope* Scope, const FRDGEventName& Event);

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

//////////////////////////////////////////////////////////////////////////
//
// GPU Stats - Aggregated counters emitted to the runtime 'stat GPU' profiler.
//
//////////////////////////////////////////////////////////////////////////

class FRDGGPUStatScope final
{
public:
	FRDGGPUStatScope(const FRDGGPUStatScope* InParentScope, const FName& InName, const FName& InStatName, int32 (*InDrawCallCounter)[MAX_NUM_GPUS])
		: ParentScope(InParentScope)
		, Name(InName)
		, StatName(InStatName)
		, DrawCallCounter(InDrawCallCounter)
	{}

	const FRDGGPUStatScope* const ParentScope;
	const FName Name;
	const FName StatName;
	int32 (*DrawCallCounter)[MAX_NUM_GPUS];
};

class RENDERCORE_API FRDGGPUStatScopeStack final
{
public:
	FRDGGPUStatScopeStack(FRHIComputeCommandList& RHICmdList);

	void BeginScope(const FName& Name, const FName& StatName, int32 (*DrawCallCounter)[MAX_NUM_GPUS]);

	void EndScope();

	void BeginExecute();

	void BeginExecutePass(const FRDGPass* Pass);

	void EndExecute();

	const FRDGGPUStatScope* GetCurrentScope() const
	{
		return ScopeStack.GetCurrentScope();
	}

private:
	static bool IsEnabled();
	TRDGScopeStack<FRDGGPUStatScope> ScopeStack;
};

class RENDERCORE_API FRDGGPUStatScopeGuard final
{
public:
	FRDGGPUStatScopeGuard(FRDGBuilder& InGraphBuilder, const FName& Name, const FName& StatName, int32 (*DrawCallCounter)[MAX_NUM_GPUS]);
	FRDGGPUStatScopeGuard(const FRDGGPUStatScopeGuard&) = delete;
	~FRDGGPUStatScopeGuard();

private:
	FRDGBuilder& GraphBuilder;
};

struct FRDGGPUScopes
{
	const FRDGEventScope* Event = nullptr;
	const FRDGGPUStatScope* Stat = nullptr;
};

/** The complete set of scope stack implementations. */
struct FRDGGPUScopeStacks
{
	FRDGGPUScopeStacks(FRHIComputeCommandList& RHICmdList);

	void BeginExecute();

	void BeginExecutePass(const FRDGPass* Pass);

	void EndExecutePass();

	void EndExecute();

	FRDGGPUScopes GetCurrentScopes() const;

	FRDGEventScopeStack Event;
	FRDGGPUStatScopeStack Stat;
};

struct FRDGGPUScopeStacksByPipeline
{
	FRDGGPUScopeStacksByPipeline(FRHICommandListImmediate& RHICmdListGraphics, FRHIComputeCommandList& RHICmdListAsyncCompute);

	void BeginEventScope(FRDGEventName&& ScopeName);

	void EndEventScope();

	void BeginStatScope(const FName& Name, const FName& StatName, int32 (*DrawCallCounter)[MAX_NUM_GPUS]);

	void EndStatScope();

	void BeginExecute();

	void BeginExecutePass(const FRDGPass* Pass);

	void EndExecutePass(const FRDGPass* Pass);

	void EndExecute();

	const FRDGGPUScopeStacks& GetScopeStacks(ERHIPipeline Pipeline) const;

	FRDGGPUScopeStacks& GetScopeStacks(ERHIPipeline Pipeline);

	FRDGGPUScopes GetCurrentScopes(ERHIPipeline Pipeline) const;

	FRDGGPUScopeStacks Graphics;
	FRDGGPUScopeStacks AsyncCompute;
};

#endif

//////////////////////////////////////////////////////////////////////////
//
// CPU CSV Stats
//
//////////////////////////////////////////////////////////////////////////

#if RDG_CPU_SCOPES

class FRDGCSVStatScope final
{
public:
	FRDGCSVStatScope(const FRDGCSVStatScope* InParentScope, const char* InStatName)
		: ParentScope(InParentScope)
		, StatName(InStatName)
	{}

	const FRDGCSVStatScope* const ParentScope;
	const char* StatName;
};

class RENDERCORE_API FRDGCSVStatScopeStack final
{
public:
	FRDGCSVStatScopeStack(FRHIComputeCommandList& RHICmdList, const char* UnaccountedStatName);

	void BeginScope(const char* StatName);

	void EndScope();

	void BeginExecute();

	void BeginExecutePass(const FRDGPass* Pass);

	void EndExecute();

	const FRDGCSVStatScope* GetCurrentScope() const
	{
		return ScopeStack.GetCurrentScope();
	}

private:
	static bool IsEnabled();
	TRDGScopeStack<FRDGCSVStatScope> ScopeStack;
	const char* const UnaccountedStatName;
};

#if CSV_PROFILER

class RENDERCORE_API FRDGScopedCsvStatExclusive : public FScopedCsvStatExclusive
{
public:
	FRDGScopedCsvStatExclusive(FRDGBuilder& InGraphBuilder, const char* InStatName);
	~FRDGScopedCsvStatExclusive();

private:
	FRDGBuilder& GraphBuilder;
};

class RENDERCORE_API FRDGScopedCsvStatExclusiveConditional : public FScopedCsvStatExclusiveConditional
{
public:
	FRDGScopedCsvStatExclusiveConditional(FRDGBuilder& InGraphBuilder, const char* InStatName, bool bInCondition);
	~FRDGScopedCsvStatExclusiveConditional();

private:
	FRDGBuilder& GraphBuilder;
};

#endif

struct FRDGCPUScopes
{
	const FRDGCSVStatScope* CSV = nullptr;
};

struct FRDGCPUScopeStacks
{
	FRDGCPUScopeStacks(FRHIComputeCommandList& RHICmdList, const char* UnaccountedCSVStat);

	void BeginExecute();

	void BeginExecutePass(const FRDGPass* Pass);

	void EndExecute();

	FRDGCPUScopes GetCurrentScopes() const;

	FRDGCSVStatScopeStack CSV;
};

#endif

#include "RenderGraphEvent.inl"