// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ForwardingChannelsFwd.h"
#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Templates/Function.h"

namespace ForwardingChannels
{
	/**
	 * Function that can be used to filter out channels when forwarding packets.
	 *
	 * @param Channel	The channel that may be filtered.
	 *
	 * @return True if the forwarded packets should be passed to the channel, false otherwise.
	 */
	using FFilterChannelType = TUniqueFunction<bool(const FForwardingChannel&)>;

	/**
	 * Forwarding groups track sets of Channels that will be used for data forwarding.
	 * There is a one to many relationship between Server Channels (one) and Client Channels (many).
	 */
	class FForwardingGroup
	{
	public:
		explicit FForwardingGroup(const FName InGroupName);

		/**
		 * Called to register a channel with a forwarding group.
		 *
		 * @param InChannel		The Channel to register. Does nothing if the FForwardingChannelSubsystem is
		 *						not currently initialized.
		 */
		bool RegisterChannel(FForwardingChannel& InChannel);

		/**
		 * Called to unregister a channel with a forwarding group.
		 *
		 * @param InChannel		The Channel to unregister. Always removes the channel from the group regardless
		 *						of whether or not FForwardingChannelSubsystem is initialized.
		 */
		void UnregisterChannel(const FForwardingChannel& InChannel);

		FName GetName() const
		{
			return GroupName;
		}

		bool IsSubsystemInitialized() const
		{
			return bIsSubsystemInitialized;
		}

		void OnSubsystemDeinitialized();

		/**
		 * Forward a packet to be queued up on clients.
		 *
		 * @param PacketToSend		The packet to be forwarded.
		 * @param Filter			Optional filter that can be passed in to prevent forwarding the packet to particular channels.
		 */
		FORWARDINGCHANNELS_API void ForwardPacket(const TSharedRef<const FForwardingPacket> PacketToSend, FFilterChannelType Filter = FFilterChannelType());

		/**
		 * Forward packets to be queued up on clients.
		 *
		 * @param PacketsToSend		The packets to be forwarded.
		 * @param Filter			Optional filter that can be passed in to prevent forwarding packets to particular channels.
		 */
		FORWARDINGCHANNELS_API void ForwardPackets(const TArray<TSharedRef<const FForwardingPacket>>& PacketsToSend, FFilterChannelType Filter = FFilterChannelType());

		/**
		 * Queue a packet on the Server channel.
		 * Does nothing if the Server channel isn't valid / registered (@see IsServerChannelAvailable).
		 *
		 * @param PacketToSend		The packet to be queued.
		 * @param Filter			Optional filter that can be passed in to prevent forwarding the packet to particular channels.
		 */
		FORWARDINGCHANNELS_API void QueuePacketOnServer(const TSharedRef<const FForwardingPacket> PacketToSend, FFilterChannelType Filter = FFilterChannelType());
		
		/**
		 * Queue a packets on the Server channel.
		 * Does nothing if the Server channel isn't valid / registered (@see IsServerChannelAvailable).
		 *
		 * @param PackestToSend		The packets to be queued.
		 * @param Filter			Optional filter that can be passed in to prevent forwarding the packet to particular channels.
		 */
		FORWARDINGCHANNELS_API void QueuePacketsOnServer(const TArray<TSharedRef<const FForwardingPacket>>& PacketsToSend, FFilterChannelType Filter = FFilterChannelType());

		/** Whether or not the Server Channel is currently available / registered. */
		FORWARDINGCHANNELS_API bool IsServerChannelAvailable() const;

	private:

		bool bIsSubsystemInitialized;
		const FName GroupName;

		// We explicitly don't use TWeakPtr here for a few reasons:
		//	1. We explicitly tie the lifetime of a FForwardingChannel registration.
		//		That is, we won't allow you to create an FForwardingChannel unless they are registered
		//		and upon deletion, they are automatically unregistered.
		//
		//	2. If we tried to use Weak Pointers, we wouldn't be able to do automatic registration
		//		as mentioned above. TWeakPtr has no way to compare its internal pointer directly
		//		to an arbitrary pointer. The interfaces provided will attempt to create a shared
		//		pointer from the weak pointer and then compare, but since we do unregistration
		//		from the destructor, the reference count will already be 0 and the pointer will
		//		be invalid.
		//
		//
		// These are also not exposed via Getters / Setters to prevent external caching which could
		// lead to dangling points.
		//
		// If we absolutely needed that, we may be able to store these as WeakPointers, and then
		// try to verify that either the ServerChannel is already invalid or that there is 1 (and
		// only one) invalid entry in ClientChannels, and that none of the other entries in
		// ClientChannels point to the entry trying to be unregistered.
		FForwardingChannel* ServerChannel;
		TArray<FForwardingChannel*> ClientChannels;
	};
}
