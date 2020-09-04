// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "RenderGraphResources.h"
#include "RenderTargetPool.h"

DEFINE_LOG_CATEGORY_STATIC(LogRDG, Log, All);

#define RDG_DUMP_GRAPH_PRODUCERS 1
#define RDG_DUMP_GRAPH_RESOURCES 2
#define RDG_DUMP_GRAPH_TRACKS 3

#define RDG_ASYNC_COMPUTE_DISABLED 0
#define RDG_ASYNC_COMPUTE_ENABLED 1
#define RDG_ASYNC_COMPUTE_FORCE_ENABLED 2

#define RDG_BREAKPOINT_WARNINGS 1
#define RDG_BREAKPOINT_PASS_COMPILE 2
#define RDG_BREAKPOINT_PASS_EXECUTE 3

#if RDG_ENABLE_DEBUG
extern int32 GRDGAsyncCompute;
extern int32 GRDGClobberResources;
extern int32 GRDGDebug;
extern int32 GRDGDebugFlushGPU;
extern int32 GRDGDumpGraph;
extern int32 GRDGDumpGraphUnknownCount;
extern int32 GRDGBreakpoint;
extern int32 GRDGTransitionLog;
extern int32 GRDGImmediateMode;
extern int32 GRDGOverlapUAVs;
extern int32 GRDGExtendResourceLifetimes;

// Colors for texture / buffer clobbering.
FLinearColor GetClobberColor();
uint32 GetClobberBufferValue();
float GetClobberDepth();
uint8 GetClobberStencil();

bool IsDebugAllowedForGraph(const TCHAR* GraphName);
bool IsDebugAllowedForPass(const TCHAR* PassName);
bool IsDebugAllowedForResource(const TCHAR* ResourceName);

inline void ConditionalDebugBreak(int32 BreakpointCVarValue, const TCHAR* GraphName, const TCHAR* PassName)
{
	if (GRDGBreakpoint == BreakpointCVarValue && IsDebugAllowedForGraph(GraphName) && IsDebugAllowedForPass(PassName))
	{
		UE_DEBUG_BREAK();
	}
}

inline void ConditionalDebugBreak(int32 BreakpointCVarValue, const TCHAR* GraphName, const TCHAR* PassName, const TCHAR* ResourceName)
{
	if (GRDGBreakpoint == BreakpointCVarValue && IsDebugAllowedForGraph(GraphName) && IsDebugAllowedForPass(PassName) && IsDebugAllowedForResource(ResourceName))
	{
		UE_DEBUG_BREAK();
	}
}

void EmitRDGWarning(const FString& WarningMessage);

#define EmitRDGWarningf(WarningMessageFormat, ...) \
	EmitRDGWarning(FString::Printf(WarningMessageFormat, ##__VA_ARGS__));

#else // !RDG_ENABLE_DEBUG

const int32 GRDGClobberResources = 0;
const int32 GRDGDebug = 0;
const int32 GRDGDebugFlushGPU = 0;
const int32 GRDGDumpGraph = 0;
const int32 GRDGBreakpoint = 0;
const int32 GRDGTransitionLog = 0;
const int32 GRDGImmediateMode = 0;
const int32 GRDGOverlapUAVs = 1;
const int32 GRDGExtendResourceLifetimes = 0;

#define EmitRDGWarningf(WarningMessageFormat, ...)

#endif

inline bool IsResourceLifetimeExtended()
{
	return (GRDGImmediateMode != 0) || (GRDGExtendResourceLifetimes != 0);
}

extern int32 GRDGAsyncCompute;
extern int32 GRDGCullPasses;
extern int32 GRDGMergeRenderPasses;

#if CSV_PROFILER
extern int32 GRDGVerboseCSVStats;
#else
const int32 GRDGVerboseCSVStats = 0;
#endif

#if STATS
extern int32 GRDGStatPassCount;
extern int32 GRDGStatPassCullCount;
extern int32 GRDGStatRenderPassMergeCount;
extern int32 GRDGStatPassDependencyCount;
extern int32 GRDGStatTextureCount;
extern int32 GRDGStatBufferCount;
extern int32 GRDGStatTransitionCount;
extern int32 GRDGStatTransitionBatchCount;
extern int32 GRDGStatMemoryWatermark;

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Passes"), STAT_RDG_PassCount, STATGROUP_RDG, RENDERCORE_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Passes Culled"), STAT_RDG_PassCullCount, STATGROUP_RDG, RENDERCORE_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Render Passes Merged"), STAT_RDG_RenderPassMergeCount, STATGROUP_RDG, RENDERCORE_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Pass Dependencies"), STAT_RDG_PassDependencyCount, STATGROUP_RDG, RENDERCORE_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Textures"), STAT_RDG_TextureCount, STATGROUP_RDG, RENDERCORE_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Buffers"), STAT_RDG_BufferCount, STATGROUP_RDG, RENDERCORE_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Resource Transitions"), STAT_RDG_TransitionCount, STATGROUP_RDG, RENDERCORE_API);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Resource Transition Batches"), STAT_RDG_TransitionBatchCount, STATGROUP_RDG, RENDERCORE_API);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Compile"), STAT_RDG_CompileTime, STATGROUP_RDG, RENDERCORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Collect Resources"), STAT_RDG_CollectResourcesTime, STATGROUP_RDG, RENDERCORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Collect Barriers"), STAT_RDG_CollectBarriersTime, STATGROUP_RDG, RENDERCORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Clear"), STAT_RDG_ClearTime, STATGROUP_RDG, RENDERCORE_API);

DECLARE_MEMORY_STAT_EXTERN(TEXT("Builder Watermark"), STAT_RDG_MemoryWatermark, STATGROUP_RDG, RENDERCORE_API);
#endif