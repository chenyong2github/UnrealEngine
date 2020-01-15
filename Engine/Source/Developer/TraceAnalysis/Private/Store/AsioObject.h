// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioObject
{
public:
						FAsioObject(asio::io_context& InIoContext);
	asio::io_context&	GetIoContext();

private:
	asio::io_context&	IoContext;
};

} // namespace Trace

#endif // TRACE_WITH_ASIO
