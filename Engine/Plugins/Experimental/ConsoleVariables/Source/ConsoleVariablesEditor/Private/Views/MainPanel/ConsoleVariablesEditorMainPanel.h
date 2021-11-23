// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConsoleVariablesEditorModule.h"
#include "Widgets/SWidget.h"

class FConsoleVariablesEditorList;
class SConsoleVariablesEditorMainPanel;
class UConsoleVariablesAsset;

class FConsoleVariablesEditorMainPanel : public TSharedFromThis<FConsoleVariablesEditorMainPanel>
{
public:
	FConsoleVariablesEditorMainPanel();

	~FConsoleVariablesEditorMainPanel();

	TSharedRef<SWidget> GetOrCreateWidget();

	static FConsoleVariablesEditorModule& GetConsoleVariablesModule();
	static TObjectPtr<UConsoleVariablesAsset> GetEditingAsset();

	void AddConsoleVariable(const FString& InConsoleCommand, const FString& InValue, const bool bScrollToNewRow = false) const;

	void RefreshList(const FString& InConsoleCommandToScrollTo = "") const;
	void UpdatePresetValuesForSave(TObjectPtr<UConsoleVariablesAsset> InAsset) const;

	void RefreshMultiUserDetails() const;

	// Save / Load

	void SavePreset();
	void SavePresetAs();
	void ImportPreset(const FAssetData& InPresetAsset);

	TWeakObjectPtr<UConsoleVariablesAsset> GetReferenceAssetOnDisk() const
	{
		return ReferenceAssetOnDisk;
	}

	TWeakPtr<FConsoleVariablesEditorList> GetEditorList() const
	{
		return EditorList;
	}

	UE::ConsoleVariables::MultiUser::Private::FManager& GetMultiUserManager()
	{
		return MultiUserManager;
	}

private:

	bool ImportPreset_Impl(const FAssetData& InPresetAsset, const TObjectPtr<UConsoleVariablesAsset> EditingAsset);

	TSharedPtr<SConsoleVariablesEditorMainPanel> MainPanelWidget;

	// The non-transient loaded asset from which we will copy to the transient asset for editing
	TWeakObjectPtr<UConsoleVariablesAsset> ReferenceAssetOnDisk;

	TSharedPtr<FConsoleVariablesEditorList> EditorList;

	void OnConnectionChanged(EConcertConnectionStatus Status);
	void OnRemoteCvarChange(const FString InName, const FString InValue);

	UE::ConsoleVariables::MultiUser::Private::FManager MultiUserManager;
	FDelegateHandle OnConnectionChangedHandle;
	FDelegateHandle OnRemoteCVarChangeHandle;
};
