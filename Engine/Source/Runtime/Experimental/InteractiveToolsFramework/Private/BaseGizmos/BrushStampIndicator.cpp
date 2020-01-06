// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/BrushStampIndicator.h"
#include "InteractiveGizmoManager.h"
#include "Components/PrimitiveComponent.h"
#include "ToolDataVisualizer.h"


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
	if (bDrawIndicatorLines)
	{
		FToolDataVisualizer Draw;
		Draw.BeginFrame(RenderAPI);

		Draw.DrawCircle(BrushPosition, BrushNormal, BrushRadius, SampleStepCount, LineColor, LineThickness, bDepthTested);

		if (bDrawSecondaryLines)
		{
			Draw.DrawCircle(BrushPosition, BrushNormal, BrushRadius*BrushFalloff, SampleStepCount, SecondaryLineColor, SecondaryLineThickness, bDepthTested);
			Draw.DrawLine(BrushPosition, BrushPosition + BrushRadius * BrushNormal, SecondaryLineColor, SecondaryLineThickness, bDepthTested);
		}

		Draw.EndFrame();
	}
}

void UBrushStampIndicator::Tick(float DeltaTime)
{
}


void UBrushStampIndicator::Update(float Radius, const FVector& Position, const FVector& Normal, float Falloff)
{
	BrushRadius = Radius;
	BrushPosition = Position;
	BrushNormal = Normal;
	BrushFalloff = Falloff;

	if (AttachedComponent != nullptr)
	{
		FTransform Transform = AttachedComponent->GetComponentTransform();

		if (ScaleInitializedComponent != AttachedComponent)
		{
			InitialComponentScale = Transform.GetScale3D();
			InitialComponentScale *= 1.0f / InitialComponentScale.Z;
			ScaleInitializedComponent = AttachedComponent;
		}

		Transform.SetTranslation(BrushPosition);

		FQuat CurRotation = Transform.GetRotation();
		FQuat ApplyRotation = FQuat::FindBetween(CurRotation.GetAxisZ(), BrushNormal);
		Transform.SetRotation(ApplyRotation * CurRotation);

		Transform.SetScale3D(Radius * InitialComponentScale);

		AttachedComponent->SetWorldTransform(Transform);
	}
}
