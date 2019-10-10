// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Gizmos/BrushStampIndicator.h"
#include "InteractiveGizmoManager.h"
#include "Drawing/ToolDataVisualizer.h"
#include "Components/PrimitiveComponent.h"

#include "PreviewMesh.h"
#include "Generators/SphereGenerator.h"
#include "ToolSetupUtil.h"


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
			Draw.DrawCircle(BrushPosition, BrushNormal, BrushRadius*0.5f, SampleStepCount, SecondaryLineColor, SecondaryLineThickness, bDepthTested);
			Draw.DrawLine(BrushPosition, BrushPosition + BrushRadius * BrushNormal, SecondaryLineColor, SecondaryLineThickness, bDepthTested);
		}

		Draw.EndFrame();
	}
}

void UBrushStampIndicator::Tick(float DeltaTime)
{
}


void UBrushStampIndicator::Update(float Radius, const FVector& Position, const FVector& Normal)
{
	BrushRadius = Radius;
	BrushPosition = Position;
	BrushNormal = Normal;

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



UPreviewMesh* UBrushStampIndicator::MakeDefaultSphereMesh(UObject* Parent, UWorld* World, int Resolution)
{
	UPreviewMesh* SphereMesh = NewObject<UPreviewMesh>(Parent);
	SphereMesh->CreateInWorld(World, FTransform::Identity);
	FSphereGenerator SphereGen;
	SphereGen.NumPhi = SphereGen.NumTheta = Resolution;
	SphereGen.Generate();
	FDynamicMesh3 Mesh(&SphereGen);
	SphereMesh->UpdatePreview(&Mesh);
	SphereMesh->SetMaterial(ToolSetupUtil::GetDefaultBrushVolumeMaterial(nullptr));
	return SphereMesh;
}