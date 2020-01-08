// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomActions.h"
#include "GroomAsset.h"

#include "EditorFramework/AssetImportData.h"
#include "Toolkits/SimpleAssetEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FGroomActions::FGroomActions()
{}

bool FGroomActions::CanFilter()
{
	return true;
}

void FGroomActions::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);
}


uint32 FGroomActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

FText FGroomActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_Groom", "Groom");
}

void FGroomActions::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
	for (UObject* Asset : TypeAssets)
	{
		const UGroomAsset* GroomAsset = CastChecked<UGroomAsset>(Asset);
		GroomAsset->AssetImportData->ExtractFilenames(OutSourceFilePaths);
	}
}

UClass* FGroomActions::GetSupportedClass() const
{
	return UGroomAsset::StaticClass();
}

FColor FGroomActions::GetTypeColor() const
{
	return FColor::White;
}

bool FGroomActions::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

void FGroomActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	// #ueent_todo: Will need a custom editor at some point, for now just use the Properties editor
	FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, InObjects);
}

#undef LOCTEXT_NAMESPACE