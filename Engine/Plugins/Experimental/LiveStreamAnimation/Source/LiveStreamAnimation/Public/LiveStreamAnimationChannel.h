// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Channel.h"
#include "ForwardingChannelsFwd.h"
#include "LiveStreamAnimationChannel.generated.h"

namespace LiveStreamAnimation
{
	class FLiveStreamAnimationPacket;
}

UCLASS(Transient)
class ULiveStreamAnimationChannel : public UChannel
{
	GENERATED_BODY()

public:

	ULiveStreamAnimationChannel();

	//~ Begin UChannel interface
	virtual void Init(UNetConnection* InConnection, int32 InChIndex, EChannelCreateFlags CreateFlags) override;
protected:
	virtual void ReceivedBunch(FInBunch& Bunch) override;
	virtual void Tick() override;
	virtual bool CanStopTicking() const override { return false; }
	virtual bool CleanUp(const bool bForDestroy, EChannelCloseReason CloseReason) override;
	virtual FString Describe() override
	{
		return FString(TEXT("LiveStreamAnimation: ")) + UChannel::Describe();
	}
	//~ End UChannel Interface

private:

	TSharedPtr<ForwardingChannels::FForwardingChannel> ForwardingChannel;
	
public:

	TSharedPtr<ForwardingChannels::FForwardingChannel> GetForwardingChannel()
	{
		return ForwardingChannel;
	}

	TSharedPtr<const ForwardingChannels::FForwardingChannel> GetForwardingChannel() const
	{
		return ForwardingChannel;
	}
};