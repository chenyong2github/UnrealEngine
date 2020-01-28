// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioTcpServer.h"
#include "Templates/UnrealTemplate.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioTcpServer::FAsioTcpServer(asio::io_context& IoContext)
: FAsioObject(IoContext)
, Acceptor(IoContext)
{
}

////////////////////////////////////////////////////////////////////////////////
FAsioTcpServer::~FAsioTcpServer()
{
	Close();
}

////////////////////////////////////////////////////////////////////////////////
void FAsioTcpServer::Close()
{
	Acceptor.close();
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAsioTcpServer::GetPort() const
{
	using asio::ip::tcp;

	if (!Acceptor.is_open())
	{
		return 0;
	}

	asio::error_code ErrorCode;
	tcp::endpoint Endpoint = Acceptor.local_endpoint(ErrorCode);
	if (ErrorCode)
	{
		return 0;
	}

	return Endpoint.port();
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioTcpServer::StartServer(uint32 Port)
{
	if (Acceptor.is_open())
	{
		return false;
	}

	using asio::ip::tcp;

	tcp::endpoint Endpoint(tcp::v4(), uint16(Port));
	tcp::acceptor TempAcceptor(GetIoContext(), Endpoint, false);

	asio::error_code ErrorCode;
	tcp::endpoint LocalEndpoint = TempAcceptor.local_endpoint(ErrorCode);
	if (ErrorCode || LocalEndpoint.port() == 0)
	{
		return false;
	}

	asio::socket_base::linger Linger(true, 0);
	TempAcceptor.set_option(Linger);

	Acceptor = MoveTemp(TempAcceptor);
	AsyncAccept();
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioTcpServer::StopServer()
{
	if (!Acceptor.is_open())
	{
		return false;
	}

	Acceptor.close();
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioTcpServer::AsyncAccept()
{
	using asio::ip::tcp;

	Acceptor.async_accept([this] (
		const asio::error_code& ErrorCode,
		tcp::socket&& Socket)
	{
		if (!ErrorCode && OnAccept(Socket))
		{
			AsyncAccept();
			return;
		}

		Close();
		return;
	});
}

} // namespace Trace
