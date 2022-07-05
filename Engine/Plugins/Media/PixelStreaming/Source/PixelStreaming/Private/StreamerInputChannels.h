// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingInputChannel.h"
#include "PixelStreamingInputChannel.h"

namespace UE::PixelStreaming
{
	// This is basically a routing Channel that allows us to create an IInputDevice for each
	// streamer and have all the events pipe through to the main message handler in the module.
	class FStreamerInputChannels : public IInputDevice
	{
	public:
		FStreamerInputChannels(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
		virtual ~FStreamerInputChannels() = default;

		TSharedPtr<IPixelStreamingInputChannel> CreateInputChannel();
		virtual void Tick(float DeltaTime) override;
		virtual void SendControllerEvents() override;
		virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override;
		
		void OverrideInputChannel(IPixelStreamingInputChannel::FCreateInputChannelFunc& CreateInputChannelFunc);

	private:
		/** Reference to the message handler which events should be passed to. */
		TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
		TArray<TWeakPtr<IInputDevice>> InputChannels;
		FCriticalSection InputChannelLock;
				
		IPixelStreamingInputChannel::FCreateInputChannelFunc OverridenCreateInputChannel;

		void ForEachChannel(TFunction<void(IInputDevice*)> const& Visitor);
	};
} // namespace UE::PixelStreaming
