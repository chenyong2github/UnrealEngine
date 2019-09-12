// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TransformMeshesTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "DynamicMesh3.h"
#include "BaseBehaviors/ClickDragBehavior.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "UTransformMeshesTool"

/*
 * ToolBuilder
 */


bool UTransformMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) >= 1;
}

UInteractiveTool* UTransformMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UTransformMeshesTool* NewTool = NewObject<UTransformMeshesTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if ( MeshComponent )
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World, SceneState.GizmoManager);

	return NewTool;
}




/*
 * Tool
 */

UTransformMeshesTool::UTransformMeshesTool()
{
}

void UTransformMeshesTool::SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManagerIn)
{
	this->TargetWorld = World;
	this->GizmoManager = GizmoManagerIn;
}


void UTransformMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	//UClickDragInputBehavior* ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
	//ClickDragBehavior->Initialize(this);
	//AddInputBehavior(ClickDragBehavior);

	TransformProps = NewObject<UTransformMeshesToolProperties>();
	AddToolPropertySource(TransformProps);

	UpdateTransformMode(TransformProps->TransformMode);
}







void UTransformMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	GizmoManager->DestroyAllGizmosByOwner(this);
}



void UTransformMeshesTool::Tick(float DeltaTime)
{
}


void UTransformMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
}

void UTransformMeshesTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	if (CurTransformMode != TransformProps->TransformMode)
	{
		UpdateTransformMode(TransformProps->TransformMode);
	}

	bool bEnableSetPivot = TransformProps->bSetPivot;
	if (TransformProps->TransformMode == ETransformMeshesTransformMode::SharedGizmoLocal ||
		TransformProps->TransformMode == ETransformMeshesTransformMode::QuickTranslate)
	{
		bEnableSetPivot = false;
	}
	UpdateSetPivotModes(bEnableSetPivot);
}


void UTransformMeshesTool::UpdateSetPivotModes(bool bEnableSetPivot)
{
	for (FTransformMeshesTarget& Target : ActiveGizmos)
	{
		Target.TransformProxy->bSetPivotMode = bEnableSetPivot;
	}
}



void UTransformMeshesTool::UpdateTransformMode(ETransformMeshesTransformMode NewMode)
{
	ResetActiveGizmos();

	switch (NewMode)
	{
		default:
		case ETransformMeshesTransformMode::SharedGizmo:
			SetActiveGizmos_Single(false);
			break;

		case ETransformMeshesTransformMode::SharedGizmoLocal:
			SetActiveGizmos_Single(true);
			break;

		case ETransformMeshesTransformMode::PerObjectGizmo:
			SetActiveGizmos_PerObject();
			break;
	}

	CurTransformMode = NewMode;
}



void UTransformMeshesTool::SetActiveGizmos_Single(bool bLocalRotations)
{
	check(ActiveGizmos.Num() == 0);

	FTransformMeshesTarget Transformable;
	Transformable.TransformProxy = NewObject<UTransformProxy>(this);
	Transformable.TransformProxy->bRotatePerObject = bLocalRotations;

	for (TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		Transformable.TransformProxy->AddComponent(Target->GetOwnerComponent());
	}
	Transformable.TransformGizmo = GizmoManager->Create3AxisTransformGizmo(FString(), this);
	Transformable.TransformGizmo->SetActiveTarget(Transformable.TransformProxy);

	ActiveGizmos.Add(Transformable);
}

void UTransformMeshesTool::SetActiveGizmos_PerObject()
{
	check(ActiveGizmos.Num() == 0);

	for (TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		FTransformMeshesTarget Transformable;
		Transformable.TransformProxy = NewObject<UTransformProxy>(this);
		Transformable.TransformProxy->AddComponent(Target->GetOwnerComponent());
		Transformable.TransformGizmo = GizmoManager->Create3AxisTransformGizmo(FString(), this);
		Transformable.TransformGizmo->SetActiveTarget(Transformable.TransformProxy);
		ActiveGizmos.Add(Transformable);
	}
}

void UTransformMeshesTool::ResetActiveGizmos()
{
	GizmoManager->DestroyAllGizmosByOwner(this);
	ActiveGizmos.Reset();
}



// does not make sense that CanBeginClickDragSequence() returns a RayHit? Needs to be an out-argument...
FInputRayHit UTransformMeshesTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	check(ActiveGizmos.Num() == 1);

	float MinHitDistance = TNumericLimits<float>::Max();
	for (TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		FHitResult WorldHit;
		if (Target->HitTest(PressPos.WorldRay, WorldHit))
		{
			MinHitDistance = FMath::Min(MinHitDistance, WorldHit.Distance);
		}
	}
	return (MinHitDistance < TNumericLimits<float>::Max()) ? FInputRayHit(MinHitDistance) : FInputRayHit();
}

void UTransformMeshesTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	check(ActiveGizmos.Num() == 1);

	FInputRayHit HitPos = CanBeginClickDragSequence(PressPos);
	check(HitPos.bHit);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("TransformToolTransformTxnName", "Transform"));

	StartDragFrameWorld = FFrame3d(PressPos.WorldRay.PointAt(HitPos.HitDepth));
	USceneComponent* GizmoComponent = ActiveGizmos[0].TransformGizmo->GetGizmoActor()->GetRootComponent();
	StartDragTransform = GizmoComponent->GetComponentToWorld();
	GizmoComponent->Modify();

	for (TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		Target->GetOwnerComponent()->Modify();
	}
}

void UTransformMeshesTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	for (TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		Target->SetOwnerVisibility(false);
	}

	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	bool bWorldHit = TargetWorld->LineTraceSingleByObjectType(Result, DragPos.WorldRay.Origin, DragPos.WorldRay.PointAt(999999), QueryParams);
	if (bWorldHit)
	{
		FVector HitPos = Result.ImpactPoint;
		FFrame3d NewFrame = StartDragFrameWorld;
		NewFrame.Origin = (FVector3d)HitPos;

		FTransform RelTransform = FTransform::Identity;
		RelTransform.SetTranslation(
			(FVector)(NewFrame.Origin - StartDragFrameWorld.Origin));

		USceneComponent* GizmoComponent = ActiveGizmos[0].TransformGizmo->GetGizmoActor()->GetRootComponent();
		FTransform NewTransform = StartDragTransform;
		NewTransform.Accumulate(RelTransform);
		GizmoComponent->SetWorldTransform(NewTransform);
	}

	for (TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		Target->SetOwnerVisibility(true);
	}
}

void UTransformMeshesTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	GetToolManager()->EndUndoTransaction();
}

void UTransformMeshesTool::OnTerminateDragSequence()
{
	GetToolManager()->EndUndoTransaction();
}





#undef LOCTEXT_NAMESPACE
