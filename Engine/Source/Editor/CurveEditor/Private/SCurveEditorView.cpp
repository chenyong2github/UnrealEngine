// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SCurveEditorView.h"
#include "ICurveEditorBounds.h"
#include "CurveEditor.h"
#include "ICurveEditorModule.h"
#include "CurveEditorScreenSpace.h"
#include "CurveModel.h"
#include "CurveEditorSnapMetrics.h"
#include "SCurveEditorPanel.h"

SCurveEditorView::SCurveEditorView()
	: bPinned(0)
	, bInteractive(1)
	, bFixedOutputBounds(0)
	, bAutoSize(1)
	, bAllowEmpty(0)
{}

FVector2D SCurveEditorView::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	FVector2D ContentDesiredSize = SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
	return FVector2D(ContentDesiredSize.X, FixedHeight.Get(ContentDesiredSize.Y));
}

void SCurveEditorView::GetInputBounds(double& OutInputMin, double& OutInputMax) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		CurveEditor->GetBounds().GetInputBounds(OutInputMin, OutInputMax);

		// This code assumes no scaling between the container and the view (which is a pretty safe assumption to make)
		const FGeometry& ViewGeometry      = GetCachedGeometry();
		const FGeometry& ContainerGeometry = CurveEditor->GetPanel()->GetViewContainerGeometry();

		const float ContainerWidth = ContainerGeometry.GetLocalSize().X;
		const float ViewWidth      = ViewGeometry.GetLocalSize().X;

		if (ViewWidth > 0.f)
		{
			const float LeftPixelCrop = ViewGeometry.LocalToAbsolute(FVector2D(0.f, 0.f)).X - ContainerGeometry.LocalToAbsolute(FVector2D(0.f, 0.f)).X;
			const float RightPixelCrop = ContainerGeometry.LocalToAbsolute(FVector2D(ContainerWidth, 0.f)).X - ViewGeometry.LocalToAbsolute(FVector2D(ViewWidth, 0.f)).X;

			const double ContainerInputPerPixel = (OutInputMax - OutInputMin) / ContainerWidth;

			// Offset by the total range first
			OutInputMin += ContainerInputPerPixel * LeftPixelCrop;
			OutInputMax -= ContainerInputPerPixel * RightPixelCrop;
		}
	}
}

FCurveEditorScreenSpace SCurveEditorView::GetViewSpace() const
{
	double InputMin = 0.0, InputMax = 1.0;
	GetInputBounds(InputMin, InputMax);

	return FCurveEditorScreenSpace(GetCachedGeometry().GetLocalSize(), InputMin, InputMax, OutputMin, OutputMax);
}

void SCurveEditorView::AddCurve(FCurveModelID CurveID)
{
	CurveInfoByID.Add(CurveID, FCurveInfo{CurveInfoByID.Num()});
	OnCurveListChanged();
}

void SCurveEditorView::RemoveCurve(FCurveModelID CurveID)
{
	if (FCurveInfo* InfoToRemove = CurveInfoByID.Find(CurveID))
	{
		const int32 CurveIndex = InfoToRemove->CurveIndex;
		InfoToRemove = nullptr;

		CurveInfoByID.Remove(CurveID);

		for (TTuple<FCurveModelID, FCurveInfo>& Info : CurveInfoByID)
		{
			if (Info.Value.CurveIndex > CurveIndex)
			{
				--Info.Value.CurveIndex;
			}
		}

		OnCurveListChanged();
	}
}

void SCurveEditorView::SetOutputBounds(double InOutputMin, double InOutputMax)
{
	if (!bFixedOutputBounds)
	{
		OutputMin = InOutputMin;
		OutputMax = InOutputMax;
	}
}

void SCurveEditorView::Zoom(const FVector2D& Amount)
{
	FCurveEditorScreenSpace ViewSpace = GetViewSpace();

	const double InputOrigin  = (ViewSpace.GetInputMax()  - ViewSpace.GetInputMin())  * 0.5;
	const double OutputOrigin = (ViewSpace.GetOutputMax() - ViewSpace.GetOutputMin()) * 0.5;

	ZoomAround(Amount, InputOrigin, OutputOrigin);
}

void SCurveEditorView::ZoomAround(const FVector2D& Amount, double InputOrigin, double OutputOrigin)
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	check(CurveEditor.IsValid());

	if (Amount.X != 0.f && CurveEditor.IsValid())
	{
		double InputMin = 0.0, InputMax = 1.0;
		CurveEditor->GetBounds().GetInputBounds(InputMin, InputMax);

		InputMin = InputOrigin - (InputOrigin - InputMin) * Amount.X;
		InputMax = InputOrigin + (InputMax - InputOrigin) * Amount.X;

		CurveEditor->GetBounds().SetInputBounds(InputMin, InputMax);
	}

	if (Amount.Y != 0.f)
	{
		OutputMin = OutputOrigin - (OutputOrigin - OutputMin) * Amount.Y;
		OutputMax = OutputOrigin + (OutputMax - OutputOrigin) * Amount.Y;
	}
}
