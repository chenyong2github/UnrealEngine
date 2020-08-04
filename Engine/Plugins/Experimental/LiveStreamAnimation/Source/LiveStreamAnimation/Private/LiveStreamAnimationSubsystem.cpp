// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LiveStreamAnimationSubsystem.h"
#include "LiveStreamAnimationDataHandler.h"
#include "LiveStreamAnimationLog.h"
#include "LiveStreamAnimationChannel.h"
#include "LiveStreamAnimationSettings.h"
#include "LiveStreamAnimationControlPacket.h"
#include "Engine/NetConnection.h"
#include "EngineLogs.h"

#include "ForwardingChannel.h"
#include "ForwardingGroup.h"
#include "ForwardingChannelsUtils.h"
#include "HAL/IConsoleManager.h"

#include "ForwardingChannelsSubsystem.h"

namespace LiveStreamAnimationPrivate
{
	bool bAllowTrackersToReceivePackets = true;
	static FAutoConsoleVariableRef CVarSetNetDormancyEnabled(
		TEXT("LiveStreamAnimation.AllowTrackersToReceivePackets"),
		bAllowTrackersToReceivePackets,
		TEXT("When True, any Live Stream Animation Subsystem that is set as a Tracker will receive and process packets."),
		ECVF_Default);
}

ULiveStreamAnimationSubsystem::ULiveStreamAnimationSubsystem()
{
}

void ULiveStreamAnimationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	using namespace LiveStreamAnimation;

	if (!bEnabled)
	{
		UE_LOG(LogLiveStreamAnimation, Log, TEXT("ULiveStreamAnimationSubsystem::Initialize: Subsystem not enabled."));
		return;
	}

	Collection.InitializeDependency(UForwardingChannelsSubsystem::StaticClass());
	if (UForwardingChannelsSubsystem* ForwardingChannelsSubsystem = GetSubsystem<UForwardingChannelsSubsystem>())
	{
		bInitialized = true;
		bShouldAcceptClientPackets = false;
		ForwardingChannelsSubsystem->RegisterForwardingChannelFactory(this);
		ForwardingGroup = ForwardingChannelsSubsystem->GetOrCreateForwardingGroup(GetChannelName());

		const TArrayView<const FSoftClassPath> DataHandlerClasses = ULiveStreamAnimationSettings::GetConfiguredDataHandlers();
		ConfiguredDataHandlers.Empty();
		ConfiguredDataHandlers.SetNumZeroed(DataHandlerClasses.Num());

		for (int32 i = 0; i < DataHandlerClasses.Num(); ++i)
		{
			if (UClass* DataHandlerClass = DataHandlerClasses[i].TryLoadClass<ULiveStreamAnimationDataHandler>())
			{
				if (ULiveStreamAnimationDataHandler* DataHandlerObject = NewObject<ULiveStreamAnimationDataHandler>(GetTransientPackage(), DataHandlerClass))
				{
					ConfiguredDataHandlers[i] = DataHandlerObject;

					// +1 because 0 is reserved for control messages from the LiveStreamAnimation subsystem.
					const uint32 PacketId = static_cast<uint32>(i + 1);

					DataHandlerObject->Startup(this, static_cast<uint32>(i + 1));

					UE_LOG(LogLiveStreamAnimation, Log, TEXT("ULiveStreamAnimationSubsystem::Initialize: Registered DataHandler `%s` with DataHandlerIndex '%d' and PacketType '%u'"),
						*GetPathName(DataHandlerClass), i, PacketId);
				}
				else
				{
					UE_LOG(LogLiveStreamAnimation, Warning, TEXT("ULiveStreamAnimationSubsystem::Initialize: Unable to create instance of %s"), *GetPathName(DataHandlerClass));

				}
			}
			else
			{
				UE_LOG(LogLiveStreamAnimation, Warning, TEXT("ULiveStreamAnimationSubsystem::Initialize: Invalid class at index %d"), i);
			}
		}
	}
	else
	{
		UE_LOG(LogLiveStreamAnimation, Error, TEXT("ULiveStreamAnimationSubsystem::Initialize: Failed to retrieve ForwardingChannelsSubsystem!"));
	}
}

void ULiveStreamAnimationSubsystem::Deinitialize()
{
	if (UForwardingChannelsSubsystem* ForwardingChannelsSubsystem = GetSubsystem<UForwardingChannelsSubsystem>())
	{
		ForwardingChannelsSubsystem->UnregisterForwardingChannelFactory(this);
	}

	for (ULiveStreamAnimationDataHandler* DataHandlerObject : ConfiguredDataHandlers)
	{
		if (DataHandlerObject)
		{
			DataHandlerObject->Shutdown();
		}
	}

	bInitialized = false;
	ForwardingGroup.Reset();
	ConfiguredDataHandlers.Empty();
}

FName ULiveStreamAnimationSubsystem::GetChannelName()
{
	static const FName ChannelName(TEXT("LiveStreamAnimation"));
	return ChannelName;
}

void ULiveStreamAnimationSubsystem::SetRole(const ELiveStreamAnimationRole NewRole)
{
	if (!IsEnabledAndInitialized())
	{
		return;
	}

	if (Role != NewRole)
	{
		Role = NewRole;
		OnRoleChanged.Broadcast(Role);

		for (ULiveStreamAnimationDataHandler* DataHandlerObject : ConfiguredDataHandlers)
		{
			if (DataHandlerObject)
			{
				DataHandlerObject->OnAnimationRoleChanged(NewRole);
			}
		}
	}
}

ULiveStreamAnimationDataHandler* ULiveStreamAnimationSubsystem::GetDataHandler(TSubclassOf<ULiveStreamAnimationDataHandler> Type) const
{
	if (IsEnabledAndInitialized())
	{
		if (UClass* DataHandlerClass = Type.Get())
		{
			for (ULiveStreamAnimationDataHandler* DataHandler : ConfiguredDataHandlers)
			{
				if (DataHandler->IsA(DataHandlerClass))
				{
					return DataHandler;
				}
			}
		}
	}

	return nullptr;
}

void ULiveStreamAnimationSubsystem::CreateForwardingChannel(UNetConnection* InNetConnection)
{
	using namespace LiveStreamAnimation;
	using namespace ForwardingChannels;

	if (!IsEnabledAndInitialized())
	{
		return;
	}

	// We only allow direct creation of channels on the Server.
	// Clients will open their channels automatically upon seeing the server created channel.
	if (InNetConnection && InNetConnection->Driver && InNetConnection->Driver->IsServer())
	{
		if (ULiveStreamAnimationChannel* Channel = Cast<ULiveStreamAnimationChannel>(InNetConnection->CreateChannelByName(GetChannelName(), EChannelCreateFlags::OpenedLocally)))
		{
			if (FForwardingChannel* ForwardingChannel = Channel->GetForwardingChannel().Get())
			{
				TArray<TSharedRef<FLiveStreamAnimationPacket>> JoinInProgressPackets;
				JoinInProgressPackets.Reserve(10);
				
				// This first packet is an empty control packet.
				// We want to send this even if there are no other JIP packets, just to make sure we
				// can establish a reliable connection.

				// TODO: Instead of leaving this packet empty, we could send some sort of ID / Setting data
				//			to validate with clients.
				//			This could also help if we needed to record settings, etc., for Demo Playback
				//			as these should be the first packets of data the given channel will receive
				//			in that case.
				TSharedRef<FLiveStreamAnimationPacket> InitialPacket = FLiveStreamAnimationPacket::CreateFromData(0, TArray<uint8>()).ToSharedRef();
				InitialPacket->SetReliable(true);
				JoinInProgressPackets.Add(InitialPacket);
				
				TArray<TArray<uint8>> JIPPacketData;
				for (int32 i = 0; i < ConfiguredDataHandlers.Num(); ++i)
				{
					if (ULiveStreamAnimationDataHandler* DataHandlerObject = ConfiguredDataHandlers[i])
					{
						JIPPacketData.Reset();
						DataHandlerObject->GetJoinInProgressPackets(JIPPacketData);
						const uint32 PacketId = static_cast<uint32>(i + 1);
						for (TArray<uint8>& JIPData : JIPPacketData)
						{
							TSharedPtr<FLiveStreamAnimationPacket> JIPPacket = FLiveStreamAnimationPacket::CreateFromData(PacketId, MoveTemp(JIPData));
							if (JIPPacket.IsValid())
							{
								JoinInProgressPackets.Add(JIPPacket.ToSharedRef());
							}
							else
							{
								UE_LOG(LogLiveStreamAnimation, Warning, TEXT("ULiveStreamAnimationSubsystem::CreateForwardingChannel: Failed to create Join In Progress packet for Data Handler %s"),
									*GetPathNameSafe(DataHandlerObject->GetClass()));
							}
						}
					}
				}
				
				ForwardingChannel->QueuePackets(JoinInProgressPackets);
			}
		}
	}
}

void ULiveStreamAnimationSubsystem::SetAcceptClientPackets(bool bInShouldAcceptClientPackets)
{
	SetAcceptClientPackets_Private(bInShouldAcceptClientPackets);
}

void ULiveStreamAnimationSubsystem::ReceivedPacket(const TSharedRef<const LiveStreamAnimation::FLiveStreamAnimationPacket>& Packet, ForwardingChannels::FForwardingChannel& FromChannel)
{
	using namespace LiveStreamAnimation;

	if (!IsEnabledAndInitialized())
	{
		return;
	}

	// If we're receiving packets from a Forwarded Channel, our
	// Forwarding Group better be valid.
	if (!ForwardingGroup.IsValid() || ForwardingGroup.Get() != &FromChannel.GetGroup().Get())
	{
		UE_LOG(LogLiveStreamAnimation, Error, TEXT("ULiveStreamAnimationSubsystem::ReceivedPacket: Invalid group!"));
		ensure(false);
	}

	// If we've received a packet from a client and we shouldn't accept those, then throw it out.
	if (!FromChannel.IsServerChannel() && !bShouldAcceptClientPackets)
	{
		return;
	}

	// TODO: This could be made more generic through some registration or
	// forwarding scheme.

	// We should only receive packets from the server if we're acting as a Proxy
	// or Processor.
	if (ELiveStreamAnimationRole::Tracker != Role || LiveStreamAnimationPrivate::bAllowTrackersToReceivePackets)
	{
		const int32 PacketType = Packet->GetPacketType();
		const int32 DataHandlerIndex = PacketType - 1;

		// Control Packet.
		if (PacketType == 0)
		{
			// TODO: We have a control packet type defined, but right now it's not actually used.
			//		But, we could put configuration data in there, or data that we might need
			//		for replays (such as the settings at the time the replay was recorded,
			//		etc.)
			ForwardingGroup->ForwardPacket(Packet, CreateDefaultForwardingFilter(FromChannel));
		}
		else if (!ConfiguredDataHandlers.IsValidIndex(DataHandlerIndex))
		{
			UE_LOG(LogLiveStreamAnimation, Error, TEXT("ULiveStreamAnimationSubsystem::ReceivedPacket: Received packet with invalid type. PacketType = %d, DataHandlerIndex = %d"),
				PacketType, DataHandlerIndex);
		}
		else if (ULiveStreamAnimationDataHandler* DataHandler = ConfiguredDataHandlers[DataHandlerIndex])
		{
			DataHandler->OnPacketReceived(Packet->GetPacketData());
			ForwardingGroup->ForwardPacket(Packet, CreateDefaultForwardingFilter(FromChannel));
		}
		else
		{
			UE_LOG(LogLiveStreamAnimation, Error, TEXT("ULiveStreamAnimationSubsystem::ReceivedPacket: Received packet with type for invalid DataHandler. PacketType = %d, DataHandlerIndex = %d"),
				PacketType, DataHandlerIndex);
		}
	}
}

bool ULiveStreamAnimationSubsystem::SendPacketToServer(const TSharedRef<const LiveStreamAnimation::FLiveStreamAnimationPacket>& Packet)
{
	if (!IsEnabledAndInitialized())
	{
		return false;
	}

	if (!ForwardingGroup.IsValid())
	{
		UE_LOG(LogLiveStreamAnimation, Error, TEXT("ULiveStreamAnimationSubsystem::SendPacketToServer: Invalid group!"));
		ensure(false);
		return false;
	}

	if (ELiveStreamAnimationRole::Tracker == Role)
	{
		ForwardingGroup->QueuePacketOnServer(Packet);
		return true;
	}

	return false;
}