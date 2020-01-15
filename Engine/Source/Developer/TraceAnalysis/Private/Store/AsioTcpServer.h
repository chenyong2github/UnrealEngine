// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioObject.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioTcpServer
	: public FAsioObject
{
public:
							FAsioTcpServer(asio::io_context& IoContext);
	virtual					~FAsioTcpServer();
	virtual bool			OnAccept(asio::ip::tcp::socket& Socket) = 0;
	uint32					GetPort() const;
	bool					StartServer(uint32 Port=0);
	bool					StopServer();

private:
	void					AsyncAccept();
	asio::ip::tcp::acceptor	Acceptor;
};

} // namespace Trace

#endif // TRACE_WITH_ASIO
