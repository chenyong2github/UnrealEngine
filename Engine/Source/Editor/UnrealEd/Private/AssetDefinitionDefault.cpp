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

EAssetCommandResult UAssetDefinitionDefault::GetSourceFiles(const FAssetSourceFileArgs& SourceFileArgs, TArray<FAssetSourceFile>& OutSourceAssets) const
{
	for (const FAssetData& Asset : SourceFileArgs.Assets)
    {
    	FString SourceFileTagData;
    	if (Asset.GetTagValue(UObject::SourceFileTagName(), SourceFileTagData))
    	{
    		TOptional<FAssetImportInfo> ImportInfo = FAssetImportInfo::FromJson(SourceFileTagData);
    		if (ImportInfo.IsSet())
    		{
    			for (const FAssetImportInfo::FSourceFile& SourceFile : ImportInfo->SourceFiles)
    			{
    				FAssetSourceFile AssetSourceFile;
    				AssetSourceFile.DisplayLabelName = SourceFile.DisplayLabelName;
    				AssetSourceFile.RelativeFilename = SourceFile.RelativeFilename;
    				OutSourceAssets.Add(MoveTemp(AssetSourceFile));
    			}
    		}
    	}
    }
    
    return EAssetCommandResult::Handled;
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

/*
virtual void PerformAssetDiff(UObject* OldAsset, UObject* NewAsset, const struct FRevisionInfo& OldRevision, const struct FRevisionInfo& NewRevision) const override
{
	check(OldAsset != nullptr);
	check(NewAsset != nullptr);

	// Dump assets to temp text files
	FString OldTextFilename = DumpAssetToTempFile(OldAsset);
	FString NewTextFilename = DumpAssetToTempFile(NewAsset);
	FString DiffCommand = GetDefault<UEditorLoadingSavingSettings>()->TextDiffToolPath.FilePath;

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateDiffProcess(DiffCommand, OldTextFilename, NewTextFilename);
}
*/

#undef LOCTEXT_NAMESPACE
