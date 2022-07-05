// Copyright Epic Games, Inc. All Rights Reserved.
#include "StreamerInputChannels.h"

namespace UE::PixelStreaming
{
	FStreamerInputChannels::FStreamerInputChannels(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
		: MessageHandler(InMessageHandler)
	{
	}

	TSharedPtr<IPixelStreamingInputChannel> FStreamerInputChannels::CreateInputChannel()
	{
		TSharedPtr<IPixelStreamingInputChannel> NewInputChannel;

		if (OverridenCreateInputChannel)
		{
			NewInputChannel = OverridenCreateInputChannel(MessageHandler);
		}
		else
		{
			NewInputChannel = StaticCastSharedRef<IPixelStreamingInputChannel>(MakeShared<FPixelStreamingInputChannel>());			
		}

		{
			FScopeLock Lock(&InputChannelLock);
			InputChannels.Add(NewInputChannel);
		}

		return NewInputChannel;
	}

	void FStreamerInputChannels::Tick(float DeltaTime)
	{
		FScopeLock Lock(&InputChannelLock);
		ForEachChannel([DeltaTime](IInputDevice* InputChannel) {
			InputChannel->Tick(DeltaTime);
		});
	}

	void FStreamerInputChannels::SendControllerEvents()
	{
		FScopeLock Lock(&InputChannelLock);
		ForEachChannel([](IInputDevice* InputChannel) {
			InputChannel->SendControllerEvents();
		});
	}

	void FStreamerInputChannels::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	{
		FScopeLock Lock(&InputChannelLock);
		MessageHandler = InMessageHandler;
		ForEachChannel([&MessageHandler = MessageHandler](IInputDevice* InputChannel) {
			InputChannel->SetMessageHandler(MessageHandler);
		});
	}

	bool FStreamerInputChannels::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
	{
		FScopeLock Lock(&InputChannelLock);
		ForEachChannel([InWorld, Cmd, &Ar](IInputDevice* InputChannel) {
			InputChannel->Exec(InWorld, Cmd, Ar);
		});
		return true;
	}

	void FStreamerInputChannels::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
	{
		FScopeLock Lock(&InputChannelLock);
		ForEachChannel([&ControllerId, &ChannelType, &Value](IInputDevice* InputChannel) {
			InputChannel->SetChannelValue(ControllerId, ChannelType, Value);
		});
	}

	void FStreamerInputChannels::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values)
	{
		FScopeLock Lock(&InputChannelLock);
		ForEachChannel([&ControllerId, &Values](IInputDevice* InputChannel) {
			InputChannel->SetChannelValues(ControllerId, Values);
		});
	}
	
	void FStreamerInputChannels::OverrideInputChannel(IPixelStreamingInputChannel::FCreateInputChannelFunc& CreateInputChannelFunc)
	{
		OverridenCreateInputChannel = CreateInputChannelFunc;
	}

	void FStreamerInputChannels::ForEachChannel(TFunction<void(IInputDevice*)> const& Visitor)
	{
		for (int i = 0; i < InputChannels.Num(); /* skip */)
		{
			TWeakPtr<IInputDevice>& WeakChannel = InputChannels[i];
			TSharedPtr<IInputDevice> Channel = WeakChannel.Pin();
			if (Channel)
			{
				Visitor(Channel.Get());
				++i;
			}
			else
			{
				InputChannels.RemoveAt(i);
			}
		}
	}
} // namespace UE::PixelStreaming
