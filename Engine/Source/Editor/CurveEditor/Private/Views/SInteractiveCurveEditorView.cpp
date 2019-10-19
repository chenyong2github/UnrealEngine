// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Views/SInteractiveCurveEditorView.h"
#include "EditorStyleSet.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "CurveEditor.h"
#include "CurveEditorScreenSpace.h"
#include "CurveDrawInfo.h"
#include "CurveEditorSettings.h"
#include "ICurveEditorBounds.h"
#include "Types/SlateStructs.h"
#include "Fonts/FontMeasure.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "DragOperations/CurveEditorDragOperation_Tangent.h"
#include "DragOperations/CurveEditorDragOperation_MoveKeys.h"
#include "DragOperations/CurveEditorDragOperation_Marquee.h"
#include "DragOperations/CurveEditorDragOperation_Pan.h"
#include "DragOperations/CurveEditorDragOperation_Zoom.h"
#include "SCurveEditorPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "CurveEditorContextMenu.h"
#include "CurveEditorCommands.h"
#include "CurveEditorHelpers.h"
#include "CurveEditor.h"
#include "ITimeSlider.h"

namespace CurveViewConstants
{
	/** The number of pixels to offset Labels from the Left/Right size. */
	constexpr float LabelOffsetPixels = 2.f;

	/** The number of pixels away the mouse can be and still be considering hovering over a curve. */
	constexpr float HoverProximityThresholdPx = 5.f;
}

#define LOCTEXT_NAMESPACE "SInteractiveCurveEditorView"

TUniquePtr<ICurveEditorKeyDragOperation> CreateKeyDrag(ECurvePointType KeyType)
{
	switch (KeyType)
	{
	case ECurvePointType::ArriveTangent:
	case ECurvePointType::LeaveTangent:
		return MakeUnique<FCurveEditorDragOperation_Tangent>();

	default:
		return MakeUnique<FCurveEditorDragOperation_MoveKeys>();
	}
}

class SDynamicToolTip : public SToolTip
{
public:
	TAttribute<bool> bIsEnabled;
	virtual bool IsEmpty() const override { return !bIsEnabled.Get(); }
};

void SInteractiveCurveEditorView::Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor)
{
	FixedHeight = InArgs._FixedHeight;
	BackgroundTint = InArgs._BackgroundTint;
	MaximumCapacity = InArgs._MaximumCapacity;
	bAutoSize = InArgs._AutoSize;

	WeakCurveEditor = InCurveEditor;

	InCurveEditor.Pin()->OnActiveToolChangedDelegate.AddSP(this, &SInteractiveCurveEditorView::OnCurveEditorToolChanged);

	TSharedRef<SDynamicToolTip> ToolTipWidget =
		SNew(SDynamicToolTip)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolTip.BrightBackground"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SNew(STextBlock)
				.Text(this, &SInteractiveCurveEditorView::GetToolTipCurveName)
				.Font(FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont"))
				.ColorAndOpacity(FLinearColor::Black)
			]
		+ SVerticalBox::Slot()
		[
			SNew(STextBlock)
			.Text(this, &SInteractiveCurveEditorView::GetToolTipTimeText)
			.Font(FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont"))
			.ColorAndOpacity(FLinearColor::Black)
		]
		+ SVerticalBox::Slot()
		[
			SNew(STextBlock)
			.Text(this, &SInteractiveCurveEditorView::GetToolTipValueText)
			.Font(FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont"))
			.ColorAndOpacity(FLinearColor::Black)
		]
	];

	ToolTipWidget->bIsEnabled = MakeAttributeSP(this, &SInteractiveCurveEditorView::IsToolTipEnabled);
	SetToolTip(ToolTipWidget);
}

FText SInteractiveCurveEditorView::GetCurveCaption() const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor && CurveInfoByID.Num() == 1)
	{
		for (const TTuple<FCurveModelID, FCurveInfo>& Pair : CurveInfoByID)
		{
			if (const FCurveModel* Curve = CurveEditor->FindCurve(Pair.Key))
			{
				return Curve->GetLongDisplayName();
			}
		}
	}

	return FText::GetEmpty();
}

FSlateColor SInteractiveCurveEditorView::GetCurveCaptionColor() const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor && CurveInfoByID.Num() == 1)
	{
		for (const TTuple<FCurveModelID, FCurveInfo>& Pair : CurveInfoByID)
		{
			if (const FCurveModel* Curve = CurveEditor->FindCurve(Pair.Key))
			{
				return Curve->GetColor();
			}
		}
	}

	return BackgroundTint.CopyWithNewOpacity(1.f);
}

void SInteractiveCurveEditorView::GetGridLinesX(TSharedRef<const FCurveEditor> CurveEditor, TArray<float>& MajorGridLines, TArray<float>& MinorGridLines, TArray<FText>* MajorGridLabels) const
{
	CurveEditor->GetGridLinesX(MajorGridLines, MinorGridLines, MajorGridLabels);

	FCurveEditorScreenSpaceH PanelSpace = CurveEditor->GetPanelInputSpace();
	FCurveEditorScreenSpaceH ViewSpace  = GetViewSpace();

	double InputOffset = ViewSpace.GetInputMin() - PanelSpace.GetInputMin();
	if (InputOffset != 0.0)
	{
		const float PixelDifference = InputOffset * PanelSpace.PixelsPerInput();
		for (float& Line : MajorGridLines)
		{
			Line -= PixelDifference;
		}
		for (float& Line : MinorGridLines)
		{
			Line -= PixelDifference;
		}
	}
}

void SInteractiveCurveEditorView::GetGridLinesY(TSharedRef<const FCurveEditor> CurveEditor, TArray<float>& MajorGridLines, TArray<float>& MinorGridLines, TArray<FText>* MajorGridLabels) const
{
	const TOptional<float> GridLineSpacing = CurveEditor->GetGridSpacing();
	if (!GridLineSpacing)
	{
		CurveEditor::ConstructYGridLines(GetViewSpace(), 4, MajorGridLines, MinorGridLines, CurveEditor->GetGridLineLabelFormatYAttribute().Get(), MajorGridLabels);
	}
	else
	{
		CurveEditor::ConstructFixedYGridLines(GetViewSpace(), 4, GridLineSpacing.GetValue(), MajorGridLines, MinorGridLines, CurveEditor->GetGridLineLabelFormatYAttribute().Get(), MajorGridLabels, TOptional<double>(), TOptional<double>());
	}
}

int32 SInteractiveCurveEditorView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	PaintView(Args, AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, InWidgetStyle, bParentEnabled);
	SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId + CurveViewConstants::ELayerOffset::WidgetContent, InWidgetStyle, bParentEnabled);

	return BaseLayerId;
}

void SInteractiveCurveEditorView::PaintView(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		DrawBackground(AllottedGeometry, OutDrawElements, BaseLayerId, DrawEffects);
		DrawGridLines(CurveEditor.ToSharedRef(), AllottedGeometry, OutDrawElements, BaseLayerId, DrawEffects);
		DrawBufferedCurves(CurveEditor.ToSharedRef(), AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, InWidgetStyle, DrawEffects);
		DrawCurves(CurveEditor.ToSharedRef(), AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, InWidgetStyle, DrawEffects);
	}
}

void SInteractiveCurveEditorView::DrawBackground(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const
{
	if (BackgroundTint != FLinearColor::White)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, BaseLayerId + CurveViewConstants::ELayerOffset::Background, AllottedGeometry.ToPaintGeometry(),
			FEditorStyle::GetBrush("ToolPanel.GroupBorder"), DrawEffects, BackgroundTint);
	}
}

void SInteractiveCurveEditorView::DrawGridLines(TSharedRef<FCurveEditor> CurveEditor, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const
{
	// Rendering info
	const float          Width = AllottedGeometry.GetLocalSize().X;
	const float          Height = AllottedGeometry.GetLocalSize().Y;
	const float          RoundedWidth = FMath::RoundToFloat(Width);
	const float          RoundedHeight = FMath::RoundToFloat(Height);
	const FLinearColor   MajorGridColor = CurveEditor->GetPanel()->GetGridLineTint();
	const FLinearColor   MinorGridColor = MajorGridColor.CopyWithNewOpacity(MajorGridColor.A * .5f);
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
	const FLinearColor	 LabelColor = FLinearColor::White.CopyWithNewOpacity(0.65f);
	const FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont");

	// Get our viewing range bounds. We go through the GetBounds() interface on the curve editor because it's more aware of what our range is than the original widget is.
	double InputValueMin, InputValueMax;
	GetInputBounds(InputValueMin, InputValueMax);

	TArray<float> MajorGridLines, MinorGridLines;
	TArray<FText> MajorGridLabels;

	GetGridLinesX(CurveEditor, MajorGridLines, MinorGridLines, &MajorGridLabels);
	ensureMsgf(MajorGridLabels.Num() == 0 || MajorGridLines.Num() == MajorGridLabels.Num(), TEXT("If grid labels are specified, one must be specified for every major grid line, even if it is just an empty FText."));

	// Pre-allocate an array of line points to draw our vertical lines. Each major grid line
	// will overwrite the X value of both points but leave the Y value untouched so they draw from the bottom to the top.
	TArray<FVector2D> LinePoints;
	LinePoints.Add(FVector2D(0.f, 0.f));
	LinePoints.Add(FVector2D(0.f, Height));

	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	// Draw major vertical grid lines
	for (int32 i = 0; i < MajorGridLines.Num(); i++)
	{
		const float RoundedLine = FMath::RoundToFloat(MajorGridLines[i]);
		if (RoundedLine < 0 || RoundedLine > RoundedWidth)
		{
			continue;
		}

		// Vertical Grid Line
		LinePoints[0].X = LinePoints[1].X = RoundedLine;

		if (MajorGridLabels.IsValidIndex(i))
		{
			FText Label = MajorGridLabels[i];
			const FVector2D LabelSize = FontMeasure->Measure(Label, FontInfo);
			const FPaintGeometry LabelGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(FVector2D(LinePoints[0].X - LabelSize.X*.5f, CurveViewConstants::LabelOffsetPixels)));

			LinePoints[0].Y = LabelSize.Y + CurveViewConstants::LabelOffsetPixels*2.f;

			FSlateDrawElement::MakeText(
				OutDrawElements,
				BaseLayerId + CurveViewConstants::ELayerOffset::GridLabels,
				LabelGeometry,
				Label,
				FontInfo,
				DrawEffects,
				LabelColor
			);
		}
		else
		{
			LinePoints[0].Y = 0.f;
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			BaseLayerId + CurveViewConstants::ELayerOffset::GridLines,
			PaintGeometry,
			LinePoints,
			DrawEffects,
			MajorGridColor,
			false
		);
	}

	LinePoints[0].Y = 0.f;

	// Now draw the minor vertical lines which are drawn with a lighter color.
	for (float PosX : MinorGridLines)
	{
		if (PosX < 0 || PosX > Width)
		{
			continue;
		}

		LinePoints[0].X = LinePoints[1].X = PosX;

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			BaseLayerId + CurveViewConstants::ELayerOffset::GridLines,
			PaintGeometry,
			LinePoints,
			DrawEffects,
			MinorGridColor,
			false
		);
	}

	MajorGridLines.Reset();
	MinorGridLines.Reset();
	MajorGridLabels.Reset();
	GetGridLinesY(CurveEditor, MajorGridLines, MinorGridLines, &MajorGridLabels);
	ensureMsgf(MajorGridLabels.Num() == 0 || MajorGridLines.Num() == MajorGridLabels.Num(), TEXT("If grid labels are specified, one must be specified for every major grid line, even if it is just an empty FText."));

	// Reset our cached Line to draw from left to right
	LinePoints[0].X = 0.f;
	LinePoints[1].X = Width;

	// Draw our major horizontal lines
	for (int32 i = 0; i < MajorGridLines.Num(); i++)
	{
		const float RoundedLine = FMath::RoundToFloat(MajorGridLines[i]);
		if (RoundedLine < 0 || RoundedLine > RoundedHeight)
		{
			continue;
		}

		// Overwrite the height of the line we're drawing to draw the different grid lines.
		LinePoints[0].Y = LinePoints[1].Y = RoundedLine;

		if (MajorGridLabels.IsValidIndex(i))
		{
			FText Label = MajorGridLabels[i];
			const FVector2D LabelSize = FontMeasure->Measure(Label, FontInfo);
			const FPaintGeometry LabelGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(FVector2D(CurveViewConstants::LabelOffsetPixels, LinePoints[0].Y - LabelSize.Y*.5f)));

			LinePoints[0].X = LabelSize.X + CurveViewConstants::LabelOffsetPixels*2.f;

			FSlateDrawElement::MakeText(
				OutDrawElements,
				BaseLayerId + CurveViewConstants::ELayerOffset::GridLabels,
				LabelGeometry,
				Label,
				FontInfo,
				DrawEffects,
				LabelColor
			);
		}
		else
		{
			LinePoints[0].X = 0.f;
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			BaseLayerId + CurveViewConstants::ELayerOffset::GridLines,
			PaintGeometry,
			LinePoints,
			DrawEffects,
			MajorGridColor,
			false
		);
	}

	LinePoints[0].X = 0.f;

	// Draw our minor horizontal lines
	for (float PosY : MinorGridLines)
	{
		if (PosY < 0 || PosY > Height)
		{
			continue;
		}

		LinePoints[0].Y = LinePoints[1].Y = PosY;

		// Now draw the minor grid lines with a lighter color.
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			BaseLayerId + CurveViewConstants::ELayerOffset::GridLines,
			PaintGeometry,
			LinePoints,
			DrawEffects,
			MinorGridColor,
			false
		);
	}
}

void SInteractiveCurveEditorView::DrawCurves(TSharedRef<FCurveEditor> CurveEditor, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, ESlateDrawEffect DrawEffects) const
{
	static const FName SelectionColorName("SelectionColor");
	FLinearColor SelectionColor = FEditorStyle::GetSlateColor(SelectionColorName).GetColor(InWidgetStyle);

	const FVector2D      VisibleSize = AllottedGeometry.GetLocalSize();
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	const float HoveredCurveThickness = 3.f;
	const float UnHoveredCurveThickness = 1.f;
	const bool  bAntiAliasCurves = true;

	TOptional<FCurveModelID> HoveredCurve = GetHoveredCurve();
	for (const FCurveDrawParams& Params : CachedDrawParams)
	{
		const bool bIsCurveHovered = HoveredCurve.IsSet() && HoveredCurve.GetValue() == Params.GetID();
		const float Thickness = bIsCurveHovered ? HoveredCurveThickness : UnHoveredCurveThickness;
		const int32 CurveLayerId = bIsCurveHovered ? BaseLayerId + CurveViewConstants::ELayerOffset::Curves : BaseLayerId + CurveViewConstants::ELayerOffset::HoveredCurves;

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			CurveLayerId,
			PaintGeometry,
			Params.InterpolatingPoints,
			DrawEffects,
			Params.Color,
			bAntiAliasCurves,
			Thickness
		);

		TArray<FVector2D> LinePoints;
		LinePoints.SetNum(2);

		// Draw tangents
		if (Params.bKeyDrawEnabled)
		{
			for (int32 PointIndex = 0; PointIndex < Params.Points.Num(); PointIndex++)
			{
				const FCurvePointInfo& Point = Params.Points[PointIndex];
				const FKeyDrawInfo& PointDrawInfo = Params.GetKeyDrawInfo(Point.Type, PointIndex);
				const bool          bSelected = CurveEditor->GetSelection().IsSelected(FCurvePointHandle(Params.GetID(), Point.Type, Point.KeyHandle));
				const FLinearColor  PointTint = bSelected ? SelectionColor : PointDrawInfo.Tint;

				const int32 KeyLayerId = BaseLayerId + Point.LayerBias + (bSelected ? CurveViewConstants::ELayerOffset::SelectedKeys : CurveViewConstants::ELayerOffset::Keys);

				if (Point.LineDelta.X != 0.f || Point.LineDelta.Y != 0.f)
				{
					LinePoints[0] = Point.ScreenPosition + Point.LineDelta.GetSafeNormal() * (PointDrawInfo.ScreenSize.X*.5f);
					LinePoints[1] = Point.ScreenPosition + Point.LineDelta;

					// Draw the connecting line - connecting lines are always drawn below everything else
					FSlateDrawElement::MakeLines(OutDrawElements, BaseLayerId + CurveViewConstants::ELayerOffset::Keys - 1, PaintGeometry, LinePoints, DrawEffects, PointTint, true);
				}

				FPaintGeometry PointGeometry = AllottedGeometry.ToPaintGeometry(
					Point.ScreenPosition - (PointDrawInfo.ScreenSize * 0.5f),
					PointDrawInfo.ScreenSize
				);

				FSlateDrawElement::MakeBox(OutDrawElements, KeyLayerId, PointGeometry, PointDrawInfo.Brush, DrawEffects, PointTint);
			}
		}
	}
}

void SInteractiveCurveEditorView::DrawBufferedCurves(TSharedRef<FCurveEditor> CurveEditor, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, ESlateDrawEffect DrawEffects) const
{
	const float BufferedCurveThickness = 1.f;
	const bool  bAntiAliasCurves = true;
	const FLinearColor CurveColor = CurveViewConstants::BufferedCurveColor;
	const TArray<TUniquePtr<IBufferedCurveModel>>& BufferedCurves = CurveEditor->GetBufferedCurves();

	const int32 CurveLayerId = BaseLayerId + CurveViewConstants::ELayerOffset::Curves;

	// Draw each buffered curve using the view space transform since the curve space for all curves is the same
	for (const TUniquePtr<IBufferedCurveModel>& BufferedCurve : BufferedCurves)
	{
		TArray<TTuple<double, double>> CurveSpaceInterpolatingPoints;
		FCurveEditorScreenSpace CurveSpace = GetViewSpace();

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

void SInteractiveCurveEditorView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Cache our Curve Drawing Params. These are used in multiple places so we cache them once each frame.
	CachedDrawParams.Reset();
	GetCurveDrawParams(CachedDrawParams);
}

void SInteractiveCurveEditorView::GetCurveDrawParams(TArray<FCurveDrawParams>& OutDrawParams) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	// Get the Min/Max values on the X axis, for Time
	double InputMin = 0, InputMax = 1;
	GetInputBounds(InputMin, InputMax);

	ECurveEditorTangentVisibility TangentVisibility = CurveEditor->GetSettings()->GetTangentVisibility();
	OutDrawParams.Reserve(CurveInfoByID.Num());

	for (const TTuple<FCurveModelID, FCurveInfo>& Pair : CurveInfoByID)
	{
		FCurveModel* CurveModel = CurveEditor->FindCurve(Pair.Key);
		if (!ensureAlways(CurveModel))
		{
			continue;
		}

		FCurveEditorScreenSpace CurveSpace = GetCurveSpace(Pair.Key);
		
		const float DisplayRatio = (CurveSpace.PixelsPerOutput() / CurveSpace.PixelsPerInput());

		const FKeyHandleSet* SelectedKeys = CurveEditor->GetSelection().GetAll().Find(Pair.Key);

		// Create a new set of Curve Drawing Parameters to represent this particular Curve
		FCurveDrawParams Params(Pair.Key);
		Params.Color = CurveModel->GetColor();
		Params.bKeyDrawEnabled = CurveModel->IsKeyDrawEnabled();

		// Gather the display metrics to use for each key type. This allows a Curve Model to override
		// whether or not the curve supports Keys, Arrive/Leave Tangents, etc. If the Curve Model doesn't
		// support a particular capability we can skip drawing them.
		CurveModel->GetKeyDrawInfo(ECurvePointType::ArriveTangent, FKeyHandle::Invalid(), Params.ArriveTangentDrawInfo);
		CurveModel->GetKeyDrawInfo(ECurvePointType::LeaveTangent, FKeyHandle::Invalid(), Params.LeaveTangentDrawInfo);

		// Gather the interpolating points in input/output space
		TArray<TTuple<double, double>> InterpolatingPoints;

		CurveModel->DrawCurve(*CurveEditor, CurveSpace, InterpolatingPoints);
		Params.InterpolatingPoints.Reserve(InterpolatingPoints.Num());

		// An Input Offset allows for a fixed offset to all keys, such as displaying them in the middle of a frame instead of at the start.
		double InputOffset = CurveModel->GetInputDisplayOffset();

		// Convert the interpolating points to screen space
		for (TTuple<double, double> Point : InterpolatingPoints)
		{
			Params.InterpolatingPoints.Add(
				FVector2D(
					CurveSpace.SecondsToScreen(Point.Get<0>() + InputOffset),
					CurveSpace.ValueToScreen(Point.Get<1>())
				)
			);
		}

		TArray<FKeyHandle> VisibleKeys;
		CurveModel->GetKeys(*CurveEditor, InputMin, InputMax, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), VisibleKeys);

		if (VisibleKeys.Num())
		{
			TArray<FKeyPosition> AllKeyPositions;
			TArray<FKeyAttributes> AllKeyAttributes;

			AllKeyPositions.SetNum(VisibleKeys.Num());
			AllKeyAttributes.SetNum(VisibleKeys.Num());

			CurveModel->GetKeyPositions(VisibleKeys, AllKeyPositions);
			CurveModel->GetKeyAttributes(VisibleKeys, AllKeyAttributes);

			for (int32 Index = 0; Index < VisibleKeys.Num(); ++Index)
			{
				const FKeyHandle      KeyHandle = VisibleKeys[Index];
				const FKeyPosition&   KeyPosition = AllKeyPositions[Index];
				const FKeyAttributes& Attributes = AllKeyAttributes[Index];

				bool bShowTangents = TangentVisibility == ECurveEditorTangentVisibility::AllTangents || (TangentVisibility == ECurveEditorTangentVisibility::SelectedKeys && SelectedKeys && SelectedKeys->Contains(VisibleKeys[Index]));

				float TimeScreenPos = CurveSpace.SecondsToScreen(KeyPosition.InputValue + InputOffset);
				float ValueScreenPos = CurveSpace.ValueToScreen(KeyPosition.OutputValue);

				// Add this key
				FCurvePointInfo Key(KeyHandle);
				Key.ScreenPosition = FVector2D(TimeScreenPos, ValueScreenPos);
				Key.LayerBias = 2;

				// Add draw info for the specific key
				CurveModel->GetKeyDrawInfo(ECurvePointType::Key, KeyHandle, /*Out*/ Key.DrawInfo);
				Params.Points.Add(Key);

				if (bShowTangents && Attributes.HasArriveTangent())
				{
					float ArriveTangent = Attributes.GetArriveTangent();

					FCurvePointInfo ArriveTangentPoint(KeyHandle);
					ArriveTangentPoint.Type = ECurvePointType::ArriveTangent;


					if (Attributes.HasTangentWeightMode() && Attributes.HasArriveTangentWeight() &&
						(Attributes.GetTangentWeightMode() == RCTWM_WeightedBoth || Attributes.GetTangentWeightMode() == RCTWM_WeightedArrive))
					{
						FVector2D TangentOffset = CurveEditor::ComputeScreenSpaceTangentOffset(CurveSpace, ArriveTangent, -Attributes.GetArriveTangentWeight());
						ArriveTangentPoint.ScreenPosition = Key.ScreenPosition + TangentOffset;
					}
					else
					{
						float PixelLength = 60.0f;
						ArriveTangentPoint.ScreenPosition = Key.ScreenPosition + CurveEditor::GetVectorFromSlopeAndLength(ArriveTangent * -DisplayRatio, -PixelLength);
					}
					ArriveTangentPoint.LineDelta = Key.ScreenPosition - ArriveTangentPoint.ScreenPosition;
					ArriveTangentPoint.LayerBias = 1;

					// Add draw info for the specific tangent
					FKeyDrawInfo TangentDrawInfo;
					CurveModel->GetKeyDrawInfo(ECurvePointType::ArriveTangent, KeyHandle, /*Out*/ ArriveTangentPoint.DrawInfo);

					Params.Points.Add(ArriveTangentPoint);
				}

				if (bShowTangents && Attributes.HasLeaveTangent())
				{
					float LeaveTangent = Attributes.GetLeaveTangent();

					FCurvePointInfo LeaveTangentPoint(KeyHandle);
					LeaveTangentPoint.Type = ECurvePointType::LeaveTangent;

					if (Attributes.HasTangentWeightMode() && Attributes.HasLeaveTangentWeight() &&
						(Attributes.GetTangentWeightMode() == RCTWM_WeightedBoth || Attributes.GetTangentWeightMode() == RCTWM_WeightedLeave))
					{
						FVector2D TangentOffset = CurveEditor::ComputeScreenSpaceTangentOffset(CurveSpace, LeaveTangent, Attributes.GetLeaveTangentWeight());

						LeaveTangentPoint.ScreenPosition = Key.ScreenPosition + TangentOffset;
					}
					else
					{
						float PixelLength = 60.0f;
						LeaveTangentPoint.ScreenPosition = Key.ScreenPosition + CurveEditor::GetVectorFromSlopeAndLength(LeaveTangent * -DisplayRatio, PixelLength);

					}

					LeaveTangentPoint.LineDelta = Key.ScreenPosition - LeaveTangentPoint.ScreenPosition;
					LeaveTangentPoint.LayerBias = 1;

					// Add draw info for the specific tangent
					FKeyDrawInfo TangentDrawInfo;
					CurveModel->GetKeyDrawInfo(ECurvePointType::LeaveTangent, KeyHandle, /*Out*/ LeaveTangentPoint.DrawInfo);

					Params.Points.Add(LeaveTangentPoint);
				}
			}
		}

		OutDrawParams.Add(MoveTemp(Params));
	}
}
void SInteractiveCurveEditorView::GetPointsWithinWidgetRange(const FSlateRect& WidgetRectangle, TArray<FCurvePointHandle>* OutPoints) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	// Iterate through all of our points and see which points the marquee overlaps. Both of these coordinate systems
	// are in screen space pixels.
	for (const FCurveDrawParams& DrawParams : CachedDrawParams)
	{
		for (int32 PointIndex = 0; PointIndex < DrawParams.Points.Num(); PointIndex++)
		{
			const FCurvePointInfo& Point = DrawParams.Points[PointIndex];

			const FKeyDrawInfo& DrawInfo = DrawParams.GetKeyDrawInfo(Point.Type, PointIndex);
			FSlateRect PointRect = FSlateRect::FromPointAndExtent(Point.ScreenPosition - DrawInfo.ScreenSize/2, DrawInfo.ScreenSize);

			if (FSlateRect::DoRectanglesIntersect(PointRect, WidgetRectangle))
			{
				OutPoints->Add(FCurvePointHandle(DrawParams.GetID(), Point.Type, Point.KeyHandle));
			}
		}
	}
}

void SInteractiveCurveEditorView::UpdateCurveProximities(FVector2D MousePixel)
{
	if (DragOperation.IsSet())
	{
		return;
	}

	CurveProximities.Reset();
	CachedToolTipData.Reset();

	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	TOptional<FCurvePointHandle> MousePoint = HitPoint(MousePixel);
	if (MousePoint.IsSet())
	{
		// If the mouse is over a point, that curve is always the closest, so just add that directly and don't
		// bother adding the others
		CurveProximities.Add(MakeTuple(MousePoint->CurveID, 0.f));
	}
	else for (const TTuple<FCurveModelID, FCurveInfo>& Pair : CurveInfoByID)
	{
		const FCurveModel* CurveModel = CurveEditor->FindCurve(Pair.Key);
		if (!ensureAlways(CurveModel))
		{
			continue;
		}

		FCurveEditorScreenSpace CurveSpace = GetCurveSpace(Pair.Key);

		double MinMouseTime = CurveSpace.ScreenToSeconds(MousePixel.X - CurveViewConstants::HoverProximityThresholdPx);
		double MaxMouseTime = CurveSpace.ScreenToSeconds(MousePixel.X + CurveViewConstants::HoverProximityThresholdPx);
		double MouseValue = CurveSpace.ScreenToValue(MousePixel.Y);
		float  PixelsPerOutput = CurveSpace.PixelsPerOutput();

		FVector2D MinPos(MousePixel.X - CurveViewConstants::HoverProximityThresholdPx, 0.0f);
		FVector2D MaxPos(MousePixel.X + CurveViewConstants::HoverProximityThresholdPx, 0.0f);

		double InputOffset = CurveModel->GetInputDisplayOffset();
		double MinEvalTime = MinMouseTime - InputOffset;
		double MaxEvalTime = MaxMouseTime - InputOffset;

		double MinValue = 0.0, MaxValue = 0.0;
		if (CurveModel->Evaluate(MinEvalTime, MinValue) && CurveModel->Evaluate(MaxEvalTime, MaxValue))
		{
			MinPos.Y = CurveSpace.ValueToScreen(MinValue);
			MaxPos.Y = CurveSpace.ValueToScreen(MaxValue);

			float Distance = (FMath::ClosestPointOnSegment2D(MousePixel, MinPos, MaxPos) - MousePixel).Size();
			CurveProximities.Add(MakeTuple(Pair.Key, Distance));
		}
	}

	Algo::SortBy(CurveProximities, [](TTuple<FCurveModelID, float> In) { return In.Get<1>(); });

	if (CurveProximities.Num() > 0 && CurveProximities[0].Get<1>() < CurveViewConstants::HoverProximityThresholdPx)
	{
		const FCurveModel* HoveredCurve = CurveEditor->FindCurve(CurveProximities[0].Get<0>());
		if (HoveredCurve)
		{
			FCurveEditorScreenSpace CurveSpace = GetCurveSpace(CurveProximities[0].Get<0>());
			double MouseTime = CurveSpace.ScreenToSeconds(MousePixel.X) - HoveredCurve->GetInputDisplayOffset();
			double EvaluatedTime = CurveEditor->GetCurveSnapMetrics(CurveProximities[0].Get<0>()).SnapInputSeconds(MouseTime);

			double EvaluatedValue = 0.0;
			HoveredCurve->Evaluate(EvaluatedTime, EvaluatedValue);

			FCachedToolTipData ToolTipData;
			ToolTipData.Text = FText::Format(LOCTEXT("CurveEditorTooltipName", "Name: {0}"), HoveredCurve->GetLongDisplayName());
			ToolTipData.EvaluatedTime = FText::Format(LOCTEXT("CurveEditorTime", "Time: {0}"), EvaluatedTime);
			ToolTipData.EvaluatedValue = FText::Format(LOCTEXT("CurveEditorValue", "Value: {0}"), EvaluatedValue);
			
			CachedToolTipData = ToolTipData;
		}
	}
}

void SInteractiveCurveEditorView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//CurveProximities.Reset();
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
}

void SInteractiveCurveEditorView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	// Don't allow hover highlights when we've exited this view as clicking won't be routed to us to select it anyways.
	CurveProximities.Reset();
	SCompoundWidget::OnMouseLeave(MouseEvent);
}

FReply SInteractiveCurveEditorView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	TSharedPtr<SCurveEditorPanel> EditorPanel = CurveEditor.IsValid() ? CurveEditor->GetPanel() : nullptr;
	if (!CurveEditor || !EditorPanel)
	{
		return FReply::Unhandled();
	}

	// Don't handle updating if we have a context menu open.
	if (ActiveContextMenu.Pin())
	{
		return FReply::Unhandled();
	}

	// Update our Curve Proximities for hover states and context actions. This also updates our cached hovered curve.
	FVector2D MousePixel = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	UpdateCurveProximities(MousePixel);

	// Cache the mouse position so that commands such as add key can work from command bindings 
	CachedMousePosition = MousePixel;

	if (DragOperation.IsSet())
	{
		FVector2D InitialPosition = DragOperation->GetInitialPosition();

		if (!DragOperation->IsDragging() && DragOperation->AttemptDragStart(MouseEvent))
		{
			DragOperation->DragImpl->BeginDrag(InitialPosition, MousePixel, MouseEvent);
			return FReply::Handled().CaptureMouse(AsShared());
		}
		else if (DragOperation->IsDragging())
		{
			DragOperation->DragImpl->Drag(InitialPosition, MousePixel, MouseEvent);
		}
		return FReply::Handled();
	}

	// We don't absorb this event as we're just updating hover states anyways.
	return FReply::Unhandled();
}

FReply SInteractiveCurveEditorView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor || bFixedOutputBounds)
	{
		return FReply::Unhandled();
	}

	FCurveEditorScreenSpace ViewSpace = GetViewSpace();

	FVector2D MousePixel   = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	double    CurrentTime  = ViewSpace.ScreenToSeconds(MousePixel.X);
	double    CurrentValue = ViewSpace.ScreenToValue(MousePixel.Y);

	// Attempt to zoom around the current time if settings specify it and there is a valid time.
	if (CurveEditor->GetSettings()->GetZoomPosition() == ECurveEditorZoomPosition::CurrentTime)
	{
		if (CurveEditor->GetTimeSliderController().IsValid())
		{
			FFrameTime ScrubPosition = CurveEditor->GetTimeSliderController()->GetScrubPosition();
			double PlaybackPosition = ScrubPosition / CurveEditor->GetTimeSliderController()->GetTickResolution();
			if (CurveEditor->GetTimeSliderController()->GetViewRange().Contains(PlaybackPosition))
			{
				CurrentTime = PlaybackPosition;
			}
		}
	}

	float ZoomDelta = 1.f - FMath::Clamp(0.1f * MouseEvent.GetWheelDelta(), -0.9f, 0.9f);
	ZoomAround(FVector2D(ZoomDelta, ZoomDelta), CurrentTime, CurrentValue);

	return FReply::Handled();
}

TOptional<FCurveModelID> SInteractiveCurveEditorView::GetHoveredCurve() const
{
	if (CurveProximities.Num() > 0 && CurveProximities[0].Get<1>() < CurveViewConstants::HoverProximityThresholdPx)
	{
		return CurveProximities[0].Get<0>();
	}

	return TOptional<FCurveModelID>();
}

bool SInteractiveCurveEditorView::IsToolTipEnabled() const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		return (CachedToolTipData.IsSet() && CurveEditor->GetSettings()->GetShowCurveEditorCurveToolTips());
	}

	return false;
}

FText SInteractiveCurveEditorView::GetToolTipCurveName() const
{
	return CachedToolTipData.IsSet() ? CachedToolTipData->Text : FText();
}

FText SInteractiveCurveEditorView::GetToolTipTimeText() const
{
	return CachedToolTipData.IsSet() ? CachedToolTipData->EvaluatedTime : FText();
}

FText SInteractiveCurveEditorView::GetToolTipValueText() const
{
	return CachedToolTipData.IsSet() ? CachedToolTipData->EvaluatedValue : FText();
}

FReply SInteractiveCurveEditorView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	TSharedPtr<SCurveEditorPanel> EditorPanel = CurveEditor.IsValid() ? CurveEditor->GetPanel() : nullptr;
	if (!CurveEditor || !EditorPanel)
	{
		return FReply::Unhandled();
	}
	
	FVector2D MousePixel = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	// Cache the mouse position so that commands such as add key can work from command bindings 
	CachedMousePosition = MousePixel;

	// Rebind our context actions so that shift click commands use the right position.
	RebindContextualActions(MousePixel);

	// // Marquee Selection
	// if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	// {
	// 	DragOperation = FCurveEditorDelayedDrag(MousePixel, MouseEvent.GetEffectingButton());
	// 	DragOperation->DragImpl = MakeUnique<FCurveEditorDragOperation_Marquee>(CurveEditor.Get(), this);
	// 	return FReply::Handled();
	// }
	// Middle Click + Alt Pan
	//else
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		if (MouseEvent.IsAltDown())
		{
			DragOperation = FCurveEditorDelayedDrag(MousePixel, MouseEvent.GetEffectingButton());
			DragOperation->DragImpl = MakeUnique<FCurveEditorDragOperation_PanInput>(CurveEditor.Get());
			return FReply::Handled();
		}
		else
		{
			// Middle Mouse can try to create keys on curves.
			TOptional<FCurvePointHandle> NewPoint;

			// Add a key to the closest curve to the mouse
			if (TOptional<FCurveModelID> HoveredCurve = GetHoveredCurve())
			{
				FCurveModel* CurveToAddTo = CurveEditor->FindCurve(HoveredCurve.GetValue());
				if (CurveToAddTo && !CurveToAddTo->IsReadOnly())
				{
					FScopedTransaction Transaction(LOCTEXT("InsertKey", "Insert Key"));

					FCurveEditorScreenSpace CurveSpace = GetCurveSpace(HoveredCurve.GetValue());
					FKeyAttributes DefaultAttributes = CurveEditor->GetDefaultKeyAttributes().Get();

					double MouseTime = CurveSpace.ScreenToSeconds(MousePixel.X);
					double MouseValue = CurveSpace.ScreenToValue(MousePixel.Y);

					FCurveSnapMetrics SnapMetrics = CurveEditor->GetCurveSnapMetrics(HoveredCurve.GetValue());
					MouseTime = SnapMetrics.SnapInputSeconds(MouseTime);
					MouseValue = SnapMetrics.SnapOutput(MouseValue);

					// When adding to a curve with no variance, add it with the same value so that
					// curves don't pop wildly in normalized views due to a slight difference between the keys
					double CurveOutputMin = 0, CurveOutputMax = 1;
					CurveToAddTo->GetValueRange(CurveOutputMin, CurveOutputMax);
					if (CurveOutputMin == CurveOutputMax)
					{
						MouseValue = CurveOutputMin;
					}

					CurveToAddTo->Modify();

					// Add a key on this curve
					TOptional<FKeyHandle> NewKey = CurveToAddTo->AddKey(FKeyPosition(MouseTime, MouseValue), DefaultAttributes);
					if (NewKey.IsSet())
					{
						NewPoint = FCurvePointHandle(HoveredCurve.GetValue(), ECurvePointType::Key, NewKey.GetValue());

						CurveEditor->GetSelection().Clear();
						CurveEditor->GetSelection().Add(NewPoint.GetValue());
					}
					else
					{
						Transaction.Cancel();
					}
				}
			}

			TUniquePtr<ICurveEditorKeyDragOperation> KeyDrag = CreateKeyDrag(CurveEditor->GetSelection().GetSelectionType());

			KeyDrag->Initialize(CurveEditor.Get(), NewPoint);

			DragOperation = FCurveEditorDelayedDrag(MousePixel, MouseEvent.GetEffectingButton());
			DragOperation->DragImpl = MoveTemp(KeyDrag);

			return FReply::Handled().PreventThrottling();
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Zoom Timeline
		if (MouseEvent.IsAltDown())
		{
			DragOperation = FCurveEditorDelayedDrag(MousePixel, MouseEvent.GetEffectingButton());
			DragOperation->DragImpl = MakeUnique<FCurveEditorDragOperation_Zoom>(CurveEditor.Get(), SharedThis(this));
			return FReply::Handled();
		}
		// Pan Timeline if we have flexible output bounds
		else if (!bFixedOutputBounds)
		{
			DragOperation = FCurveEditorDelayedDrag(MousePixel, MouseEvent.GetEffectingButton());
			DragOperation->DragImpl = MakeUnique<FCurveEditorDragOperation_PanView>(CurveEditor.Get(), SharedThis(this));
			return FReply::Handled();
		}
	}

	bool bShiftPressed = MouseEvent.IsShiftDown();
	bool bCtrlPressed = MouseEvent.IsControlDown();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Key Selection Testing
		TOptional<FCurvePointHandle> MouseDownPoint = HitPoint(MousePixel);
		if (MouseDownPoint.IsSet())
		{
			if (FCurveModel* CurveModel = CurveEditor->FindCurve(MouseDownPoint->CurveID))
			{
				if (!CurveModel->IsReadOnly())
				{
					if (bShiftPressed)
					{
						CurveEditor->GetSelection().Add(MouseDownPoint.GetValue());
					}
					else if (bCtrlPressed)
					{
						CurveEditor->GetSelection().Toggle(MouseDownPoint.GetValue());
					}
					else
					{
						if (CurveEditor->GetSelection().Contains(MouseDownPoint->CurveID, MouseDownPoint->KeyHandle))
						{
							CurveEditor->GetSelection().ChangeSelectionPointType(MouseDownPoint->PointType);
						}
						else
						{
							CurveEditor->GetSelection().Clear();
							CurveEditor->GetSelection().Add(MouseDownPoint.GetValue());
						}
					}

					TUniquePtr<ICurveEditorKeyDragOperation> KeyDrag = CreateKeyDrag(MouseDownPoint->PointType);

					KeyDrag->Initialize(CurveEditor.Get(), MouseDownPoint);

					DragOperation = FCurveEditorDelayedDrag(MousePixel, MouseEvent.GetEffectingButton());
					DragOperation->DragImpl = MoveTemp(KeyDrag);

					return FReply::Handled().PreventThrottling();
				}
			}
		}
	}

	return FReply::Unhandled();
}

FReply SInteractiveCurveEditorView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	TSharedPtr<SCurveEditorPanel> EditorPanel = CurveEditor.IsValid() ? CurveEditor->GetPanel() : nullptr;
	if (!CurveEditor || !EditorPanel)
	{
		return FReply::Unhandled();
	}

	FVector2D MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	if (DragOperation.IsSet())
	{
		if (DragOperation->IsDragging())
		{
			FVector2D InitialPosition = DragOperation->GetInitialPosition();
			DragOperation->DragImpl->EndDrag(InitialPosition, MousePosition, MouseEvent);

			DragOperation.Reset();
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	DragOperation.Reset();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Curve Selection Testing.
		TOptional<FCurveModelID> HitCurve = GetHoveredCurve();
		if (!HitPoint(MousePosition).IsSet() && HitCurve.IsSet() && !MouseEvent.IsAltDown())
		{
			FCurveModel* CurveModel = CurveEditor->FindCurve(HitCurve.GetValue());

			TArray<FKeyHandle> KeyHandles;
			KeyHandles.Reserve(CurveModel->GetNumKeys());
			CurveModel->GetKeys(*CurveEditor, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), KeyHandles);

			// Add or remove all keys from the curve.
			if (MouseEvent.IsShiftDown())
			{
				CurveEditor->GetSelection().Add(HitCurve.GetValue(), ECurvePointType::Key, KeyHandles);
			}
			else if (MouseEvent.IsControlDown())
			{
				CurveEditor->GetSelection().Toggle(HitCurve.GetValue(), ECurvePointType::Key, KeyHandles);
			}
			else
			{
				CurveEditor->GetSelection().ChangeSelectionPointType(ECurvePointType::Key);
				CurveEditor->GetSelection().Clear();
				CurveEditor->GetSelection().Add(HitCurve.GetValue(), ECurvePointType::Key, KeyHandles);
			}

			return FReply::Handled();
		}
	}

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		CreateContextMenu(MyGeometry, MouseEvent);
		return FReply::Handled();
	}

	// If we hit a curve or another UI element, do not allow mouse input to bubble
	if (HitPoint(MousePosition) || GetHoveredCurve())
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SInteractiveCurveEditorView::CreateContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	TSharedPtr<SCurveEditorPanel> EditorPanel = CurveEditor.IsValid() ? CurveEditor->GetPanel() : nullptr;
	if (!CurveEditor || !EditorPanel)
	{
		return;
	}

	FVector2D MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	TOptional<FCurvePointHandle> MouseUpPoint = HitPoint(MousePosition);
	
	// We need to update our curve proximities (again) because OnMouseLeave is called (which clears them) 
	// before this menu is created due to the parent widget capturing mouse focus. The context menu needs
	// to know which curve you have highlighted for buffering curves.
	UpdateCurveProximities(MousePosition);

	// Rebind our context menu actions based on the results of hit-testing
	RebindContextualActions(MousePosition);

	// if (!MouseUpPoint.IsSet())
	// {
	// 	CurveEditor->Selection.Clear();
	// }

	const bool bCloseAfterSelection = true;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, EditorPanel->GetCommands());

	FCurveEditorContextMenu::BuildMenu(MenuBuilder, CurveEditor.ToSharedRef(), MouseUpPoint, GetHoveredCurve());

	// Push the context menu
	FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	ActiveContextMenu = FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

TOptional<FCurvePointHandle> SInteractiveCurveEditorView::HitPoint(FVector2D MousePixel) const
{
	TOptional<FCurvePointHandle> HitPoint;
	TOptional<float> ClosestDistance;

	TOptional<FCurveModelID> HoveredCurve = GetHoveredCurve();

	// Find all keys within the current hit test time
	for (const FCurveDrawParams& Params : CachedDrawParams)
	{
		// If we have a hovered curve, only hit a point within that curve
		if (HoveredCurve.IsSet() && Params.GetID() != HoveredCurve.GetValue())
		{
			continue;
		}
	
		for (int32 PointIndex = 0; PointIndex < Params.Points.Num(); PointIndex++)
		{
			const FCurvePointInfo& Point = Params.Points[PointIndex];
			const FKeyDrawInfo& PointDrawInfo = Params.GetKeyDrawInfo(Point.Type, PointIndex);

			// We artificially inflate the hit testing region for keys by a few pixels to make them easier to hit. The PointDrawInfo.ScreenSize specifies their drawn size,
			// so we need to inflate here when doing the actual hit testing. We subtract by half the extent to center it on the drawing.
			FVector2D HitTestSize = PointDrawInfo.ScreenSize + FVector2D(4.f, 4.f);

			FSlateRect KeyRect = FSlateRect::FromPointAndExtent(Point.ScreenPosition - (HitTestSize / 2.f), HitTestSize);

			if (KeyRect.ContainsPoint(MousePixel))
			{
				float DistanceSquared = (KeyRect.GetCenter() - MousePixel).SizeSquared();
				if (DistanceSquared <= ClosestDistance.Get(DistanceSquared))
				{
					ClosestDistance = DistanceSquared;
					HitPoint = FCurvePointHandle(Params.GetID(), Point.Type, Point.KeyHandle);
				}
			}
		}
	}

	return HitPoint;
}

void SInteractiveCurveEditorView::RebindContextualActions(FVector2D InMousePosition)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	TSharedPtr<SCurveEditorPanel> CurveEditorPanel = CurveEditor.IsValid() ? CurveEditor->GetPanel() : nullptr;
	if (!CurveEditorPanel)
	{
		return;
	}

	TSharedPtr<FUICommandList> CommandList = CurveEditorPanel->GetCommands();

	CommandList->UnmapAction(FCurveEditorCommands::Get().AddKeyHovered);
	CommandList->UnmapAction(FCurveEditorCommands::Get().AddKeyToAllCurves);

	CommandList->UnmapAction(FCurveEditorCommands::Get().BufferVisibleCurves);
	CommandList->UnmapAction(FCurveEditorCommands::Get().ApplyBufferedCurves);
	

	TOptional<FCurveModelID> HoveredCurve = GetHoveredCurve();
	if (HoveredCurve.IsSet())
	{
		TSet<FCurveModelID> HoveredCurveSet;
		HoveredCurveSet.Add(HoveredCurve.GetValue());

		CommandList->MapAction(FCurveEditorCommands::Get().AddKeyHovered, FExecuteAction::CreateSP(this, &SInteractiveCurveEditorView::AddKeyAtMousePosition, HoveredCurveSet));

		// Buffer the curve they have highlighted instead of all of them.
		CommandList->MapAction(FCurveEditorCommands::Get().BufferVisibleCurves, FExecuteAction::CreateSP(this, &SInteractiveCurveEditorView::BufferCurve, HoveredCurve.GetValue()));
	}
	else
	{
		// Apply the buffering action to our entire set and not just the hovered curve.
		CommandList->MapAction(FCurveEditorCommands::Get().BufferVisibleCurves, FExecuteAction::CreateSP(this, &SInteractiveCurveEditorView::BufferVisibleCurves));
	}

	CommandList->MapAction(FCurveEditorCommands::Get().AddKeyToAllCurves, FExecuteAction::CreateSP(this, &SInteractiveCurveEditorView::AddKeyAtScrubTime, TSet<FCurveModelID>()));

	// Buffer Visible Curves. Can only apply buffered curves if the current number of visible curves matches the number of buffered curves.
	CommandList->MapAction(FCurveEditorCommands::Get().ApplyBufferedCurves, FExecuteAction::CreateSP(this, &SInteractiveCurveEditorView::ApplyBufferCurves, HoveredCurve), FCanExecuteAction::CreateSP(this, &SInteractiveCurveEditorView::CanApplyBufferedCurves, HoveredCurve));
}

void SInteractiveCurveEditorView::BufferVisibleCurves()
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor.IsValid())
	{
		// Curve Editor will handle copying and storing the curves.
		TSet<FCurveModelID> ActiveCurveIDs;
		for (const TTuple<FCurveModelID, FCurveInfo>& Pair : CurveInfoByID)
		{
			ActiveCurveIDs.Add(Pair.Key);
		}
		CurveEditor->SetBufferedCurves(ActiveCurveIDs);
	}
}

void SInteractiveCurveEditorView::BufferCurve(const FCurveModelID CurveID)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor.IsValid())
	{
		// Curve Editor will handle copying and storing the curves.
		TSet<FCurveModelID> CurveSet;
		CurveSet.Add(CurveID);
		CurveEditor->SetBufferedCurves(CurveSet);
	}
}

void SInteractiveCurveEditorView::ApplyBufferCurves(TOptional<FCurveModelID> DestinationCurve)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor.IsValid())
	{
		if (DestinationCurve.IsSet())
		{
			TSet<FCurveModelID> CurveSet;
			CurveSet.Add(DestinationCurve.GetValue());

			// Apply the buffered curve (singular) to our highlighted curve.
			CurveEditor->ApplyBufferedCurves(CurveSet);
		}
		else
		{
			// Curve Editor will handle attempting to apply the buffered curves to our currently visible ones.
			TSet<FCurveModelID> ActiveCurveIDs;
			for (const TTuple<FCurveModelID, FCurveInfo>& Pair : CurveInfoByID)
			{
				ActiveCurveIDs.Add(Pair.Key);
			}
			CurveEditor->ApplyBufferedCurves(ActiveCurveIDs);
		}

	}
}

bool SInteractiveCurveEditorView::CanApplyBufferedCurves(TOptional<FCurveModelID> DestinationCurve) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor.IsValid())
	{
		if (DestinationCurve.IsSet())
		{
			return CurveEditor->GetNumBufferedCurves() == 1;
		}
		else
		{
			// For now we just do a 1:1 mapping. Once curves have better names we can try to do an intelligent match up, ie: matching Transform.X to a new Transform.X
			return CurveEditor->GetNumBufferedCurves() == NumCurves();
		}
	}

	return false;
}

void SInteractiveCurveEditorView::AddKeyAtScrubTime(TSet<FCurveModelID> ForCurves)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor.IsValid())
	{
		return;
	}

	TSet<FCurveModelID> CurvesToAddTo;
	if (ForCurves.Num() == 0)
	{
		CurvesToAddTo = CurveEditor->GetEditedCurves();
	}
	else
	{
		CurvesToAddTo = ForCurves;
	}

	// If they don't have a time slider controller then we fall back to using mouse position.
	TSharedPtr<ITimeSliderController> TimeSliderController = CurveEditor->GetTimeSliderController();
	if (!TimeSliderController)
	{
		AddKeyAtMousePosition(CurvesToAddTo);
		return;
	}

	// Snapping of the time will be done inside AddKeyAtTime.
	double ScrubTime = TimeSliderController->GetScrubPosition() / TimeSliderController->GetTickResolution();
	AddKeyAtTime(CurvesToAddTo, ScrubTime);
}

void SInteractiveCurveEditorView::AddKeyAtMousePosition(TSet<FCurveModelID> ForCurves)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor.IsValid())
	{
		return;
	}

	// Snapping will be done inside AddKeyAtTime
	double MouseTime = GetViewSpace().ScreenToSeconds(CachedMousePosition.X);
	AddKeyAtTime(ForCurves, MouseTime);
}

void SInteractiveCurveEditorView::AddKeyAtTime(const TSet<FCurveModelID>& ToCurves, double InTime)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor.IsValid())
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("AddKeyAtTime", "Add Key"));
	bool bAddedKey = false;

	FKeyAttributes DefaultAttributes = CurveEditor->GetDefaultKeyAttribute().Get();

	// Clear the selection set as we will be selecting all the new keys created.
	CurveEditor->GetSelection().Clear();

	for (const FCurveModelID CurveModelID : ToCurves)
	{
		FCurveModel* CurveModel = CurveEditor->FindCurve(CurveModelID);
		check(CurveModel);

		if (CurveModel->IsReadOnly())
		{
			continue;
		}

		// Ensure the time is snapped if needed
		FCurveSnapMetrics SnapMetrics = CurveEditor->GetCurveSnapMetrics(CurveModelID);
		double SnappedTime = SnapMetrics.SnapInputSeconds(InTime);

		// Support optional input display offsets 
		double EvalTime = SnappedTime - CurveModel->GetInputDisplayOffset();

		double CurveValue = 0.0;
		if (CurveModel->Evaluate(EvalTime, CurveValue))
		{
			CurveModel->Modify();
			CurveValue = SnapMetrics.SnapOutput(CurveValue);

			// Curve Models allow us to create new keys ontop of existing keys which works, but causes some user confusion
			// Before we create a key, we instead check to see if there is already a key at this time, and if there is, we
			// add that key to the selection set instead. This solves issues with snapping causing keys to be created adjacent
			// to the mouse cursor (sometimes by a large amount).
			TArray<FKeyHandle> ExistingKeys;
			CurveModel->GetKeys(*CurveEditor, EvalTime - KINDA_SMALL_NUMBER, EvalTime + KINDA_SMALL_NUMBER, TNumericLimits<double>::Lowest(), TNumericLimits<double>::Max(), ExistingKeys);
			
			TOptional<FKeyHandle> NewKey;

			if (ExistingKeys.Num() > 0)
			{
				NewKey = ExistingKeys[0];
			}
			else
			{
				// Add a key on this curve
				NewKey = CurveModel->AddKey(FKeyPosition(EvalTime, CurveValue), DefaultAttributes);
			}

			// Add the key to the selection set.
			if (NewKey.IsSet())
			{
				bAddedKey = true;
				CurveEditor->GetSelection().Add(FCurvePointHandle(CurveModelID, ECurvePointType::Key, NewKey.GetValue()));
			}
		}
	}

	if (!bAddedKey)
	{
		Transaction.Cancel();
	}
}

void SInteractiveCurveEditorView::OnCurveEditorToolChanged(FCurveEditorToolID InToolId)
{
	// We need to end drag-drop operations if they switch tools. Otherwise they can start
	// a marquee select, use the keyboard to switch to a diferent tool, and then the marquee
	// select finishes after the tool has had a chance to activate.
	if (DragOperation.IsSet())
	{
		// We have to cancel it instead of ending it because ending it needs mouse position and some other stuff.
		DragOperation->DragImpl->CancelDrag();
		DragOperation.Reset();
	}
}

#undef LOCTEXT_NAMESPACE // "SInteractiveCurveEditorView"
