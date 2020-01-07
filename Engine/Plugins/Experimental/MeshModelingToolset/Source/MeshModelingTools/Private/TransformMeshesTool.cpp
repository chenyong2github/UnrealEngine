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

	UClickDragInputBehavior* ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
	ClickDragBehavior->Initialize(this);
	AddInputBehavior(ClickDragBehavior);

	TransformProps = NewObject<UTransformMeshesToolProperties>();
	AddToolPropertySource(TransformProps);

	UpdateTransformMode(TransformProps->TransformMode);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTransformMeshesTool", "To transform Objects around points, reposition the Gizmo using Set Pivot mode (S). To quickly position Objects, enable Snap Drag mode (D). A cycles through Transform modes, W and E through SnapDrag Source and Rotation types."),
		EToolMessageLevel::UserNotification);
}







void UTransformMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	GizmoManager->DestroyAllGizmosByOwner(this);
}



void UTransformMeshesTool::Tick(float DeltaTime)
{
	// make sure state is up-to-date

	if (CurTransformMode != TransformProps->TransformMode)
	{
		UpdateTransformMode(TransformProps->TransformMode);
	}

	if (bCurSetPivotMode != TransformProps->bSetPivot)
	{
		bool bEnableSetPivot = TransformProps->bSetPivot;
		if (TransformProps->TransformMode == ETransformMeshesTransformMode::SharedGizmoLocal)
		{
			bEnableSetPivot = false;
		}
		UpdateSetPivotModes(bEnableSetPivot);
		bCurSetPivotMode = TransformProps->bSetPivot;
	}
}


void UTransformMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
}

void UTransformMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
}


void UTransformMeshesTool::UpdateSetPivotModes(bool bEnableSetPivot)
{
	for (FTransformMeshesTarget& Target : ActiveGizmos)
	{
		Target.TransformProxy->bSetPivotMode = bEnableSetPivot;
	}
}



void UTransformMeshesTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 1,
		TEXT("ToggleSetPivot"),
		LOCTEXT("TransformToggleSetPivot", "Toggle Set Pivot"),
		LOCTEXT("TransformToggleSetPivotTooltip", "Toggle Set Pivot on and off"),
		EModifierKey::None, EKeys::S,
		[this]() { this->TransformProps->bSetPivot = !this->TransformProps->bSetPivot; });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
		TEXT("ToggleSnapDrag"),
		LOCTEXT("TransformToggleSnapDrag", "Toggle SnapDrag"),
		LOCTEXT("TransformToggleSnapDragTooltip", "Toggle SnapDrag on and off"),
		EModifierKey::None, EKeys::D,
		[this]() { this->TransformProps->bEnableSnapDragging = !this->TransformProps->bEnableSnapDragging; });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 3,
		TEXT("CycleTransformMode"),
		LOCTEXT("TransformCycleTransformMode", "Next Transform Mode"),
		LOCTEXT("TransformCycleTransformModeTooltip", "Cycle through available Transform Modes"),
		EModifierKey::None, EKeys::A,
		[this]() { this->TransformProps->TransformMode = (ETransformMeshesTransformMode)(((uint8)TransformProps->TransformMode+1) % (uint8)ETransformMeshesTransformMode::LastValue); });


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 4,
		TEXT("CycleSourceMode"),
		LOCTEXT("TransformCycleSourceMode", "Next SnapDrag Source Mode"),
		LOCTEXT("TransformCycleSourceModeTooltip", "Cycle through available SnapDrag Source Modes"),
		EModifierKey::None, EKeys::W,
		[this]() { this->TransformProps->SnapDragSource = (ETransformMeshesSnapDragSource)(((uint8)TransformProps->SnapDragSource + 1) % (uint8)ETransformMeshesSnapDragSource::LastValue); });

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 5,
		TEXT("CycleRotationMode"),
		LOCTEXT("TransformCycleRotationMode", "Next SnapDrag Rotation Mode"),
		LOCTEXT("TransformCycleRotationModeTooltip", "Cycle through available SnapDrag Rotation Modes"),
		EModifierKey::None, EKeys::E,
		[this]() { this->TransformProps->RotationMode = (ETransformMeshesSnapDragRotationMode)(((uint8)TransformProps->RotationMode + 1) % (uint8)ETransformMeshesSnapDragRotationMode::LastValue); });


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
	Transformable.TransformGizmo = GizmoManager->Create3AxisTransformGizmo(this);
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
		Transformable.TransformGizmo = GizmoManager->Create3AxisTransformGizmo(this);
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
	if (TransformProps->bEnableSnapDragging == false || ActiveGizmos.Num() == 0)
	{
		return FInputRayHit();
	}

	ActiveSnapDragIndex = -1;

	float MinHitDistance = TNumericLimits<float>::Max();
	FVector HitNormal;

	for ( int k = 0; k < ComponentTargets.Num(); ++k )
	{
		TUniquePtr<FPrimitiveComponentTarget>& Target = ComponentTargets[k];

		FHitResult WorldHit;
		if (Target->HitTest(PressPos.WorldRay, WorldHit))
		{
			MinHitDistance = FMath::Min(MinHitDistance, WorldHit.Distance);
			HitNormal = WorldHit.Normal;
			ActiveSnapDragIndex = k;
		}
	}
	return (MinHitDistance < TNumericLimits<float>::Max()) ? FInputRayHit(MinHitDistance, HitNormal) : FInputRayHit();
}

void UTransformMeshesTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	FInputRayHit HitPos = CanBeginClickDragSequence(PressPos);
	check(HitPos.bHit);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("TransformToolTransformTxnName", "SnapDrag"));

	FTransformMeshesTarget& ActiveTarget =
		(TransformProps->TransformMode == ETransformMeshesTransformMode::PerObjectGizmo) ?
		ActiveGizmos[ActiveSnapDragIndex] : ActiveGizmos[0];
	USceneComponent* GizmoComponent = ActiveTarget.TransformGizmo->GetGizmoActor()->GetRootComponent();
	StartDragTransform = GizmoComponent->GetComponentToWorld();

	if (TransformProps->SnapDragSource == ETransformMeshesSnapDragSource::ClickPoint)
	{
		StartDragFrameWorld = FFrame3d(PressPos.WorldRay.PointAt(HitPos.HitDepth), HitPos.HitNormal);
	}
	else
	{
		StartDragFrameWorld = FFrame3d(StartDragTransform);
	}

}


void UTransformMeshesTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams CollisionParams = FCollisionQueryParams::DefaultQueryParam;

	bool bApplyToPivot = TransformProps->bSetPivot;

	if (bApplyToPivot == false)
	{
		int IgnoreIndex = (TransformProps->TransformMode == ETransformMeshesTransformMode::PerObjectGizmo) ?
			ActiveSnapDragIndex : -1;
		for (int k = 0; k < ComponentTargets.Num(); ++k)
		{
			if (IgnoreIndex == -1 || k == IgnoreIndex)
			{
				CollisionParams.AddIgnoredComponent(ComponentTargets[k]->GetOwnerComponent());
			}
		}
	}


	bool bRotate = (TransformProps->RotationMode != ETransformMeshesSnapDragRotationMode::Ignore);
	float NormalSign = (TransformProps->RotationMode == ETransformMeshesSnapDragRotationMode::AlignFlipped) ? -1.0f : 1.0f;

	FHitResult Result;
	bool bWorldHit = TargetWorld->LineTraceSingleByObjectType(Result, DragPos.WorldRay.Origin, DragPos.WorldRay.PointAt(999999), ObjectQueryParams, CollisionParams);
	if (bWorldHit == false)
	{
		return;
	}

	if (bApplyToPivot)
	{
		FVector HitPos = Result.ImpactPoint;
		FVector TargetNormal = (-NormalSign) * Result.Normal;

		FQuaterniond AlignRotation = (bRotate) ?
			FQuaterniond(FVector3d::UnitZ(), TargetNormal) : FQuaterniond::Identity();

		FTransform NewTransform = StartDragTransform;
		NewTransform.SetRotation((FQuat)AlignRotation);
		NewTransform.SetTranslation(HitPos);

		FTransformMeshesTarget& ActiveTarget =
			(TransformProps->TransformMode == ETransformMeshesTransformMode::PerObjectGizmo) ?
			ActiveGizmos[ActiveSnapDragIndex] : ActiveGizmos[0];
		USceneComponent* GizmoComponent = ActiveTarget.TransformGizmo->GetGizmoActor()->GetRootComponent();
		GizmoComponent->SetWorldTransform(NewTransform);
	}
	else
	{
		FVector HitPos = Result.ImpactPoint;
		FVector TargetNormal = NormalSign * Result.Normal;

		FFrame3d FromFrameWorld = StartDragFrameWorld;
		FFrame3d ToFrameWorld(HitPos, TargetNormal);
		FFrame3d ObjectFrameWorld(StartDragTransform);

		FVector3d CenterShift = FromFrameWorld.Origin - ObjectFrameWorld.Origin;
		FQuaterniond AlignRotation(FromFrameWorld.Z(), ToFrameWorld.Z());
		if (bRotate == false)
		{
			AlignRotation = FQuaterniond::Identity();
		}
		FVector3d AlignTranslate = ToFrameWorld.Origin - FromFrameWorld.Origin;

		FTransform NewTransform = StartDragTransform;
		NewTransform.Accumulate( FTransform((FVector)CenterShift) );
		NewTransform.Accumulate( FTransform((FQuat)AlignRotation) );
		NewTransform.Accumulate( FTransform((FVector)AlignTranslate) );
		CenterShift = AlignRotation * CenterShift;
		NewTransform.Accumulate( FTransform((FVector)-CenterShift) );

		FTransformMeshesTarget& ActiveTarget =
			(TransformProps->TransformMode == ETransformMeshesTransformMode::PerObjectGizmo) ?
			ActiveGizmos[ActiveSnapDragIndex] : ActiveGizmos[0];
		USceneComponent* GizmoComponent = ActiveTarget.TransformGizmo->GetGizmoActor()->GetRootComponent();
		GizmoComponent->SetWorldTransform(NewTransform);
	}

}


void UTransformMeshesTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	OnTerminateDragSequence();
}

void UTransformMeshesTool::OnTerminateDragSequence()
{
	FTransformMeshesTarget& ActiveTarget =
		(TransformProps->TransformMode == ETransformMeshesTransformMode::PerObjectGizmo) ?
		ActiveGizmos[ActiveSnapDragIndex] : ActiveGizmos[0];
	USceneComponent* GizmoComponent = ActiveTarget.TransformGizmo->GetGizmoActor()->GetRootComponent();
	FTransform EndDragtransform = GizmoComponent->GetComponentToWorld();

	TUniquePtr<FComponentWorldTransformChange> Change = MakeUnique<FComponentWorldTransformChange>(StartDragTransform, EndDragtransform);
	GetToolManager()->EmitObjectChange(GizmoComponent, MoveTemp(Change),
		LOCTEXT("TransformToolTransformTxnName", "SnapDrag"));

	GetToolManager()->EndUndoTransaction();

	ActiveSnapDragIndex = -1;
}





#undef LOCTEXT_NAMESPACE
