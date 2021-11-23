// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/ConsoleVariablesEditorMainPanel.h"

#include "ConcertMessages.h"
#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorLog.h"
#include "ConsoleVariablesEditorCommandInfo.h"
#include "Views/List/ConsoleVariablesEditorList.h"
#include "Views/MainPanel/SConsoleVariablesEditorMainPanel.h"

#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "MultiUser/ConsoleVariableSyncData.h"


FConsoleVariablesEditorMainPanel::FConsoleVariablesEditorMainPanel()
{
	EditorList = MakeShared<FConsoleVariablesEditorList>();
	
	OnConnectionChangedHandle = MultiUserManager.OnConnectionChange().AddRaw(
		this, &FConsoleVariablesEditorMainPanel::OnConnectionChanged);
	OnRemoteCVarChangeHandle = MultiUserManager.OnRemoteCVarChange().AddRaw(
		this, &FConsoleVariablesEditorMainPanel::OnRemoteCvarChange);
}

FConsoleVariablesEditorMainPanel::~FConsoleVariablesEditorMainPanel()
{
	MultiUserManager.OnConnectionChange().Remove(OnConnectionChangedHandle);
	MultiUserManager.OnRemoteCVarChange().Remove(OnRemoteCVarChangeHandle);
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

TObjectPtr<UConsoleVariablesAsset> FConsoleVariablesEditorMainPanel::GetEditingAsset()
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

		RefreshList(bScrollToNewRow ? InConsoleCommand : "");
	}
}

void FConsoleVariablesEditorMainPanel::RefreshList(const FString& InConsoleCommandToScrollTo) const
{
	if (EditorList.IsValid())
	{
		EditorList->RefreshList(InConsoleCommandToScrollTo);
	}
}

void FConsoleVariablesEditorMainPanel::UpdatePresetValuesForSave(TObjectPtr<UConsoleVariablesAsset> InAsset) const
{
	if (EditorList.IsValid())
	{
		EditorList->UpdatePresetValuesForSave(InAsset);
	}
}

void FConsoleVariablesEditorMainPanel::RefreshMultiUserDetails() const
{
	if (MainPanelWidget.IsValid())
	{
		MainPanelWidget->RefreshMultiUserDetails();
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
	const TObjectPtr<UConsoleVariablesAsset> EditingAsset = GetEditingAsset();

	if (EditingAsset && ImportPreset_Impl(InPresetAsset, EditingAsset))
	{
		EditorList->RefreshList();
	}
}

bool FConsoleVariablesEditorMainPanel::ImportPreset_Impl(const FAssetData& InPresetAsset, const TObjectPtr<UConsoleVariablesAsset> EditingAsset)
{
	if (UConsoleVariablesAsset* Preset = CastChecked<UConsoleVariablesAsset>(InPresetAsset.GetAsset()))
	{
		if (EditingAsset)
		{
			ReferenceAssetOnDisk = Preset;

			EditingAsset->Modify();
			EditingAsset->CopyFrom(Preset);

			return true;
		}
	}

	return false;
}

void FConsoleVariablesEditorMainPanel::OnConnectionChanged(EConcertConnectionStatus Status)
{
	switch (Status)
	{
		case EConcertConnectionStatus::Connected:
			UE_LOG(LogConsoleVariablesEditor, Display, TEXT("Multi-user has connected to a session."));
			break;
		case EConcertConnectionStatus::Disconnected:
			UE_LOG(LogConsoleVariablesEditor, Display, TEXT("Multi-user has disconnected from session."));
			break;
		default:
			break;
	}
}

void FConsoleVariablesEditorMainPanel::OnRemoteCvarChange(const FString InName, const FString InValue)
{
	UE_LOG(LogConsoleVariablesEditor, Display, TEXT("Remote set console variable %s = %s"), *InName, *InValue);

	if (GetMutableDefault<UConcertCVarSynchronization>()->bSyncCVarTransactions)
	{
		GEngine->Exec(FConsoleVariablesEditorCommandInfo::GetCurrentWorld(),
					  *FString::Printf(TEXT("%s %s"), *InName, *InValue));
	}
}
