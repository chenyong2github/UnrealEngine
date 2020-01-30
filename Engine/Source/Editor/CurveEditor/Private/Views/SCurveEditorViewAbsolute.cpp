// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/SCurveEditorViewAbsolute.h"
#include "Widgets/Text/STextBlock.h"

void SCurveEditorViewAbsolute::Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor)
{
	SInteractiveCurveEditorView::Construct(InArgs, InCurveEditor);

	ChildSlot
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	.Padding(FMargin(0.f, CurveViewConstants::CurveLabelOffsetY, CurveViewConstants::CurveLabelOffsetX, 0.f))
	[
		SNew(STextBlock)
		.Font(FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont"))
		.ColorAndOpacity(this, &SCurveEditorViewAbsolute::GetCurveCaptionColor)
		.Text(this, &SCurveEditorViewAbsolute::GetCurveCaption)
	];
}