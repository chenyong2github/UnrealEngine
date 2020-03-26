// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace
{

#if defined(TRACE_PRIVATE_PROTOCOL_4)
inline
#endif
namespace Protocol4
{

////////////////////////////////////////////////////////////////////////////////
enum EProtocol : uint8 { Id = 4 };

////////////////////////////////////////////////////////////////////////////////
using Protocol3::EFieldType;
using Protocol3::FNewEventEvent;
using Protocol3::EEventFlags;
using Protocol3::FAuxHeader;
using Protocol3::FEventHeader;
using Protocol3::FEventHeaderSync;
using Protocol3::EKnownEventUids;

} // namespace Protocol4
} // namespace Trace
