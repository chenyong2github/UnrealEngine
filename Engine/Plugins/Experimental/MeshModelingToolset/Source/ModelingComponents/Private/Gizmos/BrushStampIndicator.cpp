// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Gizmos/BrushStampIndicator.h"
#include "InteractiveGizmoManager.h"
#include "Drawing/ToolDataVisualizer.h"



UInteractiveGizmo* UBrushStampIndicatorBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UBrushStampIndicator* NewGizmo = NewObject<UBrushStampIndicator>(SceneState.GizmoManager);
	return NewGizmo;
}



void UBrushStampIndicator::Setup()
{
}

void UBrushStampIndicator::Shutdown()
{
}

void UBrushStampIndicator::Render(IToolsContextRenderAPI* RenderAPI)
{
	FToolDataVisualizer Draw;
	Draw.BeginFrame(RenderAPI);

	FVector3f Perp1, Perp2;
	VectorUtil::MakePerpVectors((FVector3f)BrushNormal, Perp1, Perp2);

	Draw.DrawCircle(BrushPosition, BrushNormal, BrushRadius, SampleStepCount, LineColor, LineThickness, bDepthTested);

	if (bDrawSecondaryLines)
	{
		Draw.DrawCircle(BrushPosition, BrushNormal, BrushRadius*0.5f, SampleStepCount, SecondaryLineColor, SecondaryLineThickness, bDepthTested);
		Draw.DrawLine(BrushPosition, BrushPosition + BrushRadius*BrushNormal, SecondaryLineColor, SecondaryLineThickness, bDepthTested);
	}

	Draw.EndFrame();
}

void UBrushStampIndicator::Tick(float DeltaTime)
{
}


void UBrushStampIndicator::Update(float Radius, const FVector& Position, const FVector& Normal)
{
	BrushRadius = Radius;
	BrushPosition = Position;
	BrushNormal = Normal;
}