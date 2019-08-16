// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCBundlePacket.h"

#include "OSCBundle.h"
#include "OSCLog.h"
#include "OSCMessage.h"
#include "OSCTypes.h"


FOSCBundlePacket::FOSCBundlePacket()
	: IOSCPacket()
	, TimeTag(0)
{
}

FOSCBundlePacket::~FOSCBundlePacket()
{
}

void FOSCBundlePacket::SetTimeTag(uint64 NewTimeTag)
{
	TimeTag = FOSCType(NewTimeTag);
}

uint64 FOSCBundlePacket::GetTimeTag() const
{
	return TimeTag.GetTimeTag();
}

void FOSCBundlePacket::WriteData(FOSCStream& Stream)
{
	// Write bundle & time tag
	Stream.WriteString(OSC::BundleTag);
	Stream.WriteUInt64(GetTimeTag());

	for (TSharedPtr<IOSCPacket>& Packet : Packets)
	{
		int32 StreamPos = Stream.GetPosition();
		Stream.WriteInt32(0);

		int32 InitPos = Stream.GetPosition();
		Packet->WriteData(Stream);
		int32 NewPos = Stream.GetPosition();

		Stream.SetPosition(StreamPos);
		Stream.WriteInt32(NewPos - InitPos);
		Stream.SetPosition(NewPos);
	}
}

void FOSCBundlePacket::ReadData(FOSCStream& Stream)
{
	TimeTag = FOSCType(Stream.ReadUInt64());

	while (!Stream.HasReachedEnd())
	{
		TSharedPtr<IOSCPacket> Packet = IOSCPacket::CreatePacket(Stream.GetData());
		if (Packet.IsValid())
		{
			Packet->ReadData(Stream);
			Packets.Add(Packet);
		}
	}
}

FOSCBundlePacket::FPacketBundle& FOSCBundlePacket::GetPackets()
{
	return Packets;
}

bool FOSCBundlePacket::IsBundle()
{
	return true;
}

bool FOSCBundlePacket::IsMessage()
{
	return false;
}
