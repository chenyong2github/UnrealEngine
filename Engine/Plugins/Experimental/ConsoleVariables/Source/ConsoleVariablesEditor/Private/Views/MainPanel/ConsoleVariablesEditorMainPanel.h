// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWidget.h"

class FConsoleVariablesEditorList;
class SConsoleVariablesEditorMainPanel;
class UConsoleVariablesAsset;

class FConsoleVariablesEditorMainPanel : public TSharedFromThis<FConsoleVariablesEditorMainPanel>
{
public:
	FConsoleVariablesEditorMainPanel(UConsoleVariablesAsset* InEditingAsset);

	TSharedRef<SWidget> GetOrCreateWidget();

	void AddConsoleVariable(const FString& InConsoleCommand, const FString& InValue);

	void RefreshList(UConsoleVariablesAsset* InAsset) const;

	void UpdateExistingValuesFromConsoleManager() const;

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

private:

	bool ImportPreset_Impl(const FAssetData& InPresetAsset);

	TSharedPtr<SConsoleVariablesEditorMainPanel> MainPanelWidget;
	
	//TSharedPtr<FConsoleVariablesEditorList> EditorList;

	// The non-transient loaded asset from which we will copy to the transient asset for editing
	TWeakObjectPtr<UConsoleVariablesAsset> ReferenceAssetOnDisk;

	// Transient preset that's being edited so we don't affect the reference asset unless we save it
	TWeakObjectPtr<UConsoleVariablesAsset> EditingAsset;

	TSharedPtr<FConsoleVariablesEditorList> EditorList;
};
