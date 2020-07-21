// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveStreamAnimationChannel.h"
#include "LiveStreamAnimationSubsystem.h"
#include "LiveStreamAnimationPacket.h"

#include "ForwardingChannel.h"
#include "ForwardingChannelsSubsystem.h"
#include "ForwardingChannelsUtils.h"

#include "Net/DataBunch.h"
#include "Engine/NetConnection.h"


ULiveStreamAnimationChannel::ULiveStreamAnimationChannel()
{
	ChName = ULiveStreamAnimationSubsystem::GetChannelName();
}

void ULiveStreamAnimationChannel::Init(UNetConnection* InConnection, int32 InChIndex, EChannelCreateFlags CreateFlags)
{
	using namespace ForwardingChannels;

	Super::Init(InConnection, InChIndex, CreateFlags);
	ForwardingChannel = CreateDefaultForwardingChannel(*this, FCreateChannelParams(ChName));
}

void ULiveStreamAnimationChannel::ReceivedBunch(FInBunch& Bunch)
{
	using namespace LiveStreamAnimation;
	if (ForwardingChannel.IsValid() && Connection && Connection->Driver && Connection->Driver->World)
	{
		if (ULiveStreamAnimationSubsystem* LiveStreamAnimationSubsystem = UGameInstance::GetSubsystem<ULiveStreamAnimationSubsystem>(Connection->Driver->World->GetGameInstance()))
		{
			while (!Bunch.AtEnd())
			{
				TSharedPtr<FLiveStreamAnimationPacket> NewPacket = FLiveStreamAnimationPacket::ReadFromStream(Bunch);
				if (NewPacket.IsValid())
				{
					NewPacket->SetReliable(Bunch.bReliable);
					LiveStreamAnimationSubsystem->ReceivedPacket(NewPacket.ToSharedRef(), *ForwardingChannel);
				}
				else
				{
					// Unable to serialize the data because the serializer doesn't exist or there was a problem with this packet
					Bunch.SetError();
					break;
				}
			}
		}
	}
}

void ULiveStreamAnimationChannel::Tick()
{
	using namespace ForwardingChannels;
	using namespace LiveStreamAnimation;

	if (ForwardingChannel.IsValid())
	{
		DefaultFlushPacketsForChannel<FLiveStreamAnimationPacket>(
			/*Channel=*/*this,
			/*ForwardingChannel=*/*ForwardingChannel,
			/*SendFlags=*/EDefaultSendPacketFlags::AllowMerging | EDefaultSendPacketFlags::IgnoreSaturation,
			/*IsPacketReliable=*/[](const FLiveStreamAnimationPacket& Packet)
			{
				return Packet.IsReliable();
			},
			/*WritePacket=*/&FLiveStreamAnimationPacket::WriteToStream
		);
	}
}

bool ULiveStreamAnimationChannel::CleanUp(const bool bForDestroy, EChannelCloseReason CloseReason)
{
	ForwardingChannel.Reset();
	return Super::CleanUp(bForDestroy, CloseReason);
}