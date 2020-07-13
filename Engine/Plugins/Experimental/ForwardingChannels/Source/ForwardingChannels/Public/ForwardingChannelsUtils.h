// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ForwardingChannel.h"
#include "ForwardingGroup.h"
#include "Engine/Channel.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "ForwardingChannelsSubsystem.h"

namespace ForwardingChannels
{
	//~ This file defines common default implementations for various forwarding channel functions,
	//~ as well as other utilities.
	//~ These implementations should be changed sparingly!
	//~ If there's a new use case or new options are needed, it's always safer (and likely faster)
	//~ to just write custom implementations for that case.

	/**
	 * Create a default Filter that prevents forwarding to a channel that received the packet
	 * and peer connections.
	 */
	static FFilterChannelType CreateDefaultForwardingFilter(FForwardingChannel& FromChannel)
	{
		return [&FromChannel](const ForwardingChannels::FForwardingChannel& ToChannel)
		{
			return (&ToChannel != &FromChannel) && (!ToChannel.IsPeerChannel());
		};
	}

	/**
	 * Convenience method to create a forwarding channel for a UChannel.
	 *
	 * @param Channel	The UChannel that will be associated with the Forwarding Channel.
	 * @param Params	Custom Forwarding Channel Params. This is typically constructed with
	 *					the ChName of the channel as the Forwarding Group Name.
	 *
	 * @return The Forwarding Channel if it was created successfully. May return nullptr.
	 */
	static TSharedPtr<FForwardingChannel> CreateDefaultForwardingChannel(UChannel& Channel, FCreateChannelParams Params)
	{
		UNetConnection* Connection = Channel.Connection;
		if (Connection && Connection->Driver && Connection->Driver->World)
		{
			if (UGameInstance * GameInstance = Connection->Driver->World->GetGameInstance())
			{
				if (UForwardingChannelsSubsystem * ForwardingChannelsSubsystem = GameInstance->GetSubsystem<UForwardingChannelsSubsystem>())
				{
					Params.bIsServer = !Connection->Driver->IsServer();
					return ForwardingChannelsSubsystem->CreateChannel(Params);
				}
			}
		}

		return nullptr;
	}

	/** Simple default options for CreateDefaultSendPacket. */
	enum class EDefaultSendPacketFlags : uint8
	{
		None = 0,
		AllowMerging = 1 << 0,		//! Sends should allow bunches to be merged.
		IgnoreSaturation = 1 << 1,	//! Sends should ignore saturation checks (and instead rely on SendBunch failing).
	};
	ENUM_CLASS_FLAGS(EDefaultSendPacketFlags);

	/** Templated function reference that should be used to determine whether or not a packet is reliable. */
	template<typename PacketType>
	using TIsPacketReliable = TFunctionRef<bool(const PacketType&)>;

	/** Templated function reference that should be used to write a packet to an FOutBunch. */
	template<typename PacketType>
	using TWritePacket = TFunctionRef<void(FOutBunch&, const PacketType&)>;

	/**
	 * Creates a simle default implementation for FSendPacketType.
	 * This will check saturation, create a bunch, serialize the packet into the bunch,
	 * and attempt to send it on the given channel.
	 *
	 * @param Channel			The channel to send on.
	 * @param SendFlags			Flags used to customize send behavior.
	 * @param IsPacketReliable	Function that can be used to determine if a packet is reliable.
	 * @param WritePacket		Function that can be used to write a packet to an Out Bunch.
	 */
	template<typename PacketType>
	static void DefaultFlushPacketsForChannel(
		UChannel& Channel,
		FForwardingChannel& ForwardingChannel,
		const EDefaultSendPacketFlags SendFlags,
		TIsPacketReliable<PacketType> IsPacketReliable,
		TWritePacket<PacketType> WritePacket)
	{
		const bool bIgnoreSaturation = EnumHasAnyFlags(SendFlags, EDefaultSendPacketFlags::IgnoreSaturation);
		const bool bAllowMerging = EnumHasAnyFlags(SendFlags, EDefaultSendPacketFlags::AllowMerging);
		if (UNetConnection* Connection = Channel.Connection)
		{
			FSendPacketType SendPacket = [&Channel, &IsPacketReliable, &WritePacket, Connection, bIgnoreSaturation, bAllowMerging](TSharedRef<const FForwardingPacket> InPacket)
			{
				TSharedRef<const PacketType> Packet = StaticCastSharedRef<const PacketType>(InPacket);

				FSendPacketReturnType Result;
				Result.bSentReliably = Channel.OpenAcked == false || IsPacketReliable(*Packet);

				if (!bIgnoreSaturation && !Connection->IsNetReady(0))
				{
					Result.Result = ESendPacketResult::Saturated;
				}
				else
				{
					FOutBunch Bunch(&Channel, 0);

					// First send must be reliable as must any packet marked reliable
					Bunch.bReliable = Result.bSentReliably;

					// Append the packet data (copies into the bunch)
					WritePacket(Bunch, *Packet);

					// Don't submit the bunch if something went wrong
					if (Bunch.IsError() == false)
					{
						// Submit the bunching with merging on
						Result.PacketRange = Channel.SendBunch(&Bunch, 1);
					}
					else
					{
						UE_LOG(LogNet, Warning, TEXT("Bunch error: Channel = %s"), *Channel.Describe());
						Result.Result = ESendPacketResult::BadPacket;
					}
				}

				return Result;
			};

			const int32 LastAckedPacket = Connection->OutAckPacketId;
			const double CurrentTime = Connection->Driver->LastTickDispatchRealtime;
			ForwardingChannel.FlushPackets(MoveTemp(SendPacket), LastAckedPacket, CurrentTime);
		}
	}
}