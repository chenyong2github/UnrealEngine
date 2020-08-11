// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveStreamAnimationFwd.h"
#include "ForwardingPacket.h"
#include "Templates/SharedPointer.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryWriter.h"

namespace LiveStreamAnimation
{
	// It might be worth making this more flexible / extensible through a registration system.
	// For now though, it's trivial and less error prone to add new packet types manually.

	/** Packet Subtypes. */
	enum class ELiveStreamAnimationPacketType : uint8
	{
		Control,	//! Generic control packet.
		LiveLink,	//! We're holding FLiveLinkPacket data.
		INVALID
	};

	/**
	 * Generic forwarding packet that's used by Live Stream Animation.
	 * It can hold arbitrary data used by various packet subtypes.
	 */ 
	class FLiveStreamAnimationPacket : public ForwardingChannels::FForwardingPacket
	{
	public:

		virtual ~FLiveStreamAnimationPacket() {}

		ELiveStreamAnimationPacketType GetPacketType() const
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

		template<typename TPacketClass>
		static TSharedPtr<FLiveStreamAnimationPacket> CreateFromPacket(const TPacketClass& Packet)
		{
			TArray<uint8> Data;
			Data.Reserve(40);

			FMemoryWriter MemoryWriter(Data);
			TPacketClass::WriteToStream(MemoryWriter, Packet);

			Data.Shrink();

			static constexpr ELiveStreamAnimationPacketType PacketType = TPacketClass::GetAnimationPacketType();
			static_assert(PacketType != ELiveStreamAnimationPacketType::INVALID, "Class must provide a valid packet type");

			return MakeShareable<FLiveStreamAnimationPacket>(new FLiveStreamAnimationPacket(
				PacketType,
				MoveTemp(Data)
			));
		}

	private:

		FLiveStreamAnimationPacket(const ELiveStreamAnimationPacketType InPacketType, TArray<uint8>&& InPacketData)
			: PacketType(InPacketType)
			, PacketData(MoveTemp(InPacketData))
		{
		}

		const ELiveStreamAnimationPacketType PacketType;
		TArray<uint8> PacketData;
		bool bReliable = false;
	};
}