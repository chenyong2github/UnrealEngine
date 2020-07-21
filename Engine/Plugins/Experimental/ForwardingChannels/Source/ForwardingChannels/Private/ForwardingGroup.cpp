// Copyright Epic Games, Inc. All Rights Reserved.

#include "ForwardingGroup.h"
#include "ForwardingChannel.h"
#include "EngineLogs.h"

namespace ForwardingChannels
{
	FForwardingGroup::FForwardingGroup(const FName InGroupName)
		: bIsSubsystemInitialized(true)
		, GroupName(InGroupName)
		, ServerChannel(nullptr)
	{
	}

	bool FForwardingGroup::RegisterChannel(FForwardingChannel& InChannel)
	{
		if (IsSubsystemInitialized())
		{
			if (&InChannel.GetGroup().Get() == this)
			{
				if (InChannel.IsServerChannel())
				{

					if (nullptr != ServerChannel && ServerChannel != &InChannel)
					{
						UE_LOG(LogNet, Warning,
							TEXT("FForwardingGroup::RegisterChannel: Registering new server channel when another is already registered. GroupName=%s"),
							*GroupName.ToString());
					}

					ServerChannel = &InChannel;
					return true;
				}
				else
				{
					ClientChannels.AddUnique(&InChannel);
					return true;
				}
			}
			else
			{
				UE_LOG(LogNet, Warning,
					TEXT("FForwardingGroup::RegisterChannel: Unable to register channel for a different group. This Group=%s, Channels Group=%s"),
					*GroupName.ToString(), *InChannel.GetGroup()->GetName().ToString());
			}
		}
		else
		{
			UE_LOG(LogNet, Warning,
				TEXT("FForwardingGroup::RegisterChannel: Unable to register channel while subsystem is uninitialized. This Group=%s"),
				*GroupName.ToString());
		}

		return false;
	}

	void FForwardingGroup::UnregisterChannel(const FForwardingChannel& InChannel)
	{
		if (&InChannel.GetGroup().Get() == this)
		{
			if (InChannel.IsServerChannel())
			{
				if (&InChannel == ServerChannel)
				{
					ServerChannel = nullptr;
				}
				else
				{
					UE_LOG(LogNet, Warning,
						TEXT("FForwardingGroup::UnregisterChannel: Attempted to unregister non-associated server channel. This Group=%s, Channels Group=%s"),
						*GroupName.ToString(),
						*InChannel.GetGroup()->GetName().ToString());
				}
			}
			else
			{
				const int32 NumRemoved = ClientChannels.RemoveSingle(const_cast<FForwardingChannel*>(&InChannel));
				if (NumRemoved != 1)
				{
					UE_LOG(LogNet, Warning,
						TEXT("FForwardingGroup::UnregisterChannel: Failed to find channel being unregistered. This Group=%s, Channels Group=%s"),
						*GroupName.ToString(),
						*InChannel.GetGroup()->GetName().ToString());
				}
			}
		}
		else
		{
			UE_LOG(LogNet, Warning,
				TEXT("FForwardingGroup::RegisterChannel: Unable to unregister channel for a different group. This Group=%s, Channels Group=%s"),
				*GroupName.ToString(),
				*InChannel.GetGroup()->GetName().ToString());
		}
	}

	void FForwardingGroup::OnSubsystemDeinitialized()
	{
		if (ServerChannel)
		{
			ServerChannel->OnSubsystemDeinitialized();
		}

		for (FForwardingChannel* ClientChannel : ClientChannels)
		{
			ClientChannel->OnSubsystemDeinitialized();
		}

		bIsSubsystemInitialized = false;
	}

	void FForwardingGroup::ForwardPacket(const TSharedRef<const FForwardingPacket> PacketToSend, FFilterChannelType Filter)
	{
		if (!IsSubsystemInitialized())
		{
			return;
		}

		if (!Filter)
		{
			// Queue up the packet to be sent the next time we tick.
			for (FForwardingChannel* ClientChannel : ClientChannels)
			{
				FPacketHelper::QueuePacketUnchecked(*ClientChannel, PacketToSend);
			}
		}
		else
		{
			// Queue up the packet to be sent the next time we tick.
			for (FForwardingChannel* ClientChannel : ClientChannels)
			{
				if (Filter(*ClientChannel))
				{
					FPacketHelper::QueuePacketUnchecked(*ClientChannel, PacketToSend);
				}
			}
		}
	}

	void FForwardingGroup::ForwardPackets(const TArray<TSharedRef<const FForwardingPacket>>& PacketsToSend, FFilterChannelType Filter)
	{
		if (!IsSubsystemInitialized() || PacketsToSend.Num() <= 0)
		{
			return;
		}

		if (!Filter)
		{
			// Queue up the packet to be sent the next time we tick.
			for (FForwardingChannel* ClientChannel : ClientChannels)
			{
				FPacketHelper::QueuePacketsUnchecked(*ClientChannel, PacketsToSend);
			}
		}
		else
		{
			// Queue up the packet to be sent the next time we tick.
			for (FForwardingChannel* ClientChannel : ClientChannels)
			{
				if (Filter(*ClientChannel))
				{
					FPacketHelper::QueuePacketsUnchecked(*ClientChannel, PacketsToSend);
				}
			}
		}
	}

	void FForwardingGroup::QueuePacketOnServer(const TSharedRef<const FForwardingPacket> PacketToSend, FFilterChannelType Filter)
	{
		if (!IsSubsystemInitialized() || nullptr == ServerChannel)
		{
			return;
		}

		if (!Filter || Filter(*ServerChannel))
		{
			FPacketHelper::QueuePacketUnchecked(*ServerChannel, PacketToSend);
		}
	}

	void FForwardingGroup::QueuePacketsOnServer(const TArray<TSharedRef<const FForwardingPacket>>& PacketsToSend, FFilterChannelType Filter)
	{
		if (!IsSubsystemInitialized() || PacketsToSend.Num() <= 0 || nullptr == ServerChannel)
		{
			return;
		}

		if (!Filter || Filter(*ServerChannel))
		{
			FPacketHelper::QueuePacketsUnchecked(*ServerChannel, PacketsToSend);
		}
	}

	bool FForwardingGroup::IsServerChannelAvailable() const
	{
		return ServerChannel != nullptr;
	}
}
