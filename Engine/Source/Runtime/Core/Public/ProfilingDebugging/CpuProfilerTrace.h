// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if !IS_PROGRAM && !UE_BUILD_SHIPPING
#define CPUPROFILERTRACE_ENABLED 0
#else
#define CPUPROFILERTRACE_ENABLED 0
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
	CORE_API static void OutputBeginEvent(uint16 SpecId);
	CORE_API static void OutputBeginDynamicEvent(const TCHAR* Name);
	CORE_API static void OutputEndEvent();
	CORE_API static uint64 GetCurrentCycle();
};

struct FCpuProfilerEventSpec
{
	CORE_API static uint16 AssignId(const TCHAR* Name, ECpuProfilerGroup Group);
};

struct FCpuProfilerEventScope
{
	FCpuProfilerEventScope(uint16 InSpecId)
	{
		FCpuProfilerTrace::OutputBeginEvent(InSpecId);
	}

	~FCpuProfilerEventScope()
	{
		FCpuProfilerTrace::OutputEndEvent();
	}
};

struct FCpuProfilerDynamicEventScope
{
	FCpuProfilerDynamicEventScope(const TCHAR* InName)
	{
		FCpuProfilerTrace::OutputBeginDynamicEvent(InName);
	}

	~FCpuProfilerDynamicEventScope()
	{
		FCpuProfilerTrace::OutputEndEvent();
	}
};

#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, Group) \
	static uint16 PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__); \
	if (PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) == 0) { \
		PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) = FCpuProfilerEventSpec::AssignId(Name, Group); \
	} \
	FCpuProfilerEventScope PREPROCESSOR_JOIN(__CpuProfilerEventScope, __LINE__)(PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__));

#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, CpuProfilerGroup_Default)

#define TRACE_CPUPROFILER_EVENT_SCOPE_GROUP(Name, Group) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(TEXT(#Name), Group);

#define TRACE_CPUPROFILER_EVENT_SCOPE(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(TEXT(#Name), CpuProfilerGroup_Default);

#define TRACE_CPUPROFILER_DYNAMIC_EVENT_SCOPE(Name) \
	FCpuProfilerDynamicEventScope ANONYMOUS_VARIABLE(__Scope)(Name);

#else

#define TRACE_CPUPROFILER_EVENT_SCOPE_GROUP(Name, Group)
#define TRACE_CPUPROFILER_EVENT_SCOPE(Name)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, Group)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name)
#define TRACE_CPUPROFILER_DYNAMIC_EVENT_SCOPE(Name)

#endif
