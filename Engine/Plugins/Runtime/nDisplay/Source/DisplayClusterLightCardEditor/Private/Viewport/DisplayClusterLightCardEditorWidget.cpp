// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorWidget.h"

#include "SceneManagement.h"
#include "UnrealWidget.h"
#include "EditorViewportClient.h"

void FDisplayClusterLightCardEditorWidget::Draw(const FSceneView* View, const FEditorViewportClient* ViewportClient, FPrimitiveDrawInterface* PDI)
{
	const float SizeScalar = GetSizeScreenScalar(View, ViewportClient);
	const float LengthScalar = GetLengthScreenScalar(View, ViewportClient, Transform.GetTranslation());

	DrawAxis(View, PDI, EAxisList::Type::X, SizeScalar, LengthScalar);
	DrawAxis(View, PDI, EAxisList::Type::Y, SizeScalar, LengthScalar);
	DrawOrigin(PDI, SizeScalar);
}

void FDisplayClusterLightCardEditorWidget::DrawAxis(const FSceneView* View, FPrimitiveDrawInterface* PDI, EAxisList::Type Axis, float SizeScalar, float LengthScalar)
{
	const FVector Origin = Transform.GetTranslation();

	const FVector GlobalAxis = GetGlobalAxis(Axis);
	const FVector TransformedAxis = Transform.TransformVector(GlobalAxis);
	const FLinearColor AxisColor = HighlightedAxis == Axis ? HighlightColor : GetAxisColor(Axis);

	PDI->SetHitProxy(new HWidgetAxis(Axis));
	{
		const FVector AxisStart = Origin;
		const FVector AxisEnd = Origin + TransformedAxis * AxisLength * WidgetScale * LengthScalar;

		PDI->DrawLine(AxisStart, AxisEnd, AxisColor, ESceneDepthPriorityGroup::SDPG_Foreground, AxisThickness * SizeScalar, 0.0, true);

		if (WidgetMode == EWidgetMode::WM_Scale)
		{
			PDI->DrawPoint(AxisEnd, AxisColor, AxisCapSize * SizeScalar, ESceneDepthPriorityGroup::SDPG_Foreground);
		}
	}
	PDI->SetHitProxy(nullptr);
}

void FDisplayClusterLightCardEditorWidget::DrawOrigin(FPrimitiveDrawInterface* PDI, float SizeScalar)
{
	const FVector Origin = Transform.GetTranslation();
	const FLinearColor Color = HighlightedAxis == EAxisList::Type::XY ? HighlightColor : FLinearColor::Black;

	PDI->SetHitProxy(new HWidgetAxis(EAxisList::Type::XY));
	PDI->DrawPoint(Origin, Color, OriginSize * SizeScalar, ESceneDepthPriorityGroup::SDPG_Foreground);
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

float FDisplayClusterLightCardEditorWidget::GetLengthScreenScalar(const FSceneView* View, const FEditorViewportClient* ViewportClient, const FVector& Origin) const
{
	// The ideal behavior for the length of the widget axes is to remain a fixed length regardless of the field of view or distance from the camera,
	// but change proportionally with the size of the viewport, so that the widget takes up the same percentage of viewport space. 
	// To accomplish this, three factors go into the size scalar: 
	// * DPIScalar ensures the length is the same, relative to viewport size, on high density monitors
	// * ResolutionScale ensures the length remains the same, relative to viewport size, regardless of the size of the viewport.
	// * ProjectionScale ensures that the length remains the same regardless of field of view or distance from the camera. For lengths specifically,
	//   the distance from the camera needs to be taken into account as well. The ViewMatrices ScreenScale contains the appropraite FOV and viewport size factors.
	// Note we use the x axis sizes to compute the scalars as the viewport will only scale smaller when the width is resized; when the 
	// height is resized, the view is clamped

	const float DPIScale = ViewportClient->GetDPIScale();
	const float ResolutionScale = View->UnconstrainedViewRect.Size().X / (DPIScale * ReferenceResolution);

	const float DistanceFromView = FMath::Max(FVector::Dist(Origin, View->ViewMatrices.GetViewOrigin()), 1.f);
	const float ProjectionScale = DistanceFromView / View->ViewMatrices.GetScreenScale();

	const float FinalScalar = DPIScale * ResolutionScale * ProjectionScale;
	return FinalScalar;
}

float FDisplayClusterLightCardEditorWidget::GetSizeScreenScalar(const FSceneView* View, const FEditorViewportClient* ViewportClient) const
{
	// The ideal behavior for the size of the widget is to remain a fixed size regardless of the field of view or distance from the camera,
	// but change proportionally with the size of the viewport, so that the widget takes up the same percentage of viewport space. 
	// To accomplish this, three factors go into the size scalar: 
	// * DPIScalar ensures the widget is the same size, relative to viewport size, on high density monitors
	// * ResolutionScale ensures the widget remains the same size, relative to viewport size, regardless of the size of the viewport.
	// * ProjectionScale ensures that the widget remains the same size regardless of field of view or distance from the camera
	// Note we use the x axis sizes to compute the scalars as the viewport will only scale smaller when the width is resized; when the 
	// height is resized, the view is clamped

	const float DPIScale = ViewportClient->GetDPIScale();
	const float ResolutionScale = View->UnconstrainedViewRect.Size().X / (DPIScale * ReferenceResolution);

	const FMatrix& ProjectionMatrix = View->ViewMatrices.GetProjectionMatrix();
	const float ProjectionScale = FMath::Abs(ProjectionMatrix.M[0][0]);

	const float FinalScalar = DPIScale * ResolutionScale / ProjectionScale;
	return FinalScalar;
}