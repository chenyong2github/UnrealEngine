// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"
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
	asio::io_context&	GetIoContext();
	bool				StartTick(uint32 MillisecondRate);
	bool				StopTick();
	void				TickOnce(uint32 MillisecondRate);
	virtual void		OnTick() = 0;

private:
	void				AsyncTick();
	asio::steady_timer	Timer;
	uint32				MillisecondRate = 0;
};

} // namespace Trace
