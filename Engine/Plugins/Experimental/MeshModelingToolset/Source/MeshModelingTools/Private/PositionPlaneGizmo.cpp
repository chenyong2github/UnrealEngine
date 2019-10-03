// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PositionPlaneGizmo.h"
#include "InteractiveGizmoManager.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"  // for GEngine - @todo remove this?




UInteractiveGizmo* UPositionPlaneGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{

	UPositionPlaneGizmo* NewGizmo = NewObject<UPositionPlaneGizmo>(SceneState.GizmoManager);
	NewGizmo->SetWorld(SceneState.World);
	return NewGizmo;
}






void UPositionPlaneGizmo::SetWorld(UWorld* World)
{
	TargetWorld = World;
}


void UPositionPlaneGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	UPositionPlaneOnSceneInputBehavior* MouseBehavior = NewObject<UPositionPlaneOnSceneInputBehavior>();
	MouseBehavior->Initialize(this);
	AddInputBehavior(MouseBehavior);

	QuickTransformer.Initialize();
	bInTransformDrag = false;

	// create temporary mesh object for pivot
	CenterBallShape = MakeSphereMesh();
}


void UPositionPlaneGizmo::Shutdown()
{
	CenterBallShape->Disconnect();
}





UPreviewMesh* UPositionPlaneGizmo::MakeSphereMesh()
{
	UPreviewMesh* PreviewMesh = NewObject<UPreviewMesh>(this, TEXT("PreviewMesh"));
	PreviewMesh->CreateInWorld(this->TargetWorld, FTransform::Identity);
	PreviewMesh->SetVisible(true);
	PreviewMesh->bBuildSpatialDataStructure = true;
	PreviewMesh->bDrawOnTop = true;

	FLinearColor AxisColorX = FLinearColor(0.594f, 0.0197f, 0.0f);
	UMaterial* AxisMaterialBase = GEngine->ArrowMaterial;
	UMaterialInstanceDynamic* AxisMaterialX = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialX->SetVectorParameterValue("GizmoColor", AxisColorX);
	CenterBallMaterial = AxisMaterialX;

	//CenterBallMaterial = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/GizmoDefaultMaterial"));

	if (CenterBallMaterial != nullptr)
	{
		PreviewMesh->SetMaterial(CenterBallMaterial);
	}

	FMinimalBoxMeshGenerator BoxGen;
	BoxGen.Box = FOrientedBox3d(FVector3d::Zero(), FVector3d(10, 10, 3));
	FDynamicMesh3 Mesh(&BoxGen.Generate());

	PreviewMesh->UpdatePreview(&Mesh);

	return PreviewMesh;
}



void UPositionPlaneGizmo::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

	FViewCameraState CameraState;
	GetGizmoManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
	QuickTransformer.UpdateCameraState(CameraState);


	FTransform Transform = CenterBallShape->GetTransform();
	FMatrix Matrix = Transform.ToMatrixNoScale();

	DrawBox(PDI, Matrix, FVector(10, 10, 3),
		CenterBallMaterial->GetRenderProxy(), SDPG_Foreground);

	//DrawSphere(PDI, FVector(0, 0, 100), FRotator(), FVector(50, 50, 50), 16, 16,
	//	CenterBallMaterial->GetRenderProxy(), SDPG_Foreground);
	//	
	
	if (bInTransformDrag)
	{
		QuickTransformer.Render(RenderAPI);
	}
}



bool UPositionPlaneGizmo::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	if (CenterBallShape->TestRayIntersection(Ray))
	{
		OutHit.Distance = 0.1;
		return true;
	}
	return false;
}


void UPositionPlaneGizmo::OnBeginDrag(const FRay& Ray)
{
	bInTransformDrag = true;
	FTransform CurTransform = CenterBallShape->GetTransform();
	QuickTransformer.SetActiveFrameFromWorldNormal(CurTransform.GetTranslation(), CurTransform.GetRotation().GetAxisZ(), true);
}


void UPositionPlaneGizmo::OnUpdateDrag(const FRay& Ray)
{
	if (bInTransformDrag)
	{
		FVector3d SnapPos;
		if (QuickTransformer.UpdateSnap(Ray, SnapPos))
		{
			FTransform CurTransform = CenterBallShape->GetTransform();
			CurTransform.SetTranslation((FVector)SnapPos);
			CenterBallShape->SetTransform(CurTransform);
			PostUpdatedPosition();
		}
	}
	else
	{
		FVector RayStart = Ray.Origin;
		FVector RayEnd = Ray.PointAt(999999);
		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		FHitResult Result;
		bool bHitWorld = TargetWorld->LineTraceSingleByObjectType(Result, RayStart, RayEnd, QueryParams);
		if (bHitWorld)
		{
			FFrame3f UpdatedFrame(CenterBallShape->GetTransform());
			UpdatedFrame.AlignAxis(2, Result.ImpactNormal);
			UpdatedFrame.Origin = Result.ImpactPoint;
			CenterBallShape->SetTransform(UpdatedFrame.ToFTransform());
			PostUpdatedPosition();
		}
	}

}

void UPositionPlaneGizmo::OnEndDrag(const FRay& Ray)
{
	bInTransformDrag = false;
}



void UPositionPlaneGizmo::ExternalUpdatePosition(const FVector& Position, const FQuat& Orientation, bool bPostUpdate)
{
	CenterBallShape->SetTransform(FTransform(Orientation, Position));
	if (bPostUpdate)
	{
		PostUpdatedPosition();
	}
}



void UPositionPlaneGizmo::PostUpdatedPosition()
{
	FFrame3d CurPosition(CenterBallShape->GetTransform());
	if (OnPositionUpdatedFunc != nullptr)
	{
		OnPositionUpdatedFunc(CurPosition);
	}
}



/*
 * Mouse Input Behavior
 */


void UPositionPlaneOnSceneInputBehavior::Initialize(UPositionPlaneGizmo* GizmoIn)
{
	this->Gizmo = GizmoIn;
	bInDragCapture = false;
}


FInputCaptureRequest UPositionPlaneOnSceneInputBehavior::WantsCapture(const FInputDeviceState& input)
{
	if (IsPressed(input))
	{
		FHitResult OutHit;
		if (Gizmo->HitTest(input.Mouse.WorldRay, OutHit))
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, OutHit.Distance);
		}
	}
	return FInputCaptureRequest::Ignore();
}

FInputCaptureUpdate UPositionPlaneOnSceneInputBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	//Gizmo->SetShiftToggle(input.bShiftKeyDown);
	Gizmo->OnBeginDrag(input.Mouse.WorldRay);
	LastWorldRay = input.Mouse.WorldRay;
	bInDragCapture = true;
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}

FInputCaptureUpdate UPositionPlaneOnSceneInputBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
{
	LastWorldRay = input.Mouse.WorldRay;

	if (IsReleased(input))
	{
		Gizmo->OnEndDrag(input.Mouse.WorldRay);
		bInDragCapture = false;
		return FInputCaptureUpdate::End();
	}

	Gizmo->OnUpdateDrag(input.Mouse.WorldRay);
	return FInputCaptureUpdate::Continue();
}

void UPositionPlaneOnSceneInputBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	if (bInDragCapture)
	{
		Gizmo->OnEndDrag(LastWorldRay);
		bInDragCapture = false;
	}

	// nothing to do
}
