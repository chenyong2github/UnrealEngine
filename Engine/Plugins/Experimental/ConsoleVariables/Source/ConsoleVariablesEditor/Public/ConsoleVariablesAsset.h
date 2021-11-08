// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ConsoleVariablesAsset.generated.h"

struct FConsoleVariablesEditorCommandInfo;

/** An asset used to track collections of console variables that can be recalled and edited using the Console Variables Editor. */
UCLASS(BlueprintType)
class CONSOLEVARIABLESEDITOR_API UConsoleVariablesAsset : public UObject
{
	GENERATED_BODY()
public:

	/** Sets a description for this variable collection. */
	UFUNCTION(BlueprintCallable, Category = "Console Variables Editor")
	void SetVariableCollectionDescription(const FString& InVariableCollectionDescription);
	UFUNCTION(BlueprintPure, Category = "Console Variables Editor")
	FString GetVariableCollectionDescription() const
	{
		return VariableCollectionDescription;
	}

	/** Returns the saved list of console variable information such as the variable name, the type and the value of the vriable at the time the asset was saved. */
	const TMap<FString, FString>& GetSavedCommandsAndValues() const
	{
		return SavedCommandsAndValues;
	}

	void ReplaceSavedCommandsAndValues(const TMap<FString, FString>& InMap);

	/** Returns how many console variables are serialized in this asset */
	int32 GetSavedCommandsAndValuesCount() const
	{
		return GetSavedCommandsAndValues().Num();
	}

	/** Outputs the FConsoleVariablesEditorCommandInfo matching InCommand. Returns whether a match was found. Case sensitive. */
	bool FindSavedValueByCommandString(const FString& InCommandString, FString& OutValue) const;

	/** Set the value of a saved console variable if the name matches; add a new console variable to the list if a match is not found. */
	void AddOrSetConsoleVariableSavedValue(const FString& InCommandString, const FString& InNewValue);

	/** Returns true if the element was found and successfully removed. */
	bool RemoveConsoleVariable(const FString& InCommandString);

	/** Copy data from input asset to this asset */
	void CopyFrom(UConsoleVariablesAsset* InAssetToCopy);
	
private:
	
	/* User defined description of the variable collection */
	UPROPERTY(AssetRegistrySearchable, BlueprintGetter = "GetVariableCollectionDescription", EditAnywhere, Category = "Console Variables Editor")
	FString VariableCollectionDescription;

	/** A saved list of console variable information such as the variable name, the type and the value of the variable at the time the asset was saved. */
	UPROPERTY()
	TMap<FString, FString> SavedCommandsAndValues;
};
