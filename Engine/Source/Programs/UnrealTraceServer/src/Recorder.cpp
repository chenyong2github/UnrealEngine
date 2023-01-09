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
	bool				ReadMagic();
	bool				ReadMetadata(int32 Size);
	static const uint32	BufferSize = 64 * 1024;
	FAsioSocket			Input;
	FAsioWriteable*		Output = nullptr;
	FStore&				Store;
	uint8*				PreambleCursor;
	uint32				TraceId = 0;
	uint16				ControlPort = 0;
	uint8				Buffer[BufferSize];

	enum
	{
		OpMagicRead,
		OpMetadataRead,
		OpSocketRead,
		OpFileWrite,
	};

	using MagicType				= uint32;
	using MetadataSizeType		= uint16;
	using VersionType			= struct { uint8 Transport; uint8 Protocol; };

	static_assert(sizeof(VersionType) == 2, "Unexpected struct size");
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

	// Kick things off by reading the magic four bytes at the start of the stream
	// along with an additional two bytes that are likely the metadata size.
	uint32 PreambleReadSize = sizeof(MagicType) + sizeof(MetadataSizeType);
	PreambleCursor = Buffer + PreambleReadSize;
	Input.Read(Buffer, PreambleReadSize, this, OpMagicRead);
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
bool FRecorderRelay::ReadMagic()
{
	// Here we'll check the magic four bytes at the start of the stream and create
	// a trace to write into if they are bytes we're expecting.

	// We will only support clients that send the magic. Very early clients did
	// not do this but they were unreleased and should no longer be in use.
	if (Buffer[3] != 'T' || Buffer[2] != 'R' || Buffer[1] != 'C')
	{
		return false;
	}

	// We can continue to support very old clients
	if (Buffer[0] == 'E')
	{
		if (CreateTrace())
		{
			// Old clients have no metadata so we can go straight into the
			// read-write loop. We've already got read data in Buffer.
			Output->Write(Buffer, sizeof(MagicType) + sizeof(MetadataSizeType), this, OpFileWrite);
			return true;
		}
		return false;
	}

	// Later clients have a metadata block (TRC2). There's loose support for the
	// future too if need be (TRC[3-9]).
	if (Buffer[0] < '2' || Buffer[0] > '9')
	{
		return false;
	}

	// Concatenate metadata into the buffer, first validating the given size is
	// one that we can handle in a single read.
	uint32 MetadataSize = *(MetadataSizeType*)(Buffer + sizeof(MagicType));
	MetadataSize += sizeof(VersionType);
	if (MetadataSize > BufferSize - uint32(ptrdiff_t(PreambleCursor - Buffer)))
	{
		return false;
	}

	Input.Read(PreambleCursor, MetadataSize, this, OpMetadataRead);

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FRecorderRelay::ReadMetadata(int32 Size)
{
	// At this point Buffer          [magic][md_size][metadata][t_ver][p_ver]
	// looks like this;              Buffer--------->PreambleCursor--------->
	//                                               |---------Size---------|

	// We want to consume [metadata] so some adjustment is required.
	int32 ReadSize = Size - sizeof(VersionType);
	const uint8* Cursor = PreambleCursor;

	// MetadataFields
	while (ReadSize >= 2)
	{
		struct FMetadataHeader
		{
			uint8	Size;
			uint8	Id;
		};
		const auto& MetadataField = *(FMetadataHeader*)(Cursor);

		Cursor += sizeof(FMetadataHeader);
		ReadSize -= sizeof(FMetadataHeader);

		if (ReadSize < MetadataField.Size)
		{
			return false;
		}

		if (MetadataField.Id == 0) /* ControlPortFieldId */
		{
			ControlPort = *(const uint16*)Cursor;
		}

		Cursor += MetadataField.Size;
		ReadSize -= MetadataField.Size;
	}

	// There should be no data left to consume if the metadata was well-formed
	if (ReadSize != 0)
	{
		return false;
	}

	// Now we've a full preamble we are ready to write the trace.
	if (!CreateTrace())
	{
		return false;
	}

	// Analysis needs the preamble too.
	uint32 PreambleSize = uint32(ptrdiff_t(PreambleCursor - Buffer)) + Size;
	Output->Write(Buffer, PreambleSize, this, OpFileWrite);

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
	case OpMagicRead:
		if (!ReadMagic())
		{
			Close();
		}
		break;

	case OpMetadataRead:
		if (!ReadMetadata(Size))
		{
			Close();
		}
		break;

	case OpSocketRead:
		Output->Write(Buffer, Size, this, OpFileWrite);
		break;

	case OpFileWrite:
		Input.ReadSome(Buffer, BufferSize, this, OpSocketRead);
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
