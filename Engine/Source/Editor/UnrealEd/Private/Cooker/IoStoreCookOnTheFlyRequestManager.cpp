// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreCookOnTheFlyRequestManager.h"
#include "CookOnTheFlyServerInterface.h"
#include "CookOnTheFly.h"
#include "CookOnTheFlyMessages.h"
#include "Modules/ModuleManager.h"
#include "PackageStoreWriter.h"
#include "ShaderCompiler.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Templates/Function.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Async/Async.h"
#include "Serialization/BufferArchive.h"
#include "NetworkMessage.h"
#include "Engine/Engine.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "Cooker/ExternalCookOnTheFlyServer.h"

class FIoStoreCookOnTheFlyNetworkServer
{
	static constexpr uint32 ServerSenderId = ~uint32(0);
	static constexpr double HeartbeatTimeoutInSeconds = 60 * 5;

public:
	/**
	 * Connection status
	 */
	enum class EConnectionStatus
	{
		Disconnected,
		Connected
	};

	using FRequestHandler = TFunction<bool(const FName&, const UE::Cook::FCookOnTheFlyRequest&, UE::Cook::FCookOnTheFlyResponse&)>;

	using FClientConnectionHandler = TFunction<bool(const FName&, EConnectionStatus)>;

	using FFillRequest = TFunction<void(FArchive&)>;

	using FProcessResponse = TFunction<bool(FArchive&)>;

	/**
	 * Cook-on-the-fly connection server options.
	 */
	struct FServerOptions
	{
		/** The port to listen for new connections. */
		int32 Port = UE::Cook::DefaultCookOnTheFlyServingPort;

		/** Callback invoked when a client has connected or disconnected. */
		FClientConnectionHandler HandleClientConnection;

		/** Callback invoked when the server receives a new request. */
		FRequestHandler HandleRequest;
	};

	FIoStoreCookOnTheFlyNetworkServer(FServerOptions InOptions)
		: Options(MoveTemp(InOptions))
		, ServiceId(FExternalCookOnTheFlyServer::GenerateServiceId())
	{
		MessageEndpoint = FMessageEndpoint::Builder("FCookOnTheFly");
	}

	~FIoStoreCookOnTheFlyNetworkServer()
	{
		StopServer();
	}

	bool StartServer()
	{
		const int32 Port = Options.Port > 0 ? Options.Port : UE::Cook::DefaultCookOnTheFlyServingPort;
		UE_LOG(LogCookOnTheFly, Log, TEXT("Starting COTF server on port '%d'"), Port);

		check(!bIsRunning);
		bIsRunning = false;
		bStopRequested = false;

		ISocketSubsystem& SocketSubsystem = *ISocketSubsystem::Get();

		ListenAddr = SocketSubsystem.GetLocalBindAddr(*GLog);
		ListenAddr->SetPort(Port);

		// create a server TCP socket
		Socket = SocketSubsystem.CreateSocket(NAME_Stream, TEXT("COTF-Server"), ListenAddr->GetProtocolType());

		if (!Socket)
		{
			UE_LOG(LogCookOnTheFly, Error, TEXT("Could not create listen socket"));
			return false;
		}

		Socket->SetReuseAddr();
		Socket->SetNoDelay();

		if (!Socket->Bind(*ListenAddr))
		{
			UE_LOG(LogCookOnTheFly, Error, TEXT("Failed to bind socket to address '%s'"), *ListenAddr->ToString(true));
			return false;
		}

		if (!Socket->Listen(16))
		{
			UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to listen on address '%s'"), *ListenAddr->ToString(true));
			return false;
		}

		ListenAddr->SetPort(Socket->GetPortNo());

		ServerThread = AsyncThread([this] { return ServerThreadEntry(); }, 8 * 1024, TPri_AboveNormal);

		UE_LOG(LogCookOnTheFly, Display, TEXT("COTF server is ready for client(s) on '%s'!"), *ListenAddr->ToString(true));

		if (MessageEndpoint.IsValid())
		{
			FZenCookOnTheFlyRegisterServiceMessage* RegisterServiceMessage = FMessageEndpoint::MakeMessage<FZenCookOnTheFlyRegisterServiceMessage>();
			RegisterServiceMessage->ServiceId = ServiceId;
			RegisterServiceMessage->Port = ListenAddr->GetPort();
			MessageEndpoint->Publish(RegisterServiceMessage);
		}

		return true;
	}

	void StopServer()
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

	bool BroadcastMessage(const UE::Cook::FCookOnTheFlyMessage& Message, const FName& PlatformName = NAME_None)
	{
		using namespace UE::Cook;

		FCookOnTheFlyMessageHeader Header = Message.GetHeader();

		Header.MessageType = Header.MessageType | ECookOnTheFlyMessage::Message;
		Header.MessageStatus = ECookOnTheFlyMessageStatus::Ok;
		Header.SenderId = ServerSenderId;
		Header.CorrelationId = NextCorrelationId++;
		Header.Timestamp = FDateTime::UtcNow().GetTicks();

		FBufferArchive MessagePayload;
		MessagePayload.Reserve(Message.TotalSize());

		MessagePayload << Header;
		MessagePayload << const_cast<TArray<uint8>&>(Message.GetBody());

		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Sending: %s, Size='%lld'"), *Header.ToString(), Message.TotalSize());

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
				UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to send message '%s' to client '%s' (Id='%d', Platform='%s')"),
					LexToString(Message.GetHeader().MessageType), *Client->PeerAddr->ToString(true), Client->ClientId, *Client->PlatformName.ToString());

				Client->bIsRunning = false;
				bBroadcasted = false;
			}

			Client->LastActivityTime = FPlatformTime::Seconds();
		}

		return bBroadcasted;
	}

private:
	struct FClient
	{
		FSocket* Socket = nullptr;
		TSharedPtr<FInternetAddr> Addr;
		TSharedPtr<FInternetAddr> PeerAddr;
		TFuture<void> Thread;
		TAtomic<bool> bIsRunning{ false };
		TAtomic<bool> bStopRequested{ false };
		TAtomic<bool> bIsProcessingRequest{ false };
		TAtomic<double> LastActivityTime{ 0 };
		uint32 ClientId = 0;
		FName PlatformName;
	};

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

						UE_LOG(LogCookOnTheFly, Display, TEXT("New client connected from address '%s' (ID='%d')"), *Client->PeerAddr->ToString(true), Client->ClientId);
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

						UE_LOG(LogCookOnTheFly, Display, TEXT("Sending hearbeat message, ClientId='%d', Platform='%s', Address='%s', IdleTime='%.2llf's"),
							Client->ClientId, *Client->PlatformName.ToString(), *Client->PeerAddr->ToString(true), HeartbeatTimeoutInSeconds);

						FCookOnTheFlyMessage HeartbeatRequest(ECookOnTheFlyMessage::Heartbeat | ECookOnTheFlyMessage::Request);
						FCookOnTheFlyMessageHeader& Header = HeartbeatRequest.GetHeader();

						Header.MessageStatus = ECookOnTheFlyMessageStatus::Ok;
						Header.SenderId = ServerSenderId;
						Header.CorrelationId = Client->ClientId;
						Header.Timestamp = FDateTime::UtcNow().GetTicks();

						FBufferArchive RequestPayload;
						RequestPayload.Reserve(HeartbeatRequest.TotalSize());
						RequestPayload << HeartbeatRequest;

						if (!FNFSMessageHeader::WrapAndSendPayload(RequestPayload, FSimpleAbstractSocket_FSocket(Client->Socket)))
						{
							Client->bIsRunning = false;
							UE_LOG(LogCookOnTheFly, Display, TEXT("Heartbeat [Failed]"));
						}
					}

					if (!Client->bIsRunning)
					{
						UE_LOG(LogCookOnTheFly, Display, TEXT("Closing connection to client on address '%s' (Id='%d', Platform='%s')"),
							*Client->PeerAddr->ToString(true), Client->ClientId, *Client->PlatformName.ToString());

						Options.HandleClientConnection(Client->PlatformName, EConnectionStatus::Disconnected);

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
			UE_LOG(LogCookOnTheFly, Warning, TEXT("Unable to receive request from client"));
			return false;
		}

		Client.bIsProcessingRequest = true;

		FCookOnTheFlyRequest Request;
		RequestPayload << Request;

		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Received: %s, Size='%lld'"), *Request.GetHeader().ToString(), Request.TotalSize());

		EnumRemoveFlags(Request.GetHeader().MessageType, ECookOnTheFlyMessage::TypeFlags);

		FCookOnTheFlyResponse Response;
		bool bRequestOk = false;
		bool bIsResponse = false;

		switch (Request.GetHeader().MessageType)
		{
		case ECookOnTheFlyMessage::Handshake:
		{
			ProcessHandshake(Client, Request, Response);
			bRequestOk = Options.HandleClientConnection(Client.PlatformName, EConnectionStatus::Connected);
			break;
		}
		case ECookOnTheFlyMessage::Heartbeat:
		{
			const bool bHeartbeatOk = Request.GetHeader().CorrelationId == Client.ClientId;

			UE_LOG(LogCookOnTheFly, Display, TEXT("Heartbeat [%s], ClientId='%d', Platform='%s', Address='%s'"),
				bHeartbeatOk ? TEXT("Ok") : TEXT("Failed"), Client.ClientId, *Client.PlatformName.ToString(), *Client.PeerAddr->ToString(true));

			bRequestOk = bHeartbeatOk;
			bIsResponse = true;
			break;
		}
		default:
		{
			bRequestOk = Options.HandleRequest(Client.PlatformName, Request, Response);
			break;
		}
		}

		if (bRequestOk && !bIsResponse)
		{
			FCookOnTheFlyMessageHeader& ResponseHeader = Response.GetHeader();

			ResponseHeader.MessageType = Request.GetHeader().MessageType | ECookOnTheFlyMessage::Response;
			ResponseHeader.SenderId = ServerSenderId;
			ResponseHeader.CorrelationId = Request.GetHeader().CorrelationId;
			ResponseHeader.Timestamp = FDateTime::UtcNow().GetTicks();

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
		}
		Response.SetBodyTo(Client.ClientId);
		Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Ok);
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

	FIoStoreCookOnTheFlyNetworkServer::FServerOptions Options;
	TFuture<void> ServerThread;
	TSharedPtr<FInternetAddr> ListenAddr;
	FSocket* Socket = nullptr;
	FCriticalSection ClintsCriticalSection;
	TArray<TUniquePtr<FClient>> Clients;
	TAtomic<bool> bIsRunning{ false };
	TAtomic<bool> bStopRequested{ false };
	uint32 NextClientId = 1;
	TAtomic<uint32> NextCorrelationId{ 1 };
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;
	const FString ServiceId;
};

class FIoStoreCookOnTheFlyRequestManager final
	: public UE::Cook::ICookOnTheFlyRequestManager
{
public:
	FIoStoreCookOnTheFlyRequestManager(UE::Cook::ICookOnTheFlyServer& InCookOnTheFlyServer, const IAssetRegistry* AssetRegistry, UE::Cook::FIoStoreCookOnTheFlyServerOptions InOptions)
		: CookOnTheFlyServer(InCookOnTheFlyServer)
		, Options(MoveTemp(InOptions))
	{
		TArray<FAssetData> AllAssets;
		AssetRegistry->GetAllAssets(AllAssets, true);
		for (const FAssetData& AssetData : AllAssets)
		{
			AllKnownPackagesMap.Add(FPackageId::FromName(AssetData.PackageName), AssetData.PackageName);
		}
	}

private:
	class FPlatformContext
	{
	public:
		enum class EPackageStatus
		{
			None,
			Cooking,
			Cooked,
			Failed,
		};

		struct FPackage
		{
			EPackageStatus Status = EPackageStatus::None;
			FPackageStoreEntryResource Entry;
		};

		FPlatformContext(FName InPlatformName, IPackageStoreWriter* InPackageWriter)
			: PlatformName(InPlatformName)
			, PackageWriter(InPackageWriter)
		{
		}

		FCriticalSection& GetLock()
		{
			return CriticalSection;
		}

		void GetCompletedPackages(UE::ZenCookOnTheFly::Messaging::FCompletedPackages& OutCompletedPackages)
		{
			OutCompletedPackages.CookedPackages.Reserve(OutCompletedPackages.CookedPackages.Num() + Packages.Num());
			for (const auto& KV : Packages)
			{
				if (KV.Value->Status == EPackageStatus::Cooked)
				{
					OutCompletedPackages.CookedPackages.Add(KV.Value->Entry);
				}
				else if (KV.Value->Status == EPackageStatus::Failed)
				{
					OutCompletedPackages.FailedPackages.Add(KV.Key);
				}
			}
		}

		EPackageStoreEntryStatus RequestCook(UE::Cook::ICookOnTheFlyServer& InCookOnTheFlyServer, const FPackageId& PackageId, TFunctionRef<FName()> GetPackageName, FPackageStoreEntryResource& OutEntry)
		{
			FPackage& Package = GetPackage(PackageId);
			if (Package.Status == EPackageStatus::Cooked)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("0x%llX was already cooked"), PackageId.ValueForDebugging());
				OutEntry = Package.Entry;
				return EPackageStoreEntryStatus::Ok;
			}
			else if (Package.Status == EPackageStatus::Failed)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("0x%llX was already failed"), PackageId.ValueForDebugging());
				return EPackageStoreEntryStatus::Missing;
			}
			else if (Package.Status == EPackageStatus::Cooking)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("0x%llX was already cooking"), PackageId.ValueForDebugging());
				return EPackageStoreEntryStatus::Pending;
			}
			FName PackageName = GetPackageName();
			if (PackageName.IsNone())
			{
				UE_LOG(LogCookOnTheFly, Warning, TEXT("Received cook request for unknown package 0x%llX"), PackageId.ValueForDebugging());
				return EPackageStoreEntryStatus::Missing;
			}
			FString Filename;
			if (FPackageName::TryConvertLongPackageNameToFilename(PackageName.ToString(), Filename))
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("Cooking package 0x%llX '%s'"), PackageId.ValueForDebugging(), *PackageName.ToString());
				Package.Status = EPackageStatus::Cooking;
				const bool bEnqueued = InCookOnTheFlyServer.EnqueueCookRequest(UE::Cook::FCookPackageRequest{ PlatformName, Filename });
				check(bEnqueued);
				return EPackageStoreEntryStatus::Pending;
			}
			else
			{
				UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to cook package 0x%llX '%s' (File not found)"), PackageId.ValueForDebugging(), *PackageName.ToString());
				Package.Status = EPackageStatus::Failed;
				return EPackageStoreEntryStatus::Missing;
			}
		}

		void RequestRecook(UE::Cook::ICookOnTheFlyServer& InCookOnTheFlyServer, const FPackageId& PackageId, const FName& PackageName)
		{
			FPackage& Package = GetPackage(PackageId);
			if (Package.Status != EPackageStatus::Cooked && Package.Status != EPackageStatus::Failed)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("Skipping recook of package 0x%llX '%s' that was not cooked"), PackageId.ValueForDebugging(), *PackageName.ToString());
				return;
			}
			FString Filename;
			if (FPackageName::TryConvertLongPackageNameToFilename(PackageName.ToString(), Filename))
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("Recooking package 0x%llX '%s'"), PackageId.ValueForDebugging(), *PackageName.ToString());
				Package.Status = EPackageStatus::Cooking;
				const bool bEnqueued = InCookOnTheFlyServer.EnqueueCookRequest(UE::Cook::FCookPackageRequest{ PlatformName, Filename });
				check(bEnqueued);
			}
			else
			{
				UE_LOG(LogCookOnTheFly, Warning, TEXT("Failed to recook package 0x%llX '%s' (File not found)"), PackageId.ValueForDebugging(), *PackageName.ToString());
				Package.Status = EPackageStatus::Failed;
			}
		}

		void MarkAsFailed(FPackageId PackageId, UE::ZenCookOnTheFly::Messaging::FCompletedPackages& OutCompletedPackages)
		{
			UE_LOG(LogCookOnTheFly, Warning, TEXT("0x%llX failed"), PackageId.ValueForDebugging());
			FPackage& Package = GetPackage(PackageId);
			Package.Status = EPackageStatus::Failed;
			OutCompletedPackages.FailedPackages.Add(PackageId);
		}

		void MarkAsCooked(FPackageId PackageId, const FPackageStoreEntryResource& Entry, UE::ZenCookOnTheFly::Messaging::FCompletedPackages& OutCompletedPackages)
		{
			UE_LOG(LogCookOnTheFly, Verbose, TEXT("0x%llX cooked"), PackageId.ValueForDebugging());
			FPackage& Package = GetPackage(PackageId);
			Package.Status = EPackageStatus::Cooked;
			Package.Entry = Entry;
			OutCompletedPackages.CookedPackages.Add(Entry);
		}

		void AddExistingPackages(TArrayView<const FPackageStoreEntryResource> Entries, TArrayView<const IPackageStoreWriter::FOplogCookInfo> CookInfos)
		{
			Packages.Reserve(Entries.Num());

			for (int32 EntryIndex = 0, Num = Entries.Num(); EntryIndex < Num; ++EntryIndex)
			{
				const FPackageStoreEntryResource& Entry = Entries[EntryIndex];
				const IPackageStoreWriter::FOplogCookInfo& CookInfo = CookInfos[EntryIndex];
				const FPackageId PackageId = Entry.GetPackageId();

				FPackage& Package = GetPackage(PackageId);

				if (CookInfo.bUpToDate)
				{
					Package.Status = EPackageStatus::Cooked;
					Package.Entry = Entry;
				}
				else
				{
					Package.Status = EPackageStatus::None;
				}
			}
		}

		FPackage& GetPackage(FPackageId PackageId)
		{
			TUniquePtr<FPackage>& Package = Packages.FindOrAdd(PackageId, MakeUnique<FPackage>());
			check(Package.IsValid());
			return *Package;
		}

	private:
		FCriticalSection CriticalSection;
		FName PlatformName;
		TMap<FPackageId, TUniquePtr<FPackage>> Packages;
		IPackageStoreWriter* PackageWriter = nullptr;
	};

	virtual bool Initialize() override
	{
		using namespace UE::Cook;

		ICookOnTheFlyModule& CookOnTheFlyModule = FModuleManager::LoadModuleChecked<ICookOnTheFlyModule>(TEXT("CookOnTheFly"));

		const int32 Port = Options.Port > 0 ? Options.Port : UE::Cook::DefaultCookOnTheFlyServingPort;

		ConnectionServer = MakeUnique<FIoStoreCookOnTheFlyNetworkServer>(FIoStoreCookOnTheFlyNetworkServer::FServerOptions
		{
			Port,
			[this](const FName& PlatformName, FIoStoreCookOnTheFlyNetworkServer::EConnectionStatus ConnectionStatus)
			{
				return HandleClientConnection(PlatformName, ConnectionStatus);
			},
			[this](const FName& PlatformName, const FCookOnTheFlyRequest& Request, FCookOnTheFlyResponse& Response)
			{ 
				return HandleClientRequest(PlatformName, Request, Response);
			}
		});

		const bool bServerStarted = ConnectionServer->StartServer();

		return bServerStarted;
	}

	virtual void Shutdown() override
	{
		ConnectionServer->StopServer();
		ConnectionServer.Reset();
	}

	virtual void OnPackageGenerated(const FName& PackageName)
	{
		FPackageId PackageId = FPackageId::FromName(PackageName);
		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Package 0x%llX '%s' generated"), PackageId.ValueForDebugging(), *PackageName.ToString());

		FScopeLock _(&AllKnownPackagesCriticalSection);
		AllKnownPackagesMap.Add(PackageId, PackageName);
	}

	void TickRecookPackages()
	{
		TArray<FPackageId, TInlineAllocator<128>> PackageIds;
		{
			FScopeLock _(&PackagesToRecookCritical);
			if (PackagesToRecook.IsEmpty())
			{
				return;
			}
			PackageIds = PackagesToRecook.Array();
			PackagesToRecook.Empty();
		}

		TArray<FName, TInlineAllocator<128>> PackageNames;
		PackageNames.Reserve(PackageIds.Num());

		CollectGarbage(RF_NoFlags);

		for (FPackageId PackageId : PackageIds)
		{
			FName PackageName = AllKnownPackagesMap.FindRef(PackageId);
			if (!PackageName.IsNone())
			{
				PackageNames.Add(PackageName);

				UPackage* Package = FindObjectFast<UPackage>(nullptr, PackageName);
				if (Package)
				{
					UE_LOG(LogCookOnTheFly, Warning, TEXT("Can't recook package '%s'"), *PackageName.ToString());
					UEngine::FindAndPrintStaleReferencesToObject(Package, EPrintStaleReferencesOptions::Display);
				}
				else
				{
					UE_LOG(LogCookOnTheFly, Verbose, TEXT("Recooking package '%s'"), *PackageName.ToString());
				}
			}
		}

		for (const FName& PackageName : PackageNames)
		{
			CookOnTheFlyServer.MarkPackageDirty(PackageName);
		}

		ForEachContext([this, &PackageIds, &PackageNames](FPlatformContext& Context)
			{
				FScopeLock _(&Context.GetLock());
				for (int32 PackageIndex = 0; PackageIndex < PackageNames.Num(); ++PackageIndex)
				{
					const FPackageId& PackageId = PackageIds[PackageIndex];
					const FName& PackageName = PackageNames[PackageIndex];
					if (!PackageName.IsNone())
					{
						Context.RequestRecook(CookOnTheFlyServer, PackageId, PackageName);
					}
				}
				return true;
			});
	}

	virtual void Tick() override
	{
		TickRecookPackages();
	}

	virtual bool ShouldUseLegacyScheduling() override
	{
		return false;
	}

private:
	bool HandleClientConnection(const FName& PlatformName, FIoStoreCookOnTheFlyNetworkServer::EConnectionStatus ConnectionStatus)
	{
		FScopeLock _(&ContextsCriticalSection);

		if (PlatformName.IsNone())
		{
			return true;
		}

		if (ConnectionStatus == FIoStoreCookOnTheFlyNetworkServer::EConnectionStatus::Connected)
		{
			const ITargetPlatform* TargetPlatform = CookOnTheFlyServer.AddPlatform(PlatformName);
			if (TargetPlatform)
			{
				if (!PlatformContexts.Contains(PlatformName))
				{
					IPackageStoreWriter* PackageWriter = CookOnTheFlyServer.GetPackageWriter(TargetPlatform).AsPackageStoreWriter();
					check(PackageWriter); // This class should not be used except when COTFS is using an IPackageStoreWriter
					TUniquePtr<FPlatformContext>& Context = PlatformContexts.Add(PlatformName, MakeUnique<FPlatformContext>(PlatformName, PackageWriter));
					
					PackageWriter->GetEntries([&Context](TArrayView<const FPackageStoreEntryResource> Entries,
						TArrayView<const IPackageStoreWriter::FOplogCookInfo> CookInfos)
					{
						Context->AddExistingPackages(Entries, CookInfos);
					});

					PackageWriter->OnEntryCreated().AddRaw(this, &FIoStoreCookOnTheFlyRequestManager::OnPackageStoreEntryCreated);
					PackageWriter->OnCommit().AddRaw(this, &FIoStoreCookOnTheFlyRequestManager::OnPackageCooked);
					PackageWriter->OnMarkUpToDate().AddRaw(this, &FIoStoreCookOnTheFlyRequestManager::OnPackagesMarkedUpToDate);
				}

				return true;
			}

			return false;
		}
		else
		{
			CookOnTheFlyServer.RemovePlatform(PlatformName);
			return true;
		}
	}

	bool HandleClientRequest(const FName& PlatformName, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		bool bRequestOk = false;

		const double StartTime = FPlatformTime::Seconds();

		UE_LOG(LogCookOnTheFly, Verbose, TEXT("New request, Type='%s', Client='%s'"), LexToString(Request.GetHeader().MessageType), *PlatformName.ToString());

		switch (Request.GetHeader().MessageType)
		{
			case UE::Cook::ECookOnTheFlyMessage::CookPackage:
				bRequestOk = HandleCookPackageRequest(PlatformName, Request, Response);
				break;
			case UE::Cook::ECookOnTheFlyMessage::GetCookedPackages:
				bRequestOk = HandleGetCookedPackagesRequest(PlatformName, Request, Response);
				break;
			case UE::Cook::ECookOnTheFlyMessage::RecompileShaders:
				bRequestOk = HandleRecompileShadersRequest(PlatformName, Request, Response);
				break;
			case UE::Cook::ECookOnTheFlyMessage::RecookPackages:
				bRequestOk = HandleRecookPackagesRequest(PlatformName, Request, Response);
				break;
			default:
				UE_LOG(LogCookOnTheFly, Fatal, TEXT("Unknown request, Type='%s', Client='%s'"), LexToString(Request.GetHeader().MessageType), *PlatformName.ToString());
				break;
		}

		const double Duration = FPlatformTime::Seconds() - StartTime;

		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Request handled, Type='%s', Client='%s', Status='%s', Duration='%.6lfs'"),
			LexToString(Request.GetHeader().MessageType),
			*PlatformName.ToString(),
			bRequestOk ? TEXT("Ok") : TEXT("Failed"),
			Duration);

		return bRequestOk;
	}

	bool HandleGetCookedPackagesRequest(const FName& PlatformName, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		using namespace UE::Cook;
		using namespace UE::ZenCookOnTheFly::Messaging;

		if (PlatformName.IsNone())
		{
			UE_LOG(LogCookOnTheFly, Warning, TEXT("GetCookedPackagesRequest from editor client"));
			Response.SetStatus(ECookOnTheFlyMessageStatus::Error);
			return true;
		}
		
		FCompletedPackages CompletedPackages;
		{
			FPlatformContext& Context = GetContext(PlatformName);
			FScopeLock _(&Context.GetLock());
			Context.GetCompletedPackages(CompletedPackages);
		}

		Response.SetBodyTo( MoveTemp(CompletedPackages) );
		Response.SetStatus(ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	bool HandleCookPackageRequest(const FName& PlatformName, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		using namespace UE::ZenCookOnTheFly::Messaging;

		if (PlatformName.IsNone())
		{
			UE_LOG(LogCookOnTheFly, Warning, TEXT("CookPackageRequest from editor client"));
			Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Error);
			return true;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::HandleCookPackageRequest);

		FCookPackageRequest CookRequest = Request.GetBodyAs<FCookPackageRequest>();
		UE_LOG(LogCookOnTheFly, Verbose, TEXT("Received cook request 0x%llX"), CookRequest.PackageId.ValueForDebugging());
		FPlatformContext& Context = GetContext(PlatformName);
		{
			auto GetPackageNameFunc = [this, &CookRequest]()
			{
				FScopeLock _(&AllKnownPackagesCriticalSection);
				return AllKnownPackagesMap.FindRef(CookRequest.PackageId);
			};

			FScopeLock _(&Context.GetLock());
			FPackageStoreEntryResource Entry;
			EPackageStoreEntryStatus PackageStatus = Context.RequestCook(CookOnTheFlyServer, CookRequest.PackageId, GetPackageNameFunc, Entry);
			Response.SetBodyTo(FCookPackageResponse{ PackageStatus, MoveTemp(Entry) });
		}
		Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	bool HandleRecookPackagesRequest(const FName& PlatformName, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		using namespace UE::ZenCookOnTheFly::Messaging;

		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::HandleRecookPackagesRequest);

		FRecookPackagesRequest RecookRequest = Request.GetBodyAs<FRecookPackagesRequest>();

		UE_LOG(LogCookOnTheFly, Display, TEXT("Received recook request for %d packages"), RecookRequest.PackageIds.Num());

		{
			FScopeLock _(&PackagesToRecookCritical);
			for (FPackageId PackageId : RecookRequest.PackageIds)
			{
				PackagesToRecook.Add(PackageId);
			}
		}

		Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	void OnPackageStoreEntryCreated(const IPackageStoreWriter::FEntryCreatedEventArgs& EventArgs)
	{
		FPlatformContext& Context = GetContext(EventArgs.PlatformName);

		FScopeLock _(&Context.GetLock());
		for (const FPackageId& ImportedPackageId : EventArgs.Entry.ImportedPackageIds)
		{
			FPackageStoreEntryResource DummyEntry;
			auto GetPackageNameFunc = [this, &ImportedPackageId]()
			{
				FScopeLock _(&AllKnownPackagesCriticalSection);
				return AllKnownPackagesMap.FindRef(ImportedPackageId);
			};
			EPackageStoreEntryStatus PackageStatus = Context.RequestCook(CookOnTheFlyServer, ImportedPackageId, GetPackageNameFunc, DummyEntry);
		}
	}

	void OnPackageCooked(const IPackageStoreWriter::FCommitEventArgs& EventArgs)
	{
		using namespace UE::Cook;
		using namespace UE::ZenCookOnTheFly::Messaging;

		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::OnPackageCooked);

		if (EventArgs.AdditionalFiles.Num())
		{
			TArray<FString> Filenames;
			TArray<FIoChunkId> ChunkIds;

			for (const ICookedPackageWriter::FAdditionalFileInfo& FileInfo : EventArgs.AdditionalFiles)
			{
				UE_LOG(LogCookOnTheFly, Verbose, TEXT("Sending additional cooked file '%s'"), *FileInfo.Filename);
				Filenames.Add(FileInfo.Filename);
				ChunkIds.Add(FileInfo.ChunkId);
			}

			FCookOnTheFlyMessage Message(ECookOnTheFlyMessage::FilesAdded);
			{
				TUniquePtr<FArchive> Ar = Message.WriteBody();
				*Ar << Filenames;
				*Ar << ChunkIds;
			}

			ConnectionServer->BroadcastMessage(Message, EventArgs.PlatformName);
		}

		FCompletedPackages NewCompletedPackages;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::MarkAsCooked);

			FPlatformContext& Context = GetContext(EventArgs.PlatformName);
			FScopeLock _(&Context.GetLock());

			if (EventArgs.EntryIndex >= 0)
			{
				Context.MarkAsCooked(FPackageId::FromName(EventArgs.PackageName), EventArgs.Entries[EventArgs.EntryIndex], NewCompletedPackages);
			}
			else
			{
				Context.MarkAsFailed(FPackageId::FromName(EventArgs.PackageName), NewCompletedPackages);
			}
		}
		BroadcastCompletedPackages(EventArgs.PlatformName, MoveTemp(NewCompletedPackages));
	}

	void OnPackagesMarkedUpToDate(const IPackageStoreWriter::FMarkUpToDateEventArgs& EventArgs)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::OnPackagesMarkedUpToDate);

		UE::ZenCookOnTheFly::Messaging::FCompletedPackages NewCompletedPackages;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::MarkAsCooked);

			FPlatformContext& Context = GetContext(EventArgs.PlatformName);
			FScopeLock _(&Context.GetLock());

			for (int32 EntryIndex : EventArgs.PackageIndexes)
			{
				FName PackageName = EventArgs.Entries[EntryIndex].PackageName;
				Context.MarkAsCooked(FPackageId::FromName(PackageName), EventArgs.Entries[EntryIndex], NewCompletedPackages);
			}
		}
		BroadcastCompletedPackages(EventArgs.PlatformName, MoveTemp(NewCompletedPackages));
	}

	void BroadcastCompletedPackages(FName PlatformName, UE::ZenCookOnTheFly::Messaging::FCompletedPackages&& NewCompletedPackages)
	{
		using namespace UE::Cook;
		using namespace UE::ZenCookOnTheFly::Messaging;
		if (NewCompletedPackages.CookedPackages.Num() || NewCompletedPackages.FailedPackages.Num())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::SendCookedPackagesMessage);

			UE_LOG(LogCookOnTheFly, Verbose, TEXT("Sending '%s' message, Cooked='%d', Failed='%d'"),
				LexToString(ECookOnTheFlyMessage::PackagesCooked),
				NewCompletedPackages.CookedPackages.Num(),
				NewCompletedPackages.FailedPackages.Num());

			FCookOnTheFlyMessage Message(ECookOnTheFlyMessage::PackagesCooked);
			Message.SetBodyTo<FCompletedPackages>(MoveTemp(NewCompletedPackages));
			ConnectionServer->BroadcastMessage(Message, PlatformName);
		}
	}

	bool HandleRecompileShadersRequest(const FName& PlatformName, const UE::Cook::FCookOnTheFlyRequest& Request, UE::Cook::FCookOnTheFlyResponse& Response)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CookOnTheFly::HandleRecompileShadersRequest);

		if (PlatformName.IsNone())
		{
			UE_LOG(LogCookOnTheFly, Warning, TEXT("RecompileShadersRequest from editor client"));
			Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Error);
			return true;
		}

		TArray<FString> RecompileModifiedFiles;
		TArray<uint8> MeshMaterialMaps;
		TArray<uint8> GlobalShaderMap;

		FShaderRecompileData RecompileData(PlatformName.ToString(), &RecompileModifiedFiles, &MeshMaterialMaps, &GlobalShaderMap);
		{
			int32 iShaderPlatform = static_cast<int32>(RecompileData.ShaderPlatform);
			TUniquePtr<FArchive> Ar = Request.ReadBody();
			*Ar << RecompileData.MaterialsToLoad;
			*Ar << iShaderPlatform;
			*Ar << RecompileData.CommandType;
			*Ar << RecompileData.ShadersToRecompile;
		}

		RecompileShaders(RecompileData);

		{
			TUniquePtr<FArchive> Ar = Response.WriteBody();
			*Ar << MeshMaterialMaps;
			*Ar << GlobalShaderMap;
		}

		Response.SetStatus(UE::Cook::ECookOnTheFlyMessageStatus::Ok);

		return true;
	}

	void RecompileShaders(FShaderRecompileData& RecompileData)
	{
		FEvent* RecompileCompletedEvent = FPlatformProcess::GetSynchEventFromPool();
		UE::Cook::FRecompileShaderCompletedCallback RecompileCompleted = [this, RecompileCompletedEvent]()
		{
			RecompileCompletedEvent->Trigger();
		};

		const bool bEnqueued = CookOnTheFlyServer.EnqueueRecompileShaderRequest(UE::Cook::FRecompileShaderRequest
		{
			RecompileData, 
			MoveTemp(RecompileCompleted)
		});
		check(bEnqueued);

		RecompileCompletedEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(RecompileCompletedEvent);
	}

	FPlatformContext& GetContext(const FName& PlatformName)
	{
		FScopeLock _(&ContextsCriticalSection);
		TUniquePtr<FPlatformContext>& Ctx = PlatformContexts.FindChecked(PlatformName);
		check(Ctx.IsValid());
		return *Ctx;
	}

	void ForEachContext(TFunctionRef<bool(FPlatformContext&)> Callback)
	{
		FScopeLock _(&ContextsCriticalSection);
		for (auto& KV : PlatformContexts)
		{
			TUniquePtr<FPlatformContext>& Ctx = KV.Value;
			check(Ctx.IsValid());
			if (!Callback(*Ctx))
			{
				return;
			}
		}
	}

	UE::Cook::ICookOnTheFlyServer& CookOnTheFlyServer;
	UE::Cook::FIoStoreCookOnTheFlyServerOptions Options;
	TUniquePtr<FIoStoreCookOnTheFlyNetworkServer> ConnectionServer;
	FCriticalSection ContextsCriticalSection;
	TMap<FName, TUniquePtr<FPlatformContext>> PlatformContexts;
	FCriticalSection AllKnownPackagesCriticalSection;
	TMap<FPackageId, FName> AllKnownPackagesMap;
	FCriticalSection PackagesToRecookCritical;
	TSet<FPackageId> PackagesToRecook;
};

namespace UE { namespace Cook
{

TUniquePtr<ICookOnTheFlyRequestManager> MakeIoStoreCookOnTheFlyRequestManager(ICookOnTheFlyServer& CookOnTheFlyServer, const IAssetRegistry* AssetRegistry, FIoStoreCookOnTheFlyServerOptions Options)
{
	return MakeUnique<FIoStoreCookOnTheFlyRequestManager>(CookOnTheFlyServer, AssetRegistry, Options);
}

}}

