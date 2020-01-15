// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioObject.h"

#if TRACE_WITH_ASIO

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

#endif // TRACE_WITH_ASIO
