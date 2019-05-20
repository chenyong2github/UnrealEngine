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
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventBatch)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

struct FCpuProfilerTraceInternal
{
public:
	enum
	{
		MaxBufferSize = 255,
		MaxEncodedEventSize = 12, // 9 + 3
		FullBufferThreshold = MaxBufferSize - MaxEncodedEventSize,
	};

	struct FThreadState
	{
		uint64 CycleBase;
		uint64 LastCycle;
		uint32 ThreadId;
		uint32 BufferSize;
		uint8 Buffer[MaxBufferSize];
	};

	static FThreadState* GetThreadState() { return ThreadLocalThreadState; }
	FORCENOINLINE static FThreadState* InitThreadState();
	FORCENOINLINE static void FlushThreadBuffer(FThreadState* ThreadState);

private:
	static thread_local FThreadState* ThreadLocalThreadState;
};

thread_local FCpuProfilerTraceInternal::FThreadState* FCpuProfilerTraceInternal::ThreadLocalThreadState = nullptr;

FCpuProfilerTraceInternal::FThreadState* FCpuProfilerTraceInternal::InitThreadState()
{
	ThreadLocalThreadState = new FThreadState();
	ThreadLocalThreadState->BufferSize = 0;
	ThreadLocalThreadState->ThreadId = FPlatformTLS::GetCurrentThreadId();
	ThreadLocalThreadState->LastCycle = 0;
	return ThreadLocalThreadState;
}

void FCpuProfilerTraceInternal::FlushThreadBuffer(FThreadState* ThreadState)
{
	UE_TRACE_LOG(CpuProfiler, EventBatch, ThreadState->BufferSize)
		<< EventBatch.ThreadId(ThreadState->ThreadId)
		<< EventBatch.Attachment(ThreadState->Buffer, ThreadState->BufferSize);
	ThreadState->BufferSize = 0;
}

void FCpuProfilerTrace::OutputBeginEvent(uint16 SpecId)
{
	uint64 Cycle = FPlatformTime::Cycles64();
	FCpuProfilerTraceInternal::FThreadState* ThreadState = FCpuProfilerTraceInternal::GetThreadState();
	if (!ThreadState)
	{
		ThreadState = FCpuProfilerTraceInternal::InitThreadState();
	}
	uint64 CycleDiff = Cycle - ThreadState->LastCycle;
	ThreadState->LastCycle = Cycle;
	if (ThreadState->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold)
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadState);
	}
	uint8* BufferPtr = ThreadState->Buffer + ThreadState->BufferSize;
	FTraceUtils::Encode7bit((CycleDiff << 1) | 1ull, BufferPtr);
	FTraceUtils::Encode7bit(SpecId, BufferPtr);
	ThreadState->BufferSize = BufferPtr - ThreadState->Buffer;
}

void FCpuProfilerTrace::OutputEndEvent()
{
	uint64 Cycle = FPlatformTime::Cycles64();
	FCpuProfilerTraceInternal::FThreadState* ThreadState = FCpuProfilerTraceInternal::GetThreadState();
	uint64 CycleDiff = Cycle - ThreadState->LastCycle;
	ThreadState->LastCycle = Cycle;
	if (ThreadState->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold)
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadState);
	}
	uint8* BufferPtr = ThreadState->Buffer + ThreadState->BufferSize;
	FTraceUtils::Encode7bit(CycleDiff << 1, BufferPtr);
	ThreadState->BufferSize = BufferPtr - ThreadState->Buffer;
}

uint16 FCpuProfilerTrace::OutputEventType(const TCHAR* Name, ECpuProfilerGroup Group)
{
	static TAtomic<uint32> NextSpecId(1);
	uint32 WideId = NextSpecId++;
	check(WideId <= 0xFFFF);
	uint16 Id = uint16(WideId);
	uint16 NameSize = (FCString::Strlen(Name) + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(CpuProfiler, EventSpec, NameSize)
		<< EventSpec.Id(Id)
		<< EventSpec.Group(uint16(Group))
		<< EventSpec.Attachment(Name, NameSize);
	return Id;
}


#endif