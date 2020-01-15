// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioContext.h"

#if TRACE_WITH_ASIO

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioContext::FAsioContext(int32 ThreadCount)
{
	if (ThreadCount < 1)
	{
		ThreadCount = 1;
	}

	IoContext = new asio::io_context(ThreadCount);

	ThreadPool.SetNum(ThreadCount);
}

////////////////////////////////////////////////////////////////////////////////
void FAsioContext::Start()
{
	for (std::thread& Thread : ThreadPool)
	{
		std::thread TempThread([this] () { IoContext->run(); });
		Thread = MoveTemp(TempThread);
	}
}

////////////////////////////////////////////////////////////////////////////////
FAsioContext::~FAsioContext()
{
	IoContext->stop();

	for (std::thread& Thread : ThreadPool)
	{
		Thread.join();
	}

	delete IoContext;
}

////////////////////////////////////////////////////////////////////////////////
asio::io_context& FAsioContext::Get()
{
	return *IoContext;
}

} // namespace Trace

#endif // TRACE_WITH_ASIO
