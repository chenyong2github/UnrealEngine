// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetDriverEOS.h"
#include "NetConnectionEOS.h"
#include "SocketEOS.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"
#include "SocketSubsystemEOS.h"
#include "Misc/EngineVersionComparison.h"

bool UNetDriverEOS::IsAvailable() const
{
	// Use passthrough sockets if we are a dedicated server
	if (IsRunningDedicatedServer())
	{
		return false;
	}

	if (IOnlineSubsystem* Subsystem = Online::GetSubsystem(FindWorld(), EOS_SUBSYSTEM))
	{
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(EOS_SUBSYSTEM))
		{
			return true;
		}
	}

	return false;
}

bool UNetDriverEOS::InitBase(bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL, bool bReuseAddressAndPort, FString& Error)
{
	if (bIsPassthrough)
	{
		UE_LOG(LogSocketSubsystemEOS, Verbose, TEXT("Running as pass-through"));
		return Super::InitBase(bInitAsClient, InNotify, URL, bReuseAddressAndPort, Error);
	}

	if (!UNetDriver::InitBase(bInitAsClient, InNotify, URL, bReuseAddressAndPort, Error))
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Failed to init driver base"));
		return false;
	}

	FSocketSubsystemEOS* const SocketSubsystem = static_cast<FSocketSubsystemEOS*>(GetSocketSubsystem());
	if (!SocketSubsystem)
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Could not get socket subsystem"));
		return false;
	}

	// We don't care if our world is null, everything we uses handles it fine
	const UWorld* const MyWorld = FindWorld();

	// Get our local address (proves we're logged in)
	TSharedRef<FInternetAddr> LocalAddress = SocketSubsystem->GetLocalBindAddr(MyWorld, *GLog);
	if (!LocalAddress->IsValid())
	{
		// Not logged in?
		Error = TEXT("Could not bind local address");
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Could not bind local address"));
		return false;
	}

	// Create our socket
	SetSocketAndLocalAddress(SocketSubsystem->CreateSocket(NAME_DGram, TEXT("UE4"), NAME_None));
	if (GetSocket() == nullptr)
	{
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Could not create socket"));
		return false;
	}

	// Store our local address and set our port
	TSharedRef<FInternetAddrEOS> EOSLocalAddress = StaticCastSharedRef<FInternetAddrEOS>(LocalAddress);
	// Because some platforms remap ports, we will use the ID of the name of the net driver to be our channel
	EOSLocalAddress->SetChannel(GetTypeHash(NetDriverName.ToString()));
	// Set our net driver name so we don't accept connections across net driver types
	EOSLocalAddress->SetSocketName(NetDriverName.ToString());

	static_cast<FSocketEOS*>(GetSocket())->SetLocalAddress(*EOSLocalAddress);

	LocalAddr = LocalAddress;

	return true;
}

bool UNetDriverEOS::InitConnect(FNetworkNotify* InNotify, const FURL& ConnectURL, FString& Error)
{
	if (!bIsUsingP2PSockets || !IsAvailable() || !ConnectURL.Host.StartsWith(EOS_CONNECTION_URL_PREFIX, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogSocketSubsystemEOS, Verbose, TEXT("Connecting using IPNetDriver passthrough. ConnectUrl = (%s)"), *ConnectURL.ToString());

		bIsPassthrough = true;
		return Super::InitConnect(InNotify, ConnectURL, Error);
	}

	bool bIsValid = false;
	TSharedRef<FInternetAddrEOS> RemoteHost = MakeShared<FInternetAddrEOS>();
	RemoteHost->SetIp(*ConnectURL.Host, bIsValid);
	if (!bIsValid || ConnectURL.Port < 0)
	{
		Error = TEXT("Invalid remote address");
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Invalid Remote Address. ConnectUrl = (%s)"), *ConnectURL.ToString());
		return false;
	}

	UE_LOG(LogSocketSubsystemEOS, Verbose, TEXT("Connecting using EOSNetDriver. ConnectUrl = (%s)"), *ConnectURL.ToString());

	if (!InitBase(true, InNotify, ConnectURL, false, Error))
	{
		return false;
	}

	// Set the address to what was parsed (us + remote)
	LocalAddr = RemoteHost;

	// Reference to our newly created socket
	FSocket* CurSocket = GetSocket();

	// Bind our local port
	FSocketSubsystemEOS* const SocketSubsystem = static_cast<FSocketSubsystemEOS*>(GetSocketSubsystem());
	check(SocketSubsystem);
	if (!SocketSubsystem->BindNextPort(CurSocket, *LocalAddr, MaxPortCountToTry + 1, 1))
	{
		// Failure
		Error = TEXT("Could not bind local port");
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Could not bind local port in %d attempts"), MaxPortCountToTry);
		return false;
	}

	// Create an unreal connection to the server
	UNetConnectionEOS* Connection = NewObject<UNetConnectionEOS>(NetConnectionClass);
	check(Connection);

	// Set it as the server connection before anything else so everything knows this is a client
	ServerConnection = Connection;
	Connection->InitLocalConnection(this, CurSocket, ConnectURL, USOCK_Pending);

	CreateInitialClientChannels();

	return true;
}

bool UNetDriverEOS::InitListen(FNetworkNotify* InNotify, FURL& LocalURL, bool bReuseAddressAndPort, FString& Error)
{
	if (!bIsUsingP2PSockets || !IsAvailable() || LocalURL.HasOption(TEXT("bIsLanMatch")) || LocalURL.HasOption(TEXT("bUseIPSockets")))
	{
		UE_LOG(LogSocketSubsystemEOS, Verbose, TEXT("Init as IPNetDriver listen server. LocalURL = (%s)"), *LocalURL.ToString());

		bIsPassthrough = true;
		return Super::InitListen(InNotify, LocalURL, bReuseAddressAndPort, Error);
	}

	UE_LOG(LogSocketSubsystemEOS, Verbose, TEXT("Init as EOSNetDriver listen server. LocalURL = (%s)"), *LocalURL.ToString());

	if (!InitBase(false, InNotify, LocalURL, bReuseAddressAndPort, Error))
	{
		return false;
	}

	// Bind our specified port if provided
	FSocket* CurSocket = GetSocket();
	if (!CurSocket->Listen(0))
	{
		Error = TEXT("Could not listen");
		UE_LOG(LogSocketSubsystemEOS, Warning, TEXT("Could not listen on socket"));
		return false;
	}

	InitConnectionlessHandler();

	UE_LOG(LogSocketSubsystemEOS, Verbose, TEXT("Initialized as an EOSP2P listen server"));
	return true;
}

ISocketSubsystem* UNetDriverEOS::GetSocketSubsystem()
{
	return ISocketSubsystem::Get(bIsPassthrough ? PLATFORM_SOCKETSUBSYSTEM : EOS_SUBSYSTEM);
}

void UNetDriverEOS::Shutdown()
{
	UE_LOG(LogSocketSubsystemEOS, Verbose, TEXT("Shutting down NetDriver"));

	Super::Shutdown();

	// Kill our P2P sessions now, instead of when garbage collection kicks in later
	if (!bIsPassthrough)
	{
		if (UNetConnectionEOS* const EOSServerConnection = Cast<UNetConnectionEOS>(ServerConnection))
		{
			EOSServerConnection->DestroyEOSConnection();
		}
		for (UNetConnection* Client : ClientConnections)
		{
			if (UNetConnectionEOS* const EOSClient = Cast<UNetConnectionEOS>(Client))
			{
				EOSClient->DestroyEOSConnection();
			}
		}
	}
}

int UNetDriverEOS::GetClientPort()
{
	if (bIsPassthrough)
	{
		return Super::GetClientPort();
	}

	// Starting range of dynamic/private/ephemeral ports
	return 49152;
}

UWorld* UNetDriverEOS::FindWorld() const
{
	UWorld* MyWorld = GetWorld();

	// If we don't have a world, we may be a pending net driver
	if (!MyWorld && GEngine)
	{
		if (FWorldContext* WorldContext = GEngine->GetWorldContextFromPendingNetGameNetDriver(this))
		{
			MyWorld = WorldContext->World();
		}
	}

	return MyWorld;
}

