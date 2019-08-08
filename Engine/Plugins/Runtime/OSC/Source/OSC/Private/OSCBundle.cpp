// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCBundle.h"

#include "OSCAddress.h"
#include "OSCLog.h"
#include "OSCStream.h"


FOSCBundlePacket::FOSCBundlePacket()
	: FOSCPacket()
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
	if (!GetAddress().IsBundle())
	{
		UE_LOG(LogOSC, Warning, TEXT("Failed to write OSCBundlePacket. Invalid OSCAddress '%s'"), *GetAddress().Value);
		return;
	}

	// Write bundle & time tag
	Stream.WriteString(GetAddress().Value);
	Stream.WriteUInt64(GetTimeTag());

	for (TSharedPtr<FOSCPacket>& Packet : Packets)
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
		TSharedPtr<FOSCPacket> Packet = FOSCPacket::CreatePacket(Stream.GetData());

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

const FOSCAddress& FOSCBundlePacket::GetAddress() const
{
	const static FOSCAddress BundleIdentifier = FOSCAddress(OSC::BundleTag);
	return BundleIdentifier;
}