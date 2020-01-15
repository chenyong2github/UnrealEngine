// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioRecorder.h"

#if TRACE_WITH_ASIO

#include "AsioIoable.h"
#include "AsioSocket.h"
#include "AsioStore.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioRecorderPeer
	: public FAsioIoSink
{
public:
						FAsioRecorderPeer(asio::ip::tcp::socket& Socket, FAsioWriteable* InOutput);
	virtual				~FAsioRecorderPeer();
	void				Close();

private:
	virtual void		OnIoComplete(uint32 Id, int32 Size) override;
	static const uint32	BufferSize = 64 * 1024;
	enum				{ OpStart, OpSocketRead, OpFileWrite };
	FAsioSocket			Input;
	FAsioWriteable*		Output;
	uint8				Buffer[BufferSize];
};

////////////////////////////////////////////////////////////////////////////////
FAsioRecorderPeer::FAsioRecorderPeer(asio::ip::tcp::socket& Socket, FAsioWriteable* InOutput)
: Input(Socket)
, Output(InOutput)
{
	OnIoComplete(OpStart, 0);
}

////////////////////////////////////////////////////////////////////////////////
FAsioRecorderPeer::~FAsioRecorderPeer()
{
	delete Output;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioRecorderPeer::Close()
{
	Input.Close();
	Output->Close();
}

////////////////////////////////////////////////////////////////////////////////
void FAsioRecorderPeer::OnIoComplete(uint32 Id, int32 Size)
{
	if (Size < 0)
	{
		Close();
		return;
	}

	switch (Id)
	{
	case OpSocketRead:
		Output->Write(Buffer, Size, this, OpFileWrite);
		break;

	case OpStart:
	case OpFileWrite:
		Input.ReadSome(Buffer, BufferSize, this, OpSocketRead);
		break;
	}
}



////////////////////////////////////////////////////////////////////////////////
FAsioRecorder::FAsioRecorder(asio::io_context& IoContext, FAsioStore& InStore)
: FAsioTcpServer(IoContext)
, FAsioTickable(IoContext)
, Store(InStore)
{
	StartTick(500);
}

////////////////////////////////////////////////////////////////////////////////
FAsioRecorder::~FAsioRecorder()
{
	for (FAsioRecorderPeer* Peer : Peers)
	{
		Peer->Close();
		delete Peer;
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioRecorder::OnAccept(asio::ip::tcp::socket& Socket)
{
	FAsioWriteable* Output = Store.CreateTrace();
	if (Output == nullptr)
	{
		return true;
	}

	FAsioRecorderPeer* Peer = new FAsioRecorderPeer(Socket, Output);
	Peers.Add(Peer);
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioRecorder::OnTick()
{
	/* cleanup dead clients */
}

} // namespace Trace

#endif // TRACE_WITH_ASIO
