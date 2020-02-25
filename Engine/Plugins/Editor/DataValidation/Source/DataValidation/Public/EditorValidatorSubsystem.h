// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Templates/SubclassOf.h"
#include "UObject/Interface.h"
#include "UObject/ObjectMacros.h"
#include "EditorValidatorBase.h"
#include "Engine/EngineTypes.h"
#include "AssetData.h"
#include "Logging/LogMacros.h"

#include "EditorValidatorSubsystem.generated.h"

struct FAssetData;

DECLARE_LOG_CATEGORY_EXTERN(LogContentValidation, Log, All);

/**
* Implements the settings for Data Validation 
*/
UCLASS(config = Editor)
class DATAVALIDATION_API UDataValidationSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Default constructor that sets up CDO properties */
	UDataValidationSettings();

	/** Whether or not to validate assets on save */
	UPROPERTY(EditAnywhere, config, Category = "Validation Scenarios")
	uint32 bValidateOnSave : 1;
};

/**
 * UEditorValidatorSubsystem manages all the asset validation in the engine. 
 * The first validation handled is UObject::IsDataValid and its overridden functions.
 * Those validations require custom classes and are most suited to project-specific
 * classes. The next validation set is of all registered UEditorValidationBases.
 * These validators have a function to determine if they can validate a given asset,
 * and if they are currently enabled. They are good candidates for validating engine
 * classes or very specific project logic.
 */
UCLASS(Config = Editor)
class DATAVALIDATION_API UEditorValidatorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UEditorValidatorSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection);

	virtual void Deinitialize();

	/*
	* Adds a validator to the list, making sure it is a unique instance
	*/
	UFUNCTION(BlueprintCallable, Category = "Validation")
	void AddValidator(UEditorValidatorBase* InValidator);

	/**
	 * @return Returns Valid if the object contains valid data; returns Invalid if the object contains invalid data; returns NotValidated if no validations was performed on the object
	 */
	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	virtual EDataValidationResult IsObjectValid(UObject* InObject, TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings) const;

	/**
	 * @return Returns Valid if the object pointed to by AssetData contains valid data; returns Invalid if the object contains invalid data or does not exist; returns NotValidated if no validations was performed on the object
	 */
	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	virtual EDataValidationResult IsAssetValid(FAssetData& AssetData, TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings) const;

	/**
	 * Called to validate assets from either the UI or a commandlet
	 * @param bSkipExcludedDirectories If true, will not validate files in excluded directories
	 * @param bShowIfNoFailures If true, will add notifications for files with no validation and display even if everything passes
	 * @returns Number of assets with validation failures or warnings
	 */
	UFUNCTION(BlueprintCallable, Category = "Asset Validation")
	virtual int32 ValidateAssets(TArray<FAssetData> AssetDataList, bool bSkipExcludedDirectories = true, bool bShowIfNoFailures = true) const;

	/**
	 * Called to validate from an interactive save
	 */
	virtual void ValidateOnSave(TArray<FAssetData> AssetDataList) const;

	/**
	 * Schedule a validation of a saved package, this will activate next frame by default so it can combine them
	 */
	virtual void ValidateSavedPackage(FName PackageName);

protected:
	void CleanupValidators();

	/**
	 * @return Returns true if the current Path should be skipped for validation. Returns false otherwise.
	 */
	virtual bool IsPathExcludedFromValidation(const FString& Path) const;

	/**
	 * Handles validating all pending save packages
	 */
	void ValidateAllSavedPackages();

	void RegisterBlueprintValidators();

protected:
	/**
	 * Directories to ignore for data validation. Useful for test assets
	 */
	UPROPERTY(config)
	TArray<FDirectoryPath> ExcludedDirectories;

	/**
	 * Whether it should validate assets on save inside the editor
	 */
	UPROPERTY(config, meta = (DeprecatedProperty, DeprecationMessage = "Use bValidateOnSave on UDataValidationSettings instead."))
	bool bValidateOnSave;

	/** List of saved package names to validate next frame */
	TArray<FName> SavedPackagesToValidate;

	UPROPERTY(Transient)
	TMap<UClass*, UEditorValidatorBase*> Validators;

	/** Specifies whether or not to validate assets on save when saving for a cook */
	UPROPERTY(config)
	bool bValidateAssetsWhileSavingForCook;

	/** Specifies whether or not to allow Blueprint validators */
	UPROPERTY(config)
	bool bAllowBlueprintValidators;

};

