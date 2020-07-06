// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraphDefinitions.h"
#include "RenderGraphResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogRDG, Log, All);

#define RDG_DUMP_GRAPH_VERBOSITY_LOW 1
#define RDG_DUMP_GRAPH_VERBOSITY_HIGH 2
#define RDG_DUMP_GRAPH_TRACKS 3

#define RDG_ASYNC_COMPUTE_DISABLED 0
#define RDG_ASYNC_COMPUTE_ENABLED 1
#define RDG_ASYNC_COMPUTE_FORCE_ENABLED 2

#define RDG_BREAKPOINT_WARNINGS 1
#define RDG_BREAKPOINT_PASS_COMPILE 2
#define RDG_BREAKPOINT_PASS_EXECUTE 3
#define RDG_BREAKPOINT_RESOURCE_LIFETIME 4

#if RDG_ENABLE_DEBUG
extern int32 GRDGAsyncCompute;
extern int32 GRDGClobberResources;
extern int32 GRDGDebug;
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
const int32 GRDGDumpGraph = 0;
const int32 GRDGBreakpoint = 0;
const int32 GRDGTransitionLog = 0;
const int32 GRDGImmediateMode = 0;
const int32 GRDGOverlapUAVs = 1;
const int32 GRDGExtendResourceLifetimes = 1;

#define EmitRDGWarningf(WarningMessageFormat, ...)

#endif

extern int32 GRDGAsyncCompute;
extern int32 GRDGCullPasses;
extern int32 GRDGMergeRenderPasses;

inline bool GetEmitRDGEvents()
{
	check(IsInRenderingThread());
#if RDG_EVENTS != RDG_EVENTS_NONE
	return GetEmitDrawEvents() || GRDGDebug;
#else
	return false;
#endif
}

template <typename TRHIResource, typename TRDGResource>
TRHIResource* GetRHIUnchecked(TRDGResource* Resource)
{
	return Resource->GetRHIUnchecked();
}

inline EResourceTransitionPipeline GetResourceTransitionPipeline(ERDGPipeline PipelineBegin, ERDGPipeline PipelineEnd)
{
	if (PipelineBegin == ERDGPipeline::Graphics)
	{
		if (PipelineEnd == ERDGPipeline::Graphics)
		{
			return EResourceTransitionPipeline::Graphics_To_Graphics;
		}
		else
		{
			return EResourceTransitionPipeline::Graphics_To_AsyncCompute;
		}
	}
	else
	{
		if (PipelineEnd == ERDGPipeline::Graphics)
		{
			return EResourceTransitionPipeline::AsyncCompute_To_Graphics;
		}
		else
		{
			return EResourceTransitionPipeline::AsyncCompute_To_AsyncCompute;
		}
	}
}

inline const TCHAR* GetPipelineName(ERDGPipeline Pipeline)
{
	switch (Pipeline)
	{
	case ERDGPipeline::Graphics:
		return TEXT("Graphics");
	case ERDGPipeline::AsyncCompute:
		return TEXT("AsyncCompute");
	}
	checkNoEntry();
	return TEXT("");
}