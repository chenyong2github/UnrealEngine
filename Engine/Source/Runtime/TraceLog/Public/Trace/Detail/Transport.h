// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE {
namespace Trace {

////////////////////////////////////////////////////////////////////////////////
enum ETransport : uint8
{
	_Unused		= 0,
	Raw			= 1,
	Packet		= 2,
	TidPacket	= 3,
	Active		= TidPacket,
};

////////////////////////////////////////////////////////////////////////////////
enum ETransportTid : uint32
{
	Internal	= 0,
	Bias		= 1,
};

} // namespace Trace
} // namespace UE
