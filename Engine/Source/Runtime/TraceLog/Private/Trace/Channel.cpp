// Copyright Epic Games, Inc. All Rights Reserved.
#include "Trace/Detail/Channel.h"
#include "Trace/Trace.h"
#include "Trace/Detail/Atomic.h"
#include "Trace/Detail/Channel.h"
#include "Misc/Parse.h"

#if UE_TRACE_ENABLED

namespace Trace
{

///////////////////////////////////////////////////////////////////////////////
static FChannel* volatile GHeadChannel; // = nullptr;
static const size_t ChannelNameMaxLength = 64u;

///////////////////////////////////////////////////////////////////////////////
template <int DestSize>
static void ChannelToAnsiCheap(ANSICHAR(&Dest)[DestSize], const WIDECHAR* Src)
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
template <typename ElementType>
static uint32 ChannelGetHash(const ElementType* Input, int32 Length = -1)
{
	uint32 Result = 0x811c9dc5;
	for (; *Input && Length; ++Input, --Length)
	{
		Result ^= *Input;
		Result *= 0x01000193;
	}
	return Result;
}

////////////////////////////////////////////////////////////////////////////////
static uint32 ChannelGetHash(uint32 LoggerHash, uint32 NameHash)
{
	uint32 Parts[3] = { LoggerHash, NameHash, 0 };
	return ChannelGetHash(Parts);
}

///////////////////////////////////////////////////////////////////////////////
static uint32 GetChannelIdentifier(ANSICHAR(&Buffer) [ChannelNameMaxLength], const ANSICHAR* ChannelName)
{
	// Strip "Channel" suffix if it exists
	if (const ANSICHAR* ChannelStr = strstr(ChannelName, "Channel"))
	{
		const size_t Count = ChannelStr - ChannelName;
		if (Count > 0 && strlen(ChannelName) > Count)
		{
			memcpy(&Buffer, ChannelName, Count);
			Buffer[Count] = '\0';
		}
	}
	else
	{
		const size_t Count = strlen(ChannelName);
		memcpy(&Buffer, ChannelName, Count);
		Buffer[Count] = '\0';
	}

	// Convert to lower case
	const uint32 ChannelNameLen = strlen(Buffer);
	for (size_t i = 0; i < ChannelNameLen; ++i)
	{
		Buffer[i] = tolower(Buffer[i]);
	}

	return ChannelNameLen;
}

///////////////////////////////////////////////////////////////////////////////
void FChannel::Register(FChannel& Channel, const ANSICHAR* ChannelName)
{
	UE_TRACE_EVENT_BEGIN($Trace, ChannelAnnounce, Important)
		UE_TRACE_EVENT_FIELD(uint32, Id)
	UE_TRACE_EVENT_END()

	
	ANSICHAR ChannelIdentifier[ChannelNameMaxLength];
	const uint32 ChannelNameLen = GetChannelIdentifier(ChannelIdentifier, ChannelName);
	const uint32 ChannelNameHash = ChannelGetHash(ChannelIdentifier);

	Channel.ChannelNameHash = ChannelNameHash;

	// Append channel to linked list
	for (;; Private::PlatformYield())
	{
		FChannel* HeadChannel = Private::AtomicLoadRelaxed(&GHeadChannel);
		Channel.Handle = HeadChannel;
		if (Private::AtomicCompareExchangeRelease(&GHeadChannel, &Channel, HeadChannel))
		{
			break;
		}
	}

	UE_TRACE_LOG($Trace, ChannelAnnounce, TraceLogChannel, ChannelNameLen+1)
		<< ChannelAnnounce.Id(ChannelNameHash)
		<< ChannelAnnounce.Attachment(ChannelIdentifier, ChannelNameLen+1);
}

///////////////////////////////////////////////////////////////////////////////
 void FChannel::ToggleAll(bool bEnabled)
{
	FChannel* Channel = Private::AtomicLoadAcquire(&GHeadChannel);
	for (; Channel != nullptr; Channel = (FChannel*)(Channel->Handle))
	{
		FChannel::Toggle(Channel, bEnabled);
	}
}

 ///////////////////////////////////////////////////////////////////////////////
 bool FChannel::Toggle(FChannel* Channel, bool bEnabled)
 {
	UE_TRACE_EVENT_BEGIN($Trace, ChannelToggle, Important)
		UE_TRACE_EVENT_FIELD(uint32, Id)
		UE_TRACE_EVENT_FIELD(bool, IsEnabled)
	UE_TRACE_EVENT_END()

	const bool bWasEnabled = !Channel->bDisabled;
	if (bWasEnabled != bEnabled)
	{
		Channel->bDisabled = !bEnabled;
		UE_TRACE_LOG($Trace, ChannelToggle, TraceLogChannel)
			<< ChannelToggle.Id(Channel->ChannelNameHash)
			<< ChannelToggle.IsEnabled(bEnabled);
	}
	return bWasEnabled;
 }

 ///////////////////////////////////////////////////////////////////////////////
 bool FChannel::Toggle(const ANSICHAR* ChannelName, bool bEnabled)
 {
	ANSICHAR ChannelIdentifier[ChannelNameMaxLength];
	const uint32 ChannelNameLen = GetChannelIdentifier(ChannelIdentifier, ChannelName);
	const uint32 ChannelNameHash = ChannelGetHash(ChannelIdentifier);

	FChannel* Channel = Private::AtomicLoadAcquire(&GHeadChannel);
	for (; Channel != nullptr; Channel = (FChannel*)(Channel->Handle))
	{
		if (Channel->ChannelNameHash == ChannelNameHash)
		{
			return FChannel::Toggle(Channel, bEnabled);
		}
	}

	return false;
 }

 ///////////////////////////////////////////////////////////////////////////////
 bool FChannel::Toggle(const TCHAR* ChannelName, bool bEnabled)
 {
	 ANSICHAR ChannelNameA[ChannelNameMaxLength];
	 ChannelToAnsiCheap(ChannelNameA, ChannelName);

	 return FChannel::Toggle(ChannelNameA, bEnabled);
 }

}

#endif //UE_TRACE_ENABLED