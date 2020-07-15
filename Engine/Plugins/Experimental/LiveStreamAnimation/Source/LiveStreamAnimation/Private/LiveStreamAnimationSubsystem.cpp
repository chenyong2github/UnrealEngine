// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LiveStreamAnimationSubsystem.h"
#include "LiveStreamAnimationLog.h"
#include "ControlPacket.h"
#include "LiveLink/LiveLinkPacket.h"
#include "LiveLink/LiveLinkStreamingHelper.h"
#include "LiveLink/LiveStreamAnimationLiveLinkFrameTranslator.h"
#include "Engine/NetConnection.h"
#include "EngineLogs.h"
#include "LiveStreamAnimationChannel.h"
#include "ForwardingChannel.h"
#include "ForwardingGroup.h"
#include "ForwardingChannelsUtils.h"

#include "ForwardingChannelsSubsystem.h"

ULiveStreamAnimationSubsystem::ULiveStreamAnimationSubsystem()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// These are just default names, but any can be added / removed
		// using Configs.
		HandleNames.Append({
			FName(TEXT("LiveLinkSubject1")),
			FName(TEXT("LiveLinkSubject2")),
			FName(TEXT("LiveLinkSubject3")),
			FName(TEXT("LiveLinkSubject4")),
			FName(TEXT("LiveLinkSubject5")),
			FName(TEXT("LiveLinkSubject6")),
			FName(TEXT("LiveLinkSubject7")),
			FName(TEXT("LiveLinkSubject8")),
			FName(TEXT("LiveLinkSubject9")),
			FName(TEXT("LiveLinkSubject10")),
			FName(TEXT("LiveStreamAnimationHandle1")),
			FName(TEXT("LiveStreamAnimationHandle2")),
			FName(TEXT("LiveStreamAnimationHandle3")),
			FName(TEXT("LiveStreamAnimationHandle4")),
			FName(TEXT("LiveStreamAnimationHandle5")),
			FName(TEXT("LiveStreamAnimationHandle6")),
			FName(TEXT("LiveStreamAnimationHandle7")),
			FName(TEXT("LiveStreamAnimationHandle8")),
			FName(TEXT("LiveStreamAnimationHandle9")),
			FName(TEXT("LiveStreamAnimationHandle10")),
			FName(TEXT("LiveLinkFrameTranslation1")),
			FName(TEXT("LiveLinkFrameTranslation2")),
			FName(TEXT("LiveLinkFrameTranslation3")),
			FName(TEXT("LiveLinkFrameTranslation4")),
			FName(TEXT("LiveLinkFrameTranslation5")),
			FName(TEXT("LiveLinkFrameTranslation6")),
			FName(TEXT("LiveLinkFrameTranslation7")),
			FName(TEXT("LiveLinkFrameTranslation8")),
			FName(TEXT("LiveLinkFrameTranslation9")),
			FName(TEXT("LiveLinkFrameTranslation10")),
		});
	}
}

void ULiveStreamAnimationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	using namespace LiveStreamAnimation;

	bInitialized = true;
	bShouldAcceptClientPackets = false;

	Collection.InitializeDependency(UForwardingChannelsSubsystem::StaticClass());

	if (UForwardingChannelsSubsystem* ForwardingChannelsSubsystem = GetSubsystem<UForwardingChannelsSubsystem>())
	{
		ForwardingChannelsSubsystem->RegisterForwardingChannelFactory(this);
		ForwardingGroup = ForwardingChannelsSubsystem->GetOrCreateForwardingGroup(GetChannelName());
	}

	LiveLinkStreamingHelper = MakeShared<FLiveLinkStreamingHelper>(*this);
}

void ULiveStreamAnimationSubsystem::Deinitialize()
{
	if (UForwardingChannelsSubsystem* ForwardingChannelsSubsystem = GetSubsystem<UForwardingChannelsSubsystem>())
	{
		ForwardingChannelsSubsystem->UnregisterForwardingChannelFactory(this);
	}

	bInitialized = false;
	ForwardingGroup.Reset();
	LiveLinkStreamingHelper.Reset();
	FrameTranslator.Reset();
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
	}
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
				
				TSharedRef<FLiveStreamAnimationPacket> InitialPacket = FLiveStreamAnimationPacket::CreateFromPacket(FControlInitialPacket()).ToSharedRef();
				InitialPacket->SetReliable(true);
				
				JoinInProgressPackets.Add(InitialPacket);
				LiveLinkStreamingHelper->GetJoinInProgressPackets(JoinInProgressPackets);
				
				ForwardingChannel->QueuePackets(JoinInProgressPackets);
			}
		}
	}
}

void ULiveStreamAnimationSubsystem::SetAcceptClientPackets(bool bInShouldAcceptClientPackets)
{
	SetAcceptClientPackets_Private(bInShouldAcceptClientPackets);
}

bool ULiveStreamAnimationSubsystem::StartTrackingLiveLinkSubject(
	const FName LiveLinkSubject,
	const FName RegisteredName,
	const FLiveStreamAnimationLiveLinkSourceOptions Options,
	const FName TranslationProfile)
{
	using namespace LiveStreamAnimation;
	
	if (!IsEnabledAndInitialized())
	{
		return false;
	}
	
	if (ELiveStreamAnimationRole::Tracker != Role)
	{
		UE_LOG(LogLiveStreamAnimation, Warning, TEXT("ULiveStreamAnimationSubsystem::StartTrackingLiveLinkSubject: Invalid role. %d"),
			static_cast<int32>(Role));

		return false;
	}

	return LiveLinkStreamingHelper->StartTrackingSubject(
		LiveLinkSubject,
		FLiveStreamAnimationHandle(RegisteredName),
		Options,
		FLiveStreamAnimationHandle(TranslationProfile));
}

void ULiveStreamAnimationSubsystem::StopTrackingLiveLinkSubject(const FName RegisteredName)
{
	using namespace LiveStreamAnimation;

	if (!IsEnabledAndInitialized())
	{
		return;
	}
	
	if (ELiveStreamAnimationRole::Tracker != Role)
	{
		UE_LOG(LogLiveStreamAnimation, Warning, TEXT("ULiveStreamAnimationSubsystem::StartTrackingLiveLinkSubject: Invalid role. %d"),
			static_cast<int32>(Role));

		return;
	}

	LiveLinkStreamingHelper->StopTrackingSubject(FLiveStreamAnimationHandle(RegisteredName));
}

void ULiveStreamAnimationSubsystem::SetLiveLinkFrameTranslator(TSoftObjectPtr<ULiveStreamAnimationLiveLinkFrameTranslator> NewTranslator)
{
	if (!IsEnabledAndInitialized())
	{
		return;
	}

	if (NewTranslator != FrameTranslator)
	{
		FrameTranslator = NewTranslator;
		OnLiveLinkFrameTranslatorChanged.Broadcast();
	}
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
	if (ELiveStreamAnimationRole::Tracker != Role)
	{
		switch (Packet->GetPacketType())
		{
		case ELiveStreamAnimationPacketType::LiveLink:
			LiveLinkStreamingHelper->HandleLiveLinkPacket(Packet);
			break;

		default:
			break;
		}

		ForwardingGroup->ForwardPacket(Packet, CreateDefaultForwardingFilter(FromChannel));
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

TWeakPtr<const LiveStreamAnimation::FSkelMeshToLiveLinkSource> ULiveStreamAnimationSubsystem::GetOrCreateSkelMeshToLiveLinkSource()
{
	if (!IsEnabledAndInitialized())
	{
		return nullptr;
	}

	return LiveLinkStreamingHelper->GetOrCreateSkelMeshToLiveLinkSource();
}

class ULiveStreamAnimationLiveLinkFrameTranslator* ULiveStreamAnimationSubsystem::GetLiveLinkFrameTranslator() const
{
	return FrameTranslator.LoadSynchronous();
}