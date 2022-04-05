// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorWidget.h"

#include "SceneManagement.h"
#include "UnrealWidget.h"

void FDisplayClusterLightCardEditorWidget::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	DrawAxis(View, PDI, EAxisList::Type::X);
	DrawAxis(View, PDI, EAxisList::Type::Y);
	DrawOrigin(View, PDI);
}

void FDisplayClusterLightCardEditorWidget::DrawAxis(const FSceneView* View, FPrimitiveDrawInterface* PDI, EAxisList::Type Axis)
{
	const FVector Origin = Transform.GetTranslation();

	const FVector GlobalAxis = GetGlobalAxis(Axis);
	const FVector TransformedAxis = Transform.TransformVector(GlobalAxis);
	const FLinearColor AxisColor = HighlightedAxis == Axis ? HighlightColor : GetAxisColor(Axis);

	// Compute the scalar needed to make the axis length a fixed size on the screen regardless of distance and field of view
	const float DistanceScreenSpaceScalar = WidgetScale * FMath::Max(FVector::Dist(Origin, View->ViewMatrices.GetViewOrigin()), 1.f) / View->ViewMatrices.GetScreenScale();
	const float ThicknessScreenSpaceScalar = 100.f / View->ViewMatrices.GetScreenScale();


	PDI->SetHitProxy(new HWidgetAxis(Axis));
	PDI->DrawLine(Origin, Origin + TransformedAxis * AxisLength * DistanceScreenSpaceScalar, AxisColor, ESceneDepthPriorityGroup::SDPG_Foreground, AxisThickness * ThicknessScreenSpaceScalar, 0.0, true);
	PDI->SetHitProxy(nullptr);
}

void FDisplayClusterLightCardEditorWidget::DrawOrigin(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const FVector Origin = Transform.GetTranslation();
	const FLinearColor Color = HighlightedAxis == EAxisList::Type::XY ? HighlightColor : FLinearColor::Black;

	// Compute the scalar needed to make the axis length a fixed size on the screen regardless of distance and field of view
	const float ScreenSpaceScalar = 100.f / View->ViewMatrices.GetScreenScale();

	PDI->SetHitProxy(new HWidgetAxis(EAxisList::Type::XY));
	PDI->DrawPoint(Origin, Color, OriginSize * ScreenSpaceScalar, ESceneDepthPriorityGroup::SDPG_Foreground);
	PDI->SetHitProxy(nullptr);
}

FVector FDisplayClusterLightCardEditorWidget::GetGlobalAxis(EAxisList::Type Axis) const
{
	switch (Axis)
	{
	case EAxisList::Type::X:
		return FVector::UnitX();

	case EAxisList::Type::Y:
		return FVector::UnitY();
		
	case EAxisList::Type::Z:
		return FVector::UnitZ();
	}

	return FVector::ZeroVector;
}

FLinearColor FDisplayClusterLightCardEditorWidget::GetAxisColor(EAxisList::Type Axis) const
{
	switch (Axis)
	{
	case EAxisList::Type::X:
		return AxisColorX;

	case EAxisList::Type::Y:
		return AxisColorY;
		
	case EAxisList::Type::Z:
		return AxisColorZ;
	}

	return FLinearColor::Transparent;
}