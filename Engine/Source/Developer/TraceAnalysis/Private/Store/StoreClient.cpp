// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/StoreClient.h"
#include "Asio/Asio.h"

#if TRACE_WITH_ASIO

#include "AsioStore.h"
#include "CborPayload.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FStoreCborClient
{
public:
							FStoreCborClient();
							~FStoreCborClient();
	const FResponse&		GetResponse() const;
	bool					Connect(const TCHAR* Host, uint16 Port);
	bool					GetStatus();
	bool					GetTraceCount();
	bool					GetTraceInfo(uint32 Index);
	asio::ip::tcp::socket*	ReadTrace(uint32 Id);

private:
	bool					Communicate(const FPayload& Payload);
	asio::io_context		IoContext;
	asio::ip::tcp::socket	Socket;
	FResponse				Response;
};

////////////////////////////////////////////////////////////////////////////////
FStoreCborClient::FStoreCborClient()
: Socket(IoContext)
{
}

////////////////////////////////////////////////////////////////////////////////
FStoreCborClient::~FStoreCborClient()
{
}

////////////////////////////////////////////////////////////////////////////////
const FResponse& FStoreCborClient::GetResponse() const
{
	return Response;
}

////////////////////////////////////////////////////////////////////////////////
bool FStoreCborClient::Connect(const TCHAR* Host, uint16 Port)
{
	FTCHARToUTF8 HostUtf8(Host);
	char PortString[8];
	FCStringAnsi::Sprintf(PortString, "%d", Port);

	asio::ip::tcp::resolver Resolver(IoContext);
	asio::ip::tcp::resolver::results_type Endpoints = Resolver.resolve(HostUtf8.Get(), PortString);

	asio::error_code ErrorCode;

	asio::connect(Socket, Endpoints, ErrorCode);
	if (ErrorCode)
	{
		return false;
	}

	TPayloadBuilder<> Builder("connect");
	Builder.AddParam("version", int32(EStoreVersion::Value));
	FPayload Payload = Builder.Done();
	return Communicate(Payload);
}

////////////////////////////////////////////////////////////////////////////////
bool FStoreCborClient::Communicate(const FPayload& Payload)
{
	if (!Socket.is_open())
	{
		return false;
	}

	asio::error_code ErrorCode;

	// Send the payload
	uint32 PayloadSize = Payload.Size;
	asio::write(Socket, asio::buffer(&PayloadSize, sizeof(PayloadSize)), ErrorCode);
	asio::write(Socket, asio::buffer(Payload.Data, Payload.Size), ErrorCode);
	if (ErrorCode)
	{
		Socket.close();
		return false;
	}

	// Wait for a response
	uint32 ResponseSize = 0;
	asio::read(Socket, asio::buffer(&ResponseSize, sizeof(ResponseSize)), ErrorCode);
	if (ErrorCode)
	{
		Socket.close();
		return false;
	}

	if (ResponseSize == 0)
	{
		Socket.close();
		return false;
	}

	uint8* Dest = Response.Reserve(ResponseSize);
	asio::read(Socket, asio::buffer(Dest, ResponseSize), ErrorCode);
	if (ErrorCode)
	{
		Socket.close();
		return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FStoreCborClient::GetStatus()
{
	TPayloadBuilder<32> Builder("status");
	FPayload Payload = Builder.Done();
	return Communicate(Payload);
}

////////////////////////////////////////////////////////////////////////////////
bool FStoreCborClient::GetTraceCount()
{
	TPayloadBuilder<32> Builder("trace_count");
	FPayload Payload = Builder.Done();
	return Communicate(Payload);
}

////////////////////////////////////////////////////////////////////////////////
bool FStoreCborClient::GetTraceInfo(uint32 Index)
{
	TPayloadBuilder<> Builder("trace_info");
	Builder.AddParam("index", Index);
	FPayload Payload = Builder.Done();
	return Communicate(Payload);
}

////////////////////////////////////////////////////////////////////////////////
asio::ip::tcp::socket* FStoreCborClient::ReadTrace(uint32 Id)
{
	TPayloadBuilder<> Builder("trace_read");
	Builder.AddParam("id", Id);
	FPayload Payload = Builder.Done();
	if (!Communicate(Payload))
	{
		return nullptr;
	}

	uint32 SenderPort = Response.GetInteger("port", 0);
	if (!SenderPort)
	{
		return nullptr;
	}

	asio::ip::address ServerAddr = Socket.local_endpoint().address();
	asio::ip::tcp::endpoint Endpoint(ServerAddr, uint16(SenderPort));

	asio::error_code ErrorCode;
	asio::ip::tcp::socket SenderSocket(IoContext);
	SenderSocket.connect(Endpoint, ErrorCode);
	if (ErrorCode)
	{
		return nullptr;
	}

	return new asio::ip::tcp::socket(MoveTemp(SenderSocket));
}

} // namespace Trace

#endif // TRACE_WITH_ASIO



#if !TRACE_WITH_ASIO
struct FResponse
{
	int64		GetInteger(const char*, int64 Default) const		{ return Default; }
	const char* GetString(const char*, const char* Default) const	{ return Default; }
};
#endif // !TRACE_WITH_ASIO



namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
uint32 FStoreClient::FStatus::GetRecorderPort() const
{
	const auto* Response = (const FResponse*)this;
	return Response->GetInteger("recorder_port", 0);
}



////////////////////////////////////////////////////////////////////////////////
uint32 FStoreClient::FTraceInfo::GetId() const
{
	const auto* Response = (const FResponse*)this;
	return Response->GetInteger("id", 0);
}

////////////////////////////////////////////////////////////////////////////////
uint64 FStoreClient::FTraceInfo::GetSize() const
{
	const auto* Response = (const FResponse*)this;
	return Response->GetInteger("size", 0);
}



////////////////////////////////////////////////////////////////////////////////
FStoreClient::FTraceData::~FTraceData()
{
#if TRACE_WITH_ASIO
	if (auto* Inner = (asio::ip::tcp::socket*)Handle)
	{
		Inner->close();
		delete Inner;
	}
#endif // TRACE_WITH_ASIO
}

////////////////////////////////////////////////////////////////////////////////
int32 FStoreClient::FTraceData::Read(void* Dest, uint32 DestSize)
{
#if TRACE_WITH_ASIO
	auto* Inner = (asio::ip::tcp::socket*)Handle;
	if (Inner == nullptr)
	{
		return -1;
	}

	asio::error_code ErrorCode;
	size_t BytesRead = Inner->read_some(asio::buffer(Dest, DestSize), ErrorCode);
	if (ErrorCode)
	{
		Inner->close();
		return -1;
	}

	return int32(BytesRead);
#else
	return -1;
#endif // TRACE_WITH_ASIO
}

////////////////////////////////////////////////////////////////////////////////
bool FStoreClient::FTraceData::IsValid() const
{
	return (Handle != 0);
}



////////////////////////////////////////////////////////////////////////////////
struct FStoreClient::FImpl
#if TRACE_WITH_ASIO
	: public FStoreCborClient
#endif
{
};

////////////////////////////////////////////////////////////////////////////////
FStoreClient* FStoreClient::Connect(const TCHAR* Host, uint32 Port)
{
	FImpl* Impl = new FStoreClient::FImpl();
#if TRACE_WITH_ASIO
	if (!Impl->Connect(Host, Port))
#endif
	{
		delete Impl;
		return nullptr;
	}

	FStoreClient* Client = new FStoreClient();
	Client->Impl = Impl;
	return Client;
}

////////////////////////////////////////////////////////////////////////////////
FStoreClient::~FStoreClient()
{
	delete Impl;
}

////////////////////////////////////////////////////////////////////////////////
bool FStoreClient::IsValid() const
{
	return Impl != nullptr;
}

////////////////////////////////////////////////////////////////////////////////
const FStoreClient::FStatus* FStoreClient::GetStatus()
{
#if TRACE_WITH_ASIO
	if (!Impl->GetStatus())
	{
		return nullptr;
	}

	const FResponse& Response = Impl->GetResponse();
	return (FStatus*)(&Response);
#else
	return nullptr;
#endif // TRACE_WITH_ASIO
}

////////////////////////////////////////////////////////////////////////////////
uint32 FStoreClient::GetTraceCount()
{
#if TRACE_WITH_ASIO
	if (!Impl->GetTraceCount())
	{
		return 0;
	}

	return Impl->GetResponse().GetInteger("count", 0);
#else
	return 0;
#endif // TRACE_WITH_ASIO
}

////////////////////////////////////////////////////////////////////////////////
const FStoreClient::FTraceInfo* FStoreClient::GetTraceInfo(uint32 Index)
{
#if TRACE_WITH_ASIO
	if (!Impl->GetTraceInfo(Index))
	{
		return nullptr;
	}

	const FResponse& Response = Impl->GetResponse();
	return (FTraceInfo*)(&Response);
#else
	return nullptr;
#endif // TRACE_WITH_ASIO
}

////////////////////////////////////////////////////////////////////////////////
bool FStoreClient::ReadTrace(uint32 Id, FTraceData& Out)
{
#if TRACE_WITH_ASIO
	Out.Handle = UPTRINT(Impl->ReadTrace(Id));
	return (Out.Handle != 0);
#else
	return false;
#endif // TRACE_WITH_ASIO
}

} // namespace Trace
