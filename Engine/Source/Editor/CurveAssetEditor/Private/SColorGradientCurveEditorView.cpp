// Copyright Epic Games, Inc. All Rights Reserved.

#include "SColorGradientCurveEditorView.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SBorder.h"
#include "SColorGradientEditor.h"

void SColorGradientCurveEditorView::Construct(const FArguments& InArgs, TSharedRef<FCurveEditor> InCurveEditor)
{
	bPinned = 1;
	bInteractive = 0;
	bAutoSize = 1;
	bAllowEmpty = 1;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(.8f, .8f, .8f, .60f))
		.Padding(1.0f)
		[
			SAssignNew(GradientViewer, SColorGradientEditor)
			.ViewMinInput(InArgs._ViewMinInput)
			.ViewMaxInput(InArgs._ViewMaxInput)
			.IsEditingEnabled(InArgs._IsEditingEnabled)
		]
	];
}