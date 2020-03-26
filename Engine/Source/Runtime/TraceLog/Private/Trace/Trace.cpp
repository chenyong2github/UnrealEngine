// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Trace.h" // should be Config.h :(

#if UE_TRACE_ENABLED

#include "Trace/Detail/Channel.h"

namespace Trace
{

namespace Private
{

////////////////////////////////////////////////////////////////////////////////
void	Writer_Initialize(const FInitializeDesc&);
void	Writer_Update();
bool	Writer_SendTo(const ANSICHAR*, uint32);
bool	Writer_WriteTo(const ANSICHAR*);

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
template <int DestSize>
static uint32 ToAnsiCheap(ANSICHAR (&Dest)[DestSize], const WIDECHAR* Src)
{
	const WIDECHAR* Cursor = Src;
	for (ANSICHAR& Out : Dest)
	{
		Out = ANSICHAR(*Cursor++ & 0x7f);
		if (Out == '\0')
		{
			break;
		}
	}
	Dest[DestSize - 1] = '\0';
	return uint32(UPTRINT(Cursor - Src));
};

////////////////////////////////////////////////////////////////////////////////
void Initialize(const FInitializeDesc& Desc)
{
	Private::Writer_Initialize(Desc);
	FChannel::ToggleAll(false);
}

////////////////////////////////////////////////////////////////////////////////
void Update()
{
	Private::Writer_Update();
}

////////////////////////////////////////////////////////////////////////////////
bool SendTo(const TCHAR* InHost, uint32 Port)
{
	char Host[32];
	ToAnsiCheap(Host, InHost);
	return Private::Writer_SendTo(Host, Port);
}

////////////////////////////////////////////////////////////////////////////////
bool WriteTo(const TCHAR* InPath)
{
	char Path[512];
	ToAnsiCheap(Path, InPath);
	return Private::Writer_WriteTo(Path);
}

////////////////////////////////////////////////////////////////////////////////
bool ToggleChannel(const TCHAR* ChannelName, bool bEnabled)
{
	ANSICHAR ChannelNameA[64];
	ToAnsiCheap(ChannelNameA, ChannelName);
	return FChannel::Toggle(ChannelNameA, bEnabled);
}

} // namespace Trace

#else

// Workaround for module not having any exported symbols
TRACELOG_API int TraceLogExportedSymbol = 0;

#endif // UE_TRACE_ENABLED
