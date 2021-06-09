// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonInputSettings.h"
#include "CommonInputPrivatePCH.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "CommonInputBaseTypes.h"
#include "Engine/PlatformSettings.h"

UCommonInputSettings::UCommonInputSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, bInputDataLoaded(false)
{
#if WITH_EDITOR
	PlatformInput.Settings = UPlatformSettings::GetAllPlatformSettings<UCommonInputPlatformSettings>();
#endif
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
		
		//CurrentPlatform = CommonInputPlatformData[FCommonInputBase::GetCurrentPlatformName()];
		//for (TSoftClassPtr<UCommonInputBaseControllerData> ControllerData : CurrentPlatform.GetControllerData())
		//{
		//	if (TSubclassOf<UCommonInputBaseControllerData> ControllerDataClass = ControllerData.LoadSynchronous())
		//	{
		//		CurrentPlatform.ControllerDataClasses.Add(ControllerDataClass);
		//		if (bIsDisregardForGC)
		//		{
		//			ControllerDataClass->AddToRoot();
		//		}
		//	}
		//}
		bInputDataLoaded = true;
	}
}

void UCommonInputSettings::ValidateData()
{
    bInputDataLoaded &= !InputData.IsPending();
  //  for (TSoftClassPtr<UCommonInputBaseControllerData> ControllerData : CurrentPlatform.GetControllerData())
  //  {
		//bInputDataLoaded &= CurrentPlatform.ControllerDataClasses.ContainsByPredicate([&ControllerData](const TSubclassOf<UCommonInputBaseControllerData>& ControllerDataClass)
		//	{
		//		return ControllerDataClass.Get() == ControllerData.Get();
		//	});

  //      bInputDataLoaded &= !ControllerData.IsPending();
  //  }
 
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

void UCommonInputSettings::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITOR
	if (CommonInputPlatformData_DEPRECATED.Num())
	{
		for (const auto& PlatformData : CommonInputPlatformData_DEPRECATED)
		{
			const FCommonInputPlatformBaseData& OriginalData = PlatformData.Value;

			if (UCommonInputPlatformSettings* Settings = UPlatformSettings::GetSettingsForPlatform<UCommonInputPlatformSettings>(PlatformData.Key.ToString()))
			{
				Settings->bSupportsMouseAndKeyboard = OriginalData.bSupportsMouseAndKeyboard;
				Settings->bSupportsGamepad = OriginalData.bSupportsGamepad;
				Settings->bSupportsTouch = OriginalData.bSupportsTouch;
				Settings->bCanChangeGamepadType = OriginalData.bCanChangeGamepadType;
				Settings->DefaultGamepadName = OriginalData.DefaultGamepadName;
				Settings->DefaultInputType = OriginalData.DefaultInputType;
				Settings->ControllerData = OriginalData.ControllerData;
			}
			else if (PlatformData.Key == FCommonInputDefaults::PlatformPC)
			{
				TArray<UCommonInputPlatformSettings*> PCPlatforms;
				PCPlatforms.Add(UPlatformSettings::GetSettingsForPlatform<UCommonInputPlatformSettings>("Windows"));
				PCPlatforms.Add(UPlatformSettings::GetSettingsForPlatform<UCommonInputPlatformSettings>("WinGDK"));
				PCPlatforms.Add(UPlatformSettings::GetSettingsForPlatform<UCommonInputPlatformSettings>("Linux"));

				for (UCommonInputPlatformSettings* PCPlatform : PCPlatforms)
				{
					PCPlatform->bSupportsMouseAndKeyboard = OriginalData.bSupportsMouseAndKeyboard;
					PCPlatform->bSupportsGamepad = OriginalData.bSupportsGamepad;
					PCPlatform->bSupportsTouch = OriginalData.bSupportsTouch;
					PCPlatform->bCanChangeGamepadType = OriginalData.bCanChangeGamepadType;
					PCPlatform->DefaultGamepadName = OriginalData.DefaultGamepadName;
					PCPlatform->DefaultInputType = OriginalData.DefaultInputType;
					PCPlatform->ControllerData = OriginalData.ControllerData;
				}
			}
		}

		CommonInputPlatformData_DEPRECATED.Reset();
		UpdateDefaultConfigFile();
	}
#endif
}
