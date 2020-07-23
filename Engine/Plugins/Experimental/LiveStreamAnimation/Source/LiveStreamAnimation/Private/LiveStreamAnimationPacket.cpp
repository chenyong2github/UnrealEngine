// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveStreamAnimationPacket.h"
#include "LiveStreamAnimationLog.h"


namespace LiveStreamAnimation
{
	void FLiveStreamAnimationPacket::WriteToStream(FArchive& InWriter, const FLiveStreamAnimationPacket& InPacket)
	{
		uint8 LocalPacketType = static_cast<uint8>(InPacket.PacketType);
		uint32 DataSize = InPacket.PacketData.Num();
		InWriter << LocalPacketType;
		InWriter.SerializeIntPacked(DataSize);
		InWriter.Serialize(const_cast<uint8*>(InPacket.PacketData.GetData()), InPacket.PacketData.Num());
	}

	TSharedPtr<FLiveStreamAnimationPacket> FLiveStreamAnimationPacket::ReadFromStream(class FArchive& InReader)
	{
		uint8 LocalPacketType = 0;
		uint32 DataSize = 0;
		InReader << LocalPacketType;
		InReader.SerializeIntPacked(DataSize);

		if (static_cast<uint8>(ELiveStreamAnimationPacketType::INVALID) <= LocalPacketType)
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FLiveStreamAnimationPacket::ReadFromStream: Invalid packet type %d"), LocalPacketType);
			return nullptr;
		}

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

		return MakeShareable<FLiveStreamAnimationPacket>(new FLiveStreamAnimationPacket(static_cast<ELiveStreamAnimationPacketType>(LocalPacketType), MoveTemp(Data)));
	}
}