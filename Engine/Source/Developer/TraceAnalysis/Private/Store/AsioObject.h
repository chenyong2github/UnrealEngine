// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

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
