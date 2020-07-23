// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ForwardingChannelsFwd.h"
#include "UObject/CoreNet.h"
#include "Templates/Function.h"

namespace ForwardingChannels
{
	/** Optional reliability offered by Forwarding Channels. */
	enum class EChannelReliability : uint8
	{
		None,			//! No reliability. Anything that is dropped is gone for good.
		ResendOnNak		//! We will redundantly send packets any time they are NAK'd, until the packet expires.
	};

	/** Possible results from a call to a FSendPacketType function. */
	enum class ESendPacketResult : uint8
	{
		Success,		//! The packet was sent successfully.
		Saturated,		//! The packet was unable to be sent, but may succeed if we try again later.
		BadPacket,		//! The packet couldn't be serialized or sent, and will never succeed.
	};

	/** Return struct used by FSendPacketType function. */
	struct FSendPacketReturnType
	{
		/** The result of trying to send the packet. */
		ESendPacketResult Result = ESendPacketResult::Success;

		/** Should be valid if Result == ESendPacketResult::Success */
		FPacketIdRange PacketRange;

		/** Whether or not the packet was sent reliably. If it was, we can ignore our custom reliability stuff (if it is enabled). */
		bool bSentReliably = false;
	};

	/** Function that will be passed into FForwardingChannel::FlushForwardedPackets, in order to send them. */
	using FSendPacketType = TUniqueFunction<FSendPacketReturnType(TSharedRef<const FForwardingPacket>)>;

	/** Parameters used to create FForwardingChannels.*/
	struct FCreateChannelParams
	{
		FCreateChannelParams(const FName InGroupName) :
			GroupName(InGroupName)
		{
			ensure(GroupName != NAME_None);
		}

		/** Whether or not this channel is sending data to / receiving data from the server. */
		bool bIsServer = false;

		//~ If desired, bIsPeer and additional information specific to channel groups could
		//~ be added by having some sort of Per Group Descriptor type (e.g., FPerGroupChannelData)
		//~ that could be subclassed similar to FForwardingPacket.

		/**
		 * Whether or not this channel can communicate directly with other clients, and not just the server.
		 * Only meaningful for client channels.
		 */
		bool bIsPeer = false;

		/** The type of reliability to use. */
		EChannelReliability Reliability = EChannelReliability::None;

		/** When using custom reliability, how long we'll hold onto packets before evicting them (if not Acked). */
		float ResendExpiration = 0.5f;

		/** Name of the group this channel is associated with. */
		const FName GroupName;
	};

	/**
	 * A forwarding channel can be used to help marshal data from servers to clients
	 * across multiple server boundaries.
	 *
	 * In Forwarding Channel parlance, a "Server Channel" is a channel that is receiving
	 * data *from* a Server, and a Client Channel is a channel that should send data to
	 * clients. There is a one to many relationship between Server Channels (one) to
	 * Client Channels (many).
	 *
	 * Forwarding Channels don't concern themselves with how data is sent, received serialized,
	 * or otherwise processed. They are primarily concerned with grouping and queueing data to
	 * be sent, and forwarding data when desired.
	 *
	 * There are also no restrictions on sending data from Clients to the Server.
	 * 
	 * The typical way FForwardingChannels are used is by creating a new UChannel type that
	 * can handle the necessary data. When a new instace of the UChannel type is created,
	 * it would also create and maintain a reference to an FForwardingChannel. As data needs to be
	 * sent on the Channel, the data is wrapped in a FForwardingPacket subclass and queued on the
	 * associated FForwardingChannel. Once per frame in UChannel::Tick (or more frequently if needed),
	 * FForwadingPacket::FlushPackets can be called in order to send any of the queued packets.
	 * If Forwarding Reliability is being used (see EChannelReliability), you'd also set up
	 * UChannel::ReceivedNak to call FForwardingChannel::ReceivedNak.
	 *
	 * However, there's no requirement to use UChannels and FForwardingChannel (and other Forwading
	 * types) try to make as few assumptions about the underlying protocols and data as possible.
	 *
	 * See FForwardingGroup for methods used to actually Forward packets to all available
	 * client channels.
	 */
	class FForwardingChannel
	{
	private:

		explicit FForwardingChannel(
			const FCreateChannelParams& Params,
			const TSharedRef<FForwardingGroup>& InGroup);

	public:

		//~ This is purposefully not exposed.
		//~ Use UForwardingChannelSubsystem::CreateChannel instead.
		static TSharedPtr<FForwardingChannel> CreateChannel(
			const FCreateChannelParams& Params,
			UForwardingChannelsSubsystem* Subsystem);

		FORWARDINGCHANNELS_API ~FForwardingChannel();

		/**
		 * Notify the channel that a NAK was received so it can handle resends if necessary.
		 *
		 * @param NakPacketId	The packet that was NAKed.
		 */
		FORWARDINGCHANNELS_API void ReceivedNak(int32 NakPacketId);

		/**
		 * Send any packets that have been added to this channel.
		 * This will include forwarded packets or packets added directly to this channel.
		 *
		 * @param SendPacket		Function to send packets.
		 * @param CurrentTime		Arbitrary time of the send (in Seconds).
		 * @param LastAckedPacket	The last packet that we know was acked.
		 */
		FORWARDINGCHANNELS_API void FlushPackets(
			FSendPacketType SendPacket,
			const double CurrentTime,
			const int32 LastAckedPacket);

		/**
		 * Queue up a packet on this channel.
		 *
		 * @param PacketToSend		The packet to be forwarded.
		 */
		template<typename OtherPacketType>
		void QueuePacket(const TSharedRef<OtherPacketType>& PacketToSend)
		{
			if (!IsGroupInitialized())
			{
				return;
			}

			ToSend.Add(PacketToSend);
		}

		/**
		 * Queue packets on this channel.
		 *
		 * @param PacketsToSend		The packets to be forwarded.
		 */
		template<typename OtherPacketType, typename OtherAllocatorType>
		void QueuePackets(const TArray<TSharedRef<OtherPacketType>, OtherAllocatorType>& PacketsToSend)
		{
			if (!IsGroupInitialized())
			{
				return;
			}

			ToSend.Append(PacketsToSend);
		}

		void OnSubsystemDeinitialized();

		bool IsServerChannel() const
		{
			return bIsServerChannel;
		}

		bool IsPeerChannel() const
		{
			return bIsPeerChannel;
		}

		EChannelReliability GetReliability() const
		{
			return Reliability;
		}

		TSharedRef<class FForwardingGroup> GetGroup() const
		{
			return Group;
		}

		FORWARDINGCHANNELS_API bool IsGroupInitialized() const;

	private:

		void QueuePacketUnchecked(const TSharedRef<const FForwardingPacket>& PacketToSend);
		void QueuePacketsUnchecked(const TArray<TSharedRef<const FForwardingPacket>>& PacketsToSend);

		const bool bIsServerChannel;
		const bool bIsPeerChannel;
		const EChannelReliability Reliability;
		const float ResendExpiration;
		const TSharedRef<class FForwardingGroup> Group;

		struct FPendingPacket
		{
			TSharedRef<const FForwardingPacket> Packet;
			FPacketIdRange PacketRange;
			double InitiallySent = 0.f;
			bool bWasNakd = false;
		};

		TArray<TSharedRef<const FForwardingPacket>> ToSend;
		TArray<FPendingPacket> UnreliableResends;

		friend class FPacketHelper;
	};

	class FPacketHelper
	{
	private:

		static void QueuePacketUnchecked(FForwardingChannel& Channel, const TSharedRef<const FForwardingPacket>& PacketToSend);
		static void QueuePacketsUnchecked(FForwardingChannel& Channel, const TArray<TSharedRef<const FForwardingPacket>>& PacketsToSend);
		friend class FForwardingGroup;
	};
}