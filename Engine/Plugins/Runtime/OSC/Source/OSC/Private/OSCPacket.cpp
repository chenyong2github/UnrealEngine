// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
	const FOSCAddress Address(ANSI_TO_TCHAR((const ANSICHAR*)&PacketType[0]));
	if (Address.IsMessage())
	{
		return MakeShareable(new FOSCMessagePacket());
	}
	else if (Address.IsBundle())
	{
		return MakeShareable(new FOSCBundlePacket());
	}
	else
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to parse lead character of OSC message packet. "
			"Lead identifier of '%c' not valid bundle (#) or message (/) identifier."), PacketType[0]);
		return nullptr;
	}
}
