// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonInputSettings.h"
#include "CommonInputPrivatePCH.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"

UCommonInputSettings::UCommonInputSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, bInputDataLoaded(false)
{
	FCommonInputPlatformBaseData PcPlatformData;
	PcPlatformData.bSupported = true;
	PcPlatformData.DefaultInputType = ECommonInputType::MouseAndKeyboard;
	PcPlatformData.bSupportsMouseAndKeyboard = true;
	CommonInputPlatformData.Add(FCommonInputDefaults::PlatformPC, PcPlatformData);

	for (const TPair<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& Platform : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
	{
		const FName PlatformName = FName(Platform.Key);
		const FDataDrivenPlatformInfoRegistry::FPlatformInfo& PlatformInfo = Platform.Value;

		if (!PlatformInfo.bDefaultInputStandardKeyboard && PlatformInfo.bIsInteractablePlatform)
		{
			FCommonInputPlatformBaseData PlatformData;
			PlatformData.bSupported = PlatformInfo.bInputSupportConfigurable;
			if (PlatformInfo.DefaultInputType == "Gamepad")
			{
				PlatformData.DefaultInputType = ECommonInputType::Gamepad;
			}
			else if (PlatformInfo.DefaultInputType == "Touch")
			{
				PlatformData.DefaultInputType = ECommonInputType::Touch;
			}
			else if (PlatformInfo.DefaultInputType == "MouseAndKeyboard")
			{
				PlatformData.DefaultInputType = ECommonInputType::MouseAndKeyboard;
			}
			PlatformData.bSupportsMouseAndKeyboard = PlatformInfo.bSupportsMouseAndKeyboard;
			PlatformData.bSupportsGamepad = PlatformInfo.bSupportsGamepad;
			PlatformData.bCanChangeGamepadType = PlatformInfo.bSupportsTouch;
			PlatformData.bSupportsTouch = PlatformInfo.bCanChangeGamepadType;

			PlatformData.DefaultGamepadName = PlatformName;
			CommonInputPlatformData.Add(PlatformName, PlatformData);
		}
	}
}

void UCommonInputSettings::LoadData()
{
	LoadInputData();
}

#if WITH_EDITOR
void UCommonInputSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	bInputDataLoaded = false;
	LoadData();
}
#endif

const TArray<FName>& UCommonInputSettings::GetRegisteredPlatforms()
{
	return FCommonInputPlatformBaseData::GetRegisteredPlatforms();
}

void UCommonInputSettings::LoadInputData()
{
	if (!bInputDataLoaded)
	{
		// If we were created early enough to be disregarded by the GC (which we should be), then we need to 
		// add all of our members to the root set, since our hard reference to them is totally meaningless to the GC.
		const bool bIsDisregardForGC = GUObjectArray.IsDisregardForGC(this);
		
		InputDataClass = InputData.LoadSynchronous();
		if (InputDataClass)
		{
			if (bIsDisregardForGC)
			{
				InputDataClass->AddToRoot();
			}
		}
		
		CurrentPlatform = CommonInputPlatformData[FCommonInputBase::GetCurrentPlatformName()];
		for (TSoftClassPtr<UCommonInputBaseControllerData> ControllerData : CurrentPlatform.GetControllerData())
		{
			if (TSubclassOf<UCommonInputBaseControllerData> ControllerDataClass = ControllerData.LoadSynchronous())
			{
				CurrentPlatform.ControllerDataClasses.Add(ControllerDataClass);
				if (bIsDisregardForGC)
				{
					ControllerDataClass->AddToRoot();
				}
			}
		}
		bInputDataLoaded = true;
	}
}

void UCommonInputSettings::ValidateData()
{
    bInputDataLoaded &= !InputData.IsPending();
    for (TSoftClassPtr<UCommonInputBaseControllerData> ControllerData : CurrentPlatform.GetControllerData())
    {
		bInputDataLoaded &= CurrentPlatform.ControllerDataClasses.ContainsByPredicate([&ControllerData](const TSubclassOf<UCommonInputBaseControllerData>& ControllerDataClass)
			{
				return ControllerDataClass.Get() == ControllerData.Get();
			});

        bInputDataLoaded &= !ControllerData.IsPending();
    }
 
#if !WITH_EDITOR
    UE_CLOG(!bInputDataLoaded, LogCommonInput, Warning, TEXT("Trying to access unloaded CommmonInputSettings data. This may force a sync load."));
#endif // !WITH_EDITOR

    LoadData();
}

FDataTableRowHandle UCommonInputSettings::GetDefaultClickAction() const
{
	ensure(bInputDataLoaded);

	if (InputDataClass)
	{
		if (const UCommonUIInputData* InputDataPtr = InputDataClass.GetDefaultObject())
		{
			return InputDataPtr->DefaultClickAction;
		}
	}
	return FDataTableRowHandle();
}

FDataTableRowHandle UCommonInputSettings::GetDefaultBackAction() const
{
	ensure(bInputDataLoaded);

	if (InputDataClass)
	{
		if (const UCommonUIInputData* InputDataPtr = InputDataClass.GetDefaultObject())
		{
			return InputDataPtr->DefaultBackAction;
		}
	}
	return FDataTableRowHandle();
}

void UCommonInputSettings::GetCurrentPlatformDefaults(ECommonInputType& OutDefaultInputType, FName& OutDefaultGamepadName) const
{
	// Defaults can be accessed before platform data is fully loaded, so access them from the array rather than the cached CurrentPlatform.
	const FCommonInputPlatformBaseData& CurrentPlatformData = CommonInputPlatformData[FCommonInputBase::GetCurrentPlatformName()];
	OutDefaultInputType = CurrentPlatformData.GetDefaultInputType();
	OutDefaultGamepadName = CurrentPlatformData.GetDefaultGamepadName();
}

FCommonInputPlatformBaseData UCommonInputSettings::GetCurrentPlatform() const
{
    const_cast<UCommonInputSettings*>(this)->ValidateData();
    
	ensure(bInputDataLoaded);
	return CurrentPlatform;
}
