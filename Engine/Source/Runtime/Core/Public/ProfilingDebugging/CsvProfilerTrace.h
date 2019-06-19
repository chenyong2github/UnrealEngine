// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Trace.h"

#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define CSVPROFILERTRACE_ENABLED 1
#else
#define CSVPROFILERTRACE_ENABLED 0
#endif

#if CSVPROFILERTRACE_ENABLED

struct FCsvProfilerTrace
{
	CORE_API static void OutputInlineStat(const char* StatName);
	CORE_API static void OutputDeclaredStat(const FName& StatName, const TCHAR* StatNameString);
	CORE_API static void OutputCustomStat(const char* StatName, int32 Value, uint8 OpType);
	CORE_API static void OutputCustomStat(const FName& StatName, int32 Value, uint8 OpType);
	CORE_API static void OutputCustomStat(const char* StatName, float Value, uint8 OpType);
	CORE_API static void OutputCustomStat(const FName& StatName, float Value, uint8 OpType);
};

#define TRACE_CSV_PROFILER_INLINE_STAT(StatName) \
	static bool PREPROCESSOR_JOIN(__CsvProfilerStat, __LINE__); \
	if (!PREPROCESSOR_JOIN(__CsvProfilerStat, __LINE__)) { \
		FCsvProfilerTrace::OutputInlineStat(StatName); \
		PREPROCESSOR_JOIN(__CsvProfilerStat, __LINE__) = true; \
	}

#define TRACE_CSVPROFILER_DECLARED_STAT(StatName, StatNameString) \
	static bool PREPROCESSOR_JOIN(__CsvProfilerStat, __LINE__); \
	if (!PREPROCESSOR_JOIN(__CsvProfilerStat, __LINE__)) { \
		FCsvProfilerTrace::OutputDeclaredStat(StatName, StatNameString); \
		PREPROCESSOR_JOIN(__CsvProfilerStat, __LINE__) = true; \
	}

#define TRACE_CSV_PROFILER_CUSTOM_STAT(StatName, Value, OpType) \
	FCsvProfilerTrace::OutputCustomStat(StatName, Value, OpType);

#else

#define TRACE_CSV_PROFILER_INLINE_STAT(...)
#define TRACE_CSVPROFILER_DECLARED_STAT(...)
#define TRACE_CSV_PROFILER_CUSTOM_STAT(...)

#endif