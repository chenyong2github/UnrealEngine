// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorLog.h"
#include "ConsoleVariablesEditorCommandInfo.h"
#include "Views/List/ConsoleVariablesEditorList.h"
#include "Views/MainPanel/SConsoleVariablesEditorMainPanel.h"

#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"

FConsoleVariablesEditorMainPanel::FConsoleVariablesEditorMainPanel()
{
	EditorList = MakeShared<FConsoleVariablesEditorList>();
}

TSharedRef<SWidget> FConsoleVariablesEditorMainPanel::GetOrCreateWidget()
{
	if (!MainPanelWidget.IsValid())
	{
		SAssignNew(MainPanelWidget, SConsoleVariablesEditorMainPanel, SharedThis(this));
	}

	return MainPanelWidget.ToSharedRef();
}

FConsoleVariablesEditorModule& FConsoleVariablesEditorMainPanel::GetConsoleVariablesModule()
{
	return FConsoleVariablesEditorModule::Get();
}

TWeakObjectPtr<UConsoleVariablesAsset> FConsoleVariablesEditorMainPanel::GetEditingAsset()
{
	return GetConsoleVariablesModule().GetEditingAsset();
}

void FConsoleVariablesEditorMainPanel::AddConsoleVariable(
	const FString& InConsoleCommand, const FString& InValue, const bool bScrollToNewRow) const
{
	const TWeakObjectPtr<UConsoleVariablesAsset> EditingAsset = GetEditingAsset();
	
	if (EditingAsset.IsValid())
	{
		UConsoleVariablesAsset* Asset = EditingAsset.Get();

		Asset->AddOrSetConsoleVariableSavedValue(InConsoleCommand, InValue);

		RefreshList(Asset, bScrollToNewRow ? InConsoleCommand : "");
	}
}

void FConsoleVariablesEditorMainPanel::RefreshList(TObjectPtr<UConsoleVariablesAsset> InAsset, const FString& InConsoleCommandToScrollTo) const
{
	if (EditorList.IsValid())
	{
		EditorList->RefreshList(InAsset, InConsoleCommandToScrollTo);
	}
}

void FConsoleVariablesEditorMainPanel::UpdatePresetValuesForSave(TObjectPtr<UConsoleVariablesAsset> InAsset) const
{
	if (EditorList.IsValid())
	{
		EditorList->UpdatePresetValuesForSave(InAsset);
	}
}

void FConsoleVariablesEditorMainPanel::SavePreset()
{
	const TWeakObjectPtr<UConsoleVariablesAsset> EditingAsset = GetEditingAsset();
	
	if (ReferenceAssetOnDisk.IsValid() && EditingAsset.IsValid())
	{
		UpdatePresetValuesForSave(EditingAsset.Get());
		
		if (UPackage* ReferencePackage = ReferenceAssetOnDisk.Get()->GetPackage())
		{
			ReferenceAssetOnDisk->CopyFrom(EditingAsset.Get());

			UEditorLoadingAndSavingUtils::SavePackages({ ReferencePackage }, false);

			return;
		}
	}

	// Fallback
	SavePresetAs();
}

void FConsoleVariablesEditorMainPanel::SavePresetAs()
{
	const TWeakObjectPtr<UConsoleVariablesAsset> EditingAsset = GetEditingAsset();
	
	if (EditingAsset.IsValid())
	{
		UpdatePresetValuesForSave(EditingAsset.Get());
			
		TArray<UObject*> SavedAssets;
		FEditorFileUtils::SaveAssetsAs({ EditingAsset.Get() }, SavedAssets);

		if (SavedAssets.Num())
		{
			UConsoleVariablesAsset* SavedAsset = Cast<UConsoleVariablesAsset>(SavedAssets[0]);

			if (ensure(SavedAsset))
			{
				ReferenceAssetOnDisk = SavedAsset;
			}
		}
	}
}

void FConsoleVariablesEditorMainPanel::ImportPreset(const FAssetData& InPresetAsset)
{
	FSlateApplication::Get().DismissAllMenus();
	const TWeakObjectPtr<UConsoleVariablesAsset> EditingAsset = GetEditingAsset();

	if (EditingAsset.IsValid() && ImportPreset_Impl(InPresetAsset, EditingAsset))
	{
		EditorList->RefreshList(EditingAsset.Get());
	}
}

bool FConsoleVariablesEditorMainPanel::ImportPreset_Impl(const FAssetData& InPresetAsset, const TWeakObjectPtr<UConsoleVariablesAsset> EditingAsset)
{
	if (UConsoleVariablesAsset* Preset = CastChecked<UConsoleVariablesAsset>(InPresetAsset.GetAsset()))
	{
		if (EditingAsset.IsValid())
		{
			ReferenceAssetOnDisk = Preset;

			EditingAsset->Modify();
			EditingAsset->CopyFrom(Preset);

			return true;
		}
	}

	return false;
}
