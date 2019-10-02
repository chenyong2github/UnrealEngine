// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCServerProxy.h"

#include "Common/UdpSocketBuilder.h"
#include "Runtime/Core/Public/Async/TaskGraphInterfaces.h"
#include "Sockets.h"

#include "OSCLog.h"
#include "OSCStream.h"
#include "OSCServer.h"


FOSCServerProxy::FOSCServerProxy(UOSCServer& InServer)
	: Server(&InServer)
	, Socket(nullptr)
	, SocketReceiver(nullptr)
	, Port(0)
	, bMulticastLoopback(false)
	, bWhitelistClients(false)
{
}

void FOSCServerProxy::OnPacketReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Endpoint)
{
	TSharedPtr<IOSCPacket> Packet = IOSCPacket::CreatePacket(Data->GetData());
	if (!Packet.IsValid())
	{
		UE_LOG(LogOSC, Verbose, TEXT("Message received from endpoint '%s' invalid OSC packet."), *Endpoint.ToString());
		return;
	}

	FOSCStream Stream = FOSCStream(Data->GetData(), Data->Num());
	Packet->ReadData(Stream);
	Server->EnqueuePacket(Packet);

	DECLARE_CYCLE_STAT(TEXT("OSCServer.OnPacketReceived"), STAT_OSCServerOnPacketReceived, STATGROUP_OSCNetworkCommands);
	FFunctionGraphTask::CreateAndDispatchWhenReady([this, Endpoint]()
	{
		// Throw request on the ground if endpoint address not whitelisted.
		if (bWhitelistClients && !ClientWhitelist.Contains(Endpoint.Address.Value))
		{
			Server->ClearPackets();
			return;
		}

		Server->OnPacketReceived(Endpoint.Address.ToString());
	}, GET_STATID(STAT_OSCServerOnPacketReceived), nullptr, ENamedThreads::GameThread);
}

bool FOSCServerProxy::GetMulticastLoopback() const
{
	return bMulticastLoopback;
}

bool FOSCServerProxy::IsActive() const
{
	return SocketReceiver != nullptr;
}

void FOSCServerProxy::Listen(const FString& ServerName)
{
	if (IsActive())
	{
		UE_LOG(LogOSC, Error, TEXT("OSCServer currently listening: %s:%d. Failed to start new service prior to calling stop."),
			*ServerName, *ReceiveIPAddress.ToString(), Port);
		return;
	}

	FUdpSocketBuilder Builder(*ServerName);
	Builder.BoundToPort(Port);
	if (ReceiveIPAddress.IsMulticastAddress())
	{
		Builder.JoinedToGroup(ReceiveIPAddress);
		if (bMulticastLoopback)
		{
			Builder.WithMulticastLoopback();
		}
	}
	else
	{
		if (bMulticastLoopback)
		{
			UE_LOG(LogOSC, Warning, TEXT("OSCServer '%s' ReceiveIPAddress provided is not a multicast address.  Not respecting MulticastLoopback boolean."),
				*ServerName);
		}
		Builder.BoundToAddress(ReceiveIPAddress);
	}

	Socket = Builder.Build();
	if (Socket)
	{
		SocketReceiver = new FUdpSocketReceiver(Socket, FTimespan::FromMilliseconds(100), *(ServerName + TEXT("_ListenerThread")));
		SocketReceiver->OnDataReceived().BindRaw(this, &FOSCServerProxy::OnPacketReceived);
		SocketReceiver->Start();

		UE_LOG(LogOSC, Display, TEXT("OSCServer '%s' Listening: %s:%d."), *ServerName, *ReceiveIPAddress.ToString(), Port);
	}
	else
	{
		UE_LOG(LogOSC, Warning, TEXT("OSCServer '%s' failed to bind to socket on %s:%d."), *ServerName, *ReceiveIPAddress.ToString(), Port);
	}
}

bool FOSCServerProxy::SetAddress(const FString& InReceiveIPAddress, int32 InPort)
{
	if (IsActive())
	{
		UE_LOG(LogOSC, Error, TEXT("Cannot set address while OSCServer is active."));
		return false;
	}

	if (!FIPv4Address::Parse(InReceiveIPAddress, ReceiveIPAddress))
	{
		UE_LOG(LogOSC, Error, TEXT("Invalid ReceiveIPAddress '%s'. OSCServer ReceiveIP Address not updated."), *InReceiveIPAddress);
		return false;
	}

	Port = InPort;
	return true;
}

void FOSCServerProxy::SetMulticastLoopback(bool bInMulticastLoopback)
{
	if (bInMulticastLoopback != bMulticastLoopback && IsActive())
	{
		UE_LOG(LogOSC, Error, TEXT("Cannot update MulticastLoopback while OSCServer is active."));
		return;
	}

	bMulticastLoopback = bInMulticastLoopback;
}

void FOSCServerProxy::Stop()
{
	if (SocketReceiver)
	{
		delete SocketReceiver;
		SocketReceiver = nullptr;
	}

	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void FOSCServerProxy::AddWhitelistedClient(const FString& IPAddress)
{
	FIPv4Address OutAddress;
	if (!FIPv4Address::Parse(IPAddress, OutAddress))
	{
		UE_LOG(LogOSC, Warning, TEXT("OSCServer '%s' failed to whitelist IP Address '%s'. Address is invalid."), *IPAddress);
		return;
	}

	ClientWhitelist.Add(OutAddress.Value);
}

void FOSCServerProxy::RemoveWhitelistedClient(const FString& IPAddress)
{
	FIPv4Address OutAddress;
	if (!FIPv4Address::Parse(IPAddress, OutAddress))
	{
		UE_LOG(LogOSC, Warning, TEXT("OSCServer '%s' failed to remove whitelisted IP Address '%s'. Address is invalid."), *IPAddress);
		return;
	}

	ClientWhitelist.Remove(OutAddress.Value);
}

void FOSCServerProxy::ClearWhitelistedClients()
{
	ClientWhitelist.Reset();
}

TSet<FString> FOSCServerProxy::GetWhitelistedClients() const
{
	TSet<FString> OutWhitelist;
	for (uint32 Client : ClientWhitelist)
	{
		OutWhitelist.Add(FIPv4Address(Client).ToString());
	}

	return OutWhitelist;
}

void FOSCServerProxy::SetWhitelistClientsEnabled(bool bEnabled)
{
	bWhitelistClients = bEnabled;
}
