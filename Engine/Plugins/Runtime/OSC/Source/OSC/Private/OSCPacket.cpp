// Copyright Epic Games, Inc. All Rights Reserved.
#include "OSCPacket.h"

#include "OSCMessagePacket.h"
#include "OSCBundlePacket.h"
#include "OSCLog.h"


IOSCPacket::IOSCPacket()
{
}

IOSCPacket::~IOSCPacket()
{
}

TSharedPtr<IOSCPacket> IOSCPacket::CreatePacket(const uint8* PacketType)
{
	const FString PacketIdentifier(ANSI_TO_TCHAR((const ANSICHAR*)&PacketType[0]));
	if (PacketIdentifier.StartsWith(OSC::PathSeparator))
	{
		return MakeShareable(new FOSCMessagePacket());
	}
	else if (PacketIdentifier == OSC::BundleTag)
	{
		return MakeShareable(new FOSCBundlePacket());
	}
	else
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse lead character of OSC packet. "
			"Lead identifier of '%c' not valid bundle tag ('%s') or message ('%s') identifier."), *PacketIdentifier, *OSC::BundleTag, *OSC::PathSeparator);
		return nullptr;
	}
}
