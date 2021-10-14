// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesAsset.h"

#include "Algo/Find.h"
#include "Editor.h"

void FConsoleVariablesUiCommandInfo::ExecuteCommand() const
{
	GEngine->Exec(GEditor->GetWorld(), *FString::Printf(TEXT("%s %s"), *Command, *ValueAsString));
}

void UConsoleVariablesAsset::SetVariableCollectionName(const FName& InVariableCollectionName)
{
	VariableCollectionName = InVariableCollectionName;
}

void UConsoleVariablesAsset::SetVariableCollectionDescription(const FString& InVariableCollectionDescription)
{
	VariableCollectionDescription = InVariableCollectionDescription;
}

bool UConsoleVariablesAsset::FindCommandInfoByCommandString(const FString& InCommand, FConsoleVariablesUiCommandInfo& OutCommandInfo)
{
	FConsoleVariablesUiCommandInfo* MatchedInfo = Algo::FindByPredicate(
	SavedCommandsAndValues, [&InCommand](const FConsoleVariablesUiCommandInfo& Info)
	{
		return Info.Command.Equals(InCommand);
	});

	if (MatchedInfo)
	{
		OutCommandInfo = *MatchedInfo;
	}

	return MatchedInfo ? true : false;
}

void UConsoleVariablesAsset::AddOrSetConsoleVariableSavedValue(const FConsoleVariablesUiCommandInfo InCommandInfo)
{
	const int32 IndexOfMatch = SavedCommandsAndValues.IndexOfByPredicate([&InCommandInfo](const FConsoleVariablesUiCommandInfo& Comparator)
	{
		return Comparator.SimilarTo(InCommandInfo);
	});

	if (IndexOfMatch > -1)
	{
		SavedCommandsAndValues[IndexOfMatch] = InCommandInfo;
	}
	else
	{
		SavedCommandsAndValues.Add(InCommandInfo);
	}
}

bool UConsoleVariablesAsset::RemoveConsoleVariable(const FConsoleVariablesUiCommandInfo InCommandInfo)
{
	const int32 IndexOfMatch = SavedCommandsAndValues.IndexOfByPredicate([&InCommandInfo](const FConsoleVariablesUiCommandInfo& Comparator)
	{
		return Comparator.SimilarTo(InCommandInfo);
	});

	if (IndexOfMatch > -1)
	{
		SavedCommandsAndValues.RemoveAt(IndexOfMatch);

		return true;
	}

	return false;
}

const FText& UConsoleVariablesAsset::GetSource() const
{
	return Source;
}

void UConsoleVariablesAsset::SetSource(const FText& InSourceText)
{
	Source = InSourceText;
}

void UConsoleVariablesAsset::CopyFrom(UConsoleVariablesAsset* InAssetToCopy)
{
	VariableCollectionName = InAssetToCopy->VariableCollectionName;
	VariableCollectionDescription = InAssetToCopy->VariableCollectionDescription;
	SavedCommandsAndValues = InAssetToCopy->SavedCommandsAndValues;
	Source = InAssetToCopy->Source;
}
