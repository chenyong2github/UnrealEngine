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
class FAsioRecorderRelay;

////////////////////////////////////////////////////////////////////////////////
class FAsioRecorder
	: public FAsioTcpServer
	, public FAsioTickable
{
public:
	class FSession
	{
	public:
		uint32					GetId() const;
		uint32					GetTraceId() const;
		uint32					GetIpAddress() const;

	private:
		friend					FAsioRecorder;
		FAsioRecorderRelay*		Relay;
		uint32					Id;
		uint32					TraceId;
	};

								FAsioRecorder(asio::io_context& IoContext, FAsioStore& InStore);
								~FAsioRecorder();
	uint32						GetSessionCount() const;
	const FSession*				GetSessionInfo(uint32 Index) const;

private:
	virtual bool				OnAccept(asio::ip::tcp::socket& Socket) override;
	virtual void				OnTick() override;
	TArray<FSession>			Sessions;
	FAsioStore&					Store;
};

} // namespace Trace

#endif // TRACE_WITH_ASIO
