// Copyright (c) Microsoft Corporation. All rights reserved.

#include "WindowsMixedRealitySpatialInput.h"
#include "IWindowsMixedRealitySpatialInputPlugin.h"
#include "Framework/Application/SlateApplication.h"

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

private:
	TSharedPtr<IInputDevice> InputDevice;

};

IMPLEMENT_MODULE(FWindowsMixedRealitySpatialInputPlugin, WindowsMixedRealitySpatialInput)
