// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if !IS_PROGRAM && !UE_BUILD_SHIPPING && (PLATFORM_WINDOWS || PLATFORM_PS4 || PLATFORM_XBOXONE)
#define CPUPROFILERTRACE_ENABLED 1
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
	CORE_API static void Init(bool bStartEnabled);
	CORE_API static uint16 OutputEventType(const TCHAR* Name, ECpuProfilerGroup Group);
	CORE_API static void OutputBeginEvent(uint16 SpecId);
	CORE_API static void OutputEndEvent();

	struct FEventScope
	{
		FEventScope(uint16 InSpecId)
		{
			OutputBeginEvent(InSpecId);
		}

		~FEventScope()
		{
			OutputEndEvent();
		}
	};
};

#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, Group) \
	static uint16 PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__); \
	if (PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) == 0) { \
		PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) = FCpuProfilerTrace::OutputEventType(Name, Group); \
	} \
	FCpuProfilerTrace::FEventScope PREPROCESSOR_JOIN(__CpuProfilerEventScope, __LINE__)(PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__));

#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, CpuProfilerGroup_Default)

#define TRACE_CPUPROFILER_EVENT_SCOPE_GROUP(Name, Group) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(TEXT(#Name), Group);

#define TRACE_CPUPROFILER_EVENT_SCOPE(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(TEXT(#Name), CpuProfilerGroup_Default);

#else

#define TRACE_CPUPROFILER_EVENT_SCOPE_GROUP(Name, Group)
#define TRACE_CPUPROFILER_EVENT_SCOPE(Name)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_GROUP(Name, Group)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name)

#endif
