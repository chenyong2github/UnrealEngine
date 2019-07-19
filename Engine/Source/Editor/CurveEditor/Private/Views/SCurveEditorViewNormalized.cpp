// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Views/SCurveEditorViewNormalized.h"
#include "CurveEditor.h"
#include "CurveEditorHelpers.h"
#include "CurveModel.h"
#include "Widgets/Text/STextBlock.h"

constexpr float NormalizedPadding = 10.f;

void SCurveEditorViewNormalized::Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor)
{
	bFixedOutputBounds = true;
	OutputMin = 0.0;
	OutputMax = 1.0;

	SInteractiveCurveEditorView::Construct(InArgs, InCurveEditor);

	ChildSlot
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	.Padding(FMargin(0.f, CurveViewConstants::CurveLabelOffsetY, CurveViewConstants::CurveLabelOffsetX, 0.f))
	[
		SNew(STextBlock)
		.Font(FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont"))
		.ColorAndOpacity(this, &SCurveEditorViewNormalized::GetCurveCaptionColor)
		.Text(this, &SCurveEditorViewNormalized::GetCurveCaption)
	];
}

void SCurveEditorViewNormalized::GetGridLinesY(TSharedRef<const FCurveEditor> CurveEditor, TArray<float>& MajorGridLines, TArray<float>& MinorGridLines, TArray<FText>* MajorGridLabels) const
{
	FCurveEditorScreenSpace ViewSpace = GetViewSpace();

	MajorGridLines.Add(ViewSpace.ValueToScreen(0.0));
	MajorGridLines.Add(ViewSpace.ValueToScreen(0.5));
	MajorGridLines.Add(ViewSpace.ValueToScreen(1.0));

	MinorGridLines.Add(ViewSpace.ValueToScreen(0.25));
	MinorGridLines.Add(ViewSpace.ValueToScreen(0.75));
}

void SCurveEditorViewNormalized::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	if (!CurveEditor->AreBoundTransformUpdatesSuppressed())
	{
		const double ValuePerPixel = 1.0 / AllottedGeometry.GetLocalSize().Y;
		const double ValueSpacePadding = NormalizedPadding * ValuePerPixel;

		OutputMin = 0.0 - ValueSpacePadding;
		OutputMax = 1.0 + ValueSpacePadding;

		for (auto It = CurveInfoByID.CreateIterator(); It; ++It)
		{
			FCurveModel* Curve = CurveEditor->FindCurve(It.Key());
			if (!ensureAlways(Curve))
			{
				continue;
			}

			double CurveOutputMin = 0, CurveOutputMax = 1;
			Curve->GetValueRange(CurveOutputMin, CurveOutputMax);

			const double Scale = (CurveOutputMax - CurveOutputMin);
			if (CurveOutputMax > CurveOutputMin)
			{
				It->Value.ViewToCurveTransform = FTransform2D(FScale2D(1.f, Scale), FVector2D(0.f, CurveOutputMin));
			}
			else
			{
				It->Value.ViewToCurveTransform = FVector2D(0.f, CurveOutputMin-0.5);
			}
		}
	}

	SInteractiveCurveEditorView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}