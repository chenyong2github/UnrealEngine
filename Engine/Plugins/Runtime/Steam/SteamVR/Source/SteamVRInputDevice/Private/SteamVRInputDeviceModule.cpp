// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ISteamVRInputDeviceModule.h"
#include "IInputDevice.h"
#include "Runtime/Projects/Public/Interfaces/IPluginManager.h"
#include "SteamVRInputDevice.h"
#include "Engine/Engine.h"
#include "IXRTrackingSystem.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor.h"
#endif

class FSteamVRInputDeviceModule : public ISteamVRInputDeviceModule
{
	/* Creates a new instance of SteamVR Input Controller **/
	virtual TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override
	{
		static FName SystemName(TEXT("SteamVR"));
		if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
		{
			TSharedPtr<class FSteamVRInputDevice> Device(new FSteamVRInputDevice(InMessageHandler));
#if WITH_EDITOR
			FEditorDelegates::OnActionAxisMappingsChanged.AddSP(Device.ToSharedRef(), &FSteamVRInputDevice::OnActionMappingsChanged);
#endif
			return Device;
		}
		return nullptr;
	}

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

void FSteamVRInputDeviceModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
}

void FSteamVRInputDeviceModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

IMPLEMENT_MODULE(FSteamVRInputDeviceModule, SteamVRInputDevice)
