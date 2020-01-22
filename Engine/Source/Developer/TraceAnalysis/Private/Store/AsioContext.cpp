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
FAsioContext::~FAsioContext()
{
	Stop();
	delete IoContext;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioContext::Start()
{
	if (bRunning)
	{
		return;
	}

	for (std::thread& Thread : ThreadPool)
	{
		std::thread TempThread([this] () { IoContext->run(); });
		Thread = MoveTemp(TempThread);
	}

	bRunning = true;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioContext::Stop()
{
	if (!bRunning)
	{
		return;
	}

	IoContext->stop();

	for (std::thread& Thread : ThreadPool)
	{
		Thread.join();
	}

	bRunning = false;
}

////////////////////////////////////////////////////////////////////////////////
asio::io_context& FAsioContext::Get()
{
	return *IoContext;
}

} // namespace Trace

#endif // TRACE_WITH_ASIO
