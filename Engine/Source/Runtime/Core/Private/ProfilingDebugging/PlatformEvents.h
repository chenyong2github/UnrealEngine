// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Trace/Trace.inl"

#if PLATFORM_WINDOWS
#	define UE_PLATFORM_EVENTS_AVAILABLE 1
#else
#	define UE_PLATFORM_EVENTS_AVAILABLE 0
#endif

/////////////////////////////////////////////////////////////////////

UE_TRACE_CHANNEL_EXTERN(ContextSwitchChannel)
UE_TRACE_CHANNEL_EXTERN(StackSamplingChannel)

/////////////////////////////////////////////////////////////////////

// represents time interval when thread was running on specific core
UE_TRACE_EVENT_BEGIN(PlatformEvent, ContextSwitch, NoSync)
	UE_TRACE_EVENT_FIELD(uint64, StartTime)
	UE_TRACE_EVENT_FIELD(uint64, EndTime)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(uint8, CoreNumber)
UE_TRACE_EVENT_END()

// represents call stack addresses in stack sampling
UE_TRACE_EVENT_BEGIN(PlatformEvent, StackSample, NoSync)
	UE_TRACE_EVENT_FIELD(uint64, Time)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(uint64[], Addresses)
UE_TRACE_EVENT_END()

/////////////////////////////////////////////////////////////////////

enum class EPlatformEvent
{
	None = 0x00,
	ContextSwitch = 0x01,
	StackSampling = 0x02,
};

ENUM_CLASS_FLAGS(EPlatformEvent)

/////////////////////////////////////////////////////////////////////

EPlatformEvent PlatformEvents_GetEvent(const FString& Name);

void PlatformEvents_Init(uint32 SamplingIntervalUsec);
void PlatformEvents_Enable(EPlatformEvent Event);
void PlatformEvents_Disable(EPlatformEvent Event);
void PlatformEvents_Stop();

/////////////////////////////////////////////////////////////////////
