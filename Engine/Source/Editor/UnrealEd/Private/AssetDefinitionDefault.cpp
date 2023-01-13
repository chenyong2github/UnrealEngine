// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitionDefault.h"

#include "AssetToolsModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Toolkits/SimpleAssetEditor.h"

#define LOCTEXT_NAMESPACE "AssetDefinitionDefault"

EAssetCommandResult UAssetDefinitionDefault::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	if (OpenArgs.OpenMethod == EAssetOpenMethod::Edit)
	{
		FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, OpenArgs.ToolkitHost, OpenArgs.LoadObjects<UObject>());
		return EAssetCommandResult::Handled;
	}

	return EAssetCommandResult::Unhandled;
}

EAssetCommandResult UAssetDefinitionDefault::PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const
{
	check(DiffArgs.OldAsset != nullptr);
	check(DiffArgs.NewAsset != nullptr);

	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Dump assets to temp text files
	FString OldTextFilename = AssetTools.DumpAssetToTempFile(DiffArgs.OldAsset);
	FString NewTextFilename = AssetTools.DumpAssetToTempFile(DiffArgs.NewAsset);
	FString DiffCommand = GetDefault<UEditorLoadingSavingSettings>()->TextDiffToolPath.FilePath;

	AssetTools.CreateDiffProcess(DiffCommand, OldTextFilename, NewTextFilename);

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
