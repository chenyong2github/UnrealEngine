// Copyright Epic Games, Inc. All Rights Reserved.
#include "OSCPacket.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"

#include "OSCMessagePacket.h"
#include "OSCBundlePacket.h"
#include "OSCLog.h"


TSharedPtr<IOSCPacket> IOSCPacket::CreatePacket(const uint8* InPacketType, const FIPv4Endpoint& InEndpoint)
{
	const FString PacketIdentifier(ANSI_TO_TCHAR((const ANSICHAR*)&InPacketType[0]));
	
	TSharedPtr<IOSCPacket> Packet;
	if (PacketIdentifier.StartsWith(OSC::PathSeparator))
	{
		Packet = MakeShared<FOSCMessagePacket>();
	}
	else if (PacketIdentifier == OSC::BundleTag)
	{
		Packet = MakeShared<FOSCBundlePacket>();
	}
	else
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse lead character of OSC packet. "
			"Lead identifier of '%c' not valid bundle tag ('%s') or message ('%s') identifier."), *PacketIdentifier, *OSC::BundleTag, *OSC::PathSeparator);
		return nullptr;
	}

	Packet->Endpoint = InEndpoint;
	return Packet;
}

const FIPv4Endpoint& IOSCPacket::GetEndpoint() const
{
	return Endpoint;
}