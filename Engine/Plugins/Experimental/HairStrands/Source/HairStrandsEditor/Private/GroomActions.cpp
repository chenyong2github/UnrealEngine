// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomActions.h"
#include "GroomAsset.h"

#include "EditorFramework/AssetImportData.h"
#include "HairStrandsRendering.h"
#include "GroomAssetImportData.h"
#include "GroomImportOptions.h"
#include "GroomImportOptionsWindow.h"
#include "GroomCreateBindingOptions.h"
#include "GroomCreateBindingOptionsWindow.h"
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

	Section.AddMenuEntry(
		"CreateBindingAsset",
		LOCTEXT("CreateBindingAsset", "Create Binding"),
		LOCTEXT("CreateBindingAssetTooltip", "Create a binding asset between a skeletal mesh and a groom asset"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FGroomActions::ExecuteCreateBindingAsset, GroomAssets),
			FCanExecuteAction::CreateSP(this, &FGroomActions::CanCreateBindingAsset, GroomAssets)
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

bool FGroomActions::CanCreateBindingAsset(TArray<TWeakObjectPtr<UGroomAsset>> Objects) const
{
	for (TWeakObjectPtr<UGroomAsset> GroomAsset : Objects)
	{
		if (GroomAsset.IsValid())
		{
			return true;
		}
	}
	return false;
}

void FGroomActions::ExecuteCreateBindingAsset(TArray<TWeakObjectPtr<UGroomAsset>> Objects) const
{
	for (TWeakObjectPtr<UGroomAsset> GroomAsset : Objects)
	{
		if (GroomAsset.IsValid())
		{
			{

				// Duplicate the options to prevent dirtying the asset when they are modified but the rebuild is cancelled
				UGroomCreateBindingOptions* CurrentOptions = NewObject<UGroomCreateBindingOptions>();
				TSharedPtr<SGroomCreateBindingOptionsWindow> GroomOptionWindow = SGroomCreateBindingOptionsWindow::DisplayCreateBindingOptions(CurrentOptions);

				if (!GroomOptionWindow->ShouldCreate())
				{
					continue;
				}
				else if (GroomAsset.Get() && CurrentOptions && CurrentOptions->TargetSkeletalMesh)
				{
					GroomAsset->ConditionalPostLoad();
					if (CurrentOptions->SourceSkeletalMesh)
					{
						CurrentOptions->SourceSkeletalMesh->ConditionalPostLoad();
					}
					CurrentOptions->TargetSkeletalMesh->ConditionalPostLoad();

					UGroomBindingAsset* BindingAsset = CreateGroomBindinAsset(GroomAsset.Get(), CurrentOptions->SourceSkeletalMesh, CurrentOptions->TargetSkeletalMesh, CurrentOptions->NumInterpolationPoints);

					// The binding task will generate and set the binding value back to the binding asset.
					// This code is not thread safe.
					AddGroomBindingTask(BindingAsset);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE