// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualScoutingMode.h"
#include "VirtualScoutingOpenXR.h"
#include "VirtualScoutingOpenXRModule.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#include "IXRTrackingSystem.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"


#define LOCTEXT_NAMESPACE "VirtualScouting"


DECLARE_LOG_CATEGORY_EXTERN(LogVirtualScouting, Log, All);
DEFINE_LOG_CATEGORY(LogVirtualScouting);


class FVirtualScoutingEditorModule : public IModuleInterface
{
private:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};


IMPLEMENT_MODULE(FVirtualScoutingEditorModule, VirtualScoutingEditor);


void UVirtualScoutingMode::Enter()
{
	if (!GEngine->XRSystem.IsValid() || GEngine->XRSystem->GetSystemName() != FName(TEXT("OpenXR")))
	{
		Super::Enter();
		return;
	}

	TSharedPtr<FVirtualScoutingOpenXRExtension> XrExt = FVirtualScoutingOpenXRModule::Get().GetOpenXRExt();
	if (!XrExt)
	{
		UE_LOG(LogVirtualScouting, Error, TEXT("OpenXR extension plugin invalid"));
		return;
	}

#if WITH_EDITOR
	// This causes FOpenXRInput to rebuild and reattach actions.
	FEditorDelegates::OnActionAxisMappingsChanged.Broadcast();
#endif

	// Split the mode entry into two phases. This is necessary because we have to poll OpenXR for
	// the active interaction profile and translate it into a legacy plugin name, but OpenXR may
	// not return the correct interaction profile for several frames after the OpenXR session
	// (stereo rendering) has started, and we need to defer creation of the interactors, etc.
	BeginEntry();

	XrExt->GetHmdDeviceTypeFuture() = XrExt->GetHmdDeviceTypeFuture().Next([this](FName DeviceType)
	{
		if (DeviceType != NAME_None)
		{
			SetHMDDeviceTypeOverride(DeviceType);
		}
		else
		{
			UE_LOG(LogVirtualScouting, Error, TEXT("Unable to map legacy HMD device type"));
		}

		SetupSubsystems();
		FinishEntry();

		return DeviceType;
	});
}

#undef LOCTEXT_NAMESPACE
