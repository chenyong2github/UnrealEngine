// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/DataDrivenInputActionProcessor.h"
#include "Input/CommonGenericInputActionDataTable.h"
#include "CommonUITypes.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Misc/ConfigCacheIni.h"

const static FName GenericForwardAction = FName("GenericForward");
const static FName GenericBackAction = FName("GenericBack");

void UDataDrivenInputActionProcessor::ProcessInputActions(UCommonGenericInputActionDataTable* InputActionDataTable)
{
	for (const TPair<FName, FGenericDataDrivenInputInfo>& InputInfo : UDataDrivenInputActionProcessor::GetDataDrivenInputInfo())
	{
		const FName& PlatformName = InputInfo.Key;
		const FGenericDataDrivenInputInfo& PlatformInputInfo = InputInfo.Value;

		if (PlatformInputInfo.bSwapForwardAndBackButtons)
		{
			FCommonInputActionDataBase* GenericForward = InputActionDataTable->FindRow<FCommonInputActionDataBase>(GenericForwardAction, TEXT("PostLoad"), false);
			if (GenericForward && !GenericForward->HasGamepadInputOverride(PlatformName))
			{
				FCommonInputTypeInfo GamepadInputInfo = GenericForward->GetDefaultGamepadInputTypeInfo();
				GamepadInputInfo.SetKey(EKeys::Gamepad_FaceButton_Right);
				GenericForward->AddGamepadInputOverride(PlatformName, GamepadInputInfo);
			}

			FCommonInputActionDataBase* GenericBackward = InputActionDataTable->FindRow<FCommonInputActionDataBase>(GenericBackAction, TEXT("PostLoad"), false);
			if (GenericBackward && !GenericBackward->HasGamepadInputOverride(PlatformName))
			{
				FCommonInputTypeInfo GamepadInputInfo = GenericBackward->GetDefaultGamepadInputTypeInfo();
				GamepadInputInfo.SetKey(EKeys::Gamepad_FaceButton_Bottom);
				GenericBackward->AddGamepadInputOverride(PlatformName, GamepadInputInfo);
			}
		}
	}
}

const TMap<FName, FGenericDataDrivenInputInfo> UDataDrivenInputActionProcessor::GetDataDrivenInputInfo()
{
	auto ParseDataDrivenInputInfo = [&]()
	{
		TMap <FName, FGenericDataDrivenInputInfo> PlatformInputInfos;
		int32 NumDDInfoFiles = FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles();

		const TCHAR* InputInfoSection = TEXT("InputInfo ");
		for (int32 Index = 0; Index < NumDDInfoFiles; Index++)
		{
			FConfigFile IniFile;
			FString PlatformName;

			FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(Index, IniFile, PlatformName);

			for (TPair<FString, FConfigSection>& Section : IniFile)
			{
				if (Section.Key.StartsWith(InputInfoSection))
				{
					FGenericDataDrivenInputInfo PlatformInputInfo;
					PlatformInputInfo.bSwapForwardAndBackButtons = FCString::ToBool(*Section.Value.FindRef("bSwapForwardAndBackButtons").GetValue());

					PlatformInputInfos.Add(FName(PlatformName), PlatformInputInfo);
				}
			}
		}

		return PlatformInputInfos;
	};

	static TMap<FName, FGenericDataDrivenInputInfo> DataDrivenInputInfo = ParseDataDrivenInputInfo();
	return DataDrivenInputInfo;
}
