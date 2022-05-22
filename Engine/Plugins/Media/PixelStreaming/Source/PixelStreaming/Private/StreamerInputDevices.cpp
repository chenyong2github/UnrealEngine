// Copyright Epic Games, Inc. All Rights Reserved.
#include "StreamerInputDevices.h"

namespace UE::PixelStreaming
{
	FStreamerInputDevices::FStreamerInputDevices(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
		: MessageHandler(InMessageHandler)
	{
	}

	TSharedPtr<IPixelStreamingInputDevice> FStreamerInputDevices::CreateInputDevice()
	{
		TSharedPtr<IPixelStreamingInputDevice> NewInputDevice;

		if (OverridenCreateInputDevice)
		{
			NewInputDevice = OverridenCreateInputDevice(MessageHandler);
		}
		else
		{
			NewInputDevice = StaticCastSharedRef<IPixelStreamingInputDevice>(MakeShared<FInputDevice>(MessageHandler));			
		}

		{
			FScopeLock Lock(&InputDeviceLock);
			InputDevices.Add(NewInputDevice);
		}

		return NewInputDevice;
	}

	void FStreamerInputDevices::Tick(float DeltaTime)
	{
		FScopeLock Lock(&InputDeviceLock);
		ForEachDevice([DeltaTime](IInputDevice* Device) {
			Device->Tick(DeltaTime);
		});
	}

	void FStreamerInputDevices::SendControllerEvents()
	{
		FScopeLock Lock(&InputDeviceLock);
		ForEachDevice([](IInputDevice* Device) {
			Device->SendControllerEvents();
		});
	}

	void FStreamerInputDevices::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	{
		FScopeLock Lock(&InputDeviceLock);
		MessageHandler = InMessageHandler;
		ForEachDevice([&MessageHandler = MessageHandler](IInputDevice* Device) {
			Device->SetMessageHandler(MessageHandler);
		});
	}

	bool FStreamerInputDevices::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
	{
		FScopeLock Lock(&InputDeviceLock);
		ForEachDevice([InWorld, Cmd, &Ar](IInputDevice* Device) {
			Device->Exec(InWorld, Cmd, Ar);
		});
		return true;
	}

	void FStreamerInputDevices::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
	{
		FScopeLock Lock(&InputDeviceLock);
		ForEachDevice([&ControllerId, &ChannelType, &Value](IInputDevice* Device) {
			Device->SetChannelValue(ControllerId, ChannelType, Value);
		});
	}

	void FStreamerInputDevices::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values)
	{
		FScopeLock Lock(&InputDeviceLock);
		ForEachDevice([&ControllerId, &Values](IInputDevice* Device) {
			Device->SetChannelValues(ControllerId, Values);
		});
	}
	
	void FStreamerInputDevices::OverrideInputDevice(IPixelStreamingInputDevice::FCreateInputDeviceFunc& CreateInputDeviceFunc)
	{
		OverridenCreateInputDevice = CreateInputDeviceFunc;
	}

	void FStreamerInputDevices::ForEachDevice(TFunction<void(IInputDevice*)> const& Visitor)
	{
		for (int i = 0; i < InputDevices.Num(); /* skip */)
		{
			TWeakPtr<IInputDevice>& WeakDevice = InputDevices[i];
			TSharedPtr<IInputDevice> Device = WeakDevice.Pin();
			if (Device)
			{
				Visitor(Device.Get());
				++i;
			}
			else
			{
				InputDevices.RemoveAt(i);
			}
		}
	}
} // namespace UE::PixelStreaming
