// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UDMXControlConsolePreset;

struct FAssetData;
class FReply;


/** Widget to handle saving/loading of Control Console's data */
class SDMXControlConsoleEditorPresetWidget
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXControlConsoleEditorPresetWidget)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Gets current selected Control Console Preset, if valid */
	TWeakObjectPtr<UObject> GetSelectedPreset() const;

private:
	/** Called to create a new Control Console Preset */
	FReply OnCreateNewClicked();

	/** Called when a new Control Console Preset asset is selected in AssetPickerButton */
	void OnPresetSelected(const FAssetData& AssetData);

	/** Called when a Preset is saved */
	void OnPresetSaved(const UDMXControlConsolePreset* Preset);

	/** Current selected Control Console Preset */
	TWeakObjectPtr<UDMXControlConsolePreset> SelectedControlConsolePreset;
};
