// Copyright Epic Games, Inc. All Rights Reserved.

#include "ForwardingChannel.h"
#include "ForwardingGroup.h"
#include "ForwardingChannelsSubsystem.h"
#include "Net/DataBunch.h"
#include "Engine/NetConnection.h"

namespace ForwardingChannels
{
	TSharedPtr<FForwardingChannel> FForwardingChannel::CreateChannel(
		const FCreateChannelParams& Params,
		UForwardingChannelsSubsystem* Subsystem)
	{
		TSharedPtr<FForwardingChannel> ForwardingChannel;

		if (Params.GroupName != NAME_None && Subsystem != nullptr)
		{
			TSharedPtr<FForwardingGroup> Group = Subsystem->GetOrCreateForwardingGroup(Params.GroupName);
			if (Group.IsValid())
			{
				TSharedRef<FForwardingChannel> Channel = MakeShareable<FForwardingChannel>(new FForwardingChannel(Params, Group.ToSharedRef()));
				if (Group->RegisterChannel(*Channel))
				{
					ForwardingChannel = Channel;
				}
			}
		}

		return ForwardingChannel;
	}

	FForwardingChannel::FForwardingChannel(
		const FCreateChannelParams& Params,
		const TSharedRef<FForwardingGroup>& InGroup)

		: bIsServerChannel(Params.bIsServer)
		, bIsPeerChannel(Params.bIsPeer)
		, Reliability(Params.Reliability)
		, ResendExpiration(Params.ResendExpiration)
		, Group(InGroup)
	{
	}

	FForwardingChannel::~FForwardingChannel()
	{
		Group->UnregisterChannel(*this);
	}

	void FForwardingChannel::OnSubsystemDeinitialized()
	{
		ToSend.Empty();
		UnreliableResends.Empty();
	}

	void FForwardingChannel::FlushPackets(
		ForwardingChannels::FSendPacketType SendPacket,
		const double CurrentTime,
		const int32 LastAckedPacket)
	{
		if (!Group->IsSubsystemInitialized())
		{
			return;
		}
		if (!ensureMsgf(SendPacket, TEXT("SendPacket must be provided!")))
		{
			return;
		}

		const double LocalResendExpiration = ResendExpiration;
		const bool bUsingReliability = (GetReliability() != EChannelReliability::None);
		TArray<FPendingPacket> NewlyPendingPackets;

		auto HasPacketExpired = [LocalResendExpiration, CurrentTime](const FPendingPacket& ToCheck)
		{
			return LocalResendExpiration > 0.f && ToCheck.InitiallySent > 0.f && ((CurrentTime - ToCheck.InitiallySent) > LocalResendExpiration);
		};

		// Returns True if we can try another packet or False if we need to stop.
		auto TrySendPacket = [&SendPacket, &NewlyPendingPackets, bUsingReliability, CurrentTime](FPendingPacket&& Packet)
		{
			const FSendPacketReturnType Return = SendPacket(Packet.Packet);

			if (ESendPacketResult::BadPacket != Return.Result)
			{
				// Regardless of whether or not this bunch was sent, if we're using custom reliability
				// go ahead and track the send time of the bunch and put it into the correct queue.
				// We can skip this if the packet was sent reliably already.
				if (bUsingReliability && !Return.bSentReliably)
				{
					if (Packet.InitiallySent == 0)
					{
						Packet.InitiallySent = CurrentTime;
					}

					Packet.bWasNakd = false;
					NewlyPendingPackets.Emplace(MoveTemp(Packet));
				}

				// Something went wrong trying to send. Assume no future sends will succeed this tick, so we're done for now.
				if (ESendPacketResult::Saturated == Return.Result)
				{
					return false;
				}
			}

			return true;
		};


		// TODO: This might be cleaner if we use TResizableCircularQueue, and don't worry about evicting
		// expired packets until we attempt to send them. The downside being that we may hold onto
		// packet memory longer than necessary if the sender is saturated.
		// However, popping items from a TResizableCircularQueue doesn't destruct the item being popped
		// and all references are const.

		{
			int32 Index = 0;
			for (; Index < ToSend.Num(); ++Index)
			{
				if (!TrySendPacket(FPendingPacket{ ToSend[Index] }))
				{
					// If the connection is saturated, or we can't send anymore packets this flush,
					// then we're done.
					break;
				}
			}

			ToSend.RemoveAt(0, Index);
		}

		if (bUsingReliability)
		{
			int32 Index = 0;
			for (; Index < UnreliableResends.Num(); ++Index)
			{
				FPendingPacket& Packet = UnreliableResends[Index];
				if (HasPacketExpired(Packet) || (!Packet.bWasNakd && Packet.PacketRange.Last <= LastAckedPacket))
				{
					// Packet has expired or was Ackd, we don't need to resend or track it anymore.
					continue;
				}

				if (!Packet.bWasNakd)
				{
					// If we reach a packet that hasn't been NAKd or if we fail to send the
					// latest packet, we're done for now.
					break;
				}

				if (!TrySendPacket(MoveTemp(Packet)))
				{
					// In this case, we will have put the packet into the NewlyPendingPackets list already,
					// so make sure we handle that.
					++Index;
					break;
				}
			}

			// Remove any packets that we don't need to process anymore,
			// but go ahead and add in any packets that we'll need to try again later.
			const bool bAllowShrinking = (NewlyPendingPackets.Num() == 0);
			UnreliableResends.RemoveAt(0, Index, bAllowShrinking);
			UnreliableResends.Append(NewlyPendingPackets);
		}
	}

	void FForwardingChannel::ReceivedNak(int32 NakPacketId)
	{
		if (!Group->IsSubsystemInitialized() ||
			EChannelReliability::None == GetReliability())
		{
			return;
		}

		for (int32 i = 0; i < UnreliableResends.Num(); ++i)
		{
			FPendingPacket& Packet = UnreliableResends[i];

			// Skipped passed any already NAKd packets
			if (Packet.bWasNakd)
			{
				continue;
			}

			// NAKs should be handled in order, so treat this as an implicit ACK
			// because it means we must have received all the packets up to this one.
			if (NakPacketId > Packet.PacketRange.Last)
			{
				UnreliableResends.RemoveAt(i, 1);
				continue;
			}

			if (Packet.PacketRange.InRange(NakPacketId))
			{
				Packet.bWasNakd = true;
			}
			else
			{
				// We've gone beyond the NAKd packet, so we're done.
				break;
			}
		}
	}

	void FForwardingChannel::QueuePacketUnchecked(const TSharedRef<const FForwardingPacket>& PacketToSend)
	{
		ToSend.Add(PacketToSend);
	}

	void FForwardingChannel::QueuePacketsUnchecked(const TArray<TSharedRef<const FForwardingPacket>>& PacketsToSend)
	{
		ToSend.Append(PacketsToSend);
	}

	void FPacketHelper::QueuePacketUnchecked(FForwardingChannel& Channel, const TSharedRef<const FForwardingPacket>& PacketToSend)
	{
		Channel.QueuePacketUnchecked(PacketToSend);
	}

	void FPacketHelper::QueuePacketsUnchecked(FForwardingChannel& Channel, const TArray<TSharedRef<const FForwardingPacket>>& PacketsToSend)
	{
		Channel.QueuePacketsUnchecked(PacketsToSend);
	}

	bool FForwardingChannel::IsGroupInitialized() const
	{
		return Group->IsSubsystemInitialized();
	}
}