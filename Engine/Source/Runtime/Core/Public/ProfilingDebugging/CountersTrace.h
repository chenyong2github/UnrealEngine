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
};

enum ETraceCounterDisplayHint
{
	TraceCounterDisplayHint_None,
	TraceCounterDisplayHint_Memory,
};

#if COUNTERSTRACE_ENABLED

struct FCountersTrace
{
	CORE_API static uint16 OutputInitCounter(const TCHAR* CounterName, ETraceCounterType CounterType, ETraceCounterDisplayHint CounterDisplayHint);
	CORE_API static void OutputSetValue(uint16 CounterId, int64 Value);
	CORE_API static void OutputSetValue(uint16 CounterId, double Value);

	class FCounter
	{
	public:
		FCounter()
			: IntValue(0)
		{
		}

		FCounter(const TCHAR* InCounterName, ETraceCounterType InCounterType, ETraceCounterDisplayHint InCounterDisplayHint)
			: IntValue(0)
			, CounterType(InCounterType)
		{
			CounterId = OutputInitCounter(InCounterName, InCounterType, InCounterDisplayHint);
		}

		void Init(const TCHAR* InCounterName, ETraceCounterType InCounterType, ETraceCounterDisplayHint InCounterDisplayHint)
		{
			if (CounterId == 0)
			{
				CounterType = InCounterType;
				CounterId = OutputInitCounter(InCounterName, InCounterType, InCounterDisplayHint);
			}
		}

		void Set(int64 Value)
		{
			if (CounterType == TraceCounterType_Int)
			{
				if (IntValue != Value)
				{
					IntValue = Value;
					OutputSetValue(CounterId, IntValue);
				}
			}
			else
			{
				double ValueAsDouble = static_cast<double>(Value);
				if (FloatValue != ValueAsDouble)
				{
					FloatValue = ValueAsDouble;
					OutputSetValue(CounterId, FloatValue);
				}
			}
		}

		void Set(double Value)
		{
			if (CounterType == TraceCounterType_Int)
			{
				int64 ValueAsInt = static_cast<int64>(Value);
				if (IntValue != ValueAsInt)
				{
					IntValue = ValueAsInt;
					OutputSetValue(CounterId, IntValue);
				}
			}
			else
			{
				if (FloatValue != Value)
				{
					FloatValue = Value;
					OutputSetValue(CounterId, FloatValue);
				}
			}
		}

		void Add(int64 Value)
		{
			if (Value != 0)
			{
				if (CounterType == TraceCounterType_Int)
				{
					IntValue += Value;
					OutputSetValue(CounterId, IntValue);
				}
				else
				{
					FloatValue += static_cast<double>(Value);
					OutputSetValue(CounterId, FloatValue);
				}
			}
		}

		void Add(double Value)
		{
			if (Value != 0)
			{
				if (CounterType == TraceCounterType_Int)
				{
					IntValue += static_cast<int64>(Value);
					OutputSetValue(CounterId, IntValue);
				}
				else
				{
					FloatValue += Value;
					OutputSetValue(CounterId, FloatValue);
				}
			}
		}

	private:
		union
		{
			int64 IntValue;
			double FloatValue;
		};
		uint16 CounterId;
		ETraceCounterType CounterType;
	};

	CORE_API static void Init(const TCHAR* CmdLine);
};

#define TRACE_COUNTERS_INIT(CmdLine) \
	FCountersTrace::Init(CmdLine);

#define __TRACE_DECLARE_COUNTER(CounterName, CounterType, CounterDisplayHint) \
	FCountersTrace::FCounter PREPROCESSOR_JOIN(__GTraceCounter, CounterName)(TEXT(#CounterName), CounterType, CounterDisplayHint);

#define __TRACE_DECLARE_INLINE_COUNTER(CounterName, CounterType, CounterDisplayHint) \
	static FCountersTrace::FCounter PREPROCESSOR_JOIN(__TraceCounter, __LINE__); \
	PREPROCESSOR_JOIN(__TraceCounter, __LINE__).Init(TEXT(#CounterName), CounterType, CounterDisplayHint); \

#define TRACE_INT_VALUE(CounterName, Value) \
	__TRACE_DECLARE_INLINE_COUNTER(CounterName, TraceCounterType_Int, TraceCounterDisplayHint_None) \
	PREPROCESSOR_JOIN(__TraceCounter, __LINE__).Set(int64(Value));

#define TRACE_FLOAT_VALUE(CounterName, Value) \
	__TRACE_DECLARE_INLINE_COUNTER(CounterName, TraceCounterType_Float, TraceCounterDisplayHint_None) \
	PREPROCESSOR_JOIN(__TraceCounter, __LINE__).Set(double(Value));

#define TRACE_MEMORY_VALUE(CounterName, Value) \
	__TRACE_DECLARE_INLINE_COUNTER(CounterName, TraceCounterType_Int, TraceCounterDisplayHint_Memory) \
	PREPROCESSOR_JOIN(__TraceCounter, __LINE__).Set(int64(Value));

#define TRACE_DECLARE_INT_COUNTER(CounterName) \
	__TRACE_DECLARE_COUNTER(CounterName, TraceCounterType_Int, TraceCounterDisplayHint_None)

#define TRACE_DECLARE_FLOAT_COUNTER(CounterName) \
	__TRACE_DECLARE_COUNTER(CounterName, TraceCounterType_Float, TraceCounterDisplayHint_None)

#define TRACE_DECLARE_MEMORY_COUNTER(CounterName) \
	__TRACE_DECLARE_COUNTER(CounterName, TraceCounterType_Int, TraceCounterDisplayHint_Memory)

#define TRACE_DECLARE_COUNTER_EXTERN(CounterName) \
	extern FCountersTrace::FCounter PREPROCESSOR_JOIN(__GTraceCounter, CounterName);

#define TRACE_COUNTER_SET(CounterName, Value) \
	PREPROCESSOR_JOIN(__GTraceCounter, CounterName).Set(Value);

#define TRACE_COUNTER_ADD(CounterName, Value) \
	PREPROCESSOR_JOIN(__GTraceCounter, CounterName).Add(Value);

#define TRACE_COUNTER_INCREMENT(CounterName) \
	PREPROCESSOR_JOIN(__GTraceCounter, CounterName).Add(1);

#define TRACE_COUNTER_DECREMENT(CounterName) \
	PREPROCESSOR_JOIN(__GTraceCounter, CounterName).Add(-1);

#else

#define TRACE_COUNTERS_INIT(CmdLine)
#define TRACE_INT_VALUE(CounterName, Value)
#define TRACE_FLOAT_VALUE(CounterName, Value)
#define TRACE_MEMORY_VALUE(CounterName, Value)
#define TRACE_DECLARE_INT_COUNTER(CounterName)
#define TRACE_DECLARE_FLOAT_COUNTER(CounterName)
#define TRACE_DECLARE_MEMORY_COUNTER(CounterName)
#define TRACE_DECLARE_COUNTER_EXTERN(CounterName)
#define TRACE_COUNTER_SET(CounterName, Value)
#define TRACE_COUNTER_ADD(CounterName, Value)
#define TRACE_COUNTER_INCREMENT(CounterName)
#define TRACE_COUNTER_DECREMENT(CounterName)

#endif