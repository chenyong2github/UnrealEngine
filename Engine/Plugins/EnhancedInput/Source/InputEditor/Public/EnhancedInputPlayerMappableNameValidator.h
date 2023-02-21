// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet2/Kismet2NameValidators.h"

class FEnhancedInputPlayerMappableNameValidator : public FStringSetNameValidator
{
public:
	FEnhancedInputPlayerMappableNameValidator(FName InExistingName);

	// Begin FNameValidatorInterface
	virtual EValidatorResult IsValid(const FString& Name, bool bOriginal) override;
	// End FNameValidatorInterface
};