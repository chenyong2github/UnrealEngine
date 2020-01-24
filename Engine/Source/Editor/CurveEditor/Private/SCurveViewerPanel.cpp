// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCurveViewerPanel.h"
#include "Rendering/DrawElements.h"
#include "CurveDrawInfo.h"
#include "EditorStyleSet.h"
#include "Misc/Attribute.h"
#include "CurveEditorScreenSpace.h"
#include "Views/SInteractiveCurveEditorView.h"

#define LOCTEXT_NAMESPACE "SCurveViewerPanel"

namespace CurveViewerConstants
{
	static bool  bAntiAliasCurves = true;
}

void SCurveViewerPanel::Construct(const FArguments& InArgs, TSharedRef<FCurveEditor> InCurveEditor)
{
	WeakCurveEditor = InCurveEditor;
	CurveThickness = InArgs._CurveThickness;

	InCurveEditor->SetView(SharedThis(this));

	for (const TPair<FCurveModelID, TUniquePtr<FCurveModel>>& CurvePair : InCurveEditor->GetCurves())
	{
		CurveInfoByID.Add(CurvePair.Key);
	}

	SetClipping(EWidgetClipping::ClipToBounds);
}

void SCurveViewerPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	CachedDrawParams.Reset();
	GetCurveDrawParams(CachedDrawParams);
}

int32 SCurveViewerPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	DrawCurves(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle, DrawEffects);

	return LayerId + CurveViewConstants::ELayerOffset::Last;
}

void SCurveViewerPanel::DrawCurves(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, ESlateDrawEffect DrawEffects) const
{
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	for (const FCurveDrawParams& Params : CachedDrawParams)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			BaseLayerId + CurveViewConstants::ELayerOffset::Curves,
			PaintGeometry,
			Params.InterpolatingPoints,
			DrawEffects,
			Params.Color,
			CurveViewerConstants::bAntiAliasCurves,
			CurveThickness.Get()
		);

		if (Params.bKeyDrawEnabled)
		{
			for (int32 PointIndex = 0; PointIndex < Params.Points.Num(); PointIndex++)
			{
				const FCurvePointInfo& Point = Params.Points[PointIndex];
				const FKeyDrawInfo& PointDrawInfo = Params.GetKeyDrawInfo(Point.Type, PointIndex);

				const int32 KeyLayerId = BaseLayerId + Point.LayerBias + CurveViewConstants::ELayerOffset::Keys;

				FPaintGeometry PointGeometry = AllottedGeometry.ToPaintGeometry(
					Point.ScreenPosition - (PointDrawInfo.ScreenSize * 0.5f),
					PointDrawInfo.ScreenSize
				);

				FSlateDrawElement::MakeBox(OutDrawElements, KeyLayerId, PointGeometry, PointDrawInfo.Brush, DrawEffects, PointDrawInfo.Tint);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE