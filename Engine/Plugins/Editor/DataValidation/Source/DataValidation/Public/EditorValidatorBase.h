// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Templates/SubclassOf.h"
#include "UObject/Interface.h"
#include "UObject/ObjectMacros.h"
#include "Logging/LogVerbosity.h"
#include "Internationalization/Text.h"
#include "UObject/TextProperty.h"
#include "EditorValidatorBase.generated.h"

/*
* The EditorValidatorBase is a class which verifies that an asset meets a specific ruleset.
* It should be used when checking engine-level classes, as UObject::IsDataValid requires
* overriding the base class. You can create project-specific version of the validator base,
* with custom logging and enabled logic.
*
* C++ and Blueprint validators will be gathered on editor start, while python validators need
* to register themselves
*/
UCLASS(Abstract, Blueprintable, meta = (ShowWorldContextPin))
class DATAVALIDATION_API UEditorValidatorBase : public UObject
{
	GENERATED_BODY()

public:
	UEditorValidatorBase();

	/** Override this to determine whether or not you can validate a given asset with this validator */
	UFUNCTION(BlueprintNativeEvent, Category = "Asset Validation")
	bool CanValidateAsset(UObject* InAsset) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Asset Validation")
	EDataValidationResult ValidateLoadedAsset(UObject* InAsset, UPARAM(ref) TArray<FText>& ValidationErrors);

	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	void AssetFails(UObject* InAsset, const FText& InMessage, UPARAM(ref) TArray<FText>& ValidationErrors);

	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	void AssetPasses(UObject* InAsset);

	virtual bool IsEnabled() const
	{
		return bIsEnabled;
	}

	void ResetValidationState();

	bool IsValidationStateSet() const 
	{
		return ValidationResult != EDataValidationResult::NotValidated;
	}

	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	EDataValidationResult GetValidationResult() const 
	{
		return ValidationResult;
	}

protected:
	void LogElapsedTime(FFormatNamedArguments &Arguments);

protected:
	UPROPERTY(EditAnywhere, Category = "Asset Validation", meta = (BlueprintProtected = "true"))
	bool bIsEnabled;

private:
	EDataValidationResult ValidationResult;

	FDateTime ValidationTime;
};





