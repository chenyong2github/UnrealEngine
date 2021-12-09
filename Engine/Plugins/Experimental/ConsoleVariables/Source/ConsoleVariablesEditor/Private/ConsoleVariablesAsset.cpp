// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesAsset.h"

#include "ConsoleVariablesEditorCommandInfo.h"

#include "Algo/Find.h"

void UConsoleVariablesAsset::SetVariableCollectionDescription(const FString& InVariableCollectionDescription)
{
	VariableCollectionDescription = InVariableCollectionDescription;
}

void UConsoleVariablesAsset::ReplaceSavedCommands(const TArray<FConsoleVariablesEditorAssetSaveData>& Replacement)
{
	SavedCommands = Replacement;
}

bool UConsoleVariablesAsset::FindSavedDataByCommandString(const FString& InCommandString, FConsoleVariablesEditorAssetSaveData& OutValue) const
{
	if (const auto* Match = Algo::FindByPredicate(
		SavedCommands,
		[&InCommandString] (const FConsoleVariablesEditorAssetSaveData& Comparator)
		{
			return Comparator.CommandName.Equals(InCommandString);
		}))
	{
		OutValue = *Match;
		return true;
	}
	
	return false;
}

void UConsoleVariablesAsset::AddOrSetConsoleVariableSavedData(const FConsoleVariablesEditorAssetSaveData& InData)
{
	RemoveConsoleVariable(InData.CommandName);
	SavedCommands.Add(InData);
}

bool UConsoleVariablesAsset::RemoveConsoleVariable(const FString& InCommandString)
{
	FConsoleVariablesEditorAssetSaveData ExistingData;
	if (FindSavedDataByCommandString(InCommandString, ExistingData))
	{
		return SavedCommands.Remove(ExistingData) > 0;
	}
	
	return false;
}

void UConsoleVariablesAsset::CopyFrom(const UConsoleVariablesAsset* InAssetToCopy)
{
	VariableCollectionDescription = InAssetToCopy->GetVariableCollectionDescription();
	SavedCommands = InAssetToCopy->GetSavedCommands();
}
