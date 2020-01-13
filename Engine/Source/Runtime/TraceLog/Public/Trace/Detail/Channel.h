// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

namespace Trace
{

/*
	A named channel which can be used to filter trace events. Channels can be 
	combined using the '|' operator which allows expressions like

	```
	UE_TRACE_LOG(FooWriter, FooEvent, FooChannel|BarChannel);
	```

	Channels are by default enabled until this method is called. This is to allow
	events to be emitted during static initialization. In fact all events during
	this phase are always emitted. In this method we disable all channels except
	those specified on the command line using -tracechannels argument.
*/
struct FChannel 
{
	TRACELOG_API static void Register(FChannel& Channel, const ANSICHAR* ChannelName);
	TRACELOG_API static bool Toggle(FChannel* Channel, bool bEnabled);
	TRACELOG_API static bool Toggle(const ANSICHAR* ChannelName, bool bEnabled);
	TRACELOG_API static bool Toggle(const TCHAR* ChannelName, bool bEnabled);
	TRACELOG_API static void ToggleAll(bool bEnabled);
	bool IsEnabled() const;
	explicit operator bool() const;
	bool operator|(const FChannel& Rhs) const;

	void*			Handle;
	uint32			ChannelNameHash;
	bool			bDisabled;
};

}

#endif //UE_TRACE_ENABLED