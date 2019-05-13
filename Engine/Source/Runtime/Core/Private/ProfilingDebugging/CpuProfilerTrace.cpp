// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Templates/UnrealTemplate.h"
#include "Trace/Trace.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "Containers/Map.h"
#include "ProfilingDebugging/MiscTrace.h"

#if CPUPROFILERTRACE_ENABLED

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventSpec)
	UE_TRACE_EVENT_FIELD(uint16, Id)
	UE_TRACE_EVENT_FIELD(uint16, Group)
	UE_TRACE_EVENT_FIELD(uint16, NameSize)
	//UE_TRACE_EVENT_FIELD(uint8[], Name)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, DynamicEventName)
	UE_TRACE_EVENT_FIELD(uint16, Id)
	UE_TRACE_EVENT_FIELD(uint16, NameSize)
	//UE_TRACE_EVENT_FIELD(uint8[], Name)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventBatch)
	UE_TRACE_EVENT_FIELD(uint64, CycleBase)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(uint8, BufferSize)
	//UE_TRACE_EVENT_FIELD(uint8[], Buffer)
UE_TRACE_EVENT_END()

struct FCpuProfilerTraceInternal
{
public:
	enum
	{
		MaxBufferSize = 255,
		MaxEncodedEventSize = 11, // 9 + 2
		MaxEncodedDynamicEventSize = 13, // 9 + 2 + 2,
		FullBufferThreshold = MaxBufferSize - MaxEncodedEventSize,
		DynamicEventFullBufferThreshold = MaxBufferSize - MaxEncodedDynamicEventSize
	};

	struct FThreadState
	{
		TMap<uint32, uint16> KnownDynamicStrings;
		uint64 CycleBase;
		uint64 LastCycle;
		uint32 ThreadId;
		uint32 BufferSize;
		uint8 Buffer[MaxBufferSize];
	};

	FORCENOINLINE static FThreadState* InitThreadState(uint64 CycleBase);
	FORCENOINLINE static void FlushThreadBuffer(FThreadState* ThreadState, uint64 NewCycleBase);
	FORCENOINLINE static uint16 NewDynamicEventName(const TCHAR* Name);
	static void EncodeSpecId(uint16 SpecId, uint8*& BufferPtr);

	static uint32 TlsSlot;

private:
	friend struct FCpuProfilerEventSpec;

	FCpuProfilerTraceInternal();

	TAtomic<uint32> NextSpecId;
	TAtomic<uint16> NextDynamicEventNameId;

	static FCpuProfilerTraceInternal& Instance();
};


uint32 FCpuProfilerTraceInternal::TlsSlot;

FCpuProfilerTraceInternal::FCpuProfilerTraceInternal()
	: NextSpecId(1)
	, NextDynamicEventNameId(0)
{
	TlsSlot = FPlatformTLS::AllocTlsSlot();
}

void FCpuProfilerTraceInternal::EncodeSpecId(uint16 SpecId, uint8*& BufferPtr)
{
	*BufferPtr++ = uint8(SpecId);
	*BufferPtr++ = uint8(SpecId >> 8);
}

FCpuProfilerTraceInternal::FThreadState* FCpuProfilerTraceInternal::InitThreadState(uint64 CycleBase)
{
	FThreadState* ThreadState = new FThreadState();
	ThreadState->BufferSize = 0;
	ThreadState->ThreadId = FPlatformTLS::GetCurrentThreadId();
	ThreadState->CycleBase = CycleBase;
	ThreadState->LastCycle = CycleBase;
	FPlatformTLS::SetTlsValue(TlsSlot, ThreadState);
	return ThreadState;
}

void FCpuProfilerTraceInternal::FlushThreadBuffer(FThreadState* ThreadState, uint64 NewCycleBase)
{
	UE_TRACE_LOG(CpuProfiler, EventBatch, ThreadState->BufferSize)
		<< EventBatch.CycleBase(ThreadState->CycleBase)
		<< EventBatch.ThreadId(ThreadState->ThreadId)
		<< EventBatch.BufferSize(ThreadState->BufferSize)
		<< EventBatch.Attachment(ThreadState->Buffer, ThreadState->BufferSize);

	ThreadState->BufferSize = 0;
	ThreadState->CycleBase = NewCycleBase;
	ThreadState->LastCycle = NewCycleBase;
}

uint16 FCpuProfilerTraceInternal::NewDynamicEventName(const TCHAR* Name)
{
	uint16 NameLength = FCString::Strlen(Name);
	uint16 Id = FCpuProfilerTraceInternal::Instance().NextDynamicEventNameId++;
	auto NameCopy = [NameLength, Name](uint8* Out) {
		int32 i = 0;
		for (; i < NameLength; ++i)
		{
			Out[i] = Name[i] & 0x7f;
		}
		Out[i] = 0;
	};
	UE_TRACE_LOG(CpuProfiler, DynamicEventName, NameLength + 1)
		<< DynamicEventName.Id(Id)
		<< DynamicEventName.NameSize(NameLength)
		<< DynamicEventName.Attachment(NameCopy);
	return Id;
}

FCpuProfilerTraceInternal& FCpuProfilerTraceInternal::Instance()
{
	static FCpuProfilerTraceInternal I;
	return I;
}

void FCpuProfilerTrace::OutputBeginEvent(uint16 SpecId)
{
	uint64 Cycle = FPlatformTime::Cycles64();
	FCpuProfilerTraceInternal::FThreadState* ThreadState = (FCpuProfilerTraceInternal::FThreadState*)FPlatformTLS::GetTlsValue(FCpuProfilerTraceInternal::TlsSlot);
	if (!ThreadState)
	{
		ThreadState = FCpuProfilerTraceInternal::InitThreadState(Cycle);
	}
	uint64 CycleDiff = Cycle - ThreadState->LastCycle;
	ThreadState->LastCycle = Cycle;
	if (ThreadState->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold)
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadState, Cycle);
		CycleDiff = 0;
	}

	uint8* BufferPtr = ThreadState->Buffer + ThreadState->BufferSize;
	FTraceUtils::Encode7bit((CycleDiff << 1) | 1ull, BufferPtr);
	FCpuProfilerTraceInternal::EncodeSpecId(SpecId, BufferPtr);
	ThreadState->BufferSize = BufferPtr - ThreadState->Buffer;
}

void FCpuProfilerTrace::OutputBeginDynamicEvent(const TCHAR* Name)
{
	uint64 Cycle = FPlatformTime::Cycles64();
	FCpuProfilerTraceInternal::FThreadState* ThreadState = (FCpuProfilerTraceInternal::FThreadState*)FPlatformTLS::GetTlsValue(FCpuProfilerTraceInternal::TlsSlot);
	if (!ThreadState)
	{
		ThreadState = FCpuProfilerTraceInternal::InitThreadState(Cycle);
	}
	uint64 CycleDiff = Cycle - ThreadState->LastCycle;
	ThreadState->LastCycle = Cycle;
	if (ThreadState->BufferSize >= FCpuProfilerTraceInternal::DynamicEventFullBufferThreshold)
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadState, Cycle);
		CycleDiff = 0;
	}

	uint8* BufferPtr = ThreadState->Buffer + ThreadState->BufferSize;
	FTraceUtils::Encode7bit((CycleDiff << 1) | 1ull, BufferPtr);
	FCpuProfilerTraceInternal::EncodeSpecId(0, BufferPtr);

	uint32 NameHash = FCrc::StrCrc32(Name);
	uint16* NameId = ThreadState->KnownDynamicStrings.Find(NameHash);
	if (!NameId)
	{
		uint16 NewNameId = FCpuProfilerTraceInternal::NewDynamicEventName(Name);
		FCpuProfilerTraceInternal::EncodeSpecId(NewNameId, BufferPtr);
		ThreadState->KnownDynamicStrings.Add(NameHash, NewNameId);
	}
	else
	{
		FCpuProfilerTraceInternal::EncodeSpecId(*NameId, BufferPtr);
	}
	ThreadState->BufferSize = BufferPtr - ThreadState->Buffer;
}

void FCpuProfilerTrace::OutputEndEvent()
{
	uint64 Cycle = FPlatformTime::Cycles64();
	FCpuProfilerTraceInternal::FThreadState* ThreadState = (FCpuProfilerTraceInternal::FThreadState*)FPlatformTLS::GetTlsValue(FCpuProfilerTraceInternal::TlsSlot);
	uint64 CycleDiff = Cycle - ThreadState->LastCycle;
	ThreadState->LastCycle = Cycle;
	if (ThreadState->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold)
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadState, Cycle);
		CycleDiff = 0;
	}
	uint8* BufferPtr = ThreadState->Buffer + ThreadState->BufferSize;
	FTraceUtils::Encode7bit(CycleDiff << 1, BufferPtr);
	ThreadState->BufferSize = BufferPtr - ThreadState->Buffer;
}

uint64 FCpuProfilerTrace::GetCurrentCycle()
{
	return FPlatformTime::Cycles64();
}

uint16 FCpuProfilerEventSpec::AssignId(const TCHAR* Name, ECpuProfilerGroup Group)
{
	uint32 WideId = FCpuProfilerTraceInternal::Instance().NextSpecId++;
	check(WideId <= 0xFFFF);
	uint16 Id = uint16(WideId);
	uint16 NameLength = FCString::Strlen(Name);

	auto NameCopy = [NameLength, Name](uint8* Out) {
		int32 i = 0;
		for (; i < NameLength; ++i)
		{
			Out[i] = Name[i] & 0x7f;
		}
		Out[i] = 0;
	};

	UE_TRACE_LOG(CpuProfiler, EventSpec, NameLength + 1)
		<< EventSpec.Id(Id)
		<< EventSpec.Group(uint16(Group))
		<< EventSpec.NameSize(NameLength)
		<< EventSpec.Attachment(NameCopy);
	return Id;
}


#endif