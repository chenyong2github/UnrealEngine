// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Detail/Channel.h"
#include "Math/UnrealMathUtility.h"
#include "Trace/Trace.inl"
#include "Trace/Detail/Atomic.h"
#include "Trace/Detail/Channel.h"

#include <ctype.h>

#if UE_TRACE_ENABLED

// General trace channel. Used by all built in events.
Trace::FTraceChannel TraceLogChannel;

namespace Trace {

///////////////////////////////////////////////////////////////////////////////
static FChannel* volatile	GHeadChannel;			// = nullptr;
static FChannel* volatile	GNewChannelList;		// = nullptr;

////////////////////////////////////////////////////////////////////////////////
static uint32 GetChannelHash(const ANSICHAR* Input, int32 Length)
{
	uint32 Result = 0x811c9dc5;
	for (; Length; ++Input, --Length)
	{
		Result ^= *Input | 0x20; // a cheap ASCII-only case insensitivity.
		Result *= 0x01000193;
	}
	return Result;
}

///////////////////////////////////////////////////////////////////////////////
static uint32 GetChannelNameLength(const ANSICHAR* ChannelName)
{
	// Strip "Channel" suffix if it exists
	size_t Len = uint32(strlen(ChannelName));
	if (Len > 7)
	{
		if (strcmp(ChannelName + Len - 7, "Channel") == 0)
		{
			Len -= 7;
		}
	}

	return Len;
}



///////////////////////////////////////////////////////////////////////////////
FChannel::Iter::~Iter()
{
	if (Inner[2] == nullptr)
	{
		return;
	}

	using namespace Private;
	for (auto* Node = (FChannel*)Inner[2];; PlatformYield())
	{
		Node->Next = AtomicLoadRelaxed(&GHeadChannel);
		if (AtomicCompareExchangeRelaxed(&GHeadChannel, (FChannel*)Inner[1], Node->Next))
		{
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
const FChannel* FChannel::Iter::GetNext()
{
	auto* Ret = (const FChannel*)Inner[0];
	if (Ret != nullptr)
	{
		Inner[0] = Ret->Next;
		if (Inner[0] != nullptr)
		{
			Inner[2] = Inner[0];
		}
	}
	return Ret;
}



///////////////////////////////////////////////////////////////////////////////
FChannel::Iter FChannel::ReadNew()
{
	using namespace Private;

	FChannel* List = AtomicLoadRelaxed(&GNewChannelList);
	if (List == nullptr)
	{
		return {};
	}

	while (!AtomicCompareExchangeAcquire(&GNewChannelList, (FChannel*)nullptr, List))
	{
		PlatformYield();
		List = AtomicLoadRelaxed(&GNewChannelList);
	}

	return { { List, List, List } };
}

///////////////////////////////////////////////////////////////////////////////
void FChannel::Initialize(const ANSICHAR* InChannelName)
{
	using namespace Private;

	Name.Ptr = InChannelName;
	Name.Len = GetChannelNameLength(Name.Ptr);
	Name.Hash = GetChannelHash(Name.Ptr, Name.Len);

	// Append channel to the linked list of new channels.
	for (;; PlatformYield())
	{
		FChannel* HeadChannel = AtomicLoadRelaxed(&GNewChannelList);
		Next = HeadChannel;
		if (AtomicCompareExchangeRelease(&GNewChannelList, this, Next))
		{
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
void FChannel::Announce() const
{
	UE_TRACE_EVENT_BEGIN(Trace, ChannelAnnounce, Important)
		UE_TRACE_EVENT_FIELD(uint32, Id)
		UE_TRACE_EVENT_FIELD(bool, IsEnabled)
	UE_TRACE_EVENT_END()

	ANSICHAR Buffer[128];
	uint32 Count = FMath::Min<uint32>(sizeof(Buffer) - 1, Name.Len);
	memcpy(Buffer, Name.Ptr, Count);
	Buffer[Count] = '\0';

	UE_TRACE_LOG(Trace, ChannelAnnounce, TraceLogChannel, Count + 1)
		<< ChannelAnnounce.Id(Name.Hash)
		<< ChannelAnnounce.IsEnabled(!bDisabled)
		<< ChannelAnnounce.Attachment(Buffer, Count + 1);
}

///////////////////////////////////////////////////////////////////////////////
void FChannel::ToggleAll(bool bEnabled)
{
	using namespace Private;

	FChannel* ChannelLists[] =
	{
		AtomicLoadAcquire(&GNewChannelList),
		AtomicLoadAcquire(&GHeadChannel),
	};
	for (FChannel* Channel : ChannelLists)
	{
		for (; Channel != nullptr; Channel = (FChannel*)(Channel->Next))
		{
			Channel->Toggle(bEnabled);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
bool FChannel::Toggle(bool bEnabled)
{
	UE_TRACE_EVENT_BEGIN(Trace, ChannelToggle, Important)
		UE_TRACE_EVENT_FIELD(uint32, Id)
		UE_TRACE_EVENT_FIELD(bool, IsEnabled)
	UE_TRACE_EVENT_END()

	const bool bWasEnabled = !bDisabled;
	if (bWasEnabled != bEnabled)
	{
		bDisabled = !bEnabled;
		UE_TRACE_LOG(Trace, ChannelToggle, TraceLogChannel)
			<< ChannelToggle.Id(Name.Hash)
			<< ChannelToggle.IsEnabled(bEnabled);
	}
	return bWasEnabled;
}

///////////////////////////////////////////////////////////////////////////////
bool FChannel::Toggle(const ANSICHAR* ChannelName, bool bEnabled)
{
	using namespace Private;

	const uint32 ChannelNameLen = GetChannelNameLength(ChannelName);
	const uint32 ChannelNameHash = GetChannelHash(ChannelName, ChannelNameLen);

	FChannel* ChannelLists[] =
	{
		AtomicLoadAcquire(&GNewChannelList),
		AtomicLoadAcquire(&GHeadChannel),
	};
	for (FChannel* Channel : ChannelLists)
	{
		for (; Channel != nullptr; Channel = (FChannel*)(Channel->Next))
		{
			if (Channel->Name.Hash == ChannelNameHash)
			{
				return Channel->Toggle(bEnabled);
			}
		}
	}

	return false;
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
