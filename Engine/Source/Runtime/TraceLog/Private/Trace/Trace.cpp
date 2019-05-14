//
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
//

#include "Trace/Trace.h"
#include "Trace/Private/Event.h"

#if UE_TRACE_ENABLED

namespace Trace
{

namespace Private
{

////////////////////////////////////////////////////////////////////////////////
extern UPTRINT	GPendingDataHandle;
bool			Writer_ConnectImpl(const ANSICHAR*);
bool			Writer_ToggleEventImpl(const ANSICHAR*, const ANSICHAR*, bool);

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
template <int DestSize>
static void ToAnsiCheap(ANSICHAR (&Dest)[DestSize], const WIDECHAR* Src)
{
	for (ANSICHAR& Out : Dest)
	{
		Out = ANSICHAR(*Src++ & 0x7f);
		if (Out == '\0')
		{
			break;
		}
	}
};

////////////////////////////////////////////////////////////////////////////////
bool Connect(const TCHAR* InHost)
{
	char Host[32];
	ToAnsiCheap(Host, InHost);

	return Private::Writer_ConnectImpl(Host);
}

////////////////////////////////////////////////////////////////////////////////
bool ToggleEvent(const TCHAR* LoggerName, const TCHAR* EventName, bool State)
{
	ANSICHAR LoggerNameA[64];
	ANSICHAR EventNameA[64];
	ToAnsiCheap(LoggerNameA, LoggerName);
	ToAnsiCheap(EventNameA, EventName);

	return Private::Writer_ToggleEventImpl(LoggerNameA, EventNameA, State);
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
