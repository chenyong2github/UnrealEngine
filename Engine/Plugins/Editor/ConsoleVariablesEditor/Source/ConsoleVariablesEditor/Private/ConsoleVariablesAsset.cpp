// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesAsset.h"

#include "ConsoleVariablesEditorCommandInfo.h"
#include "ConsoleVariablesEditorLog.h"
#include "ConsoleVariablesEditorModule.h"

void UConsoleVariablesAsset::SetVariableCollectionDescription(const FString& InVariableCollectionDescription)
{
	VariableCollectionDescription = InVariableCollectionDescription;
}

void UConsoleVariablesAsset::ReplaceSavedCommands(const TArray<FConsoleVariablesEditorAssetSaveData>& Replacement)
{
	SavedCommands = Replacement;
}

bool UConsoleVariablesAsset::FindSavedDataByCommandString(
	const FString InCommandString, FConsoleVariablesEditorAssetSaveData& OutValue, const ESearchCase::Type SearchCase) const
{
	for (const FConsoleVariablesEditorAssetSaveData& Command : SavedCommands)
	{
		if (Command.CommandName.TrimStartAndEnd().Equals(InCommandString.TrimStartAndEnd(), SearchCase))
		{
			OutValue = Command;
			return true;
		}
	}
	
	return false;
}

void UConsoleVariablesAsset::AddOrSetConsoleObjectSavedData(const FConsoleVariablesEditorAssetSaveData& InData)
{
	FConsoleVariablesEditorModule& ConsoleVariablesEditorModule = FConsoleVariablesEditorModule::Get();
	UE_LOG(LogConsoleVariablesEditor, VeryVerbose, TEXT("%hs: Adding %s to editable asset"),
		__FUNCTION__, *InData.CommandName);
	if (const TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo =
			ConsoleVariablesEditorModule.FindCommandInfoByName(InData.CommandName);
		CommandInfo.IsValid())
	{
		const IConsoleVariable* AsVariable = CommandInfo.Pin()->GetConsoleVariablePtr();
		
		if (AsVariable && AsVariable->TestFlags(ECVF_RenderThreadSafe))
		{
			UE_LOG(LogConsoleVariablesEditor, Verbose,
				TEXT("The console variable named %s is flagged as ECVF_RenderThreadSafe. The value on the render thread will lag behind the value on the main thread by one frame if r.OneFrameThreadLag is 1."),
				*InData.CommandName
			);
		}
	}
	
	RemoveConsoleVariable(InData.CommandName);
	SavedCommands.Add(InData);
	// Make a copy because this array can change during iteration
	TArray<FConsoleVariablesEditorAssetSaveData> SavedCommandsLocal = SavedCommands; 
	for (int32 CommandIndex = 0; CommandIndex < SavedCommandsLocal.Num(); CommandIndex++)
	{
		FConsoleVariablesEditorAssetSaveData& SavedCommand = SavedCommandsLocal[CommandIndex];
		UE_LOG(LogConsoleVariablesEditor, VeryVerbose, TEXT("%hs: Command named '%s' at Index %i"),
			__FUNCTION__, *SavedCommand.CommandName, CommandIndex);
	}
}

bool UConsoleVariablesAsset::RemoveConsoleVariable(const FString InCommandString)
{
	FConsoleVariablesEditorAssetSaveData ExistingData;
	int32 RemoveCount = 0;
	while (FindSavedDataByCommandString(InCommandString, ExistingData, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogConsoleVariablesEditor, VeryVerbose, TEXT("%hs: Removing %s from editable asset"),
			__FUNCTION__, *InCommandString);
		
		if (SavedCommands.Remove(ExistingData) > 0)
		{
			RemoveCount++;
		}
	}
	
	return RemoveCount > 0;
}

void UConsoleVariablesAsset::CopyFrom(const UConsoleVariablesAsset* InAssetToCopy)
{
	VariableCollectionDescription = InAssetToCopy->GetVariableCollectionDescription();
	SavedCommands = InAssetToCopy->GetSavedCommands();
}
