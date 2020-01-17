// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Config.h"
#include "Trace/Detail/Channel.h"
#include "Trace/Detail/Channel.inl"

#if !defined(CPUPROFILERTRACE_ENABLED)
#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define CPUPROFILERTRACE_ENABLED 1
#else
#define CPUPROFILERTRACE_ENABLED 0
#endif
#endif

#if CPUPROFILERTRACE_ENABLED

// @note Cannot use the declare macros in this header since including
// Trace.h will result in a circular dependency.
CORE_API extern Trace::FChannel CpuProfilerChannel;
CORE_API extern Trace::FChannel NamedEventsChannel;

struct FCpuProfilerTrace
{
	CORE_API static void Init(const TCHAR* CmdLine);
	CORE_API static void Shutdown();
	FORCENOINLINE CORE_API static uint32 OutputEventType(const ANSICHAR* Name);
	FORCENOINLINE CORE_API static uint32 OutputEventType(const TCHAR* Name);
	CORE_API static void OutputBeginEvent(uint32 SpecId, const Trace::FChannel& Channel);
	CORE_API static void OutputBeginDynamicEvent(const ANSICHAR* Name, const Trace::FChannel& Channel);
	CORE_API static void OutputBeginDynamicEvent(const TCHAR* Name, const Trace::FChannel& Channel);
	CORE_API static void OutputEndEvent(const Trace::FChannel& Channel);

	struct FEventScope
	{
		FEventScope(uint32 InSpecId, const Trace::FChannel& Channel)
			: Channel(Channel)
		{
			OutputBeginEvent(InSpecId, Channel); 
		}

		~FEventScope()
		{
			OutputEndEvent(Channel);
		}

		const Trace::FChannel& Channel;
	};

	struct FDynamicEventScope
	{
		FDynamicEventScope(const ANSICHAR* InEventName, const Trace::FChannel& Channel)
			: Channel(Channel)
		{
			OutputBeginDynamicEvent(InEventName, Channel);
		}

		FDynamicEventScope(const TCHAR* InEventName, const Trace::FChannel& Channel)
			: Channel(Channel)
		{
			OutputBeginDynamicEvent(InEventName, Channel);
		}

		~FDynamicEventScope()
		{
			OutputEndEvent(Channel);
		}

		const Trace::FChannel& Channel;
	};
};

#define TRACE_CPUPROFILER_INIT(CmdLine) \
	FCpuProfilerTrace::Init(CmdLine);

#define TRACE_CPUPROFILER_SHUTDOWN() \
	FCpuProfilerTrace::Shutdown();

#define TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(NameStr, Channel) \
	static uint16 PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__); \
	if (bool(Channel) && PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) == 0) { \
		PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) = FCpuProfilerTrace::OutputEventType(NameStr); \
	} \
	FCpuProfilerTrace::FEventScope PREPROCESSOR_JOIN(__CpuProfilerEventScope, __LINE__)(PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__), Channel);

#define TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(Name, Channel) \
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(#Name, Channel)

#define TRACE_CPUPROFILER_EVENT_SCOPE(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(Name, CpuProfilerChannel)

#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(Name, Channel) \
	FCpuProfilerTrace::FDynamicEventScope PREPROCESSOR_JOIN(__CpuProfilerEventScope, __LINE__)(Name, Channel);
	
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(Name, CpuProfilerChannel)

#else

#define TRACE_CPUPROFILER_INIT(CmdLine)
#define TRACE_CPUPROFILER_SHUTDOWN()
#define TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(NameStr, Channel)
#define TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(Name, Channel)
#define TRACE_CPUPROFILER_EVENT_SCOPE(Name)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(Name, Channel)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name)

#endif
