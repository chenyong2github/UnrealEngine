// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/Trace.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "ProfilingDebugging/MiscTrace.h"

#if CPUPROFILERTRACE_ENABLED

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventSpec, Always)
	UE_TRACE_EVENT_FIELD(uint16, Id)
	UE_TRACE_EVENT_FIELD(uint16, Group)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventBatch)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EndCapture, Always)
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
		uint64 LastCycle = 0;
		uint32 ThreadId;
		uint16 Depth = 0;
		uint16 BufferSize = 0;
		bool bEnabled = false;
		uint8 Buffer[MaxBufferSize];
	};

	FORCENOINLINE static FThreadState* InitThreadState();
	FORCENOINLINE static void FlushThreadBuffer(FThreadState* ThreadState);
	FORCENOINLINE static void EndCapture(FThreadState* ThreadState);

	static thread_local FThreadState* ThreadLocalThreadState;
};

thread_local FCpuProfilerTraceInternal::FThreadState* FCpuProfilerTraceInternal::ThreadLocalThreadState = nullptr;

FCpuProfilerTraceInternal::FThreadState* FCpuProfilerTraceInternal::InitThreadState()
{
	ThreadLocalThreadState = new FThreadState();
	ThreadLocalThreadState->ThreadId = FPlatformTLS::GetCurrentThreadId();
	return ThreadLocalThreadState;
}

void FCpuProfilerTraceInternal::FlushThreadBuffer(FThreadState* ThreadState)
{
	UE_TRACE_LOG(CpuProfiler, EventBatch, ThreadState->BufferSize)
		<< EventBatch.ThreadId(ThreadState->ThreadId)
		<< EventBatch.Attachment(ThreadState->Buffer, ThreadState->BufferSize);
	ThreadState->BufferSize = 0;
}

void FCpuProfilerTraceInternal::EndCapture(FThreadState* ThreadState)
{
	UE_TRACE_LOG(CpuProfiler, EndCapture, ThreadState->BufferSize)
		<< EndCapture.ThreadId(ThreadState->ThreadId)
		<< EndCapture.Attachment(ThreadState->Buffer, ThreadState->BufferSize);
	ThreadState->BufferSize = 0;
}

void FCpuProfilerTrace::OutputBeginEvent(uint16 SpecId)
{
	FCpuProfilerTraceInternal::FThreadState* ThreadState = FCpuProfilerTraceInternal::ThreadLocalThreadState;
	if (!ThreadState)
	{
		ThreadState = FCpuProfilerTraceInternal::InitThreadState();
	}
	++ThreadState->Depth;
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(CpuProfiler, EventBatch);
	if ((!bEventEnabled) & (bEventEnabled == ThreadState->bEnabled))
	{
		return;
	}
	ThreadState->bEnabled = bEventEnabled;
	if (!bEventEnabled)
	{
		FCpuProfilerTraceInternal::EndCapture(ThreadState);
		return;
	}
	if (ThreadState->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold)
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadState);
	}
	uint64 Cycle = FPlatformTime::Cycles64();
	uint64 CycleDiff = Cycle - ThreadState->LastCycle;
	ThreadState->LastCycle = Cycle;
	uint8* BufferPtr = ThreadState->Buffer + ThreadState->BufferSize;
	FTraceUtils::Encode7bit((CycleDiff << 1) | 1ull, BufferPtr);
	FTraceUtils::Encode7bit(SpecId, BufferPtr);
	ThreadState->BufferSize = BufferPtr - ThreadState->Buffer;
}

void FCpuProfilerTrace::OutputEndEvent()
{
	FCpuProfilerTraceInternal::FThreadState* ThreadState = FCpuProfilerTraceInternal::ThreadLocalThreadState;
	--ThreadState->Depth;
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(CpuProfiler, EventBatch);
	if ((!bEventEnabled) & (bEventEnabled == ThreadState->bEnabled))
	{
		return;
	}
	ThreadState->bEnabled = bEventEnabled;
	if (!bEventEnabled)
	{
		FCpuProfilerTraceInternal::EndCapture(ThreadState);
		return;
	}
	if ((ThreadState->Depth == 0) | (ThreadState->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold))
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadState);
	}
	uint64 Cycle = FPlatformTime::Cycles64();
	uint64 CycleDiff = Cycle - ThreadState->LastCycle;
	ThreadState->LastCycle = Cycle;
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

void FCpuProfilerTrace::Init(bool bStartEnabled)
{
	if (bStartEnabled)
	{
		UE_TRACE_EVENT_IS_ENABLED(CpuProfiler, EventBatch);
		Trace::ToggleEvent(TEXT("CpuProfiler.EventBatch"), true);
	}
}

#endif