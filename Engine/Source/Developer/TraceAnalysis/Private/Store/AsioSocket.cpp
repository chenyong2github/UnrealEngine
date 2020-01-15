// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioSocket.h"

#if TRACE_WITH_ASIO

#include "Templates/UnrealTemplate.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioSocket::FAsioSocket(asio::ip::tcp::socket& InSocket)
: Socket(MoveTemp(InSocket))
{
}

////////////////////////////////////////////////////////////////////////////////
FAsioSocket::~FAsioSocket()
{
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioSocket::IsOpen() const
{
	return Socket.is_open();
}

////////////////////////////////////////////////////////////////////////////////
void FAsioSocket::Close()
{
	Socket.close();
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioSocket::Read(void* Dest, uint32 Size, FAsioIoSink* Sink, uint32 Id)
{
	if (!SetSink(Sink, Id))
	{
		return false;
	}

	asio::async_read(
		Socket,
        asio::buffer(Dest, Size),
		[this] (const asio::error_code& ErrorCode, size_t BytesReceived)
		{
			OnIoComplete(ErrorCode, BytesReceived);
		}
	);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioSocket::ReadSome(void* Dest, uint32 BufferSize, FAsioIoSink* Sink, uint32 Id)
{
	if (!SetSink(Sink, Id))
	{
		return false;
	}

	Socket.async_receive(
        asio::buffer(Dest, BufferSize),
		[this] (const asio::error_code& ErrorCode, size_t BytesReceived)
		{
			return OnIoComplete(ErrorCode, BytesReceived);
		}
	);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioSocket::Write(const void* Src, uint32 Size, FAsioIoSink* Sink, uint32 Id)
{
	if (!SetSink(Sink, Id))
	{
		return false;
	}

	asio::async_write(
		Socket,
		asio::buffer(Src, Size),
		[this] (const asio::error_code& ErrorCode, size_t BytesSent)
		{
			return OnIoComplete(ErrorCode, BytesSent);
		}
	);

	return true;
}

} // namespace Trace

#endif // TRACE_WITH_ASIO
