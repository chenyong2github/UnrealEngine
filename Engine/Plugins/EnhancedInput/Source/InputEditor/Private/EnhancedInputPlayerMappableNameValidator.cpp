// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputPlayerMappableNameValidator.h"
#include "PlayerMappableKeySettings.h"
#include "InputEditorModule.h"
#include "Internationalization/Text.h"	// For FFormatNamedArguments
#include "HAL/IConsoleManager.h"		// For FAutoConsoleVariableRef

namespace UE::EnhancedInput
{
	/** Enables editor validation on player mapping names */
	static bool bEnableMappingNameValidation = true;
	
	FAutoConsoleVariableRef CVarEnableMappingNameValidation(
	TEXT("EnhancedInput.Editor.EnableMappingNameValidation"),
	bEnableMappingNameValidation,
	TEXT("Enables editor validation on player mapping names"),
	ECVF_Default);
}

FEnhancedInputPlayerMappableNameValidator::FEnhancedInputPlayerMappableNameValidator(FName InExistingName)
	: FStringSetNameValidator(InExistingName.ToString())
{ }

EValidatorResult FEnhancedInputPlayerMappableNameValidator::IsValid(const FString& Name, bool bOriginal)
{
	EValidatorResult Result = FStringSetNameValidator::IsValid(Name, bOriginal);

	if (UE::EnhancedInput::bEnableMappingNameValidation &&
		Result != EValidatorResult::ExistingName &&
		FInputEditorModule::IsMappingNameInUse(FName(Name)))
	{
		Result = EValidatorResult::AlreadyInUse;
	}

	return Result;
}

FText FEnhancedInputPlayerMappableNameValidator::GetErrorText(const FString& Name, EValidatorResult ErrorCode)
{
	// Attempt to specify what asset is using this name
	if (ErrorCode == EValidatorResult::AlreadyInUse)
	{
		if (const UPlayerMappableKeySettings* Settings = FInputEditorModule::FindMappingByName(FName(Name)))
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("AssetUsingName"), FText::FromString(GetNameSafe(Settings->GetOuter())));

			return FText::Format(NSLOCTEXT("EnhancedInput", "MappingNameInUseBy_Error", "Name is already in use by '{AssetUsingName}'"), Args);	
		}
	}
	
	return INameValidatorInterface::GetErrorText(Name, ErrorCode);
}