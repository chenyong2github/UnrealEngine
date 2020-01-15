// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioStoreCborServer.h"

#if TRACE_WITH_ASIO

#include "AsioIoable.h"
#include "AsioObject.h"
#include "AsioRecorder.h"
#include "AsioSocket.h"
#include "AsioStore.h"
#include "AsioTraceRelay.h"
#include "CborPayload.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAsioStoreCborPeer
	: public FAsioIoSink
	, public FAsioObject
{
public:
					FAsioStoreCborPeer(asio::ip::tcp::socket& InSocket, FAsioStore& InStore, FAsioRecorder& InRecorder);
	virtual			~FAsioStoreCborPeer();
	bool			IsActive() const;
	void			Close();

protected:
	enum class EStatusCode
	{
		Success				= 200,
		BadRequest			= 400,
		MethodNotAllowed	= 405,
		InternalError		= 500,
	};

	void			OnPayload();
	void			OnConnect();
	void			OnStatus();
	void			OnTraceCount();
	void			OnTraceInfo();
	void			OnTraceRead();
	void			SendError(EStatusCode StatusCode);
	void			SendResponse(const FPayload& Payload);
	virtual void	OnIoComplete(uint32 Id, int32 Size) override;
	enum			{ OpStart, OpReadPayloadSize, OpReadPayload, OpSendResponse };
	FAsioStore&		Store;
	FAsioRecorder&	Recorder;
	FAsioSocket		Socket;
	uint32			PayloadSize;
	FResponse		Response;
};

////////////////////////////////////////////////////////////////////////////////
FAsioStoreCborPeer::FAsioStoreCborPeer(
	asio::ip::tcp::socket& InSocket,
	FAsioStore& InStore,
	FAsioRecorder& InRecorder)
: FAsioObject(InSocket.get_executor().context())
, Store(InStore)
, Recorder(InRecorder)
, Socket(InSocket)
{
	OnIoComplete(OpStart, 0);
}

////////////////////////////////////////////////////////////////////////////////
FAsioStoreCborPeer::~FAsioStoreCborPeer()
{
	Close();
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioStoreCborPeer::IsActive() const
{
	return Socket.IsOpen();
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::Close()
{
	if (IsActive())
	{
		Socket.Close();
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnConnect()
{
	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddParam("version", int32(EStoreVersion::Value));
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnStatus()
{
	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddParam("recorder_port", Recorder.GetPort());
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnTraceCount()
{
	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddParam("count", Store.GetTraceCount());
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnTraceInfo()
{
	int32 Index = int32(Response.GetInteger("index", -1));
	if (Index < 0)
	{
		return SendError(EStatusCode::BadRequest);
	}

	const FAsioStore::FTrace* Trace = Store.GetTraceInfo(Index);
	if (Trace == nullptr)
	{
		return SendError(EStatusCode::BadRequest);
	}

	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddParam("id", Trace->GetId());
	Builder.AddParam("size", Trace->GetSize());
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnTraceRead()
{
	uint32 Id = uint32(Response.GetInteger("id", 0));
	if (!Id)
	{
		return SendError(EStatusCode::BadRequest);
	}

	FAsioReadable* Input = Store.OpenTrace(Id);
	if (Input == nullptr)
	{
		return SendError(EStatusCode::BadRequest);
	}

	FAsioTraceRelay* Relay = new FAsioTraceRelay(GetIoContext(), Input);
	uint32 Port = Relay->GetPort();
	if (!Port)
	{
		delete Relay;
		return SendError(EStatusCode::InternalError);
	}

	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddParam("port", Port);
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnPayload()
{
	const char* Method = Response.GetString("$method");
	if (Method[0] == '\0')
	{
		SendError(EStatusCode::BadRequest);
		return;
	}

	struct {
		uint32	Hash;
		void	(FAsioStoreCborPeer::*Func)();
	} const DispatchTable[] = {
		{ QuickStoreHash("connect"),	&FAsioStoreCborPeer::OnConnect },
		{ QuickStoreHash("status"),		&FAsioStoreCborPeer::OnStatus },
		{ QuickStoreHash("trace_count"),&FAsioStoreCborPeer::OnTraceCount },
		{ QuickStoreHash("trace_info"),	&FAsioStoreCborPeer::OnTraceInfo },
		{ QuickStoreHash("trace_read"),	&FAsioStoreCborPeer::OnTraceRead },
	};

	uint32 MethodHash = QuickStoreHash(Method);
	for (const auto& Row : DispatchTable)
	{
		if (Row.Hash == MethodHash)
		{
			return (this->*(Row.Func))();
		}
	}

	SendError(EStatusCode::MethodNotAllowed);
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::SendError(EStatusCode StatusCode)
{
	TPayloadBuilder<> Builder((int32)StatusCode);
	FPayload Payload = Builder.Done();
	SendResponse(Payload);
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::SendResponse(const FPayload& Payload)
{
	struct {
		uint32	Size;
		uint8	Data[];
	}* Dest;

	Dest = decltype(Dest)(Response.Reserve(sizeof(*Dest) + Payload.Size));
	Dest->Size = Payload.Size;
	memcpy(Dest->Data, Payload.Data, Payload.Size);
	Socket.Write(Dest, sizeof(*Dest) + Payload.Size, this, OpSendResponse);
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnIoComplete(uint32 Id, int32 Size)
{
	if (Size < 0)
	{
		Socket.Close();
		return;
	}

	switch (Id)
	{
	case OpStart:
	case OpSendResponse:
		Socket.Read(&PayloadSize, sizeof(uint32), this, OpReadPayloadSize);
		break;

	case OpReadPayloadSize:
		Socket.Read(Response.Reserve(PayloadSize), PayloadSize, this, OpReadPayload);
		break;

	case OpReadPayload:
		OnPayload();
		break;
	}
}



////////////////////////////////////////////////////////////////////////////////
FAsioStoreCborServer::FAsioStoreCborServer(
	asio::io_context& IoContext,
	FAsioStore& InStore,
	FAsioRecorder& InRecorder)
: FAsioTcpServer(IoContext)
, Store(InStore)
, Recorder(InRecorder)
{
	StartServer();
}

////////////////////////////////////////////////////////////////////////////////
FAsioStoreCborServer::~FAsioStoreCborServer()
{
	for (FAsioStoreCborPeer* Peer : Peers)
	{
		Peer->Close();
		delete Peer;
	}
}

////////////////////////////////////////////////////////////////////////////////
FAsioStore& FAsioStoreCborServer::GetStore() const
{
	return Store;
}

////////////////////////////////////////////////////////////////////////////////
FAsioRecorder& FAsioStoreCborServer::GetRecorder() const
{
	return Recorder;
}

////////////////////////////////////////////////////////////////////////////////
bool FAsioStoreCborServer::OnAccept(asio::ip::tcp::socket& Socket)
{
	FAsioStoreCborPeer* Peer = new FAsioStoreCborPeer(Socket, Store, Recorder);
	Peers.Add(Peer);
	return true;
}

} // namespace Trace

#endif // TRACE_WITH_ASIO
