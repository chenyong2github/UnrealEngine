// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomActions.h"
#include "GroomAsset.h"

#include "EditorFramework/AssetImportData.h"
#include "GroomAssetImportData.h"
#include "GroomImportOptions.h"
#include "GroomImportOptionsWindow.h"
#include "Toolkits/SimpleAssetEditor.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FGroomActions::FGroomActions()
{}

bool FGroomActions::CanFilter()
{
	return true;
}

void FGroomActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	FAssetTypeActions_Base::GetActions(InObjects, Section);

	TArray<TWeakObjectPtr<UGroomAsset>> GroomAssets = GetTypedWeakObjectPtrs<UGroomAsset>(InObjects);

	Section.AddMenuEntry(
		"RebuildGroom",
		LOCTEXT("RebuildGroom", "Rebuild"),
		LOCTEXT("RebuildGroomTooltip", "Rebuild the groom with new build settings"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FGroomActions::ExecuteRebuild, GroomAssets),
			FCanExecuteAction::CreateSP(this, &FGroomActions::CanRebuild, GroomAssets)
		)
	);
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
		if (GroomAsset && GroomAsset->AssetImportData)
		{
			GroomAsset->AssetImportData->ExtractFilenames(OutSourceFilePaths);
		}
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

bool FGroomActions::CanRebuild(TArray<TWeakObjectPtr<UGroomAsset>> Objects) const
{
	for (TWeakObjectPtr<UGroomAsset> GroomAsset : Objects)
	{
		if (GroomAsset.IsValid() && GroomAsset->CanRebuildFromDescription())
		{
			return true;
		}
	}
	return false;
}

void FGroomActions::ExecuteRebuild(TArray<TWeakObjectPtr<UGroomAsset>> Objects) const
{
	for (TWeakObjectPtr<UGroomAsset> GroomAsset : Objects)
	{
		if (GroomAsset.IsValid() && GroomAsset->CanRebuildFromDescription() && GroomAsset->AssetImportData)
		{
			UGroomAssetImportData* GroomAssetImportData = Cast<UGroomAssetImportData>(GroomAsset->AssetImportData);
			if (GroomAssetImportData && GroomAssetImportData->ImportOptions)
			{
				FString Filename(GroomAssetImportData->GetFirstFilename());

				// Duplicate the options to prevent dirtying the asset when they are modified but the rebuild is cancelled
				UGroomImportOptions* CurrentOptions = DuplicateObject<UGroomImportOptions>(GroomAssetImportData->ImportOptions, nullptr);
				TSharedPtr<SGroomImportOptionsWindow> GroomOptionWindow = SGroomImportOptionsWindow::DisplayRebuildOptions(CurrentOptions, Filename);

				if (!GroomOptionWindow->ShouldImport())
				{
					continue;
				}

				bool bSucceeded = GroomAsset->CacheDerivedData(&CurrentOptions->BuildSettings);
				if (bSucceeded)
				{
					// Move the transient ImportOptions to the asset package and set it on the GroomAssetImportData for serialization
					CurrentOptions->Rename(nullptr, GroomAssetImportData);
					GroomAssetImportData->ImportOptions = CurrentOptions;
					GroomAsset->MarkPackageDirty();
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE