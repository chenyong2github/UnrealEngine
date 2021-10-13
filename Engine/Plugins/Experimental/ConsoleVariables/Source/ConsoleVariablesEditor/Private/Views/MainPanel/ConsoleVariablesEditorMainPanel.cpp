// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "ConsoleVariablesAsset.h"
#include "Views/List/ConsoleVariablesEditorList.h"
#include "Views/MainPanel/SConsoleVariablesEditorMainPanel.h"

#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"

FConsoleVariablesEditorMainPanel::FConsoleVariablesEditorMainPanel(UConsoleVariablesAsset* InEditingAsset)
{
	EditingAsset = InEditingAsset;

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

void FConsoleVariablesEditorMainPanel::AddConsoleVariable(const FString& InConsoleCommand, const FString& InValue)
{
	if (EditingAsset.IsValid())
	{
		IConsoleVariable* AsVariable = IConsoleManager::Get().FindConsoleVariable(*InConsoleCommand); 

		if (!ensureAlwaysMsgf(AsVariable, TEXT("%hs: InConsoleCommand '%s' was not found in ConsoleManager. Make sure this command is valid."), __FUNCTION__, *InConsoleCommand))
		{
			return;
		}

		EConsoleVariablesUiVariableType CommandType = EConsoleVariablesUiVariableType::ConsoleVariablesType_Bool;

		FString NewValue = InValue;

		FString TypeAsString = "bool";

		if (AsVariable->IsVariableFloat())
		{
			CommandType = EConsoleVariablesUiVariableType::ConsoleVariablesType_Float;
			TypeAsString = "float";
		}
		else if (AsVariable->IsVariableInt())
		{
			CommandType = EConsoleVariablesUiVariableType::ConsoleVariablesType_Integer;
			TypeAsString = "int";
		}
		else if (AsVariable->IsVariableString())
		{
			CommandType = EConsoleVariablesUiVariableType::ConsoleVariablesType_String;
			TypeAsString = "string";
		}

		if (NewValue.IsEmpty())
		{
			NewValue = AsVariable->GetString();
		}

		UConsoleVariablesAsset* Asset = EditingAsset.Get();

		Asset->AddOrSetConsoleVariableSavedValue(FConsoleVariablesUiCommandInfo(InConsoleCommand, NewValue, CommandType, AsVariable->GetHelp()));

		RefreshList(Asset);

		UE_LOG(LogTemp, Log, TEXT("%hs: Added new console command '%s' with value '%s' of type '%s'."), __FUNCTION__, *InConsoleCommand, *NewValue, *TypeAsString);
	}
}

void FConsoleVariablesEditorMainPanel::RefreshList(UConsoleVariablesAsset* InAsset) const
{
	if (MainPanelWidget.IsValid())
	{
		MainPanelWidget->RefreshList(InAsset);
	}
}

void FConsoleVariablesEditorMainPanel::UpdateExistingValuesFromConsoleManager() const
{
	EditorList->UpdateExistingValuesFromConsoleManager();
}

void FConsoleVariablesEditorMainPanel::SavePreset()
{
	if (ReferenceAssetOnDisk.IsValid() && EditingAsset.IsValid())
	{
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

void FConsoleVariablesEditorMainPanel::ImportPreset(const FAssetData& InPresetAsset)
{
	FSlateApplication::Get().DismissAllMenus();

	if (ImportPreset_Impl(InPresetAsset) && EditingAsset.IsValid())
	{
		EditorList->RefreshList(EditingAsset.Get());
	}
}

bool FConsoleVariablesEditorMainPanel::ImportPreset_Impl(const FAssetData& InPresetAsset)
{
	if (UConsoleVariablesAsset* Preset = CastChecked<UConsoleVariablesAsset>(InPresetAsset.GetAsset()))
	{
		ReferenceAssetOnDisk = Preset;

		EditingAsset->Modify();
		EditingAsset->CopyFrom(Preset);

		return Preset ? true : false;
	}

	return false;
}
