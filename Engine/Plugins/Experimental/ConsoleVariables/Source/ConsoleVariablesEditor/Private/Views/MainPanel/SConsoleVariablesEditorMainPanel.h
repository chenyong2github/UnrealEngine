// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"

class FConsoleVariablesEditorMainPanel;
class SMultiLineEditableTextBox;
class UConsoleVariablesAsset;

class SConsoleVariablesEditorMainPanel final : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS(SConsoleVariablesEditorMainPanel)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FConsoleVariablesEditorMainPanel>& InMainPanel);

	virtual ~SConsoleVariablesEditorMainPanel() override;

	/** A handler for when text is committed to the Console Input Box that appears when clicking the "Add Console Variable" button */
	FReply HandleConsoleInputTextCommitted(const FGeometry& MyGeometry, const FKeyEvent& KeyPressed);

private:

	/** A reference to the struct that controls this widget */
	TWeakPtr<FConsoleVariablesEditorMainPanel> MainPanel;

	/** A reference to the button which opens the plugin settings */
	TSharedPtr<SCheckBox> SettingsButtonPtr;

	/** A special text box widget that can search console variables as text is typed into it */
	TSharedPtr<SWidget> ConsoleInput;
	/** A reference to the actual text box inside ConsoleInput */
	TSharedPtr<SMultiLineEditableTextBox> ConsoleInputEditableTextBox;

	/** Creates the toolbar at the top of the MainPanel widget */
	TSharedRef<SWidget> GeneratePanelToolbar(const TSharedRef<SWidget> InConsoleInputWidget);

	// Save / Load

	#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

	const FText NoLoadedPresetText = LOCTEXT("NoLoadedPreset", "No Loaded Preset");
	const FText LoadedPresetFormatText = LOCTEXT("LoadedPresetFormat", "Current Preset: {0}");

    #undef LOCTEXT_NAMESPACE

	/** Creates a special asset picker widget to display when the Save/Load button is clicked */
	TSharedRef<SWidget> OnGeneratePresetsMenu();
};
