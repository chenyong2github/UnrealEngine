// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesAsset.h"

#include "ConsoleVariablesEditorCommandInfo.h"

#include "Algo/Find.h"

void UConsoleVariablesAsset::SetVariableCollectionDescription(const FString& InVariableCollectionDescription)
{
	VariableCollectionDescription = InVariableCollectionDescription;
}

void UConsoleVariablesAsset::ReplaceSavedCommandsAndValues(const TMap<FString, FString>& InMap)
{
	SavedCommandsAndValues = InMap;
}

bool UConsoleVariablesAsset::FindSavedValueByCommandString(const FString& InCommandString, FString& OutValue) const
{
	TArray<FString> KeyArray;
	SavedCommandsAndValues.GetKeys(KeyArray);

	if (const bool bMatchFound = KeyArray.Contains(InCommandString))
	{
		OutValue = SavedCommandsAndValues[InCommandString];
		return true;
	}
	
	return false;
}

void UConsoleVariablesAsset::AddOrSetConsoleVariableSavedValue(const FString& InCommandString, const FString& InNewValue)
{
	SavedCommandsAndValues.Add(InCommandString, InNewValue);
}

bool UConsoleVariablesAsset::RemoveConsoleVariable(const FString& InCommandString)
{
	TArray<FString> KeyArray;
	SavedCommandsAndValues.GetKeys(KeyArray);

	if (const bool bMatchFound = KeyArray.Contains(InCommandString))
	{
		return SavedCommandsAndValues.Remove(InCommandString) > 0;
	}
	
	return false;
}

void UConsoleVariablesAsset::CopyFrom(UConsoleVariablesAsset* InAssetToCopy)
{
	VariableCollectionDescription = InAssetToCopy->VariableCollectionDescription;
	SavedCommandsAndValues = InAssetToCopy->SavedCommandsAndValues;
}
