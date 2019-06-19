// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/Trace.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/TlsAutoCleanup.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Misc/Parse.h"

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

	struct FThreadBuffer
		: public FTlsAutoCleanup
	{
		uint64 LastCycle = 0;
		uint16 BufferSize = 0;
		uint8 Buffer[MaxBufferSize];
	};

	FORCENOINLINE static FThreadBuffer* CreateThreadBuffer();
	FORCENOINLINE static void FlushThreadBuffer(FThreadBuffer* ThreadBuffer);
	FORCENOINLINE static void EndCapture(FThreadBuffer* ThreadBuffer);

	static thread_local uint32 ThreadDepth;
	static thread_local bool bThreadEnabled;
	static thread_local FThreadBuffer* ThreadBuffer;
};

thread_local uint32 FCpuProfilerTraceInternal::ThreadDepth = 0;
thread_local bool FCpuProfilerTraceInternal::bThreadEnabled = false;
thread_local FCpuProfilerTraceInternal::FThreadBuffer* FCpuProfilerTraceInternal::ThreadBuffer = nullptr;

FCpuProfilerTraceInternal::FThreadBuffer* FCpuProfilerTraceInternal::CreateThreadBuffer()
{
	ThreadBuffer = new FThreadBuffer();
	ThreadBuffer->Register();
	return ThreadBuffer;
}

void FCpuProfilerTraceInternal::FlushThreadBuffer(FThreadBuffer* InThreadBuffer)
{
	UE_TRACE_LOG(CpuProfiler, EventBatch, InThreadBuffer->BufferSize)
		<< EventBatch.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< EventBatch.Attachment(InThreadBuffer->Buffer, InThreadBuffer->BufferSize);
	InThreadBuffer->BufferSize = 0;
}

void FCpuProfilerTraceInternal::EndCapture(FThreadBuffer* InThreadBuffer)
{
	UE_TRACE_LOG(CpuProfiler, EndCapture, InThreadBuffer->BufferSize)
		<< EndCapture.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< EndCapture.Attachment(InThreadBuffer->Buffer, InThreadBuffer->BufferSize);
	InThreadBuffer->BufferSize = 0;
}

void FCpuProfilerTrace::OutputBeginEvent(uint16 SpecId)
{
	++FCpuProfilerTraceInternal::ThreadDepth;
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(CpuProfiler, EventBatch);
	if ((!bEventEnabled) & (bEventEnabled == FCpuProfilerTraceInternal::bThreadEnabled))
	{
		return;
	}
	FCpuProfilerTraceInternal::bThreadEnabled = bEventEnabled;
	FCpuProfilerTraceInternal::FThreadBuffer* ThreadBuffer = FCpuProfilerTraceInternal::ThreadBuffer;
	if (!ThreadBuffer)
	{
		ThreadBuffer = FCpuProfilerTraceInternal::CreateThreadBuffer();
	}
	if (!bEventEnabled)
	{
		FCpuProfilerTraceInternal::EndCapture(ThreadBuffer);
		return;
	}
	if (ThreadBuffer->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold)
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadBuffer);
	}
	uint64 Cycle = FPlatformTime::Cycles64();
	uint64 CycleDiff = Cycle - ThreadBuffer->LastCycle;
	ThreadBuffer->LastCycle = Cycle;
	uint8* BufferPtr = ThreadBuffer->Buffer + ThreadBuffer->BufferSize;
	FTraceUtils::Encode7bit((CycleDiff << 1) | 1ull, BufferPtr);
	FTraceUtils::Encode7bit(SpecId, BufferPtr);
	ThreadBuffer->BufferSize = BufferPtr - ThreadBuffer->Buffer;
}

void FCpuProfilerTrace::OutputEndEvent()
{
	--FCpuProfilerTraceInternal::ThreadDepth;
	bool bEventEnabled = UE_TRACE_EVENT_IS_ENABLED(CpuProfiler, EventBatch);
	if ((!bEventEnabled) & (bEventEnabled == FCpuProfilerTraceInternal::bThreadEnabled))
	{
		return;
	}
	FCpuProfilerTraceInternal::bThreadEnabled = bEventEnabled;
	FCpuProfilerTraceInternal::FThreadBuffer* ThreadBuffer = FCpuProfilerTraceInternal::ThreadBuffer;
	if (!ThreadBuffer)
	{
		ThreadBuffer = FCpuProfilerTraceInternal::CreateThreadBuffer();
	}
	if (!bEventEnabled)
	{
		FCpuProfilerTraceInternal::EndCapture(ThreadBuffer);
		return;
	}
	if ((FCpuProfilerTraceInternal::ThreadDepth == 0) | (ThreadBuffer->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold))
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadBuffer);
	}
	uint64 Cycle = FPlatformTime::Cycles64();
	uint64 CycleDiff = Cycle - ThreadBuffer->LastCycle;
	ThreadBuffer->LastCycle = Cycle;
	uint8* BufferPtr = ThreadBuffer->Buffer + ThreadBuffer->BufferSize;
	FTraceUtils::Encode7bit(CycleDiff << 1, BufferPtr);
	ThreadBuffer->BufferSize = BufferPtr - ThreadBuffer->Buffer;
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

void FCpuProfilerTrace::Init(const TCHAR* CmdLine)
{
	if (FParse::Param(CmdLine, TEXT("cpuprofilertrace")))
	{
		UE_TRACE_EVENT_IS_ENABLED(CpuProfiler, EventBatch);
		Trace::ToggleEvent(TEXT("CpuProfiler.EventBatch"), true);
	}
}

#endif