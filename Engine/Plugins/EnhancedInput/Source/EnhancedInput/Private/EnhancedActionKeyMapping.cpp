// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedActionKeyMapping.h"
#include "PlayerMappableKeySettings.h"
#include "InputTriggers.h"
#include "InputModifiers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EnhancedActionKeyMapping)

#define LOCTEXT_NAMESPACE "ActionKeyMapping"

UPlayerMappableKeySettings* FEnhancedActionKeyMapping::GetPlayerMappableKeySettings() const
{
	switch (SettingBehavior)
	{
		case EPlayerMappableKeySettingBehaviors::InheritSettingsFromAction:	
			return Action != nullptr ? Action->GetPlayerMappableKeySettings() : nullptr;
		case EPlayerMappableKeySettingBehaviors::OverrideSettings:			
			return PlayerMappableKeySettings;
		case EPlayerMappableKeySettingBehaviors::IgnoreSettings:				
			return nullptr;
	}
	return nullptr;
}

ENHANCEDINPUT_API FName FEnhancedActionKeyMapping::GetMappingName() const
{
	if (IsPlayerMappable())
	{
		if (UPlayerMappableKeySettings* MappableKeySettings = GetPlayerMappableKeySettings())
		{
			return MappableKeySettings->MakeMappingName(this);
		}
		return PlayerMappableOptions.Name;
	}
	return NAME_None;
}

bool FEnhancedActionKeyMapping::IsPlayerMappable() const
{
	return GetPlayerMappableKeySettings() != nullptr || bIsPlayerMappable;
}

#if WITH_EDITOR
EDataValidationResult FEnhancedActionKeyMapping::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = EDataValidationResult::Valid;

	//Validate Action Reference.
	if (Action == nullptr)
	{
		Result = EDataValidationResult::Invalid;
		ValidationErrors.Add(LOCTEXT("NullInputAction", "A mapping cannot have an empty input action!"));
	}

	//Validate Player Mappable Options Name.
	if (bIsPlayerMappable && PlayerMappableOptions.Name == NAME_None)
	{
		ValidationErrors.Add(LOCTEXT("InvalidPlayerMappableName", "A player mappable key mapping must have a valid 'Name'"));
		return EDataValidationResult::Invalid;
	}

	// Validate Settings.
	if (PlayerMappableKeySettings != nullptr)
	{
		Result = CombineDataValidationResults(Result, PlayerMappableKeySettings->IsDataValid(ValidationErrors));
	}

	// Validate the triggers.
	for (const TObjectPtr<UInputTrigger> Trigger : Triggers)
	{
		if (Trigger != nullptr)
		{
			Result = CombineDataValidationResults(Result, Trigger->IsDataValid(ValidationErrors));
		}
		else
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(LOCTEXT("NullInputTrigger", "There cannot be a null Input Trigger on a key mapping"));
		}
	}

	// Validate the modifiers.
	for (const TObjectPtr<UInputModifier> Modifier : Modifiers)
	{
		if (Modifier != nullptr)
		{
			Result = CombineDataValidationResults(Result, Modifier->IsDataValid(ValidationErrors));
		}
		else
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(LOCTEXT("NullInputModifier", "There cannot be a null Input Modifier on a key mapping"));
		}
	}
	return Result;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
