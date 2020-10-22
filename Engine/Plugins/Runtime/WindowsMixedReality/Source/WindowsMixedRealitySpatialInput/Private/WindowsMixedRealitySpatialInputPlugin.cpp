// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealitySpatialInput.h"
#include "IWindowsMixedRealitySpatialInputPlugin.h"
#include "Framework/Application/SlateApplication.h"
#include "WindowsMixedRealitySpatialInputFunctionLibrary.h"
#include "WindowsMixedRealityStatics.h"

//---------------------------------------------------
// Microsoft Windows MixedReality SpatialInput plugin
//---------------------------------------------------

class FWindowsMixedRealitySpatialInputPlugin :
	public IWindowsMixedRealitySpatialInputPlugin
{
public:
	FWindowsMixedRealitySpatialInputPlugin()
	{
	}

	virtual void StartupModule() override
	{
		IInputDeviceModule::StartupModule();
		
		WindowsMixedReality::FWindowsMixedRealitySpatialInput::RegisterKeys();

		WindowsMixedReality::FWindowsMixedRealityStatics::ConfigureGesturesHandle = WindowsMixedReality::FWindowsMixedRealityStatics::OnConfigureGesturesDelegate.AddRaw(this, &FWindowsMixedRealitySpatialInputPlugin::OnConfigureGestures);
	}

	virtual TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override
	{
		if (!InputDevice.IsValid())
		{
			TSharedPtr<IInputDevice> WindowsMixedRealitySpatialInput = MakeShared< WindowsMixedReality::FWindowsMixedRealitySpatialInput>(InMessageHandler);
			InputDevice = WindowsMixedRealitySpatialInput;
			return InputDevice;
		}
		else
		{
			InputDevice.Get()->SetMessageHandler(InMessageHandler);
			return InputDevice;
		}
		return nullptr;
	}

	virtual TSharedPtr<IInputDevice> GetInputDevice() override
	{
		if (!InputDevice.IsValid())
		{
			InputDevice = CreateInputDevice(FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler());
		}
		return InputDevice;
	}

	void OnConfigureGestures(const FXRGestureConfig& InGestureConfig, bool& bSuccess) override
	{
		bSuccess = UWindowsMixedRealitySpatialInputFunctionLibrary::CaptureGestures(
			InGestureConfig.bTap,
			InGestureConfig.bHold,
			(ESpatialInputAxisGestureType)InGestureConfig.AxisGesture,
			InGestureConfig.bNavigationAxisX,
			InGestureConfig.bNavigationAxisY,
			InGestureConfig.bNavigationAxisZ);
	}

private:
	TSharedPtr<IInputDevice> InputDevice;

};


IMPLEMENT_MODULE(FWindowsMixedRealitySpatialInputPlugin, WindowsMixedRealitySpatialInput)
