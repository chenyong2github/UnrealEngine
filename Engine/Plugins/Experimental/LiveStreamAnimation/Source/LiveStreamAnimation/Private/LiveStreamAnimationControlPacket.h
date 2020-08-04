// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "Templates/UniquePtr.h"
#include "LiveStreamAnimationPacket.h"

namespace LiveStreamAnimation
{
	enum class EControlPacketType : uint8
	{
		Initial
	};

	/**
	 * Generic packet that is used as a base for all Live Stream Animation Control
	 * @see EControlPacketType for the types of packets.
	 */
	class FControlPacket
	{
	public:

		virtual ~FControlPacket() = 0;

		EControlPacketType GetPacketType() const
		{
			return PacketType;
		}

		/**
		 * Writes a Control Packet to the given archive.
		 * 
		 * @param InWriter	The archive to write into.
		 * @param InPacket	The packet to write.
		 */
		static void WriteToStream(class FArchive& InWriter, const FControlPacket& InPacket);

		/**
		 * Reads a Control Packet from the given archive.
		 * The type read can be determined by using GetPacketType() on the resulting packet.
		 * If we fail to read the packet, nullptr will be returned.
		 * 
		 * @param InReader	The archive to read from.
		 * 
		 * @return The read packet, or null if serialization failed.
		 */ 
		static TUniquePtr<FControlPacket> ReadFromStream(class FArchive& InReader);

	protected:

		FControlPacket(const EControlPacketType InPacketType)
			: PacketType(InPacketType)
		{
		}

	private:

		const EControlPacketType PacketType;
	};
	
	class FControlInitialPacket : public FControlPacket
	{
	public:
	
		FControlInitialPacket()
			: FControlPacket(EControlPacketType::Initial)
		{
		}
		
		virtual ~FControlInitialPacket()
		{
		}
	};
};