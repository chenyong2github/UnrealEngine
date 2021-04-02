// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonInputBaseTypes.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "CommonInputSettings.h"
#include "ICommonInputModule.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"

const FName FCommonInputDefaults::PlatformPC(TEXT("PC"));
const FName FCommonInputDefaults::GamepadGeneric(TEXT("Generic"));

FCommonInputKeyBrushConfiguration::FCommonInputKeyBrushConfiguration()
{
	KeyBrush.DrawAs = ESlateBrushDrawType::Image;
}

FCommonInputKeySetBrushConfiguration::FCommonInputKeySetBrushConfiguration()
{
	KeyBrush.DrawAs = ESlateBrushDrawType::Image;
}

bool UCommonUIInputData::NeedsLoadForServer() const
{
	const UUserInterfaceSettings* UISettings = GetDefault<UUserInterfaceSettings>(UUserInterfaceSettings::StaticClass());
	check(UISettings);
	return UISettings->bLoadWidgetsOnDedicatedServer;
}

bool UCommonInputBaseControllerData::NeedsLoadForServer() const
{
	const UUserInterfaceSettings* UISettings = GetDefault<UUserInterfaceSettings>(UUserInterfaceSettings::StaticClass());
	check(UISettings);
	return UISettings->bLoadWidgetsOnDedicatedServer;
}

bool UCommonInputBaseControllerData::TryGetInputBrush(FSlateBrush& OutBrush, const FKey& Key) const
{
	const FCommonInputKeyBrushConfiguration* DisplayConfig = InputBrushDataMap.FindByPredicate([&Key](const FCommonInputKeyBrushConfiguration& KeyBrushPair) -> bool
	{
		return KeyBrushPair.Key == Key;
	});

	if (DisplayConfig)
	{
		OutBrush = DisplayConfig->GetInputBrush();
		return true;
	}

	return false;
}

bool UCommonInputBaseControllerData::TryGetInputBrush(FSlateBrush& OutBrush, const TArray<FKey>& Keys) const
{
	if (Keys.Num() == 0)
	{
		return false;
	}

	if (Keys.Num() == 1)
	{
		return TryGetInputBrush(OutBrush, Keys[0]);
	}

	const FCommonInputKeySetBrushConfiguration* DisplayConfig = InputBrushKeySets.FindByPredicate([&Keys](const FCommonInputKeySetBrushConfiguration& KeyBrushPair) -> bool
	{
		if (KeyBrushPair.Keys.Num() < 2)
		{
			return false;
		}

		if (Keys.Num() == KeyBrushPair.Keys.Num())
		{
			for (const FKey& Key : Keys)
			{
				if (!KeyBrushPair.Keys.Contains(Key))
				{
					return false;
				}
			}

			return true;
		}

		return false;
	});

	if (DisplayConfig)
	{
		OutBrush = DisplayConfig->GetInputBrush();
		return true;
	}

	return false;
}

void UCommonInputBaseControllerData::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	if (TargetPlatform == nullptr)
	{
		// These have been organized by a human already, better to sort using this array.
		TArray<FKey> AllKeys;
		EKeys::GetAllKeys(AllKeys);

		// Organize the keys so they're nice and clean
		InputBrushDataMap.Sort([&AllKeys](const FCommonInputKeyBrushConfiguration& A, const FCommonInputKeyBrushConfiguration& B) {
			return AllKeys.IndexOfByKey(A.Key) < AllKeys.IndexOfByKey(B.Key);
		});

		// Delete any brush data where we have no image assigned
		InputBrushDataMap.RemoveAll([](const FCommonInputKeyBrushConfiguration& A) {
			return A.GetInputBrush().GetResourceObject() == nullptr;
		});
	}
}

const TArray<FName>& UCommonInputBaseControllerData::GetRegisteredGamepads()
{
	auto GenerateRegisteredGamepads = []()
	{
		TArray<FName> RegisteredGamepads;
		RegisteredGamepads.Add(FCommonInputDefaults::GamepadGeneric);

		for (const TPair<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& Platform : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
		{
			const FName PlatformName = FName(Platform.Key);
			const FDataDrivenPlatformInfoRegistry::FPlatformInfo& PlatformInfo = Platform.Value;

			if (!PlatformInfo.bDefaultInputStandardKeyboard && PlatformInfo.bIsInteractablePlatform && PlatformInfo.bHasDedicatedGamepad)
			{
				RegisteredGamepads.Add(PlatformName);
			}
		}
		return RegisteredGamepads;
	};
	static TArray<FName> RegisteredGamepads = GenerateRegisteredGamepads();
	return RegisteredGamepads;
}

bool FCommonInputPlatformBaseData::TryGetInputBrush(FSlateBrush& OutBrush, FKey Key, ECommonInputType InputType, const FName& GamepadName) const
{
	if (ControllerDataClasses.Num() > 0)
	{
		for (const TSubclassOf<UCommonInputBaseControllerData>& ControllerDataPtr : ControllerDataClasses)
		{
			const UCommonInputBaseControllerData* DefaultControllerData = ControllerDataPtr.GetDefaultObject();
			if (DefaultControllerData && DefaultControllerData->InputType == InputType)
			{
				if (DefaultControllerData->InputType != ECommonInputType::Gamepad || DefaultControllerData->GamepadName == GamepadName)
				{
					return DefaultControllerData->TryGetInputBrush(OutBrush, Key);
				}
			}
		}
	}

	return false;
}

bool FCommonInputPlatformBaseData::TryGetInputBrush(FSlateBrush& OutBrush, const TArray<FKey>& Keys, ECommonInputType InputType, const FName& GamepadName) const
{
	if (ControllerDataClasses.Num() > 0)
	{
		for (const TSubclassOf<UCommonInputBaseControllerData>& ControllerDataPtr : ControllerDataClasses)
		{
			const UCommonInputBaseControllerData* DefaultControllerData = ControllerDataPtr.GetDefaultObject();
			if (DefaultControllerData && DefaultControllerData->InputType == InputType)
			{
				if (DefaultControllerData->InputType != ECommonInputType::Gamepad || DefaultControllerData->GamepadName == GamepadName)
				{
					return DefaultControllerData->TryGetInputBrush(OutBrush, Keys);
				}
			}
		}
	}

	return false;
}

const TArray<FName>& FCommonInputPlatformBaseData::GetRegisteredPlatforms()
{
	auto GenerateRegisteredPlatforms = []()
	{
		TArray<FName> RegisteredPlatforms;
		RegisteredPlatforms.Add(FCommonInputDefaults::PlatformPC);

		for (const TPair<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& Platform : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
		{
			const FName PlatformName = FName(Platform.Key);
			const FDataDrivenPlatformInfoRegistry::FPlatformInfo& PlatformInfo = Platform.Value;

			if (!PlatformInfo.bDefaultInputStandardKeyboard && PlatformInfo.bIsInteractablePlatform)
			{
				RegisteredPlatforms.Add(PlatformName);
			}
		}
		return RegisteredPlatforms;
	};
	static TArray<FName> RegisteredPlatforms = GenerateRegisteredPlatforms();
	return RegisteredPlatforms;
}

FName FCommonInputBase::GetCurrentPlatformName()
{
#if defined(UE_COMMONINPUT_PLATFORM_TYPE)
	return FName(PREPROCESSOR_TO_STRING(UE_COMMONINPUT_PLATFORM_TYPE));
#else
	return FCommonInputDefaults::PlatformPC;
#endif
}

UCommonInputSettings* FCommonInputBase::GetInputSettings()
{
	return &ICommonInputModule::GetSettings();
}

void FCommonInputBase::GetCurrentPlatformDefaults(ECommonInputType& OutDefaultInputType, FName& OutDefaultGamepadName)
{
	return ICommonInputModule::GetSettings().GetCurrentPlatformDefaults(OutDefaultInputType, OutDefaultGamepadName);
}

FCommonInputPlatformBaseData FCommonInputBase::GetCurrentBasePlatformData()
{
	return ICommonInputModule::GetSettings().GetCurrentPlatform();
}
