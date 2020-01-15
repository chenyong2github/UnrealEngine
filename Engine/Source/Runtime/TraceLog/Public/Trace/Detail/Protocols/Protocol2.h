// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace
{

#if defined(TRACE_PRIVATE_PROTOCOL_2)
inline
#endif
namespace Protocol2
{

////////////////////////////////////////////////////////////////////////////////
enum EProtocol : uint8 { Id = 2 };

////////////////////////////////////////////////////////////////////////////////
using Protocol1::EFieldType;
using Protocol1::FNewEventEvent;
using Protocol1::EEventFlags;
using Protocol1::EKnownEventUids;
using Protocol1::FAuxHeader;

////////////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
struct FEventHeader
{
	uint16		Uid;
	uint16		Size;
	uint16		SerialLow;		// 24-bit...
	uint8		SerialHigh;		// ...serial no.
	uint8		EventData[];
};
#pragma pack(pop)
static_assert(sizeof(FEventHeader) == 7, "Packing assumption doesn't hold");

} // namespace Protocol1
} // namespace Trace
