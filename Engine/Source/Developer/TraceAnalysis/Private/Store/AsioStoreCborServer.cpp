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
	void			OnSessionCount();
	void			OnSessionInfo();
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
	Builder.AddInteger("version", int32(EStoreVersion::Value));
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnSessionCount()
{
	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddInteger("count", Recorder.GetSessionCount());
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnSessionInfo()
{
	const FAsioRecorder::FSession* Session = nullptr;

	int32 Index = int32(Response.GetInteger("index", -1));
	if (Index >= 0)
	{
		Session = Recorder.GetSessionInfo(Index);
	}
	else if (uint32 Id = uint32(Response.GetInteger("id", 0)))
	{
		for (int i = 0, n = Recorder.GetSessionCount(); i < n; ++i)
		{
			const FAsioRecorder::FSession* Candidate = Recorder.GetSessionInfo(i);
			if (Candidate->GetId() == Id)
			{
				Session = Candidate;
				break;
			}
		}
	}
	else if (uint32 TraceId = uint32(Response.GetInteger("trace_id", 0)))
	{
		for (int i = 0, n = Recorder.GetSessionCount(); i < n; ++i)
		{
			const FAsioRecorder::FSession* Candidate = Recorder.GetSessionInfo(i);
			if (Candidate->GetTraceId() == TraceId)
			{
				Session = Candidate;
				break;
			}
		}
	}

	if (Session == nullptr)
	{
		return SendError(EStatusCode::BadRequest);
	}

	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddInteger("id", Session->GetId());
	Builder.AddInteger("trace_id", Session->GetTraceId());
	Builder.AddInteger("ip_address", Session->GetIpAddress());
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnStatus()
{
	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddInteger("recorder_port", Recorder.GetPort());
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnTraceCount()
{
	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddInteger("count", Store.GetTraceCount());
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnTraceInfo()
{
	const FAsioStore::FTrace* Trace = nullptr;

	int32 Index = int32(Response.GetInteger("index", -1));
	if (Index >= 0)
	{
		Trace = Store.GetTraceInfo(Index);
	}
	else
	{
		uint32 Id = uint32(Response.GetInteger("id", 0));
		if (Id != 0)
		{
			for (int i = 0, n = Store.GetTraceCount(); i < n; ++i)
			{
				const FAsioStore::FTrace* Candidate = Store.GetTraceInfo(i);
				if (Candidate->GetId() == Id)
				{
					Trace = Candidate;
					break;
				}
			}
		}
	}

	if (Trace == nullptr)
	{
		return SendError(EStatusCode::BadRequest);
	}

	const TCHAR* Name = Trace->GetName();
	char OutName[128];
	for (char& Out : OutName)
	{
		Out = char(*Name++);
		if (Out == '\0')
		{
			break;
		}
	}
	OutName[sizeof(OutName) - 1] = '\0';

	TPayloadBuilder<> Builder((int32)EStatusCode::Success);
	Builder.AddInteger("id", Trace->GetId());
	Builder.AddInteger("size", Trace->GetSize());
	Builder.AddString("name", OutName);
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
	Builder.AddInteger("port", Port);
	SendResponse(Builder.Done());
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStoreCborPeer::OnPayload()
{
	FAnsiStringView Method = Response.GetString("$method", "");
	if (!Method.Len())
	{
		SendError(EStatusCode::BadRequest);
		return;
	}

	static struct {
		uint32	Hash;
		void	(FAsioStoreCborPeer::*Func)();
	} const DispatchTable[] = {
		{ QuickStoreHash("connect"),		&FAsioStoreCborPeer::OnConnect },
		{ QuickStoreHash("session/count"),	&FAsioStoreCborPeer::OnSessionCount },
		{ QuickStoreHash("session/info"),	&FAsioStoreCborPeer::OnSessionInfo },
		{ QuickStoreHash("status"),			&FAsioStoreCborPeer::OnStatus },
		{ QuickStoreHash("trace/count"),	&FAsioStoreCborPeer::OnTraceCount },
		{ QuickStoreHash("trace/info"),		&FAsioStoreCborPeer::OnTraceInfo },
		{ QuickStoreHash("trace/read"),		&FAsioStoreCborPeer::OnTraceRead },
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
