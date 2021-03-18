// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomBindingActions.h"
#include "GroomAsset.h"

#include "EditorFramework/AssetImportData.h"
#include "GeometryCache.h"
#include "HairStrandsRendering.h"
#include "Toolkits/SimpleAssetEditor.h"
#include "ToolMenuSection.h"
#include "GroomBindingBuilder.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FGroomBindingActions::FGroomBindingActions()
{}

bool FGroomBindingActions::CanFilter()
{
	return true;
}

void FGroomBindingActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	FAssetTypeActions_Base::GetActions(InObjects, Section);

	TArray<TWeakObjectPtr<UGroomBindingAsset>> GroomBindingAssets = GetTypedWeakObjectPtrs<UGroomBindingAsset>(InObjects);

	Section.AddMenuEntry(
		"RebuildGroomBinding",
		LOCTEXT("RebuildGroomBinding", "Rebuild"),
		LOCTEXT("RebuildGroomBindingTooltip", "Rebuild the groom binding"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FGroomBindingActions::ExecuteRebuildBindingAsset, GroomBindingAssets),
			FCanExecuteAction::CreateSP(this, &FGroomBindingActions::CanRebuildBindingAsset, GroomBindingAssets)
		)
	);
}

uint32 FGroomBindingActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

FText FGroomBindingActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_GroomBinding", "GroomBinding");
}

UClass* FGroomBindingActions::GetSupportedClass() const
{
	return UGroomBindingAsset::StaticClass();
}

FColor FGroomBindingActions::GetTypeColor() const
{
	return FColor::White;
}

bool FGroomBindingActions::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

void FGroomBindingActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	// #ueent_todo: Will need a custom editor at some point, for now just use the Properties editor
	FSimpleAssetEditor::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, InObjects);
}

bool FGroomBindingActions::CanRebuildBindingAsset(TArray<TWeakObjectPtr<UGroomBindingAsset>> Objects) const
{
	for (TWeakObjectPtr<UGroomBindingAsset> BindingAsset : Objects)
	{
		if (BindingAsset.IsValid())
		{
			return true;
		}
	}
	return false;
}

void FGroomBindingActions::ExecuteRebuildBindingAsset(TArray<TWeakObjectPtr<UGroomBindingAsset>> Objects) const
{
	for (TWeakObjectPtr<UGroomBindingAsset> BindingAsset : Objects)
	{
		if (BindingAsset.IsValid() && BindingAsset->Groom && BindingAsset->HasValidTarget())
		{
			BindingAsset->Groom->ConditionalPostLoad();
			if (BindingAsset->GroomBindingType == EGroomBindingMeshType::SkeletalMesh)
			{
				BindingAsset->TargetSkeletalMesh->ConditionalPostLoad();
				if (BindingAsset->SourceSkeletalMesh)
				{
					BindingAsset->SourceSkeletalMesh->ConditionalPostLoad();
				}
			}
			else
			{
				BindingAsset->TargetGeometryCache->ConditionalPostLoad();
				if (BindingAsset->SourceGeometryCache)
				{
					BindingAsset->SourceGeometryCache->ConditionalPostLoad();
				}
			}
			FGroomBindingBuilder::BuildBinding(BindingAsset.Get(), false, true);
			BindingAsset->GetOutermost()->MarkPackageDirty();
		}
	}
}

#undef LOCTEXT_NAMESPACE