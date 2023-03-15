// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedActionKeyMapping.h"
#include "PlayerMappableKeySettings.h"

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

#if WITH_EDITORONLY_DATA

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FEnhancedActionKeyMapping::FEnhancedActionKeyMapping(const UInputAction* InAction /*= nullptr*/, const FKey InKey /*= EKeys::Invalid*/)
	: PlayerMappableOptions(InAction)
	, Action(InAction)
	, Key(InKey)
	, bShouldBeIgnored(false)
	, bIsPlayerMappable(false)
{}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#else

FEnhancedActionKeyMapping::FEnhancedActionKeyMapping(const UInputAction* InAction /*= nullptr*/, const FKey InKey /*= EKeys::Invalid*/)
	: Action(InAction)
	, Key(InKey)
	, bShouldBeIgnored(false)
{}

#endif	// WITH_EDITORONLY_DATA



FName FEnhancedActionKeyMapping::GetMappingName() const
{
	if (IsPlayerMappable())
	{
		if (UPlayerMappableKeySettings* MappableKeySettings = GetPlayerMappableKeySettings())
		{
			return MappableKeySettings->MakeMappingName(this);
		}
	}
	return NAME_None;
}

const FText& FEnhancedActionKeyMapping::GetDisplayName() const
{
	if (UPlayerMappableKeySettings* MappableKeySettings = GetPlayerMappableKeySettings())
	{
		return MappableKeySettings->DisplayName;
	}
	return FText::GetEmpty();
}

const FText& FEnhancedActionKeyMapping::GetDisplayCategory() const
{
	if (UPlayerMappableKeySettings* MappableKeySettings = GetPlayerMappableKeySettings())
	{
		return MappableKeySettings->DisplayCategory;
	}
	return FText::GetEmpty();
}

bool FEnhancedActionKeyMapping::IsPlayerMappable() const
{
	return GetPlayerMappableKeySettings() != nullptr;
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

	// Validate Settings.
	if (PlayerMappableKeySettings != nullptr)
	{
		Result = CombineDataValidationResults(Result, PlayerMappableKeySettings->IsDataValid(ValidationErrors));
	}

	// Validate the triggers.
	bool bContextContainsComboTrigger = false;
	bool bContextContainsNonComboTrigger = false;
	for (const TObjectPtr<UInputTrigger> Trigger : Triggers)
	{
		if (Trigger != nullptr)
		{
			// check if it the trigger is a combo or not
			Trigger.IsA(UInputTriggerCombo::StaticClass()) ? bContextContainsComboTrigger = true : bContextContainsNonComboTrigger = true;
			
			Result = CombineDataValidationResults(Result, Trigger->IsDataValid(ValidationErrors));
		}
		else
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(LOCTEXT("NullInputTrigger", "There cannot be a null Input Trigger on a key mapping"));
		}
	}

	if (Action)
	{
		bool bInputActionContainsComboTrigger = false;
	    bool bInputActionContainsNonComboTrigger = false;
		// we also need to check the input action triggers for combo triggers
		for (const TObjectPtr<UInputTrigger> Trigger : Action->Triggers)
		{
			if (Trigger != nullptr)
			{
				Trigger.IsA(UInputTriggerCombo::StaticClass()) ? bInputActionContainsComboTrigger = true : bInputActionContainsNonComboTrigger = true;
			}
		}

		FFormatNamedArguments Args;
		Args.Add("InputActionName", FText::FromName(Action->GetFName()));
		Args.Add("KeyBeingMapped", Key.GetDisplayName());
		FText DefaultComboNonComboWarning = LOCTEXT("DefaultComboNonComboWarningText", "The mapping of {InputActionName} to {KeyBeingMapped} has a Combo Trigger ({ComboTriggerLocation}) with additional non-combo triggers ({NonComboTriggerLocation}). Mixing Combo Triggers with other types of Triggers is not supported. Consider putting the Combo Trigger(s) on a seperate mapping or making a seperate Input Action for them.");
		if (bContextContainsComboTrigger)
		{
			Args.Add("ComboTriggerLocation", LOCTEXT("ComboInContextText", "From the Mapping Context"));
			if (bContextContainsNonComboTrigger)
			{
				Result = EDataValidationResult::Invalid;
				Args.Add("NonComboTriggerLocation", LOCTEXT("NonComboInContextText", "From the Mapping Context"));
				ValidationErrors.Add(FText::Format(DefaultComboNonComboWarning, Args));
			}
			// Input Action contains non-combo trigger(s) 
			if (bInputActionContainsNonComboTrigger)
			{
				Args.Add("NonComboTriggerLocation", LOCTEXT("NonComboInInputActionText", "From the Input Action"));
				Result = EDataValidationResult::Invalid;
				ValidationErrors.Add(FText::Format(DefaultComboNonComboWarning, Args));
			}
		}
		// Input Action contains combo trigger(s)
		if (bInputActionContainsComboTrigger)
		{
			Args.Add("ComboTriggerLocation", LOCTEXT("ComboInInputActionText", "From the Input Action"));
			// Context contains non-combo trigger(s)
			if (bContextContainsNonComboTrigger)
			{
				Result = EDataValidationResult::Invalid;
				Args.Add("NonComboTriggerLocation", LOCTEXT("NonComboInContextText", "From the Mapping Context"));
				ValidationErrors.Add(FText::Format(DefaultComboNonComboWarning, Args));
			}
			// Input Action contains non-combo trigger(s) 
			if (bInputActionContainsNonComboTrigger)
			{
				Args.Add("NonComboTriggerLocation", LOCTEXT("NonComboInInputActionText", "From the Input Action"));
				Result = EDataValidationResult::Invalid;
				ValidationErrors.Add(FText::Format(DefaultComboNonComboWarning, Args));
			}
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

bool FEnhancedActionKeyMapping::operator==(const FEnhancedActionKeyMapping& Other) const
{
	return (Action == Other.Action &&
			Key == Other.Key &&
			Triggers == Other.Triggers &&
			Modifiers == Other.Modifiers);
}

#undef LOCTEXT_NAMESPACE
