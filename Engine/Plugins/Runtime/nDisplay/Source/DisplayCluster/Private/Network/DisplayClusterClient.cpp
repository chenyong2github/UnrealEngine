// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/DisplayClusterClient.h"
#include "Common/TcpSocketBuilder.h"

#include "Misc/ScopeLock.h"

#include "Misc/DisplayClusterLog.h"


bool FDisplayClusterClientBase::Connect(const FString& InAddr, const int32 InPort, const int32 TriesAmount, const float TryDelay)
{
	FScopeLock Lock(&GetSyncObj());

	// Generate IPv4 address
	FIPv4Address IPAddr;
	if (!FIPv4Address::Parse(InAddr, IPAddr))
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s couldn't parse the address: %s"), *GetName(), *InAddr);
		return false;
	}

	// Generate internet address
	TSharedRef<FInternetAddr> InternetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	InternetAddr->SetIp(IPAddr.Value);
	InternetAddr->SetPort(InPort);

	// Start connection loop
	int32 TryIdx = 0;
	while(GetSocket()->Connect(*InternetAddr) == false)
	{
		UE_LOG(LogDisplayClusterNetwork, Log, TEXT("%s couldn't connect to the server %s [%d]"), *GetName(), *(InternetAddr->ToString(true)), TryIdx);
		if (TriesAmount > 0 && ++TryIdx >= TriesAmount)
		{
			UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s connection attempts limit reached"), *GetName());
			return false;
		}

		// Sleep some time before next try
		FPlatformProcess::Sleep(TryDelay / 1000.f);
	}

	return IsOpen();
}

void FDisplayClusterClientBase::Disconnect()
{
	FScopeLock Lock(&GetSyncObj());

	UE_LOG(LogDisplayClusterNetwork, Log, TEXT("%s disconnecting..."), *GetName());

	if (IsOpen())
	{
		GetSocket()->Close();
	}
}

FSocket* FDisplayClusterClientBase::CreateSocket(const FString& InName)
{
	FSocket* NewSocket = FTcpSocketBuilder(*InName).AsBlocking();
	check(NewSocket);

	// Set TCP_NODELAY=1
	NewSocket->SetNoDelay(true);

	return NewSocket;
}
