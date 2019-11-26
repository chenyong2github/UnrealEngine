// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CsvProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "HAL/PlatformTime.h"
#include "UObject/NameTypes.h"
#include "Trace/Trace.h"

#if CSVPROFILERTRACE_ENABLED

UE_TRACE_EVENT_BEGIN(CsvProfiler, RegisterCategory, Always)
	UE_TRACE_EVENT_FIELD(int32, Index)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, DefineInlineStat, Always)
	UE_TRACE_EVENT_FIELD(uint64, StatId)
	UE_TRACE_EVENT_FIELD(int32, CategoryIndex)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, DefineDeclaredStat, Always)
	UE_TRACE_EVENT_FIELD(uint64, StatId)
	UE_TRACE_EVENT_FIELD(int32, CategoryIndex)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, BeginStat, Always)
	UE_TRACE_EVENT_FIELD(uint64, StatId)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, EndStat, Always)
	UE_TRACE_EVENT_FIELD(uint64, StatId)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, BeginExclusiveStat, Always)
	UE_TRACE_EVENT_FIELD(uint64, StatId)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, EndExclusiveStat, Always)
	UE_TRACE_EVENT_FIELD(uint64, StatId)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, CustomStatInt, Always)
	UE_TRACE_EVENT_FIELD(uint64, StatId)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(int32, Value)
	UE_TRACE_EVENT_FIELD(uint8, OpType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, CustomStatFloat, Always)
	UE_TRACE_EVENT_FIELD(uint64, StatId)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(float, Value)
	UE_TRACE_EVENT_FIELD(uint8, OpType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, Event, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(int32, CategoryIndex)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, BeginCapture, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, RenderThreadId)
	UE_TRACE_EVENT_FIELD(uint32, RHIThreadId)
	UE_TRACE_EVENT_FIELD(bool, EnableCounts)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, EndCapture, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CsvProfiler, Metadata, Always)
	UE_TRACE_EVENT_FIELD(uint16, ValueOffset)
UE_TRACE_EVENT_END()

struct FCsvProfilerTraceInternal
{
	union FStatId
	{
		struct
		{
			uint64 IsFName : 1;
			uint64 CategoryIndex : 11;
			uint64 FNameOrCString : 52;
		} Fields;
		uint64 Hash;
	};

	FORCEINLINE static uint64 GetStatId(const char* StatName, int32 CategoryIndex)
	{
		FStatId StatId;
		StatId.Fields.IsFName = false;
		StatId.Fields.CategoryIndex = CategoryIndex;
		StatId.Fields.FNameOrCString = uint64(StatName);
		return StatId.Hash;
	}

	FORCEINLINE static uint64 GetStatId(const FName& StatName, int32 CategoryIndex)
	{
		FStatId StatId;
		StatId.Fields.IsFName = true;
		StatId.Fields.CategoryIndex = CategoryIndex;
		StatId.Fields.FNameOrCString = StatName.GetComparisonIndex().ToUnstableInt();
		return StatId.Hash;
	}
};

void FCsvProfilerTrace::OutputRegisterCategory(int32 Index, const TCHAR* Name)
{
	uint16 NameSize = (FCString::Strlen(Name) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(CsvProfiler, RegisterCategory, NameSize)
		<< RegisterCategory.Index(Index)
		<< RegisterCategory.Attachment(Name, NameSize);
}

void FCsvProfilerTrace::OutputInlineStat(const char* StatName, int32 CategoryIndex)
{
	uint16 NameSize = (strlen(StatName) + 1) * sizeof(char);
	UE_TRACE_LOG(CsvProfiler, DefineInlineStat, NameSize)
		<< DefineInlineStat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< DefineInlineStat.CategoryIndex(CategoryIndex)
		<< DefineInlineStat.Attachment(StatName, NameSize);
}

CSV_DECLARE_CATEGORY_EXTERN(Exclusive);

void FCsvProfilerTrace::OutputInlineStatExclusive(const char* StatName)
{
	OutputInlineStat(StatName, CSV_CATEGORY_INDEX(Exclusive));
}

void FCsvProfilerTrace::OutputDeclaredStat(const FName& StatName, int32 CategoryIndex)
{
	TCHAR NameString[NAME_SIZE];
	StatName.GetPlainNameString(NameString);
	uint16 NameSize = (FCString::Strlen(NameString) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(CsvProfiler, DefineDeclaredStat, NameSize)
		<< DefineDeclaredStat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< DefineDeclaredStat.CategoryIndex(CategoryIndex)
		<< DefineDeclaredStat.Attachment(NameString, NameSize);
}

void FCsvProfilerTrace::OutputBeginStat(const char* StatName, int32 CategoryIndex, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, BeginStat)
		<< BeginStat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< BeginStat.Cycle(Cycles)
		<< BeginStat.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FCsvProfilerTrace::OutputBeginStat(const FName& StatName, int32 CategoryIndex, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, BeginStat)
		<< BeginStat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< BeginStat.Cycle(Cycles)
		<< BeginStat.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FCsvProfilerTrace::OutputEndStat(const char* StatName, int32 CategoryIndex, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, EndStat)
		<< EndStat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< EndStat.Cycle(Cycles)
		<< EndStat.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FCsvProfilerTrace::OutputEndStat(const FName& StatName, int32 CategoryIndex, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, EndStat)
		<< EndStat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< EndStat.Cycle(Cycles)
		<< EndStat.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FCsvProfilerTrace::OutputBeginExclusiveStat(const char* StatName, int32 CategoryIndex, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, BeginExclusiveStat)
		<< BeginExclusiveStat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< BeginExclusiveStat.Cycle(Cycles)
		<< BeginExclusiveStat.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FCsvProfilerTrace::OutputEndExclusiveStat(const char* StatName, int32 CategoryIndex, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, EndExclusiveStat)
		<< EndExclusiveStat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< EndExclusiveStat.Cycle(Cycles)
		<< EndExclusiveStat.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FCsvProfilerTrace::OutputCustomStat(const char* StatName, int32 CategoryIndex, int32 Value, uint8 OpType, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, CustomStatInt)
		<< CustomStatInt.Cycle(Cycles)
		<< CustomStatInt.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< CustomStatInt.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< CustomStatInt.Value(Value)
		<< CustomStatInt.OpType(OpType);
}

void FCsvProfilerTrace::OutputCustomStat(const FName& StatName, int32 CategoryIndex, int32 Value, uint8 OpType, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, CustomStatInt)
		<< CustomStatInt.Cycle(Cycles)
		<< CustomStatInt.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< CustomStatInt.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< CustomStatInt.Value(Value)
		<< CustomStatInt.OpType(OpType);
}

void FCsvProfilerTrace::OutputCustomStat(const char* StatName, int32 CategoryIndex, float Value, uint8 OpType, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, CustomStatFloat)
		<< CustomStatFloat.Cycle(Cycles)
		<< CustomStatFloat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< CustomStatFloat.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< CustomStatFloat.Value(Value)
		<< CustomStatFloat.OpType(OpType);
}

void FCsvProfilerTrace::OutputCustomStat(const FName& StatName, int32 CategoryIndex, float Value, uint8 OpType, uint64 Cycles)
{
	UE_TRACE_LOG(CsvProfiler, CustomStatFloat)
		<< CustomStatFloat.Cycle(Cycles)
		<< CustomStatFloat.StatId(FCsvProfilerTraceInternal::GetStatId(StatName, CategoryIndex))
		<< CustomStatFloat.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< CustomStatFloat.Value(Value)
		<< CustomStatFloat.OpType(OpType);
}

void FCsvProfilerTrace::OutputBeginCapture(const TCHAR* Filename, uint32 RenderThreadId, uint32 RHIThreadId, const char* DefaultWaitStatName, bool bEnableCounts)
{
	OutputInlineStat(DefaultWaitStatName, CSV_CATEGORY_INDEX(Exclusive));
	uint16 NameSize = (FCString::Strlen(Filename) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(CsvProfiler, BeginCapture, NameSize)
		<< BeginCapture.Cycle(FPlatformTime::Cycles64())
		<< BeginCapture.RenderThreadId(RenderThreadId)
		<< BeginCapture.RHIThreadId(RHIThreadId)
		<< BeginCapture.EnableCounts(bEnableCounts)
		<< BeginCapture.Attachment(Filename, NameSize);
}

void FCsvProfilerTrace::OutputEvent(const TCHAR* Text, int32 CategoryIndex, uint64 Cycles)
{
	uint16 TextSize = (FCString::Strlen(Text) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(CsvProfiler, Event, TextSize)
		<< Event.Cycle(Cycles)
		<< Event.CategoryIndex(CategoryIndex)
		<< Event.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< Event.Attachment(Text, TextSize);
}

void FCsvProfilerTrace::OutputEndCapture()
{
	UE_TRACE_LOG(CsvProfiler, EndCapture)
		<< EndCapture.Cycle(FPlatformTime::Cycles64());
}

void FCsvProfilerTrace::OutputMetadata(const TCHAR* Key, const TCHAR* Value)
{
	uint16 KeySize = (FCString::Strlen(Key) + 1) * sizeof(TCHAR);
	uint16 ValueSize = (FCString::Strlen(Value) + 1) * sizeof(TCHAR);
	auto Attachment = [Key, KeySize, Value, ValueSize](uint8* Out)
	{
		memcpy(Out, Key, KeySize);
		memcpy(Out + KeySize, Value, ValueSize);
	};
	UE_TRACE_LOG(CsvProfiler, Metadata, KeySize + ValueSize)
		<< Metadata.ValueOffset(KeySize)
		<< Metadata.Attachment(Attachment);
}

#endif
