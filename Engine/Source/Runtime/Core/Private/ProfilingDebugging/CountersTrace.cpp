// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/PlatformTime.h"
#include "Misc/Parse.h"

#if COUNTERSTRACE_ENABLED

UE_TRACE_EVENT_BEGIN(Counters, Spec, Always)
	UE_TRACE_EVENT_FIELD(uint16, Id)
	UE_TRACE_EVENT_FIELD(uint8, Type)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Counters, SetValueInt)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(int64, Value)
	UE_TRACE_EVENT_FIELD(uint16, CounterId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Counters, SetValueFloat)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, Value)
	UE_TRACE_EVENT_FIELD(uint16, CounterId)
UE_TRACE_EVENT_END()


struct FCountersTracePrivate
{
	static uint16 InitCounter(const TCHAR* CounterName, ETraceCounterType Type);
	
	static void SetValue(uint16 CounterId, int64 Value)
	{
		UE_TRACE_LOG(Counters, SetValueInt)
			<< SetValueInt.Cycle(FPlatformTime::Cycles64())
			<< SetValueInt.Value(Value)
			<< SetValueInt.CounterId(CounterId);
	}

	static void SetValue(uint16 CounterId, float Value)
	{
		UE_TRACE_LOG(Counters, SetValueFloat)
			<< SetValueFloat.Cycle(FPlatformTime::Cycles64())
			<< SetValueFloat.Value(Value)
			<< SetValueFloat.CounterId(CounterId);
	}
};

uint16 FCountersTracePrivate::InitCounter(const TCHAR* CounterName, ETraceCounterType Type)
{
	static TAtomic<uint16> NextId(1);
	uint16 CounterId = uint16(NextId++);
	uint16 NameSize = (FCString::Strlen(CounterName) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(Counters, Spec, NameSize)
		<< Spec.Id(CounterId)
		<< Spec.Type(uint8(Type))
		<< Spec.Attachment(CounterName, NameSize);
	return CounterId;
}

void FCountersTrace::FCounterInt::Init(const TCHAR* CounterName)
{
	CounterId = FCountersTracePrivate::InitCounter(CounterName, TraceCounterType_Int);
}

void FCountersTrace::FCounterInt::Set(int64 Value)
{
	if (Value != CurrentValue)
	{
		CurrentValue = Value;
		FCountersTracePrivate::SetValue(CounterId, Value);
	}
}

void FCountersTrace::FCounterFloat::Init(const TCHAR* CounterName)
{
	CounterId = FCountersTracePrivate::InitCounter(CounterName, TraceCounterType_Float);
}

void FCountersTrace::FCounterFloat::Set(float Value)
{
	if (Value != CurrentValue)
	{
		CurrentValue = Value;
		FCountersTracePrivate::SetValue(CounterId, Value);
	}
}

void FCountersTrace::FCounterMemory::Init(const TCHAR* CounterName)
{
	CounterId = FCountersTracePrivate::InitCounter(CounterName, TraceCounterType_Memory);
}

void FCountersTrace::FCounterMemory::Set(uint64 Value)
{
	if (Value != CurrentValue)
	{
		CurrentValue = Value;
		FCountersTracePrivate::SetValue(CounterId, (int64)Value);
	}
}

void FCountersTrace::Init(const TCHAR* CmdLine)
{
	if (FParse::Param(CmdLine, TEXT("counterstrace")))
	{
		UE_TRACE_EVENT_IS_ENABLED(Counters, SetValueInt);
		UE_TRACE_EVENT_IS_ENABLED(Counters, SetValueFloat);
		Trace::ToggleEvent(TEXT("Counters"), true);
	}
}

#endif