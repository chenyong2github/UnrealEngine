// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioRecorder.h"
#include "AsioIoable.h"
#include "AsioSocket.h"
#include "AsioStore.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioRecorderRelay
	: public FAsioIoSink
{
public:
						FAsioRecorderRelay(uint32 Magic, asio::ip::tcp::socket& Socket, FAsioWriteable* InOutput);
	virtual				~FAsioRecorderRelay();
	bool				IsOpen();
	void				Close();
	uint32				GetIpAddress() const;

private:
	virtual void		OnIoComplete(uint32 Id, int32 Size) override;
	static const uint32	BufferSize = 64 * 1024;
	enum				{ OpSocketRead, OpFileWrite };
	FAsioSocket			Input;
	FAsioWriteable*		Output;
	union
	{
		uint32			Magic;
		uint8			Buffer[BufferSize];
	};
};

////////////////////////////////////////////////////////////////////////////////
FAsioRecorderRelay::FAsioRecorderRelay(
	uint32 InMagic,
	asio::ip::tcp::socket& Socket,
	FAsioWriteable* InOutput)
: Input(Socket)
, Output(InOutput)
, Magic(InMagic)
{
	Output->Write(&Magic, sizeof(Magic), this, OpFileWrite);
}

////////////////////////////////////////////////////////////////////////////////
FAsioRecorderRelay::~FAsioRecorderRelay()
{
	check(!Input.IsOpen());
	check(!Output->IsOpen());
	delete Output;
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioRecorderRelay::IsOpen()
{
	return Input.IsOpen();
}

////////////////////////////////////////////////////////////////////////////////
void FAsioRecorderRelay::Close()
{
	Input.Close();
	Output->Close();
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAsioRecorderRelay::GetIpAddress() const
{
	return Input.GetRemoteAddress();
}

////////////////////////////////////////////////////////////////////////////////
void FAsioRecorderRelay::OnIoComplete(uint32 Id, int32 Size)
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

	case OpFileWrite:
		Input.ReadSome(Buffer, BufferSize, this, OpSocketRead);
		break;
	}
}



////////////////////////////////////////////////////////////////////////////////
uint32 FAsioRecorder::FSession::GetId() const
{
	return Id;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAsioRecorder::FSession::GetTraceId() const
{
	return TraceId;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAsioRecorder::FSession::GetIpAddress() const
{
	return Relay->GetIpAddress();
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
	check(!FAsioTickable::IsActive());
	check(!FAsioTcpServer::IsOpen());

	for (FSession& Session : Sessions)
	{
		delete Session.Relay;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAsioRecorder::Close()
{
	FAsioTickable::StopTick();
	FAsioTcpServer::Close();

	for (FSession& Session : Sessions)
	{
		Session.Relay->Close();
	}
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAsioRecorder::GetSessionCount() const
{
	return uint32(Sessions.Num());
}

////////////////////////////////////////////////////////////////////////////////
const FAsioRecorder::FSession* FAsioRecorder::GetSessionInfo(uint32 Index) const
{
	if (Index >= uint32(Sessions.Num()))
	{
		return nullptr;
	}

	return Sessions.GetData() + Index;
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioRecorder::OnAccept(asio::ip::tcp::socket& Socket)
{
#if 0
	auto TraceAcceptor = [this, Socket=MoveTemp(Socket), Buffer] (
			const asio::error_code& ErrorCode,
			size_t Size
		) mutable
#endif // 0
	struct FTraceAcceptor
	{
		void OnMagic(const asio::error_code& ErrorCode, size_t Size)
		{
			if (ErrorCode || Size != sizeof(Magic))
			{
				delete this;
				return;
			}

			if (Magic != 'TRCE' && Magic != 'ECRT')
			{
				delete this;
				return;
			}

			OuterSelf->OnAcceptable(Magic, Socket);
			delete this;
		}
		asio::ip::tcp::socket	Socket;
		FAsioRecorder*			OuterSelf;
		uint32					Magic;
	};

	FTraceAcceptor* TraceAcceptor = new FTraceAcceptor{MoveTemp(Socket)};
	TraceAcceptor->OuterSelf = this;

	asio::async_read(
		TraceAcceptor->Socket, 
		asio::buffer(&(TraceAcceptor->Magic), sizeof(TraceAcceptor->Magic)),
		[TraceAcceptor] (const asio::error_code& ErrorCode, size_t Size)
		{
			return TraceAcceptor->OnMagic(ErrorCode, Size);
		}
	);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioRecorder::OnAcceptable(uint32 Magic, asio::ip::tcp::socket& Socket)
{
	FAsioStore::FNewTrace Trace = Store.CreateTrace();
	if (Trace.Writeable == nullptr)
	{
		return;
	}

	uint32 IdPieces[] =
	{
		Socket.local_endpoint().address().to_v4().to_uint(),
		Socket.remote_endpoint().port(),
		Socket.local_endpoint().port(),
		0,
	};

	auto* Relay = new FAsioRecorderRelay(Magic, Socket, Trace.Writeable);

	FSession Session;
	Session.Relay = Relay;
	Session.Id = QuickStoreHash(IdPieces);
	Session.TraceId = Trace.Id;
	Sessions.Add(Session);
}

////////////////////////////////////////////////////////////////////////////////
void FAsioRecorder::OnTick()
{
	uint32 FinalNum = 0;
	for (int i = 0, n = Sessions.Num(); i < n; ++i)
	{
		FSession& Session = Sessions[i];
		if (Session.Relay->IsOpen())
		{
			Sessions[FinalNum] = Session;
			++FinalNum;
			continue;
		}

		delete Session.Relay;
	}

	Sessions.SetNum(FinalNum);
}

} // namespace Trace
