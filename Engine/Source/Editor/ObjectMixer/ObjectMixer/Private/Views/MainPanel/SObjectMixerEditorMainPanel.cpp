// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/SObjectMixerEditorMainPanel.h"

#include "ObjectMixerEditorLog.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "Kismet2/SClassPickerDialog.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

void SObjectMixerEditorMainPanel::Construct(
	const FArguments& InArgs, const TSharedRef<FObjectMixerEditorMainPanel>& InMainPanel)
{
	check(InMainPanel->GetEditorList().IsValid());

	MainPanel = InMainPanel;
	
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Vertical)
		.ResizeMode(ESplitterResizeMode::FixedSize)

		+SSplitter::Slot()
		[
			MainPanel.Pin()->GetEditorList().Pin()->GetOrCreateWidget()
		]
	];
}

SObjectMixerEditorMainPanel::~SObjectMixerEditorMainPanel()
{
	MainPanel.Reset();
}

#undef LOCTEXT_NAMESPACE
