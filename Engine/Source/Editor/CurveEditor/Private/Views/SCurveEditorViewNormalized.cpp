// Copyright Epic Games, Inc. All Rights Reserved.

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

FTransform2D CalculateViewToCurveTransform(const double InCurveOutputMin, const double InCurveOutputMax)
{
	const double Scale = (InCurveOutputMax - InCurveOutputMin);
	if (InCurveOutputMax > InCurveOutputMin)
	{
		return FTransform2D(FScale2D(1.f, Scale), FVector2D(0.f, InCurveOutputMin));
	}
	else
	{
		return FVector2D(0.f, InCurveOutputMin - 0.5);
	}
}

void SCurveEditorViewNormalized::DrawBufferedCurves(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	const float BufferedCurveThickness = 1.f;
	const bool  bAntiAliasCurves = true;
	const FLinearColor CurveColor = CurveViewConstants::BufferedCurveColor;
	const int32 CurveLayerId = BaseLayerId + CurveViewConstants::ELayerOffset::Curves;

	const double ValuePerPixel = 1.0 / AllottedGeometry.GetLocalSize().Y;
	const double ValueSpacePadding = NormalizedPadding * ValuePerPixel;

	// Calculate the normalized view to curve transform for each buffered curve, then draw
	const TArray<TUniquePtr<IBufferedCurveModel>>& BufferedCurves = CurveEditor->GetBufferedCurves();
	for (const TUniquePtr<IBufferedCurveModel>& BufferedCurve : BufferedCurves)
	{
		FTransform2D ViewToBufferedCurveTransform;
		double CurveOutputMin = BufferedCurve->GetValueMin(), CurveOutputMax = BufferedCurve->GetValueMax();

		ViewToBufferedCurveTransform = CalculateViewToCurveTransform(CurveOutputMin, CurveOutputMax);

		TArray<TTuple<double, double>> CurveSpaceInterpolatingPoints;
		FCurveEditorScreenSpace CurveSpace = GetViewSpace().ToCurveSpace(ViewToBufferedCurveTransform);

		BufferedCurve->DrawCurve(*CurveEditor, CurveSpace, CurveSpaceInterpolatingPoints);

		TArray<FVector2D> ScreenSpaceInterpolatingPoints;
		for (TTuple<double, double> Point : CurveSpaceInterpolatingPoints)
		{
			ScreenSpaceInterpolatingPoints.Add(FVector2D(
				CurveSpace.SecondsToScreen(Point.Get<0>()),
				CurveSpace.ValueToScreen(Point.Get<1>())
			));
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CurveLayerId,
			AllottedGeometry.ToPaintGeometry(),
			ScreenSpaceInterpolatingPoints,
			DrawEffects,
			CurveColor,
			bAntiAliasCurves,
			BufferedCurveThickness
		);
	}
}

void SCurveEditorViewNormalized::PaintView(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		DrawBackground(AllottedGeometry, OutDrawElements, BaseLayerId, DrawEffects);
		DrawGridLines(CurveEditor.ToSharedRef(), AllottedGeometry, OutDrawElements, BaseLayerId, DrawEffects);
		DrawBufferedCurves(AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, DrawEffects);
		DrawCurves(CurveEditor.ToSharedRef(), AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, InWidgetStyle, DrawEffects);
	}
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

			It->Value.ViewToCurveTransform = CalculateViewToCurveTransform(CurveOutputMin, CurveOutputMax);
		}
	}

	SInteractiveCurveEditorView::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}