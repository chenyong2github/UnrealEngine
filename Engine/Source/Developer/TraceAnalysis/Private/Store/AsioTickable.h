// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioObject.h"

#include <chrono>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioTickable
	: public FAsioObject
{
public:
						FAsioTickable(asio::io_context& IoContext);
	virtual				~FAsioTickable();
	bool				StartTick(uint32 MillisecondRate);
	bool				StopTick();
	virtual void		OnTick() = 0;

private:
	void				AsyncTick();
	asio::steady_timer	Timer;
	uint32				MillisecondRate = 0;
};

} // namespace Trace

#endif // TRACE_WITH_ASIO
