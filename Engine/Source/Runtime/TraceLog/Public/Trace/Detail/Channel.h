// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "CoreTypes.h"

namespace Trace {

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
class FChannel 
{
public:
	struct Iter
	{
						~Iter();
		const FChannel*	GetNext();
		void*			Inner[3];
	};

	TRACELOG_API void	Initialize(const ANSICHAR* InChannelName);
	static Iter			ReadNew();
	void				Announce() const;
	static bool			Toggle(const ANSICHAR* ChannelName, bool bEnabled);
	static void			ToggleAll(bool bEnabled);
	static FChannel*	FindChannel(const ANSICHAR* ChannelName);
	bool				Toggle(bool bEnabled);
	bool				IsEnabled() const;
	explicit			operator bool () const;
	bool				operator | (const FChannel& Rhs) const;

private:
	FChannel*			Next;
	struct
	{
		const ANSICHAR*	Ptr;
		uint32			Len;
		uint32			Hash;
	}					Name;
	volatile int32		Enabled;
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
