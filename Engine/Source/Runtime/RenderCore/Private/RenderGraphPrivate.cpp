// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphPrivate.h"

#if RDG_ENABLE_DEBUG

int32 GRDGDumpGraphUnknownCount = 0;

int32 GRDGImmediateMode = 0;
FAutoConsoleVariableRef CVarImmediateMode(
	TEXT("r.RDG.ImmediateMode"),
	GRDGImmediateMode,
	TEXT("Executes passes as they get created. Useful to have a callstack of the wiring code when crashing in the pass' lambda."),
	ECVF_RenderThreadSafe);

int32 GRDGDebug = 0;
FAutoConsoleVariableRef CVarRDGDebug(
	TEXT("r.RDG.Debug"),
	GRDGDebug,
	TEXT("Allow to output warnings for inefficiencies found during wiring and execution of the passes.\n")
	TEXT(" 0: disabled;\n")
	TEXT(" 1: emit warning once (default);\n")
	TEXT(" 2: emit warning everytime issue is detected."),
	ECVF_RenderThreadSafe);

int32 GRDGDebugFlushGPU = 0;
FAutoConsoleVariableRef CVarRDGDebugFlushGPU(
	TEXT("r.RDG.Debug.FlushGPU"),
	GRDGDebugFlushGPU,
	TEXT("Enables flushing the GPU after every pass. Disables async compute when set (r.RDG.AsyncCompute=0).\n")
	TEXT(" 0: disabled (default);\n")
	TEXT(" 1: enabled (default)."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
	{
		if (GRDGDebugFlushGPU)
		{
			GRDGAsyncCompute = 0;
		}
	}),
	ECVF_RenderThreadSafe);

int32 GRDGDumpGraph = 0;
FAutoConsoleVariableRef CVarDumpGraph(
	TEXT("r.RDG.DumpGraph"),
	GRDGDumpGraph,
	TEXT("Dumps several visualization logs to disk.\n")
	TEXT(" 0: disabled;\n")
	TEXT(" 1: visualizes producer / consumer pass dependencies;\n")
	TEXT(" 2: visualizes resource states and transitions;\n")
	TEXT(" 3: visualizes graphics / async compute overlap;\n"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
	{
		if (GRDGDumpGraph)
		{
			GRDGDebug = 1;
		}
	}),
	ECVF_RenderThreadSafe);

int32 GRDGBreakpoint = 0;
FAutoConsoleVariableRef CVarRDGBreakpoint(
	TEXT("r.RDG.Breakpoint"),
	GRDGBreakpoint,
	TEXT("Breakpoint in debugger when certain conditions are met.\n")
	TEXT(" 0: off (default);\n")
	TEXT(" 1: On an RDG warning;\n")
	TEXT(" 2: When a graph / pass matching the debug filters compiles;\n")
	TEXT(" 3: When a graph / pass matching the debug filters executes;\n")
	TEXT(" 4: When a graph / pass / resource matching the debug filters is created or destroyed;\n"),
	ECVF_RenderThreadSafe);

int32 GRDGClobberResources = 0;
FAutoConsoleVariableRef CVarRDGClobberResources(
	TEXT("r.RDG.ClobberResources"),
	GRDGClobberResources,
	TEXT("Clears all render targets and texture / buffer UAVs with the requested clear color at allocation time. Useful for debugging.\n")
	TEXT(" 0:off (default);\n")
	TEXT(" 1: 1000 on RGBA channels;\n")
	TEXT(" 2: NaN on RGBA channels;\n")
	TEXT(" 3: +INFINITY on RGBA channels.\n"),
	ECVF_Cheat | ECVF_RenderThreadSafe);

int32 GRDGOverlapUAVs = 1;
FAutoConsoleVariableRef CVarRDGOverlapUAVs(
	TEXT("r.RDG.OverlapUAVs"), GRDGOverlapUAVs,
	TEXT("RDG will overlap UAV work when requested; if disabled, UAV barriers are always inserted."),
	ECVF_RenderThreadSafe);

int32 GRDGExtendResourceLifetimes = 0;
FAutoConsoleVariableRef CVarRDGExtendResourceLifetimes(
	TEXT("r.RDG.ExtendResourceLifetimes"), GRDGExtendResourceLifetimes,
	TEXT("RDG will extend resource lifetimes to the full length of the graph. Increases memory usage."),
	ECVF_RenderThreadSafe);

int32 GRDGTransitionLog = 0;
FAutoConsoleVariableRef CVarRDGTransitionLog(
	TEXT("r.RDG.TransitionLog"), GRDGTransitionLog,
	TEXT("Logs resource transitions to the console.\n")
	TEXT(" 0: disabled(default);\n")
	TEXT(">0: enabled for N frames;\n")
	TEXT("<0: enabled;\n"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<FString> CVarRDGDebugGraphFilter(
	TEXT("r.RDG.Debug.GraphFilter"), TEXT(""),
	TEXT("Filters certain debug events to a specific graph.\n"),
	ECVF_Default);

FString GRDGDebugGraphFilterName;

FAutoConsoleVariableSink CVarRDGDebugGraphSink(FConsoleCommandDelegate::CreateLambda([]()
{
	GRDGDebugGraphFilterName = CVarRDGDebugGraphFilter.GetValueOnGameThread();
}));

inline bool IsDebugAllowed(const FString& FilterString, const TCHAR* Name)
{
	if (FilterString.IsEmpty())
	{
		return true;
	}

	const bool bFound = FCString::Strifind(Name, *FilterString) != nullptr;
	if (!bFound)
	{
		return false;
	}

	return FilterString[0] != TEXT('!');
}

bool IsDebugAllowedForGraph(const TCHAR* GraphName)
{
	return IsDebugAllowed(GRDGDebugGraphFilterName, GraphName);
}

TAutoConsoleVariable<FString> CVarRDGDebugPassFilter(
	TEXT("r.RDG.Debug.PassFilter"), TEXT(""),
	TEXT("Filters certain debug events to specific passes.\n"),
	ECVF_Default);

FString GRDGDebugPassFilterName;

FAutoConsoleVariableSink CVarRDGDebugPassSink(FConsoleCommandDelegate::CreateLambda([]()
{
	GRDGDebugPassFilterName = CVarRDGDebugPassFilter.GetValueOnGameThread();
}));

bool IsDebugAllowedForPass(const TCHAR* PassName)
{
	return IsDebugAllowed(GRDGDebugPassFilterName, PassName);
}

TAutoConsoleVariable<FString> CVarRDGDebugResourceFilter(
	TEXT("r.RDG.Debug.ResourceFilter"), TEXT(""),
	TEXT("Filters certain debug events to a specific resource.\n"),
	ECVF_Default);

FString GRDGDebugResourceFilterName;

FAutoConsoleVariableSink CVarRDGDebugResourceSink(FConsoleCommandDelegate::CreateLambda([]()
{
	GRDGDebugResourceFilterName = CVarRDGDebugResourceFilter.GetValueOnGameThread();
}));

bool IsDebugAllowedForResource(const TCHAR* ResourceName)
{
	return IsDebugAllowed(GRDGDebugResourceFilterName, ResourceName);
}

FLinearColor GetClobberColor()
{
	switch (GRDGClobberResources)
	{
	case 1:
		return FLinearColor(1000, 1000, 1000, 1000);
	case 2:
		return FLinearColor(NAN, NAN, NAN, NAN);
	case 3:
		return FLinearColor(INFINITY, INFINITY, INFINITY, INFINITY);
	case 4:
		return FLinearColor(0, 0, 0, 0);
	default:
		return FLinearColor::Black;
	}
}

uint32 GetClobberBufferValue()
{
	return 1000;
}

float GetClobberDepth()
{
	return 0.123456789f;
}

uint8 GetClobberStencil()
{
	return 123;
}

void EmitRDGWarning(const FString& WarningMessage)
{
	if (!GRDGDebug)
	{
		return;
	}

	static TSet<FString> GAlreadyEmittedWarnings;

	const int32 kRDGEmitWarningsOnce = 1;

	if (GRDGDebug == kRDGEmitWarningsOnce)
	{
		if (!GAlreadyEmittedWarnings.Contains(WarningMessage))
		{
			GAlreadyEmittedWarnings.Add(WarningMessage);
			UE_LOG(LogRDG, Warning, TEXT("%s"), *WarningMessage);

			if (GRDGBreakpoint == RDG_BREAKPOINT_WARNINGS)
			{
				UE_DEBUG_BREAK();
			}
		}
	}
	else
	{
		UE_LOG(LogRDG, Warning, TEXT("%s"), *WarningMessage);

		if (GRDGBreakpoint == RDG_BREAKPOINT_WARNINGS)
		{
			UE_DEBUG_BREAK();
		}
	}
}

#endif

int32 GRDGAsyncCompute = 1;
TAutoConsoleVariable<int32> CVarRDGAsyncCompute(
	TEXT("r.RDG.AsyncCompute"),
	RDG_ASYNC_COMPUTE_ENABLED,
	TEXT("Controls the async compute policy.\n")
	TEXT(" 0:disabled, no async compute is used;\n")
	TEXT(" 1:enabled for passes tagged for async compute (default);\n")
	TEXT(" 2:enabled for all compute passes implemented to use the compute command list;\n"),
	ECVF_RenderThreadSafe);

FAutoConsoleVariableSink CVarRDGAsyncComputeSink(FConsoleCommandDelegate::CreateLambda([]()
{
	GRDGAsyncCompute = CVarRDGAsyncCompute.GetValueOnGameThread();

	if (GRDGAsyncCompute && !GSupportsEfficientAsyncCompute)
	{
		GRDGAsyncCompute = 0;
	}
}));

int32 GRDGCullPasses = 1;
FAutoConsoleVariableRef CVarRDGCullPasses(
	TEXT("r.RDG.CullPasses"),
	GRDGCullPasses,
	TEXT("The graph will cull passes with unused outputs.\n")
	TEXT(" 0:off;\n")
	TEXT(" 1:on(default);\n"),
	ECVF_RenderThreadSafe);

int32 GRDGMergeRenderPasses = 1;
FAutoConsoleVariableRef CVarRDGMergeRenderPasses(
	TEXT("r.RDG.MergeRenderPasses"),
	GRDGMergeRenderPasses,
	TEXT("The graph will merge identical, contiguous render passes into a single render pass.\n")
	TEXT(" 0:off;\n")
	TEXT(" 1:on(default);\n"),
	ECVF_RenderThreadSafe);

#if CSV_PROFILER
int32 GRDGVerboseCSVStats = 0;
FAutoConsoleVariableRef CVarRDGVerboseCSVStats(
	TEXT("r.RDG.VerboseCSVStats"),
	GRDGVerboseCSVStats,
	TEXT("Controls the verbosity of CSV profiling stats for RDG.\n")
	TEXT(" 0: emits one CSV profile for graph execution;\n")
	TEXT(" 1: emits a CSV profile for each phase of graph execution."),
	ECVF_RenderThreadSafe);
#endif

#if STATS
int32 GRDGStatPassCount = 0;
int32 GRDGStatPassCullCount = 0;
int32 GRDGStatPassDependencyCount = 0;
int32 GRDGStatRenderPassMergeCount = 0;
int32 GRDGStatTextureCount = 0;
int32 GRDGStatBufferCount = 0;
int32 GRDGStatTransitionCount = 0;
int32 GRDGStatTransitionBatchCount = 0;
int32 GRDGStatMemoryWatermark = 0;

DEFINE_STAT(STAT_RDG_PassCount);
DEFINE_STAT(STAT_RDG_PassCullCount);
DEFINE_STAT(STAT_RDG_RenderPassMergeCount);
DEFINE_STAT(STAT_RDG_PassDependencyCount);
DEFINE_STAT(STAT_RDG_TextureCount);
DEFINE_STAT(STAT_RDG_BufferCount);
DEFINE_STAT(STAT_RDG_TransitionCount);
DEFINE_STAT(STAT_RDG_TransitionBatchCount);
DEFINE_STAT(STAT_RDG_CompileTime);
DEFINE_STAT(STAT_RDG_CollectResourcesTime);
DEFINE_STAT(STAT_RDG_CollectBarriersTime);
DEFINE_STAT(STAT_RDG_ClearTime);
DEFINE_STAT(STAT_RDG_MemoryWatermark);
#endif

void InitRenderGraph()
{
#if RDG_ENABLE_DEBUG_WITH_ENGINE
	if (FParse::Param(FCommandLine::Get(), TEXT("rdgimmediate")))
	{
		GRDGImmediateMode = 1;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("rdgdebug")))
	{
		GRDGDebug = 1;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("rdgtransitionlog")))
	{
		// Set to -1 to specify infinite number of frames.
		GRDGTransitionLog = -1;
	}

	int32 BreakpointValue = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgbreakpoint"), BreakpointValue))
	{
		GRDGBreakpoint = BreakpointValue;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("rdgclobberresources")))
	{
		GRDGClobberResources = 1;
	}

	int32 CullPassesValue = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgcullpasses"), CullPassesValue))
	{
		GRDGCullPasses = CullPassesValue;
	}

	int32 MergeRenderPassesValue = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgmergerenderpasses"), MergeRenderPassesValue))
	{
		GRDGMergeRenderPasses = MergeRenderPassesValue;
	}

	int32 OverlapUAVsValue = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgoverlapuavs"), OverlapUAVsValue))
	{
		GRDGOverlapUAVs = OverlapUAVsValue;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("rdgextendresourcelifetimes")))
	{
		GRDGExtendResourceLifetimes = 1;
	}

	int32 DumpGraphValue = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgdumpgraph"), DumpGraphValue))
	{
		CVarDumpGraph->Set(DumpGraphValue);
	}

	int32 AsyncComputeValue = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgasynccompute"), AsyncComputeValue))
	{
		CVarRDGAsyncCompute->Set(AsyncComputeValue);
	}

	FString GraphFilter;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgdebuggraphfilter"), GraphFilter))
	{
		CVarRDGDebugGraphFilter->Set(*GraphFilter);
	}

	FString PassFilter;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgdebugpassfilter"), PassFilter))
	{
		CVarRDGDebugPassFilter->Set(*PassFilter);
	}

	FString ResourceFilter;
	if (FParse::Value(FCommandLine::Get(), TEXT("rdgdebugresourcefilter"), ResourceFilter))
	{
		CVarRDGDebugResourceFilter->Set(*ResourceFilter);
	}
#endif
}