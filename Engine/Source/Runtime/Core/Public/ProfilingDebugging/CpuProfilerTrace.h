// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Config.h"

#if !defined(CPUPROFILERTRACE_ENABLED)
#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define CPUPROFILERTRACE_ENABLED 1
#else
#define CPUPROFILERTRACE_ENABLED 0
#endif
#endif

enum ECpuProfilerGroup
{
	CpuProfilerGroup_Default,
	CpuProfilerGroup_Stats,
	CpuProfilerGroup_LoadTime,
	CpuProfilerGroup_CsvProfiler,
};

#if CPUPROFILERTRACE_ENABLED

struct FCpuProfilerTrace
{
	CORE_API static void Init(const TCHAR* CmdLine);
	CORE_API static void Shutdown();
	FORCENOINLINE CORE_API static uint32 OutputEventType(const ANSICHAR* Name, ECpuProfilerGroup Group);
	FORCENOINLINE CORE_API static uint32 OutputEventType(const TCHAR* Name, ECpuProfilerGroup Group);
	CORE_API static void OutputBeginEvent(uint32 SpecId);
	CORE_API static void OutputBeginDynamicEvent(const ANSICHAR* Name, ECpuProfilerGroup Group);
	CORE_API static void OutputBeginDynamicEvent(const TCHAR* Name, ECpuProfilerGroup Group);
	CORE_API static void OutputEndEvent();

	struct FEventScope
	{
		FEventScope(uint32 InSpecId)
		{
			OutputBeginEvent(InSpecId);
		}

		~FEventScope()
		{
			OutputEndEvent();
		}
	};

	struct FDynamicEventScope
	{
		FDynamicEventScope(const ANSICHAR* InEventName, ECpuProfilerGroup Group)
		{
			OutputBeginDynamicEvent(InEventName, Group);
		}

		FDynamicEventScope(const TCHAR* InEventName, ECpuProfilerGroup Group)
		{
			OutputBeginDynamicEvent(InEventName, Group);
		}

		~FDynamicEventScope()
		{
			OutputEndEvent();
		}
	};
};

#define TRACE_CPUPROFILER_INIT(CmdLine) \
	FCpuProfilerTrace::Init(CmdLine);

#define TRACE_CPUPROFILER_SHUTDOWN() \
	FCpuProfilerTrace::Shutdown();

#define TRACE_CPUPROFILER_EVENT_SCOPE_GROUP(Name, Group) \
	static uint16 PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__); \
	if (PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) == 0) { \
		PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) = FCpuProfilerTrace::OutputEventType(#Name, Group); \
	} \
	FCpuProfilerTrace::FEventScope PREPROCESSOR_JOIN(__CpuProfilerEventScope, __LINE__)(PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__));

#define TRACE_CPUPROFILER_EVENT_SCOPE(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_GROUP(Name, CpuProfilerGroup_Default)

#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, Group) \
	FCpuProfilerTrace::FDynamicEventScope PREPROCESSOR_JOIN(__CpuProfilerEventScope, __LINE__)(Name, Group);
	
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, CpuProfilerGroup_Default)

#else

#define TRACE_CPUPROFILER_INIT(CmdLine)
#define TRACE_CPUPROFILER_SHUTDOWN()
#define TRACE_CPUPROFILER_EVENT_SCOPE_GROUP(Name, Group)
#define TRACE_CPUPROFILER_EVENT_SCOPE(Name)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, Group)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name)

#endif
