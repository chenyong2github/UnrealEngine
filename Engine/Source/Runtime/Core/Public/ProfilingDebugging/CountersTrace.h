// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Trace.h"

#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define COUNTERSTRACE_ENABLED 1
#else
#define COUNTERSTRACE_ENABLED 0
#endif

enum ETraceCounterType
{
	TraceCounterType_Int,
	TraceCounterType_Float,
	TraceCounterType_Memory,
};

#if COUNTERSTRACE_ENABLED

struct FCountersTrace
{
	struct FCounterInt
	{
		CORE_API void Init(const TCHAR* CounterName);
		CORE_API void Set(int64 Value);

		int64 CurrentValue;
		uint16 CounterId;
	};

	struct FCounterFloat
	{
		CORE_API void Init(const TCHAR* CounterName);
		CORE_API void Set(float Value);

		double CurrentValue;
		uint16 CounterId;
	};

	struct FCounterMemory
	{
		CORE_API void Init(const TCHAR* CounterName);
		CORE_API void Set(uint64 Value);

		uint64 CurrentValue;
		uint16 CounterId;
	};

	CORE_API static void Init(const TCHAR* CmdLine);
};

#define TRACE_COUNTERS_INIT(CmdLine) \
	FCountersTrace::Init(CmdLine);

#define __TRACE_COUNTER_VALUE(CounterType, CounterName, Value) \
	static FCountersTrace::CounterType PREPROCESSOR_JOIN(__TraceCounter, __LINE__); \
	if (!PREPROCESSOR_JOIN(__TraceCounter, __LINE__).CounterId) \
	{ \
		PREPROCESSOR_JOIN(__TraceCounter, __LINE__).Init(TEXT(#CounterName)); \
	} \
	PREPROCESSOR_JOIN(__TraceCounter, __LINE__).Set(Value);

#define TRACE_INTEGER_VALUE(CounterName, Value) \
	__TRACE_COUNTER_VALUE(FCounterInt, CounterName, Value)

#define TRACE_FLOAT_VALUE(CounterName, Value) \
	__TRACE_COUNTER_VALUE(FCounterFloat, CounterName, Value)

#define TRACE_MEMORY_VALUE(CounterName, Value) \
	__TRACE_COUNTER_VALUE(FCounterMemory, CounterName, Value)

#else

#define TRACE_COUNTERS_INIT(CmdLine)
#define TRACE_INTEGER_VALUE(CounterName, Value)
#define TRACE_FLOAT_VALUE(CounterName, Value)
#define TRACE_MEMORY_VALUE(CounterName, Value)

#endif