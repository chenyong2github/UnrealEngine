// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CsvProfilerTrace.h"
#include "HAL/PlatformTime.h"
#include "UObject/NameTypes.h"

#if CSVPROFILERTRACE_ENABLED

UE_TRACE_EVENT_BEGIN(CsvProfiler, InlineStat, Always)
	UE_TRACE_EVENT_FIELD(const char*, StatNamePointer)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, DeclaredStat, Always)
	UE_TRACE_EVENT_FIELD(uint32, StatId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, CustomStatInlineInt)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const char*, StatNamePointer)
	UE_TRACE_EVENT_FIELD(int32, Value)
	UE_TRACE_EVENT_FIELD(uint8, OpType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, CustomStatInlineFloat)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const char*, StatNamePointer)
	UE_TRACE_EVENT_FIELD(float, Value)
	UE_TRACE_EVENT_FIELD(uint8, OpType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, CustomStatDeclaredInt)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, StatId)
	UE_TRACE_EVENT_FIELD(int32, Value)
	UE_TRACE_EVENT_FIELD(uint8, OpType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, CustomStatDeclaredFloat)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, StatId)
	UE_TRACE_EVENT_FIELD(float, Value)
	UE_TRACE_EVENT_FIELD(uint8, OpType)
UE_TRACE_EVENT_END()

void FCsvProfilerTrace::OutputInlineStat(const char* StatName)
{
	uint16 NameSize = (strlen(StatName) + 1) * sizeof(char);
	UE_TRACE_LOG(CsvProfiler, InlineStat, NameSize)
		<< InlineStat.StatNamePointer(StatName)
		<< InlineStat.Attachment(StatName, NameSize);
}

void FCsvProfilerTrace::OutputDeclaredStat(const FName& StatName, const TCHAR* StatNameString)
{
	uint16 NameSize = (FCString::Strlen(StatNameString) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(CsvProfiler, DeclaredStat, NameSize)
		<< DeclaredStat.StatId(StatName.GetComparisonIndex().ToUnstableInt())
		<< DeclaredStat.Attachment(StatNameString, NameSize);
}

void FCsvProfilerTrace::OutputCustomStat(const char* StatName, int32 Value, uint8 OpType)
{
	UE_TRACE_LOG(CsvProfiler, CustomStatInlineInt)
		<< CustomStatInlineInt.Cycle(FPlatformTime::Cycles64())
		<< CustomStatInlineInt.StatNamePointer(StatName)
		<< CustomStatInlineInt.Value(Value)
		<< CustomStatInlineInt.OpType(OpType);
}

void FCsvProfilerTrace::OutputCustomStat(const FName& StatName, int32 Value, uint8 OpType)
{
	UE_TRACE_LOG(CsvProfiler, CustomStatDeclaredInt)
		<< CustomStatDeclaredInt.Cycle(FPlatformTime::Cycles64())
		<< CustomStatDeclaredInt.StatId(StatName.GetComparisonIndex().ToUnstableInt())
		<< CustomStatDeclaredInt.Value(Value)
		<< CustomStatDeclaredInt.OpType(OpType);
}

void FCsvProfilerTrace::OutputCustomStat(const char* StatName, float Value, uint8 OpType)
{
	UE_TRACE_LOG(CsvProfiler, CustomStatInlineFloat)
		<< CustomStatInlineFloat.Cycle(FPlatformTime::Cycles64())
		<< CustomStatInlineFloat.StatNamePointer(StatName)
		<< CustomStatInlineFloat.Value(Value)
		<< CustomStatInlineFloat.OpType(OpType);
}

void FCsvProfilerTrace::OutputCustomStat(const FName& StatName, float Value, uint8 OpType)
{
	UE_TRACE_LOG(CsvProfiler, CustomStatDeclaredFloat)
		<< CustomStatDeclaredFloat.Cycle(FPlatformTime::Cycles64())
		<< CustomStatDeclaredFloat.StatId(StatName.GetComparisonIndex().ToUnstableInt())
		<< CustomStatDeclaredFloat.Value(Value)
		<< CustomStatDeclaredFloat.OpType(OpType);
}

#endif