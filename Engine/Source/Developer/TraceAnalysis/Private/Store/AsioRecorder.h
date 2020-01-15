// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioTcpServer.h"
#include "AsioTickable.h"
#include "Containers/Array.h"

namespace Trace
{

class FAsioStore;
class FAsioRecorderPeer;

////////////////////////////////////////////////////////////////////////////////
class FAsioRecorder
	: public FAsioTcpServer
	, public FAsioTickable
{
public:
								FAsioRecorder(asio::io_context& IoContext, FAsioStore& InStore);
								~FAsioRecorder();

private:
	virtual bool				OnAccept(asio::ip::tcp::socket& Socket) override;
	virtual void				OnTick() override;
	TArray<FAsioRecorderPeer*>	Peers;
	FAsioStore&					Store;
};

} // namespace Trace

#endif // TRACE_WITH_ASIO
