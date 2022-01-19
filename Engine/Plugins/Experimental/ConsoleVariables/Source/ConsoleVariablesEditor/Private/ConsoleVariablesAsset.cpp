// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesAsset.h"

#include "ConsoleVariablesEditorModule.h"
#include "Algo/Find.h"

void UConsoleVariablesAsset::SetVariableCollectionDescription(const FString& InVariableCollectionDescription)
{
	VariableCollectionDescription = InVariableCollectionDescription;
}

void UConsoleVariablesAsset::ReplaceSavedCommands(const TArray<FConsoleVariablesEditorAssetSaveData>& Replacement)
{
	SavedCommands = Replacement;
}

bool UConsoleVariablesAsset::FindSavedDataByCommandString(const FString InCommandString, FConsoleVariablesEditorAssetSaveData& OutValue) const
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

void UConsoleVariablesAsset::AddOrSetConsoleObjectSavedData(const FConsoleVariablesEditorAssetSaveData& InData)
{
	FConsoleVariablesEditorModule& ConsoleVariablesEditorModule = FConsoleVariablesEditorModule::Get();

	if (const TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo =
			ConsoleVariablesEditorModule.FindCommandInfoByName(InData.CommandName);
		CommandInfo.IsValid())
	{
		const IConsoleVariable* AsVariable = CommandInfo.Pin()->GetConsoleVariablePtr();
		
		if (AsVariable && AsVariable->TestFlags(ECVF_RenderThreadSafe))
		{
			UE_LOG(LogConsoleVariablesEditor, Warning,
				TEXT("The console variable named %s is flagged as ECVF_RenderThreadSafe. The value on the render thread will lag behind the value on the main thread by one frame if r.OneFrameThreadLag is 1."),
				*InData.CommandName
			);
		}
	}
	
	RemoveConsoleVariable(InData.CommandName);
	SavedCommands.Add(InData);
}

bool UConsoleVariablesAsset::RemoveConsoleVariable(const FString InCommandString)
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
