// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorValidatorBase.h"
#include "UncontrolledChangelistValidator.generated.h"

/**
* Validates there is no additional uncontrolled changes waiting to be reconciled.
*/
UCLASS()
class DATAVALIDATION_API UUncontrolledChangelistValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(UObject* InAsset) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors) override;
};
