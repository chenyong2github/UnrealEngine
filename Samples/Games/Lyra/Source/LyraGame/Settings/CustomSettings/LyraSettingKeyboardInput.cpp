// Copyright Epic Games, Inc. All Rights Reserved.

#include "LyraSettingKeyboardInput.h"

#include "../LyraSettingsLocal.h"
#include "Player/LyraLocalPlayer.h"
#include "PlayerMappableInputConfig.h"
#include "PlayerMappableKeySettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LyraSettingKeyboardInput)

class ULocalPlayer;

#define LOCTEXT_NAMESPACE "LyraSettings"

void FKeyboardOption::ResetToDefault()
{
	if (OwningConfig)
	{
		InputMapping = OwningConfig->GetMappingByName(InputMapping.GetMappingName());	
	}
	// If we don't have an owning config, then there is no default binding for this and it can simply be removed
	else
	{
		InputMapping = FEnhancedActionKeyMapping();
	}
}

void FKeyboardOption::SetInitialValue(FKey InKey)
{
	InitialMapping = InKey;
}

ULyraSettingKeyboardInput::ULyraSettingKeyboardInput()
{
	bReportAnalytics = false;
}

void ULyraSettingKeyboardInput::OnInitialized()
{
	DynamicDetails = FGetGameSettingsDetails::CreateLambda([this](ULocalPlayer&)
	{
		if (UPlayerMappableKeySettings* Settings = FirstMappableOption.InputMapping.GetPlayerMappableKeySettings())
		{
			return FText::Format(LOCTEXT("DynamicDetails_KeyboardInputAction", "Bindings for {0}"), Settings->DisplayName);
		}
		return FText::GetEmpty();
	});

	Super::OnInitialized();
}

void ULyraSettingKeyboardInput::SetInputData(FEnhancedActionKeyMapping& BaseMapping, const UPlayerMappableInputConfig* InOwningConfig, int32 InKeyBindSlot)
{
	if (InKeyBindSlot == 0)
	{
		FirstMappableOption.InputMapping = BaseMapping;
		FirstMappableOption.OwningConfig = InOwningConfig;
		FirstMappableOption.SetInitialValue(BaseMapping.Key);
	}
	else if (InKeyBindSlot == 1)
	{
		SecondaryMappableOption.InputMapping = BaseMapping;
		SecondaryMappableOption.OwningConfig = InOwningConfig;
		SecondaryMappableOption.SetInitialValue(BaseMapping.Key);
	}
	else
	{
		ensureMsgf(false, TEXT("Invalid key bind slot provided!"));
	}

	
	const FString NameString = TEXT("KBM_Input_") + FirstMappableOption.InputMapping.GetMappingName().ToString();
	SetDevName(*NameString);
	SetDisplayName(FirstMappableOption.InputMapping.GetDisplayName());
}

FText ULyraSettingKeyboardInput::GetPrimaryKeyText() const
{
	return FirstMappableOption.InputMapping.Key.GetDisplayName();
}

FText ULyraSettingKeyboardInput::GetSecondaryKeyText() const
{
	return SecondaryMappableOption.InputMapping.Key.GetDisplayName();
}

void ULyraSettingKeyboardInput::ResetToDefault()
{
	// Find the UPlayerMappableInputConfig that this came from and reset it to the value in there
	FirstMappableOption.ResetToDefault();
	SecondaryMappableOption.ResetToDefault();
}

void ULyraSettingKeyboardInput::StoreInitial()
{
	FirstMappableOption.SetInitialValue(FirstMappableOption.InputMapping.Key);
	SecondaryMappableOption.SetInitialValue(SecondaryMappableOption.InputMapping.Key);
}

void ULyraSettingKeyboardInput::RestoreToInitial()
{	
	ChangeBinding(0, FirstMappableOption.GetInitialStoredValue());
	ChangeBinding(1, SecondaryMappableOption.GetInitialStoredValue());
}

bool ULyraSettingKeyboardInput::ChangeBinding(int32 InKeyBindSlot, FKey NewKey)
{
	// Early out if they hit the same button that is already bound. This allows for them to exit binding if they made a mistake.
	if ((InKeyBindSlot == 0 && FirstMappableOption.InputMapping.Key == NewKey) || (InKeyBindSlot == 1 && SecondaryMappableOption.InputMapping.Key == NewKey))
	{
		return false;
	}

	if (!NewKey.IsGamepadKey())
	{
		ULyraLocalPlayer* LyraLocalPlayer = CastChecked<ULyraLocalPlayer>(LocalPlayer);
		ULyraSettingsLocal* LocalSettings = LyraLocalPlayer->GetLocalSettings();
		if (InKeyBindSlot == 0)
		{
			LocalSettings->AddOrUpdateCustomKeyboardBindings(FirstMappableOption.InputMapping.GetMappingName(), NewKey, LyraLocalPlayer);
			FirstMappableOption.InputMapping.Key = NewKey;
		}
		else if (InKeyBindSlot == 1)
		{
			// If there is no default secondary binding then we can create one based off of data from the primary binding
			if (SecondaryMappableOption.InputMapping.GetMappingName() == NAME_None)
			{
				SecondaryMappableOption = FKeyboardOption(FirstMappableOption);
			}
			
			LocalSettings->AddOrUpdateCustomKeyboardBindings(SecondaryMappableOption.InputMapping.GetMappingName(), NewKey, LyraLocalPlayer);
			SecondaryMappableOption.InputMapping.Key = NewKey;
		}
		else
		{
			ensureMsgf(false, TEXT("Invalid key bind slot provided!"));
		}

		// keybindings are never reset to default or initial
		NotifySettingChanged(EGameSettingChangeReason::Change);

		return true;
	}

	return false;
}

void ULyraSettingKeyboardInput::GetAllMappedActionsFromKey(int32 InKeyBindSlot, FKey Key, TArray<FName>& OutActionNames) const
{
	if (InKeyBindSlot == 1)
	{
		if (SecondaryMappableOption.InputMapping.Key == Key)
		{
			return;
		}
	}
	else
	{
		if (FirstMappableOption.InputMapping.Key == Key)
		{
			return;
		}
	}

	if (const ULyraLocalPlayer* LyraLocalPlayer = CastChecked<ULyraLocalPlayer>(LocalPlayer))
	{
		ULyraSettingsLocal* LocalSettings = LyraLocalPlayer->GetLocalSettings();
		LocalSettings->GetAllMappingNamesFromKey(Key, OutActionNames);
	}
}

#undef LOCTEXT_NAMESPACE

