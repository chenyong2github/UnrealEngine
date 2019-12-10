// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

////////////////////////////////////////////////////////////////////////////////
#if UE_TRACE_ENABLED
#	define TRACE_PRIVATE_PROTOCOL_2
#endif

#if defined(_MSC_VER)
	#pragma warning(push)
	#pragma warning(disable : 4200) // non-standard zero-sized array
#endif

#include "Protocols/Protocol0.h"
#include "Protocols/Protocol1.h"
#include "Protocols/Protocol2.h"

#if defined(_MSC_VER)
	#pragma warning(pop)
#endif

////////////////////////////////////////////////////////////////////////////////
namespace Trace
{

enum ETransport : uint8
{
	_Unused		= 0,
	Raw			= 1,
	Packet		= 2,
	TidPacket	= 3,
};

} // namespace Trace
