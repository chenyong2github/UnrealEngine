// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioObject.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioObject::FAsioObject(asio::io_context& InIoContext)
: IoContext(InIoContext)
{
}

////////////////////////////////////////////////////////////////////////////////
asio::io_context& FAsioObject::GetIoContext()
{
	return IoContext;
}

} // namespace Trace
