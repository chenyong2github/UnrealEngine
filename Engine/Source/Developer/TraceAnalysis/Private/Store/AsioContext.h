// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include <thread>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioContext
{
public:
							FAsioContext(int32 ThreadCount);
							~FAsioContext();
	asio::io_context&		Get();
	void					Start();

private:
	asio::io_context*		IoContext;
	TArray<std::thread>		ThreadPool;
};

} // namespace Trace

#endif // TRACE_WITH_ASIO
