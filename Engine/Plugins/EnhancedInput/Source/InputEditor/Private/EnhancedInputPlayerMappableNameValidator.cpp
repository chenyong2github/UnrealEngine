// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputPlayerMappableNameValidator.h"
#include "InputEditorModule.h"

FEnhancedInputPlayerMappableNameValidator::FEnhancedInputPlayerMappableNameValidator(FName InExistingName)
	: FStringSetNameValidator(InExistingName.ToString())
{ }

EValidatorResult FEnhancedInputPlayerMappableNameValidator::IsValid(const FString& Name, bool bOriginal)
{
	EValidatorResult Result = FStringSetNameValidator::IsValid(Name, bOriginal);

	if (Result != EValidatorResult::ExistingName && FInputEditorModule::IsMappingNameInUse(FName(Name)))
	{
		Result = EValidatorResult::AlreadyInUse;
	}

	return Result;
}