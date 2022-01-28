// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookOnTheFly.h"
#include "HAL/PlatformMisc.h"
#include "Async/Async.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "MultichannelTcpSocket.h"
#include "NetworkMessage.h"
#include "Sockets.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Serialization/MemoryReader.h"

DEFINE_LOG_CATEGORY_STATIC(LogCotfServerConnection, Log, All);

static TArray<TSharedPtr<FInternetAddr>> GetAddressFromString(ISocketSubsystem& SocketSubsystem, TArrayView<const FString> HostAddresses, const int32 Port)
{
	TArray<TSharedPtr<FInternetAddr>> InterntAddresses;

	for (const FString& HostAddr : HostAddresses)
	{
		TSharedPtr<FInternetAddr> Addr = SocketSubsystem.GetAddressFromString(HostAddr);

		if (!Addr.IsValid() || !Addr->IsValid())
		{
			FAddressInfoResult GAIRequest = SocketSubsystem.GetAddressInfo(*HostAddr, nullptr, EAddressInfoFlags::Default, NAME_None);
			if (GAIRequest.ReturnCode == SE_NO_ERROR && GAIRequest.Results.Num() > 0)
			{
				Addr = GAIRequest.Results[0].Address;
			}
		}

		if (Addr.IsValid() && Addr->IsValid())
		{
			Addr->SetPort(Port);
			InterntAddresses.Emplace(MoveTemp(Addr));
		}
	}

	return InterntAddresses;
}

class FCookOnTheFlyServerConnection final
	: public UE::Cook::ICookOnTheFlyServerConnection
{
public:
	FCookOnTheFlyServerConnection()
	{ }

	~FCookOnTheFlyServerConnection()
	{
		Disconnect();
	}

	bool Connect(const UE::Cook::FCookOnTheFlyHostOptions& HostOptions)
	{
		check(HostOptions.Hosts.Num());

		const int32 Port = HostOptions.Port > 0 ? HostOptions.Port : UE::Cook::DefaultCookOnTheFlyServingPort;

		ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get();
		TArray<TSharedPtr<FInternetAddr>> HostAddresses = GetAddressFromString(SocketSubsystem, HostOptions.Hosts, Port);

		if (!HostAddresses.Num())
		{
			UE_LOG(LogCotfServerConnection, Error, TEXT("No valid COTF server address found"));
			return false;
		}

		bool bConnected = false;
		const double ServerWaitEndTime = FPlatformTime::Seconds() + HostOptions.ServerStartupWaitTime.GetTotalSeconds();

		for (;;)
		{
			for (const TSharedPtr<FInternetAddr>& Addr : HostAddresses)
			{
				UE_LOG(LogCotfServerConnection, Display, TEXT("Connecting to COTF server at '%s'..."), *Addr->ToString(true));
			
				Socket.Reset(SocketSubsystem.CreateSocket(NAME_Stream, TEXT("COTF-ServerConnection"), Addr->GetProtocolType()));
				if (Socket.IsValid() && Socket->Connect(*Addr))
				{
					ServerAddr = Addr;
					bConnected = true;
					break;
				}
			}

			if (bConnected || FPlatformTime::Seconds() > ServerWaitEndTime)
			{
				break;
			}

			FPlatformProcess::Sleep(1.0f);
		};

		if (!bConnected)
		{
			UE_LOG(LogCotfServerConnection, Error, TEXT("Failed to connect to COTF server"));
			return false;
		}

		if (!SendHandshakeMessage())
		{
			UE_LOG(LogCotfServerConnection, Fatal, TEXT("Failed to handshake with COTF server at '%s'"), *ServerAddr->ToString(true));
			return false;
		}

		Thread = AsyncThread([this] { return ThreadEntry(); },  8 * 1024, TPri_Normal);

		UE_LOG(LogCotfServerConnection, Log, TEXT("Connected to COTF server at '%s'"), *ServerAddr->ToString(true));

		return true;
	}

	virtual bool IsConnected() const override
	{
		return ClientId > 0;
	}

	virtual TFuture<UE::Cook::FCookOnTheFlyResponse> SendRequest(const UE::Cook::FCookOnTheFlyRequest& Request) override
	{
		using namespace UE::Cook;

		const uint32 CorrelationId					= NextCorrelationId++;
		FCookOnTheFlyMessageHeader RequestHeader	= Request.GetHeader();

		RequestHeader.MessageType	= RequestHeader.MessageType | ECookOnTheFlyMessage::Request;
		RequestHeader.MessageStatus = ECookOnTheFlyMessageStatus::Ok;
		RequestHeader.SenderId		= ClientId;
		RequestHeader.CorrelationId = CorrelationId;
		RequestHeader.Timestamp		= FDateTime::UtcNow().GetTicks();

		FBufferArchive RequestPayload;
		RequestPayload.Reserve(Request.TotalSize());

		RequestPayload << RequestHeader;
		RequestPayload << const_cast<TArray<uint8>&>(Request.GetBody());

		FPendingRequest* PendingRequest = Alloc(CorrelationId);
		PendingRequest->RequestHeader = RequestHeader;
		
		TFuture<FCookOnTheFlyResponse> FutureResponse = PendingRequest->ResponsePromise.GetFuture();

		UE_LOG(LogCotfServerConnection, Verbose, TEXT("Sending: %s, Size='%lld'"), *RequestHeader.ToString(), Request.TotalSize());

		if (SendMessage(RequestPayload))
		{
			return FutureResponse;
		}
		else
		{
			UE_LOG(LogCotfServerConnection, Warning, TEXT("Failed to send: %s, Size='%lld'"), *RequestHeader.ToString(), Request.TotalSize());

			Free(PendingRequest);

			FCookOnTheFlyResponse ErrorResponse;
			ErrorResponse.SetStatus(ECookOnTheFlyMessageStatus::Error);

			TPromise<FCookOnTheFlyResponse> ErrorResponsePromise;
			ErrorResponsePromise.SetValue(ErrorResponse);
			return ErrorResponsePromise.GetFuture();
		}
	}

	virtual void Disconnect() override
	{
		if (IsConnected())
		{
			if (!bStopRequested.Exchange(true))
			{
				Socket->Close();
				Thread.Wait();
				Thread.Reset();
				Socket.Reset();
				ClientId  = -1;
			}
		}
	}

	DECLARE_DERIVED_EVENT(FCookOnTheFlyServerConnection, UE::Cook::ICookOnTheFlyServerConnection::FMessageEvent, FMessageEvent);
	virtual FMessageEvent& OnMessage() override
	{
		return MessageEvent;
	}

	bool SendMessage(const TArray<uint8>& Message)
	{
		if (!FNFSMessageHeader::WrapAndSendPayload(Message, FSimpleAbstractSocket_FSocket(Socket.Get())))
		{
			UE_LOG(LogCotfServerConnection, Fatal, TEXT("Failed sending payload to COTF server"));
			return false;
		}

		return true;
	}

	bool ReceiveMessage(FArrayReader& Message)
	{
		if (!FNFSMessageHeader::ReceivePayload(Message, FSimpleAbstractSocket_FSocket(Socket.Get())))
		{
			UE_LOG(LogCotfServerConnection, Warning, TEXT("Failed reveiving payload from COTF server"));
			return false;
		}

		return true;
	}

	bool SendHandshakeMessage()
	{
		using namespace UE::Cook;

		FCookOnTheFlyMessage HandshakeRequest(ECookOnTheFlyMessage::Handshake | ECookOnTheFlyMessage::Request);
		{
			TArray<FString> TargetPlatformNames;
			FPlatformMisc::GetValidTargetPlatforms(TargetPlatformNames);
			check(TargetPlatformNames.Num() > 0);
			FString PlatformName(MoveTemp(TargetPlatformNames[0]));
			FString ProjectName(FApp::GetProjectName());

			TUniquePtr<FArchive> Ar = HandshakeRequest.WriteBody();
			*Ar << PlatformName;
			*Ar << ProjectName;
		}

		FBufferArchive HandshakeRequestPayload;
		HandshakeRequestPayload << HandshakeRequest;

		if (!SendMessage(HandshakeRequestPayload))
		{
			return false;
		}

		FArrayReader HandshakeResponsePayload;
		if (!ReceiveMessage(HandshakeResponsePayload))
		{
			return false;
		}

		FCookOnTheFlyMessage HandshakeResponse;
		HandshakeResponsePayload << HandshakeResponse;
		{
			TUniquePtr<FArchive> Ar = HandshakeResponse.ReadBody();
			*Ar << ClientId;
		}

		UE_CLOG(ClientId > 0, LogCotfServerConnection, Display, TEXT("Connected to server with ID='%d'"), ClientId);

		return ClientId > 0;
	}

	void ThreadEntry()
	{
		using namespace UE::Cook;

		while (!bStopRequested.Load())
		{
			FArrayReader MessagePayload;
			if (!ReceiveMessage(MessagePayload))
			{
				UE_LOG(LogCotfServerConnection, Warning, TEXT("Failed to receive message from '%s'"), *ServerAddr->ToString(true));
				break;
			}

			FCookOnTheFlyMessageHeader MessageHeader;
			TArray<uint8> MessageBody;

			MessagePayload << MessageHeader;
			MessagePayload << MessageBody;

			UE_LOG(LogCotfServerConnection, Verbose, TEXT("Received: %s, Size='%lld'"), *MessageHeader.ToString(), MessagePayload.Num());

			const bool bIsResponse = EnumHasAnyFlags(MessageHeader.MessageType, ECookOnTheFlyMessage::Response);
			const bool bIsRequest = EnumHasAnyFlags(MessageHeader.MessageType, ECookOnTheFlyMessage::Request);
			EnumRemoveFlags(MessageHeader.MessageType, ECookOnTheFlyMessage::TypeFlags);

			if (bIsRequest)
			{
				UE_CLOG(
					MessageHeader.MessageType != ECookOnTheFlyMessage::Heartbeat,
					LogCotfServerConnection, Fatal, TEXT("Invalid server request message '%s'"), LexToString(MessageHeader.MessageType));

				FCookOnTheFlyMessage HeartbeatResponse(ECookOnTheFlyMessage::Heartbeat | ECookOnTheFlyMessage::Response);
				FCookOnTheFlyMessageHeader& ResponseHeader = HeartbeatResponse.GetHeader();

				ResponseHeader.MessageStatus	= ECookOnTheFlyMessageStatus::Ok;
				ResponseHeader.SenderId			= ClientId;
				ResponseHeader.CorrelationId	= MessageHeader.CorrelationId;
				ResponseHeader.Timestamp		= FDateTime::UtcNow().GetTicks();
				
				FBufferArchive ResponsePayload;
				ResponsePayload << HeartbeatResponse;

				UE_LOG(LogCotfServerConnection, Warning, TEXT("Sending heartbeat response to '%s'"), *ServerAddr->ToString(true));

				if (!SendMessage(ResponsePayload))
				{
					UE_LOG(LogCotfServerConnection, Warning, TEXT("Failed to send heartbeat response to '%s'"), *ServerAddr->ToString(true));
					break;
				}
			}
			else if (bIsResponse)
			{
				FPendingRequest* PendingRequest = GetRequest(MessageHeader.CorrelationId);
				check(PendingRequest);
				check(PendingRequest->RequestHeader.CorrelationId == MessageHeader.CorrelationId);

				FCookOnTheFlyResponse Response;
				Response.SetHeader(MessageHeader);
				Response.SetBody(MoveTemp(MessageBody));

				PendingRequest->ResponsePromise.SetValue(MoveTemp(Response));

				Free(PendingRequest);
			}
			else
			{
				FCookOnTheFlyMessage Message;
				Message.SetHeader(MessageHeader);
				Message.SetBody(MoveTemp(MessageBody));
				
				if (MessageEvent.IsBound())
				{
					MessageEvent.Broadcast(Message);
				}
			}
		}

		UE_LOG(LogCotfServerConnection, Display, TEXT("Terminating connection to server '%s'"), *ServerAddr->ToString(true));
	}

	struct FPendingRequest
	{
		UE::Cook::FCookOnTheFlyMessageHeader RequestHeader;
		TPromise<UE::Cook::FCookOnTheFlyResponse> ResponsePromise;
	};

	FPendingRequest* Alloc(uint32 CorrelationId)
	{
		FScopeLock _(&RequestsCriticalSection);
		TUniquePtr<FPendingRequest>& NewPendingRequest = PendingRequests.Add(CorrelationId, MakeUnique<FPendingRequest>());
		return NewPendingRequest.Get();
	}

	void Free(FPendingRequest* PendingRequest)
	{
		FScopeLock _(&RequestsCriticalSection);
		PendingRequests.Remove(PendingRequest->RequestHeader.CorrelationId);
	}
	
	FPendingRequest* GetRequest(uint32 CorrelationId)
	{
		FScopeLock _(&RequestsCriticalSection);
		
		if (TUniquePtr<FPendingRequest>* PendingRequest = PendingRequests.Find(CorrelationId))
		{
			return PendingRequest->Get();
		}

		return nullptr;
	}

	FMessageEvent MessageEvent;
	TSharedPtr<FInternetAddr> ServerAddr;
	TUniquePtr<FSocket> Socket;
	uint32 ClientId = 0;
	TFuture<void> Thread;
	TAtomic<bool> bStopRequested { false };

	FCriticalSection RequestsCriticalSection;
	TMap<uint32, TUniquePtr<FPendingRequest>> PendingRequests;
	TAtomic<uint32> NextCorrelationId { 1 };
};

namespace UE { namespace Cook
{

TUniquePtr<ICookOnTheFlyServerConnection> MakeServerConnection(const UE::Cook::FCookOnTheFlyHostOptions& HostOptions)
{
	TUniquePtr<FCookOnTheFlyServerConnection> Connection = MakeUnique<FCookOnTheFlyServerConnection>();
	if (!Connection->Connect(HostOptions))
	{
		Connection.Release();
	}
	return Connection;
}

}} // namespace UE::Cook
