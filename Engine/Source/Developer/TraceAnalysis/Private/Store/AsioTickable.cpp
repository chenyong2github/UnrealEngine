// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioTickable.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioTickable::FAsioTickable(asio::io_context& IoContext)
: FAsioObject(IoContext)
, Timer(IoContext)
{
}

////////////////////////////////////////////////////////////////////////////////
FAsioTickable::~FAsioTickable()
{
	StopTick();
}

////////////////////////////////////////////////////////////////////////////////
asio::io_context& FAsioTickable::GetIoContext()
{
	return Timer.get_executor().context();
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioTickable::StartTick(uint32 InMillisecondRate)
{
	if (MillisecondRate)
	{
		return false;
	}

	MillisecondRate = InMillisecondRate;
	if (MillisecondRate)
	{
		AsyncTick();
		return true;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioTickable::StopTick()
{
	if (!MillisecondRate)
	{
		return false;
	}

	MillisecondRate = 0;
	Timer.cancel();
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioTickable::TickOnce(uint32 InMillisecondRate)
{
	if (MillisecondRate)
	{
		return;
	}

	MillisecondRate = InMillisecondRate;
	if (MillisecondRate)
	{
		AsyncTick();
		MillisecondRate = 0;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAsioTickable::AsyncTick()
{
	auto StdTime = std::chrono::milliseconds(MillisecondRate);
	Timer.expires_after(StdTime);

	Timer.async_wait([this] (const asio::error_code& ErrorCode)
	{
		if (ErrorCode)
		{
			return;
		}

		OnTick();

		if (MillisecondRate)
		{
			AsyncTick();
		}
	});
}

} // namespace Trace
