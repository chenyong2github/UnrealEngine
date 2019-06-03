// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "OSCBundle.h"
#include "OSCStream.h"

FOSCBundlePacket::FOSCBundlePacket() 
: FOSCPacket(), TimeTag(0) 
{}

FOSCBundlePacket::~FOSCBundlePacket() 
{}

void FOSCBundlePacket::SetTimeTag(uint64 NewTimeTag) 
{ 
	TimeTag = FOSCType(NewTimeTag); 
}

uint64 FOSCBundlePacket::GetTimeTag() const 
{ 
	return TimeTag.GetTimeTag(); 
}

void FOSCBundlePacket::AddPacket(TSharedPtr<FOSCPacket> Packet)
{
	Packets.Add(Packet);
}

void FOSCBundlePacket::WriteData(FOSCStream& Stream)
{
	// Write bundle
	Stream.WriteString(FString("#bundle"));

	// Write bundle time tag
	Stream.WriteUInt64(GetTimeTag());

	// for each packet
	for (auto& packet : Packets)
	{
		int sizePosition = Stream.GetPosition();
		Stream.WriteInt32(0);

		int prePos = Stream.GetPosition();
		packet->WriteData(Stream);
		int postPos = Stream.GetPosition();

		Stream.SetPosition(sizePosition);
		Stream.WriteInt32(postPos - prePos);
		Stream.SetPosition(postPos);
	}
}

void FOSCBundlePacket::ReadData(FOSCStream& Stream)
{
	// Read address
	FString address = Stream.ReadString();

	// read time tag
	TimeTag = FOSCType(Stream.ReadUInt64());

	// While we still have data
	while (!Stream.HasReachedEnd())
	{
		int32 sizeOfPacket = Stream.ReadInt32();

		TSharedPtr<FOSCPacket> packet = FOSCPacket::CreatePacket(Stream.GetBuffer());

		if (packet.IsValid())
		{
			packet->ReadData(Stream);
			Packets.Add(packet);
		}		
	}
}

const TSharedPtr<FOSCBundlePacket> FOSCBundle::GetOrCreatePacket()
{
	if (!Packet.IsValid())
	{
		Packet = MakeShareable(new FOSCBundlePacket());
	}

	return Packet;
}

