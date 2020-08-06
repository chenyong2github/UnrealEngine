// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveStreamAnimationFwd.h"
#include "LiveStreamAnimationHandle.h"
#include "ForwardingPacket.h"
#include "Templates/SharedPointer.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryWriter.h"

namespace LiveStreamAnimation
{
	/**
	 * Generic forwarding packet that's used by Live Stream Animation.
	 * It can hold arbitrary data used by various animation data handlers.
	 */ 
	class FLiveStreamAnimationPacket : public ForwardingChannels::FForwardingPacket
	{
	public:

		virtual ~FLiveStreamAnimationPacket() {}

		int32 GetPacketType() const
		{
			return PacketType;
		}

		bool IsReliable() const
		{
			return bReliable;
		}

		void SetReliable(bool bInReliable)
		{
			bReliable = bInReliable;
		}

		TArrayView<const uint8> GetPacketData() const
		{
			return PacketData;
		}

		static void WriteToStream(class FArchive& InWriter, const FLiveStreamAnimationPacket& InPacket);

		static TSharedPtr<FLiveStreamAnimationPacket> ReadFromStream(class FArchive& InReader);

		static TSharedPtr<FLiveStreamAnimationPacket> CreateFromData(const uint32 InPacketType, TArray<uint8>&& InPacketData);

	private:

		FLiveStreamAnimationPacket(const uint32 InPacketType, TArray<uint8>&& InPacketData)
			: PacketType(InPacketType)
			, PacketData(MoveTemp(InPacketData))
		{
		}

		const uint32 PacketType;
		TArray<uint8> PacketData;
		bool bReliable = false;
	};
}