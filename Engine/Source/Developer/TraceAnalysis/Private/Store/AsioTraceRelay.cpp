// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioTraceRelay.h"
#include "AsioRecorder.h"
#include "AsioSocket.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioTraceRelay::FAsioTraceRelay(
	asio::io_context& IoContext,
	FAsioReadable* InInput,
	uint32 InSessionId,
	FAsioRecorder& InRecorder)
: FAsioTcpServer(IoContext)
, Input(InInput)
, Recorder(InRecorder)
, SessionId(InSessionId)
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
bool FAsioTraceRelay::IsOpen() const
{
	return (Output != nullptr) && Output->IsOpen();
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
		if (Id == OpRead && SessionId)
		{
			for (int i = 0, n = Recorder.GetSessionCount(); i < n; ++i)
			{
				const FAsioRecorder::FSession* Session = Recorder.GetSessionInfo(i);
				if (Session->GetId() == SessionId)
				{
					FPlatformProcess::SleepNoStats(0.2f);
					return OnIoComplete(OpStart, 0);
				}
			}
		}

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
