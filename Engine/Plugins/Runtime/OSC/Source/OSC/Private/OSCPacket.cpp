// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCPacket.h"
#include "OSCMessage.h"
#include "OSCBundle.h"

TSharedPtr<FOSCPacket> FOSCPacket::CreatePacket(const char* PacketType)
{
	if (PacketType[0] == '/')
	{
		return  MakeShareable(new FOSCMessagePacket());
	}
	else if (PacketType[0] == '#')
	{
		return  MakeShareable(new FOSCBundlePacket());
	}

	return nullptr;
}