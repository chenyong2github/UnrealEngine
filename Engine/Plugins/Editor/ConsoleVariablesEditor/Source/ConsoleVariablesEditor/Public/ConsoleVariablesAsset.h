// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConsoleVariablesEditorCommandInfo.h"

#include "CoreMinimal.h"
#include "Widgets/Input/SCheckBox.h"

#include "ConsoleVariablesAsset.generated.h"

/** Data that will be serialized with this asset */
USTRUCT()
struct FConsoleVariablesEditorAssetSaveData
{
	GENERATED_BODY()

	FORCEINLINE bool operator==(const FConsoleVariablesEditorAssetSaveData& Comparator) const
	{
		return CommandName.Equals(Comparator.CommandName);
	}
	
	UPROPERTY()
	FString CommandName;

	UPROPERTY()
	FString CommandValueAsString;

	UPROPERTY()
	/** If Undetermined, we can assume this data was not previously saved */
	ECheckBoxState CheckedState = ECheckBoxState::Undetermined;
};

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
	const TArray<FConsoleVariablesEditorAssetSaveData>& GetSavedCommands() const
	{
		return SavedCommands;
	}

	/** Completely replaces the saved data with new saved data */
	void ReplaceSavedCommands(const TArray<FConsoleVariablesEditorAssetSaveData>& Replacement);

	/** Returns how many console variables are serialized in this asset */
	int32 GetSavedCommandsCount() const
	{
		return GetSavedCommands().Num();
	}

	/** Outputs the FConsoleVariablesEditorCommandInfo matching InCommand. Returns whether a match was found. Case sensitive. */
	bool FindSavedDataByCommandString(const FString InCommandString, FConsoleVariablesEditorAssetSaveData& OutValue) const;

	/** Set the value of a saved console variable if the name matches; add a new console variable to the list if a match is not found. */
	void AddOrSetConsoleObjectSavedData(const FConsoleVariablesEditorAssetSaveData& InData);

	/** Returns true if the element was found and successfully removed. */
	bool RemoveConsoleVariable(const FString InCommandString);

	/** Copy data from input asset to this asset */
	void CopyFrom(const UConsoleVariablesAsset* InAssetToCopy);
	
private:
	
	/* User defined description of the variable collection */
	UPROPERTY(AssetRegistrySearchable, BlueprintGetter = "GetVariableCollectionDescription", EditAnywhere, Category = "Console Variables Editor")
	FString VariableCollectionDescription;

	/** A saved list of console variable information such as the variable name, the type and the value of the variable at the time the asset was saved. */
	UPROPERTY()
	TArray<FConsoleVariablesEditorAssetSaveData> SavedCommands;
};
