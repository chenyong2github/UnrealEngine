// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace
{

#if defined(TRACE_PRIVATE_PROTOCOL_1)
inline
#endif
namespace Protocol1
{

////////////////////////////////////////////////////////////////////////////////
enum EProtocol : uint8 { Id = 1 };

////////////////////////////////////////////////////////////////////////////////
using Protocol0::EFieldType;
using Protocol0::FNewEventEvent;

////////////////////////////////////////////////////////////////////////////////
struct FEventHeader
{
	uint16		Uid;
	uint16		Size;
	uint16		Serial;
	uint8		EventData[];
};

} // namespace Protocol1
} // namespace Trace
