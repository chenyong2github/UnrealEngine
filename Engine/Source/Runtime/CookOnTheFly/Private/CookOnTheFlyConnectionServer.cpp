// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookOnTheFly.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Async/Async.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "NetworkMessage.h"
#include "Serialization/BufferReader.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDeviceRedirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogCotfConnectionServer, Log, All);

class FCookOnTheFlyConnectionServer final
	: public UE::Cook::ICookOnTheFlyConnectionServer
{
	static constexpr uint32 ServerSenderId = ~uint32(0);
	static constexpr double HeartbeatTimeoutInSeconds = 60 * 5;

public:
	FCookOnTheFlyConnectionServer(UE::Cook::FCookOnTheFlyServerOptions InOptions)
		: Options(MoveTemp(InOptions)) { }

	~FCookOnTheFlyConnectionServer()
	{
		StopServer();
	}

	virtual bool StartServer() override
	{
		const int32 Port = Options.Port > 0 ? Options.Port : UE::Cook::DefaultCookOnTheFlyServingPort; 
		UE_LOG(LogCotfConnectionServer, Log, TEXT("Starting COTF server on port '%d'"), Port);

		check(!bIsRunning);
		bIsRunning = false;
		bStopRequested = false;

		ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get();

		ListenAddr = SocketSubsystem.GetLocalBindAddr(*GLog);
		ListenAddr->SetPort(Port);

		// create a server TCP socket
		Socket = SocketSubsystem.CreateSocket(NAME_Stream, TEXT("COTF-Server"), ListenAddr->GetProtocolType());

		if(!Socket)
		{
			UE_LOG(LogCotfConnectionServer, Error, TEXT("Could not create listen socket"));
			return false;
		}

		Socket->SetReuseAddr();

		if (!Socket->Bind(*ListenAddr))
		{
			UE_LOG(LogCotfConnectionServer, Error, TEXT("Failed to bind socket to addres '%s'"), *ListenAddr->ToString(true));
			return false;
		}

		if (!Socket->Listen(16))
		{
			UE_LOG(LogCotfConnectionServer, Warning, TEXT("Failed to listen on address '%s'"), *ListenAddr->ToString(true));
			return false;
		}

		ListenAddr->SetPort(Socket->GetPortNo());

		ServerThread = AsyncThread([this] { return ServerThreadEntry(); },  8 * 1024, TPri_AboveNormal);

		UE_LOG(LogCotfConnectionServer, Display, TEXT("COTF server is ready for client(s) on '%s'!"), *ListenAddr->ToString(true));

		return true;
	}

private:

	struct FClient
	{
		FSocket* Socket = nullptr;
		TSharedPtr<FInternetAddr> Addr;
		TSharedPtr<FInternetAddr> PeerAddr;
		TFuture<void> Thread;
		TAtomic<bool> bIsRunning { false };
		TAtomic<bool> bStopRequested { false };
		TAtomic<bool> bIsProcessingRequest { false };
		TAtomic<double> LastActivityTime {0};
		uint32 ClientId = 0;
		FName PlatformName;
	};

	virtual void StopServer() override
	{
		if (bIsRunning && !bStopRequested)
		{
			bStopRequested = true;
			
			ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get();

			while (bIsRunning)
			{
				FPlatformProcess::Sleep(0.25f);
			}

			for (TUniquePtr<FClient>& Client : Clients)
			{
				Client->bStopRequested = true;
				Client->Socket->Close();
				Client->Thread.Wait();
				SocketSubsystem.DestroySocket(Client->Socket);
			}

			Clients.Empty();
		}
	}

	virtual bool BroadcastMessage(const UE::Cook::FCookOnTheFlyMessage& Message, const FName& PlatformName = NAME_None) override
	{
		using namespace UE::Cook;

		FCookOnTheFlyMessageHeader Header = Message.GetHeader();

		Header.MessageType		= Header.MessageType | ECookOnTheFlyMessage::Message;
		Header.MessageStatus	= ECookOnTheFlyMessageStatus::Ok;
		Header.SenderId			= ServerSenderId;
		Header.CorrelationId	= NextCorrelationId++;
		Header.Timestamp		= FDateTime::UtcNow().GetTicks();

		FBufferArchive MessagePayload;
		MessagePayload.Reserve(Message.TotalSize());

		MessagePayload << Header;
		MessagePayload << const_cast<TArray<uint8>&>(Message.GetBody());

		UE_LOG(LogCotfConnectionServer, Verbose, TEXT("Sending: %s, Size='%lld'"), *Header.ToString(), Message.TotalSize());

		TArray<FClient*, TInlineAllocator<4>> ClientsToBroadcast;
		{
			FScopeLock _(&ClintsCriticalSection);

			for (TUniquePtr<FClient>& Client : Clients)
			{
				if (PlatformName.IsNone() || Client->PlatformName == PlatformName)
				{
					ClientsToBroadcast.Add(Client.Get());
				}
			}
		}

		bool bBroadcasted = true;
		for (FClient* Client : ClientsToBroadcast)
		{
			if (!FNFSMessageHeader::WrapAndSendPayload(MessagePayload, FSimpleAbstractSocket_FSocket(Client->Socket)))
			{
				UE_LOG(LogCotfConnectionServer, Warning, TEXT("Failed to send message '%s' to client '%s' (Id='%d', Platform='%s')"),
					LexToString(Message.GetHeader().MessageType), *Client->PeerAddr->ToString(true), Client->ClientId, *Client->PlatformName.ToString());
				
				Client->bIsRunning = false;
				bBroadcasted = false;
			}

			Client->LastActivityTime = FPlatformTime::Seconds();
		}

		return bBroadcasted;
	}

	void ServerThreadEntry()
	{
		using namespace UE::Cook;

		ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get();
		bIsRunning = true;

		while (!bStopRequested)
		{
			bool bIsReady = false;
			if (Socket->WaitForPendingConnection(bIsReady, FTimespan::FromSeconds(0.25f)))
			{
				if (bIsReady)
				{
					if (FSocket* ClientSocket = Socket->Accept(TEXT("COTF-Client")))
					{
						TUniquePtr<FClient> Client = MakeUnique<FClient>();
						Client->Socket = ClientSocket;
						Client->Addr = SocketSubsystem.CreateInternetAddr();
						Client->PeerAddr = SocketSubsystem.CreateInternetAddr();
						ClientSocket->GetAddress(*Client->Addr);
						ClientSocket->GetPeerAddress(*Client->PeerAddr);
						Client->ClientId = NextClientId++;
						Client->bIsRunning = true;
						Client->LastActivityTime = FPlatformTime::Seconds();
						Client->Thread = AsyncThread([this, ConnectedClient = Client.Get()]()
						{ 
							ClientThreadEntry(ConnectedClient);
						});

						UE_LOG(LogCotfConnectionServer, Display, TEXT("New client connected from address '%s' (ID='%d')"), *Client->PeerAddr->ToString(true), Client->ClientId);
						{
							FScopeLock _(&ClintsCriticalSection);
							Clients.Add(MoveTemp(Client));
						}
					}
				}
			}
			else
			{
				FPlatformProcess::Sleep(0.25f);
			}

			{
				FScopeLock _(&ClintsCriticalSection);
				for (auto It = Clients.CreateIterator(); It; ++It)
				{
					TUniquePtr<FClient>& Client = *It;

					const double SecondsSinceLastActivity = FPlatformTime::Seconds() - Client->LastActivityTime.Load();

					if (SecondsSinceLastActivity > HeartbeatTimeoutInSeconds && !Client->bIsProcessingRequest)
					{
						Client->LastActivityTime = FPlatformTime::Seconds();

						UE_LOG(LogCotfConnectionServer, Display, TEXT("Sending hearbeat message, ClientId='%d', Platform='%s', Address='%s', IdleTime='%.2llf's"),
							Client->ClientId, *Client->PlatformName.ToString(), *Client->PeerAddr->ToString(true), HeartbeatTimeoutInSeconds);
						
						FCookOnTheFlyMessage HeartbeatRequest(ECookOnTheFlyMessage::Heartbeat | ECookOnTheFlyMessage::Request);
						FCookOnTheFlyMessageHeader& Header = HeartbeatRequest.GetHeader();

						Header.MessageStatus	= ECookOnTheFlyMessageStatus::Ok;
						Header.SenderId			= ServerSenderId;
						Header.CorrelationId	= Client->ClientId;
						Header.Timestamp		= FDateTime::UtcNow().GetTicks();
						
						FBufferArchive RequestPayload;
						RequestPayload.Reserve(HeartbeatRequest.TotalSize());
						RequestPayload << HeartbeatRequest;

						if (!FNFSMessageHeader::WrapAndSendPayload(RequestPayload, FSimpleAbstractSocket_FSocket(Client->Socket)))
						{
							Client->bIsRunning = false;
							UE_LOG(LogCotfConnectionServer, Display, TEXT("Heartbeat [Failed]"));
						}
					}

					if (!Client->bIsRunning)
					{
						UE_LOG(LogCotfConnectionServer, Display, TEXT("Closing connection to client on address '%s' (Id='%d', Platform='%s')"),
							*Client->PeerAddr->ToString(true), Client->ClientId, *Client->PlatformName.ToString());
						
						Options.HandleClientConnection(UE::Cook::FCookOnTheFlyClient { Client->ClientId, Client->PlatformName }, UE::Cook::ECookOnTheFlyConnectionStatus::Disconnected);

						Client->Socket->Close();
						Client->Thread.Wait();
						SocketSubsystem.DestroySocket(Client->Socket);
						It.RemoveCurrent();
					}
				}
			}
		}

		bIsRunning = false;
	}

	void ClientThreadEntry(FClient* Client)
	{
		while (!bStopRequested && !Client->bStopRequested)
		{
			Client->LastActivityTime = FPlatformTime::Seconds();
			if (!ProcesseRequest(*Client))
			{
				break;
			}
		}

		Client->bIsRunning = false;
	}

	bool ProcesseRequest(FClient& Client)
	{
		using namespace UE::Cook;

		Client.bIsProcessingRequest = false;

		FArrayReader RequestPayload;
		if (!FNFSMessageHeader::ReceivePayload(RequestPayload, FSimpleAbstractSocket_FSocket(Client.Socket)))
		{
			UE_LOG(LogCotfConnectionServer, Warning, TEXT("Unable to receive request from client"));
			return false;
		}

		Client.bIsProcessingRequest = true;

		FCookOnTheFlyRequest Request;
		RequestPayload << Request;

		UE_LOG(LogCotfConnectionServer, Verbose, TEXT("Received: %s, Size='%lld'"), *Request.GetHeader().ToString(), Request.TotalSize());

		EnumRemoveFlags(Request.GetHeader().MessageType, ECookOnTheFlyMessage::TypeFlags);

		FCookOnTheFlyResponse Response;
		bool bRequestOk = false;
		bool bIsResponse = false;

		switch (Request.GetHeader().MessageType)
		{
			case ECookOnTheFlyMessage::Handshake:
			{
				ProcessHandshake(Client, Request, Response);
				bRequestOk = Options.HandleClientConnection(UE::Cook::FCookOnTheFlyClient { Client.ClientId, Client.PlatformName }, UE::Cook::ECookOnTheFlyConnectionStatus::Connected);
				break;
			}
			case ECookOnTheFlyMessage::Heartbeat:
			{
				const bool bHeartbeatOk = Request.GetHeader().CorrelationId == Client.ClientId;

				UE_LOG(LogCotfConnectionServer, Display, TEXT("Heartbeat [%s], ClientId='%d', Platform='%s', Address='%s'"),
					bHeartbeatOk ? TEXT("Ok") : TEXT("Failed"), Client.ClientId, *Client.PlatformName.ToString(), *Client.PeerAddr->ToString(true));

				bRequestOk = bHeartbeatOk;
				bIsResponse  = true;
				break;
			}
			default:
			{
				bRequestOk = Options.HandleRequest(UE::Cook::FCookOnTheFlyClient { Client.ClientId, Client.PlatformName }, Request, Response);
				break;
			}
		}

		if (bRequestOk && !bIsResponse)
		{
			FCookOnTheFlyMessageHeader& ResponseHeader = Response.GetHeader();

			ResponseHeader.MessageType		= Request.GetHeader().MessageType | ECookOnTheFlyMessage::Response;
			ResponseHeader.SenderId			= ServerSenderId;
			ResponseHeader.CorrelationId	= Request.GetHeader().CorrelationId;
			ResponseHeader.Timestamp		= FDateTime::UtcNow().GetTicks();

			Response.SetHeader(ResponseHeader);

			FBufferArchive ResponsePayload;
			ResponsePayload.Reserve(Response.TotalSize());

			ResponsePayload << Response;
			bRequestOk = FNFSMessageHeader::WrapAndSendPayload(ResponsePayload, FSimpleAbstractSocket_FSocket(Client.Socket));
		}

		return bRequestOk;
	}

	void ProcessHandshake(FClient& Client, UE::Cook::FCookOnTheFlyRequest& HandshakeRequest, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		FString PlatformName;
		FString ProjectName;
		
		{
			TUniquePtr<FArchive> Ar = HandshakeRequest.ReadBody();
			*Ar << PlatformName;
			*Ar << ProjectName;
		}

		if (PlatformName.Len())
		{
			Client.PlatformName = FName(*PlatformName);

			Response.SetBodyTo(Client.ClientId);
			Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Ok);
		}
		else
		{
			Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Error);
		}
	}

	FClient* GetClientById(uint32 ClientId)
	{
		FScopeLock _(&ClintsCriticalSection);

		for (TUniquePtr<FClient>& Client : Clients)
		{
			if (Client->ClientId == ClientId)
			{
				return Client.Get();
			}
		}

		return nullptr;
	}

	UE::Cook::FCookOnTheFlyServerOptions Options;
	TFuture<void> ServerThread;
	TSharedPtr<FInternetAddr> ListenAddr;
	FSocket* Socket = nullptr;
	FCriticalSection ClintsCriticalSection;
	TArray<TUniquePtr<FClient>> Clients;
	TAtomic<bool> bIsRunning { false };
	TAtomic<bool> bStopRequested { false };
	uint32 NextClientId = 1;
	TAtomic<uint32> NextCorrelationId { 1 };
};

namespace UE { namespace Cook
{

TUniquePtr<ICookOnTheFlyConnectionServer> MakeCookOnTheFlyConnectionServer(FCookOnTheFlyServerOptions Options)
{
	return MakeUnique<FCookOnTheFlyConnectionServer>(MoveTemp(Options));
}

}} // namespace UE::Cook
