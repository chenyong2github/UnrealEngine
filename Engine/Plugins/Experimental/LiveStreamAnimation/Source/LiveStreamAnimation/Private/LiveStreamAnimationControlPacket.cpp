// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveStreamAnimationControlPacket.h"
#include "LiveStreamAnimationLog.h"
#include "LiveStreamAnimationHandle.h"

namespace LiveStreamAnimation
{

	FControlPacket::~FControlPacket()
	{
	}

	void FControlPacket::WriteToStream(FArchive& InWriter, const FControlPacket& InPacket)
	{
		uint8 PacketTypeValue = static_cast<uint8>(InPacket.PacketType);
		InWriter << PacketTypeValue;

		if (InWriter.IsError())
		{
			return;
		}

		switch (InPacket.PacketType)
		{
		case EControlPacketType::Initial:
			break;

		default:
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FControlPacket::WriteToStream: Invalid packet type %d"), static_cast<int32>(InPacket.PacketType));
			InWriter.SetError();
			break;
		}
	}

	TUniquePtr<FControlPacket> FControlPacket::ReadFromStream(FArchive& InReader)
	{
		FLiveStreamAnimationHandle Handle;
		uint8 PacketTypeValue = 0;
		InReader << PacketTypeValue;

		if (InReader.IsError())
		{
			return nullptr;
		}

		const EControlPacketType PacketType = static_cast<EControlPacketType>(PacketTypeValue);

		switch (PacketType)
		{
		case EControlPacketType::Initial:
			return MakeUnique<FControlInitialPacket>();

		default:
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("FControlPacket::ReadFromStream: Invalid packet type %d"), static_cast<int32>(PacketType));
			InReader.SetError();
			return nullptr;
		}
	}
}