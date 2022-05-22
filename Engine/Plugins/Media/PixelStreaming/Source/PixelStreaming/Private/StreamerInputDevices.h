// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingInputDevice.h"
#include "InputDevice.h"

namespace UE::PixelStreaming
{
	class FStreamerInputDevices : public IInputDevice
	{
	public:
		FStreamerInputDevices(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
		virtual ~FStreamerInputDevices() = default;

		TSharedPtr<IPixelStreamingInputDevice> CreateInputDevice();
		virtual void Tick(float DeltaTime) override;
		virtual void SendControllerEvents() override;
		virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override;
		
		void OverrideInputDevice(IPixelStreamingInputDevice::FCreateInputDeviceFunc& CreateInputDeviceFunc);

	private:
		/** Reference to the message handler which events should be passed to. */
		TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
		TArray<TWeakPtr<IInputDevice>> InputDevices;
		FCriticalSection InputDeviceLock;
				
		IPixelStreamingInputDevice::FCreateInputDeviceFunc OverridenCreateInputDevice;

		void ForEachDevice(TFunction<void(IInputDevice*)> const& Visitor);
	};
} // namespace UE::PixelStreaming
