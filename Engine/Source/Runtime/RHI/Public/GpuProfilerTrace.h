// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Trace.h"

#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define GPUPROFILERTRACE_ENABLED 0
#else
#define GPUPROFILERTRACE_ENABLED 0
#endif

#if GPUPROFILERTRACE_ENABLED

struct FGpuProfilerTrace
{
	struct FEventType
	{
		RHI_API FEventType(const TCHAR* Name);
	};

	RHI_API static void BeginFrame();
	RHI_API static void BeginEvent(const FEventType* EventType, uint32 FrameNumber, uint64 TimestampMicroseconds);
	RHI_API static void EndEvent(uint64 TimestampMicroseconds);
	RHI_API static void EndFrame();

private:
	enum
	{
		MaxEventBufferSize = 32768
	};

	struct FFrame
	{
		uint64 TimestampBase;
		uint64 LastTimestamp;
		uint32 RenderingFrameNumber;
		uint16 EventBufferSize;
		uint8 EventBuffer[MaxEventBufferSize];
	};
	static FFrame CurrentFrame;
};

#define TRACE_GPUPROFILER_DEFINE_EVENT_TYPE(Name) \
	FGpuProfilerTrace::FEventType PREPROCESSOR_JOIN(__GGpuProfilerEventType, Name)(TEXT(#Name));

#define TRACE_GPUPROFILER_DECLARE_EVENT_TYPE_EXTERN(Name) \
	extern FGpuProfilerTrace::FEventType PREPROCESSOR_JOIN(__GGpuProfilerEventType, Name);

#define TRACE_GPUPROFILER_EVENT_TYPE(Name) \
	&PREPROCESSOR_JOIN(__GGpuProfilerEventType, Name)

#define TRACE_GPUPROFILER_BEGIN_FRAME() \
	FGpuProfilerTrace::BeginFrame();

#define TRACE_GPUPROFILER_BEGIN_EVENT(EventType, FrameNumber, TimestampMicroseconds) \
	FGpuProfilerTrace::BeginEvent(EventType, FrameNumber, TimestampMicroseconds);

#define TRACE_GPUPROFILER_END_EVENT(TimestampMicroseconds) \
	FGpuProfilerTrace::EndEvent(TimestampMicroseconds);

#define TRACE_GPUPROFILER_END_FRAME() \
	FGpuProfilerTrace::EndFrame();

#else

struct FGpuProfilerTrace
{
	struct FEventType
	{
		FEventType(const TCHAR* Name) {};
	};
};

#define TRACE_GPUPROFILER_DEFINE_EVENT_TYPE(...)
#define TRACE_GPUPROFILER_DECLARE_EVENT_TYPE_EXTERN(...)
#define TRACE_GPUPROFILER_EVENT_TYPE(...) nullptr
#define TRACE_GPUPROFILER_BEGIN_FRAME(...)
#define TRACE_GPUPROFILER_BEGIN_EVENT(...)
#define TRACE_GPUPROFILER_END_EVENT(...)
#define TRACE_GPUPROFILER_END_FRAME(...)

#endif
