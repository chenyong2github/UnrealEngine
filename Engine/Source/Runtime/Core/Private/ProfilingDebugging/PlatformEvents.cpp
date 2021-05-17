// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/PlatformEvents.h"
#include "Containers/UnrealString.h"

/////////////////////////////////////////////////////////////////////

UE_TRACE_CHANNEL_DEFINE(ContextSwitchChannel)
UE_TRACE_CHANNEL_DEFINE(StackSamplingChannel)

/////////////////////////////////////////////////////////////////////

EPlatformEvent PlatformEvents_GetEvent(const FString& Name)
{
	if (Name == TEXT("contextswitch"))
	{
		return EPlatformEvent::ContextSwitch;
	}
	else if (Name == TEXT("stacksampling"))
	{
		return EPlatformEvent::StackSampling;
	}
	else
	{
		return EPlatformEvent::None;
	}
}

/////////////////////////////////////////////////////////////////////

#if !UE_PLATFORM_EVENTS_AVAILABLE

void PlatformEvents_Init(uint32 SamplingIntervalUsec)
{
}

void PlatformEvents_Enable(EPlatformEvent Event)
{
}

void PlatformEvents_Disable(EPlatformEvent Event)
{
}

void PlatformEvents_Stop()
{
}

#endif
