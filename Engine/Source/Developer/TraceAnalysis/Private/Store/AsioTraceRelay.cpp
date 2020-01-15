// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioTraceRelay.h"

#if TRACE_WITH_ASIO

#include "AsioSocket.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioTraceRelay::FAsioTraceRelay(asio::io_context& IoContext, FAsioReadable* InInput)
: FAsioTcpServer(IoContext)
, Input(InInput)
{
	StartServer();
}

////////////////////////////////////////////////////////////////////////////////
FAsioTraceRelay::~FAsioTraceRelay()
{
	if (Output != nullptr)
	{
		delete Output;
	}
	delete Input;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioTraceRelay::Close()
{
	if (Output != nullptr)
	{
		Output->Close();
	}

	Input->Close();
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioTraceRelay::OnAccept(asio::ip::tcp::socket& Socket)
{
	Output = new FAsioSocket(Socket);
	OnIoComplete(OpStart, 0);
	return false;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioTraceRelay::OnIoComplete(uint32 Id, int32 Size)
{
	if (Size < 0)
	{
		Output->Close();
		Input->Close();
		return;
	}

	switch (Id)
	{
	case OpStart:
	case OpSend:
		Input->Read(Buffer, BufferSize, this, OpRead);
		break;

	case OpRead:
		Output->Write(Buffer, Size, this, OpSend);
		break;
	}
}

} // namespace Trace

#endif // TRACE_WITH_ASIO
