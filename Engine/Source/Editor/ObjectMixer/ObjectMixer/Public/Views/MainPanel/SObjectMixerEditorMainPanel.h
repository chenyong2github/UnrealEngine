// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"

class FObjectMixerEditorMainPanel;
class SBox;
class SHorizontalBox;
class SVerticalBox;

class OBJECTMIXEREDITOR_API SObjectMixerEditorMainPanel final : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS(SObjectMixerEditorMainPanel)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FObjectMixerEditorMainPanel>& InMainPanel);

	virtual ~SObjectMixerEditorMainPanel() override;

private:

	/** A reference to the struct that controls this widget */
	TWeakPtr<FObjectMixerEditorMainPanel> MainPanel;

	TSharedPtr<SHorizontalBox> ToolbarHBox;

	/** A reference to the button which opens the plugin settings */
	TSharedPtr<SCheckBox> ConcertButtonPtr;

	/** Creates the toolbar at the top of the MainPanel widget */
	TSharedRef<SWidget> GeneratePanelToolbar();

	TSharedRef<SWidget> OnGeneratePresetsMenu();
};
