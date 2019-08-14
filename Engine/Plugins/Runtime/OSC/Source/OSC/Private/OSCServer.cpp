// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCServer.h"

#include "Runtime/Core/Public/Async/TaskGraphInterfaces.h"
#include "Sockets.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"

#include "OSCStream.h"
#include "OSCMessage.h"
#include "OSCMessagePacket.h"
#include "OSCBundle.h"
#include "OSCBundlePacket.h"
#include "OSCLog.h"
#include "OSCServerProxy.h"


UOSCServer::UOSCServer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ServerProxy(new FOSCServerProxy(*this))
{
}

void UOSCServer::Connect()
{
	ServerProxy.Reset(new FOSCServerProxy(*this));
}

bool UOSCServer::GetMulticastLoopback() const
{
	check(ServerProxy.IsValid());
	return ServerProxy->GetMulticastLoopback();
}

bool UOSCServer::IsActive() const
{
	check(ServerProxy.IsValid());
	return ServerProxy->IsActive();
}

void UOSCServer::Listen()
{
	check(ServerProxy.IsValid());
	ServerProxy->Listen(GetName());
}

bool UOSCServer::SetAddress(const FString& InReceiveIPAddress, int32 InPort)
{
	check(ServerProxy.IsValid());
	return ServerProxy->SetAddress(InReceiveIPAddress, InPort);
}

void UOSCServer::SetMulticastLoopback(bool bInMulticastLoopback)
{
	check(ServerProxy.IsValid());
	ServerProxy->SetMulticastLoopback(bInMulticastLoopback);
}

void UOSCServer::Stop()
{
	if (ServerProxy.IsValid())
	{
		ServerProxy->Stop();
	}
}

void UOSCServer::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}

void UOSCServer::SetWhitelistClientsEnabled(bool bEnabled)
{
	check(ServerProxy.IsValid());
	ServerProxy->SetWhitelistClientsEnabled(bEnabled);
}

void UOSCServer::AddWhitelistedClient(const FString& InIPAddress)
{
	check(ServerProxy.IsValid());
	ServerProxy->AddWhitelistedClient(InIPAddress);
}

void UOSCServer::RemoveWhitelistedClient(const FString& InIPAddress)
{
	check(ServerProxy.IsValid());
	ServerProxy->RemoveWhitelistedClient(InIPAddress);
}

void UOSCServer::ClearWhitelistedClients()
{
	check(ServerProxy.IsValid());
	ServerProxy->ClearWhitelistedClients();
}

TSet<FString> UOSCServer::GetWhitelistedClients() const
{
	check(ServerProxy.IsValid());
	return ServerProxy->GetWhitelistedClients();
}

void UOSCServer::BindEventToOnOSCAddressPatternMatchesPath(const FOSCAddress& InOSCAddressPattern, const FOSCDispatchMessageEventBP& InEvent)
{
	if (InOSCAddressPattern.IsValidPattern())
	{
		FOSCDispatchMessageEvent& MessageEvent = AddressPatterns.FindOrAdd(InOSCAddressPattern);
		MessageEvent.AddUnique(InEvent);
	}
}

void UOSCServer::UnbindEventFromOnOSCAddressPatternMatchesPath(const FOSCAddress& InOSCAddressPattern, const FOSCDispatchMessageEventBP& InEvent)
{
	if (InOSCAddressPattern.IsValidPattern())
	{
		if (FOSCDispatchMessageEvent* AddressPatternEvent = AddressPatterns.Find(InOSCAddressPattern))
		{
			AddressPatternEvent->Remove(InEvent);
			if (!AddressPatternEvent->IsBound())
			{
				AddressPatterns.Remove(InOSCAddressPattern);
			}
		}
	}
}

void UOSCServer::UnbindAllEventsFromOnOSCAddressPatternMatchesPath(const FOSCAddress& InOSCAddressPattern)
{
	if (InOSCAddressPattern.IsValidPattern())
	{
		AddressPatterns.Remove(InOSCAddressPattern);
	}
}

void UOSCServer::UnbindAllEventsFromOnOSCAddressPatternMatching()
{
	AddressPatterns.Reset();
}

TArray<FOSCAddress> UOSCServer::GetBoundOSCAddressPatterns() const
{
	TArray<FOSCAddress> OutAddressPatterns;
	for (const TPair<FOSCAddress, FOSCDispatchMessageEvent>& Pair : AddressPatterns)
	{
		OutAddressPatterns.Add(Pair.Key);
	}
	return MoveTemp(OutAddressPatterns);
}

void UOSCServer::ClearPackets()
{
	OSCPackets.Empty();
}

void UOSCServer::EnqueuePacket(TSharedPtr<IOSCPacket> Packet)
{
	OSCPackets.Enqueue(Packet);
}

void UOSCServer::DispatchBundle(const FString& InIPAddress, const FOSCBundle& InBundle)
{
	OnOscBundleReceived.Broadcast(InBundle);

	TSharedPtr<FOSCBundlePacket> BundlePacket = StaticCastSharedPtr<FOSCBundlePacket>(InBundle.GetPacket());
	FOSCBundlePacket::FPacketBundle Packets = BundlePacket->GetPackets();
	for (TSharedPtr<IOSCPacket>& Packet : Packets)
	{
		if (Packet->IsMessage())
		{
			DispatchMessage(InIPAddress, FOSCMessage(Packet));
		}
		else if (Packet->IsBundle())
		{
			DispatchBundle(InIPAddress, FOSCBundle(Packet));
		}
		else
		{
			UE_LOG(LogOSC, Warning, TEXT("Failed to parse invalid received message. Invalid OSC type (packet is neither identified as message nor bundle)."));
		}
	}
}

void UOSCServer::DispatchMessage(const FString& InIPAddress, const FOSCMessage& InMessage)
{
	OnOscMessageReceived.Broadcast(InMessage);
	UE_LOG(LogOSC, Verbose, TEXT("Message received from endpoint '%s', OSCAddress of '%s'."), *InIPAddress, *InMessage.GetAddress().GetFullPath());

	for (const TPair<FOSCAddress, FOSCDispatchMessageEvent>& Pair : AddressPatterns)
	{
		const FOSCDispatchMessageEvent& DispatchEvent = Pair.Value;
		if (Pair.Key.Matches(InMessage.GetAddress()))
		{
			DispatchEvent.Broadcast(Pair.Key, InMessage);
			UE_LOG(LogOSC, Verbose, TEXT("Message dispatched from endpoint '%s', OSCAddress path of '%s' matched OSCAddress pattern '%s'."),
				*InIPAddress,
				*InMessage.GetAddress().GetFullPath(),
				*Pair.Key.GetFullPath());
		}
	}
}

void UOSCServer::OnMessageReceived(const FString& InIPAddress)
{
	TSharedPtr<IOSCPacket> Packet;
	while (OSCPackets.Dequeue(Packet))
	{
		if (Packet->IsMessage())
		{
			DispatchMessage(InIPAddress, FOSCMessage(Packet));
		}
		else if (Packet->IsBundle())
		{
			DispatchBundle(InIPAddress, FOSCBundle(Packet));
		}
		else
		{
			UE_LOG(LogOSC, Warning, TEXT("Failed to parse invalid received message. Invalid OSC type (packet is neither identified as message nor bundle)."));
		}
	}
}