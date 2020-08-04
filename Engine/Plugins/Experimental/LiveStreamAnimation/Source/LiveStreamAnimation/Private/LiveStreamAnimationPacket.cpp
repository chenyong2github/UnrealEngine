// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveStreamAnimationPacket.h"
#include "LiveStreamAnimationLog.h"
#include "LiveStreamAnimationSubsystem.h"


namespace LiveStreamAnimation
{
	void FLiveStreamAnimationPacket::WriteToStream(class FArchive& InWriter, const FLiveStreamAnimationPacket& InPacket)
	{
		uint32 PacketId = InPacket.PacketType;
		InWriter.SerializeIntPacked(PacketId);

		uint32 DataSize = InPacket.PacketData.Num();
		InWriter.SerializeIntPacked(DataSize);

		InWriter.Serialize(const_cast<uint8*>(InPacket.PacketData.GetData()), InPacket.PacketData.Num());
	}

	TSharedPtr<FLiveStreamAnimationPacket> FLiveStreamAnimationPacket::ReadFromStream(class FArchive& InReader)
	{
		uint32 PacketType;
		InReader.SerializeIntPacked(PacketType);

		uint32 DataSize;
		InReader.SerializeIntPacked(DataSize);
		const int32 SignedDataSize = static_cast<int32>(DataSize);
		if (SignedDataSize < 0)
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveStreamAnimationPacket::ReadFromStream: Invalid data size %d"), SignedDataSize);
			return nullptr;
		}

		// We could actually pass this to the appropriate packet type to do validation.
		// For now, we'll just assume the data is correct and pass it through (letting 
		// terminal connections handle anything that's been malformed).

		TArray<uint8> Data;
		Data.SetNumUninitialized(SignedDataSize);
		InReader.Serialize(Data.GetData(), SignedDataSize);

		if (InReader.IsError())
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveStreamAnimationPacket::ReadFromStream: Failed to serialize data"));
			return nullptr;
		}

		return MakeShareable<FLiveStreamAnimationPacket>(new FLiveStreamAnimationPacket(PacketType, MoveTemp(Data)));
	}

	TSharedPtr<FLiveStreamAnimationPacket> FLiveStreamAnimationPacket::CreateFromData(const uint32 InPacketType, TArray<uint8>&& InPacketData)
	{
		return MakeShareable<FLiveStreamAnimationPacket>(new FLiveStreamAnimationPacket(InPacketType, MoveTemp(InPacketData)));
	}
}