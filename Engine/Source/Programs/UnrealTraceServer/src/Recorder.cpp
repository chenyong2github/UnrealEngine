// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pch.h"
#include "AsioIoable.h"
#include "AsioSocket.h"
#include "Recorder.h"
#include "Store.h"

#if TS_USING(TS_PLATFORM_WINDOWS)
#	include <Mstcpip.h>
#endif

////////////////////////////////////////////////////////////////////////////////
constexpr uint32 FourCc(int A, int B, int C, int D)
{
    return ((A << 24) | (B << 16) | (C << 8) | D);
}



////////////////////////////////////////////////////////////////////////////////
class FRecorderRelay
	: public FAsioIoSink
{
public:
						FRecorderRelay(asio::ip::tcp::socket& Socket, FStore& InStore);
	virtual				~FRecorderRelay();
	bool				IsOpen();
	void				Close();
	uint32				GetTraceId() const;
	uint32				GetIpAddress() const;
	uint32				GetControlPort() const;

private:
	virtual void		OnIoComplete(uint32 Id, int32 Size) override;
	bool				CreateTrace();
	bool				ReadMagic(uint32 Magic);
	bool				ReadMetadata(int32 Size);
	static const uint32	BufferSize = 64 * 1024;
	FAsioSocket			Input;
	FAsioWriteable*		Output = nullptr;
	FStore&				Store;
	uint32				ActiveReadOp = OpSocketReadMetadata;
	uint32				TraceId = 0;
	uint16				ControlPort = 0;
	uint8				Buffer[BufferSize];

	enum
	{
		OpStart,
		OpSocketReadMetadata,
		OpSocketRead,
		OpFileWrite,
	};
};

////////////////////////////////////////////////////////////////////////////////
FRecorderRelay::FRecorderRelay(asio::ip::tcp::socket& Socket, FStore& InStore)
: Input(Socket)
, Store(InStore)
{
#if TS_USING(TS_PLATFORM_WINDOWS)
	// Trace data is a stream and communication is one way. It is implemented
	// this way to share code between sending trace data over the wire and writing
	// it to a file. Because there's no ping/pong we can end up with a half-open
	// TCP connection if the other end doesn't close its socket. So we'll enable
	// keep-alive on the socket and set a short timeout (default is 2hrs).
	tcp_keepalive KeepAlive =
	{
		1,		// on
		15000,	// timeout_ms
		2000,	// interval_ms
	};

	DWORD BytesReturned;
	WSAIoctl(
		Socket.native_handle(),
		SIO_KEEPALIVE_VALS,
		&KeepAlive, sizeof(KeepAlive),
		nullptr, 0,
		&BytesReturned,
		nullptr,
		nullptr
	);
#endif

	if (CreateTrace())
	{
		OnIoComplete(OpStart, 0);
	}
}

////////////////////////////////////////////////////////////////////////////////
FRecorderRelay::~FRecorderRelay()
{
	check(!Input.IsOpen());
	if (Output != nullptr)
	{
		check(!Output->IsOpen());
		delete Output;
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorderRelay::IsOpen()
{
	return Input.IsOpen();
}

////////////////////////////////////////////////////////////////////////////////
void FRecorderRelay::Close()
{
	Input.Close();
	if (Output != nullptr)
	{
		Output->Close();
	}
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorderRelay::GetTraceId() const
{
	return TraceId;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorderRelay::GetIpAddress() const
{
	return Input.GetRemoteAddress();
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorderRelay::GetControlPort() const
{
	return ControlPort;
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorderRelay::CreateTrace()
{
	FStore::FNewTrace Trace = Store.CreateTrace();
	TraceId = Trace.Id;
	Output = Trace.Writeable;
	return (Output != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorderRelay::ReadMagic(uint32 Magic)
{
	switch (Magic)
	{
	/* trace with metadata data */
	case FourCc('T', 'R', 'C', '2'):
		break;

	/* valid, but to old or wrong endian for us */
	case FourCc('T', 'R', 'C', 'E'):
	case FourCc('E', 'C', 'R', 'T'):
	case FourCc('2', 'C', 'R', 'T'):
		return true;

	/* unexpected magic */
	default:
		return false;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorderRelay::ReadMetadata(int32 Size)
{
	const uint8* Cursor = Buffer;
	auto Read = [&] (int32 SizeToRead)
	{
		const uint8* Ptr = Cursor;
		Cursor += SizeToRead;
		Size -= SizeToRead;
		return Ptr;
	};

	// Stream header
	uint32 Magic;
	if (Size < sizeof(Magic))
	{
		return true;
	}

	Magic = *(const uint32*)(Read(sizeof(Magic)));
	if (!ReadMagic(Magic))
	{
		return false;
	}

	// MetadataSize field
	if (Size < 2)
	{
		return true;
	}
	Size = *(const uint16*)(Read(2));

	// MetadataFields
	while (Size >= 2)
	{
		struct {
			uint8	Size;
			uint8	Id;
		} MetadataField;
		MetadataField = *(const decltype(MetadataField)*)(Read(sizeof(MetadataField)));

		if (Size < MetadataField.Size)
		{
			break;
		}

		if (MetadataField.Id == 0) /* ControlPortFieldId */
		{
			ControlPort = *(const uint16*)Cursor;
		}

		Size -= MetadataField.Size;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FRecorderRelay::OnIoComplete(uint32 Id, int32 Size)
{
	if (Size < 0)
	{
		Close();
		return;
	}

	switch (Id)
	{
	case OpSocketReadMetadata:
		ActiveReadOp = OpSocketRead;
		if (!ReadMetadata(Size))
		{
			Close();
			return;
		}
		/* fallthrough */

	case OpSocketRead:
		Output->Write(Buffer, Size, this, OpFileWrite);
		break;

	case OpStart:
	case OpFileWrite:
		Input.ReadSome(Buffer, BufferSize, this, ActiveReadOp);
		break;
	}
}



////////////////////////////////////////////////////////////////////////////////
uint32 FRecorder::FSession::GetId() const
{
	return Id;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorder::FSession::GetTraceId() const
{
	return Relay->GetTraceId();
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorder::FSession::GetIpAddress() const
{
	return Relay->GetIpAddress();
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorder::FSession::GetControlPort() const
{
	return Relay->GetControlPort();
}



////////////////////////////////////////////////////////////////////////////////
FRecorder::FRecorder(asio::io_context& IoContext, FStore& InStore)
: FAsioTcpServer(IoContext)
, FAsioTickable(IoContext)
, Store(InStore)
{
	StartTick(500);
}

////////////////////////////////////////////////////////////////////////////////
FRecorder::~FRecorder()
{
	check(!FAsioTickable::IsActive());
	check(!FAsioTcpServer::IsOpen());

	for (FSession& Session : Sessions)
	{
		delete Session.Relay;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FRecorder::Close()
{
	FAsioTickable::StopTick();
	FAsioTcpServer::Close();

	for (FSession& Session : Sessions)
	{
		Session.Relay->Close();
	}
}

////////////////////////////////////////////////////////////////////////////////
uint32 FRecorder::GetSessionCount() const
{
	return uint32(Sessions.Num());
}

////////////////////////////////////////////////////////////////////////////////
const FRecorder::FSession* FRecorder::GetSessionInfo(uint32 Index) const
{
	if (Index >= uint32(Sessions.Num()))
	{
		return nullptr;
	}

	return Sessions.GetData() + Index;
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorder::OnAccept(asio::ip::tcp::socket& Socket)
{
	auto* Relay = new FRecorderRelay(Socket, Store);

	uint32 IdPieces[] = {
		Relay->GetIpAddress(),
		Socket.remote_endpoint().port(),
		Socket.local_endpoint().port(),
		0,
	};

	FSession Session;
	Session.Relay = Relay;
	Session.Id = QuickStoreHash(IdPieces);
	Sessions.Add(Session);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FRecorder::OnTick()
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

/* vim: set noexpandtab : */
