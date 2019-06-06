// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "GpuProfilerTrace.h"
#include "Trace/Trace.h"
#include "Misc/CString.h"
#include "GPUProfiler.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "HAL/PlatformMisc.h"

#if GPUPROFILERTRACE_ENABLED

UE_TRACE_EVENT_BEGIN(GpuProfiler, EventSpec, Always)
	UE_TRACE_EVENT_FIELD(const void*, EventType)
	UE_TRACE_EVENT_FIELD(uint16, NameLength)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(GpuProfiler, Frame)
	UE_TRACE_EVENT_FIELD(uint64, TimestampBase)
	UE_TRACE_EVENT_FIELD(uint32, RenderingFrameNumber)
UE_TRACE_EVENT_END()

FGpuProfilerTrace::FFrame FGpuProfilerTrace::CurrentFrame;

FGpuProfilerTrace::FEventType::FEventType(const TCHAR* Name)
{
	uint16 NameLength = FCString::Strlen(Name);
	uint16 NameSize = (NameLength + 1) * sizeof(TCHAR);
	UE_TRACE_LOG(GpuProfiler, EventSpec, NameSize) 
		<< EventSpec.EventType(this)
		<< EventSpec.NameLength(NameLength)
		<< EventSpec.Attachment(Name, NameSize);
}

void FGpuProfilerTrace::BeginFrame()
{
	CurrentFrame.TimestampBase = 0;
	CurrentFrame.EventBufferSize = 0;
}

void FGpuProfilerTrace::BeginEvent(const FEventType* EventType, uint32 FrameNumber, uint64 TimestampMicroseconds)
{
	if (CurrentFrame.EventBufferSize >= MaxEventBufferSize - 17) // 9 + 8
	{
		return;
	}
	if (CurrentFrame.TimestampBase == 0)
	{
		CurrentFrame.TimestampBase = TimestampMicroseconds;
		CurrentFrame.LastTimestamp = CurrentFrame.TimestampBase;
		CurrentFrame.RenderingFrameNumber = FrameNumber;
	}
	uint8* BufferPtr = CurrentFrame.EventBuffer + CurrentFrame.EventBufferSize;
	uint64 TimestampDelta = TimestampMicroseconds - CurrentFrame.LastTimestamp;
	CurrentFrame.LastTimestamp = TimestampMicroseconds;
	FTraceUtils::Encode7bit((TimestampDelta << 1ull) | 0x1, BufferPtr);
	*reinterpret_cast<uint64*>(BufferPtr) = uint64(EventType);
	CurrentFrame.EventBufferSize = BufferPtr - CurrentFrame.EventBuffer + sizeof(uint64);
}

void FGpuProfilerTrace::EndEvent(uint64 TimestampMicroseconds)
{
	uint64 TimestampDelta = TimestampMicroseconds - CurrentFrame.LastTimestamp;
	CurrentFrame.LastTimestamp = TimestampMicroseconds;
	uint8* BufferPtr = CurrentFrame.EventBuffer + CurrentFrame.EventBufferSize;
	FTraceUtils::Encode7bit(TimestampDelta << 1ull, BufferPtr);
	CurrentFrame.EventBufferSize = BufferPtr - CurrentFrame.EventBuffer;
}

void FGpuProfilerTrace::EndFrame()
{
	if (CurrentFrame.EventBufferSize)
	{
		UE_TRACE_LOG(GpuProfiler, Frame, CurrentFrame.EventBufferSize)
			<< Frame.TimestampBase(CurrentFrame.TimestampBase)
			<< Frame.RenderingFrameNumber(CurrentFrame.RenderingFrameNumber)
			<< Frame.Attachment(CurrentFrame.EventBuffer, CurrentFrame.EventBufferSize);
	}
}

#endif
