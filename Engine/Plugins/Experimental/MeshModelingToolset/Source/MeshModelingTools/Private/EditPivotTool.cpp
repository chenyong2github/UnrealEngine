// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditPivotTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshTransforms.h"
#include "BaseBehaviors/ClickDragBehavior.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "UEditPivotTool"

/*
 * ToolBuilder
 */


bool UEditPivotToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) >= 1;
}

UInteractiveTool* UEditPivotToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UEditPivotTool* NewTool = NewObject<UEditPivotTool>(SceneState.ToolManager);

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

UEditPivotTool::UEditPivotTool()
{
}

void UEditPivotTool::SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManagerIn)
{
	this->TargetWorld = World;
	this->GizmoManager = GizmoManagerIn;
}


void UEditPivotTool::Setup()
{
	UInteractiveTool::Setup();

	UClickDragInputBehavior* ClickDragBehavior = NewObject<UClickDragInputBehavior>(this);
	ClickDragBehavior->Initialize(this);
	AddInputBehavior(ClickDragBehavior);

	TransformProps = NewObject<UEditPivotToolProperties>();
	AddToolPropertySource(TransformProps);

	ResetActiveGizmos();
	SetActiveGizmos_Single(false);
	UpdateSetPivotModes(true);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartEditPivotTool", "To transform Objects around points, reposition the Gizmo using Set Pivot mode (S). To quickly position Objects, enable Snap Drag mode (D). A cycles through Transform modes, W and E through SnapDrag Source and Rotation types."),
		EToolMessageLevel::UserNotification);

	GetToolManager()->DisplayMessage(
		LOCTEXT("EditPivotWarning", "WARNING: This Tool will Modify the selected StaticMesh Assets! If you do not wish to modify the original Assets, please make copies in the Content Browser first!"),
		EToolMessageLevel::UserWarning);
}







void UEditPivotTool::Shutdown(EToolShutdownType ShutdownType)
{
	FFrame3d CurPivotFrame(ActiveGizmos[0].TransformProxy->GetTransform());

	GizmoManager->DestroyAllGizmosByOwner(this);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		UpdateAssets(CurPivotFrame);
	}
}



void UEditPivotTool::Tick(float DeltaTime)
{
}

void UEditPivotTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UEditPivotTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
}


void UEditPivotTool::UpdateSetPivotModes(bool bEnableSetPivot)
{
	for (FEditPivotTarget& Target : ActiveGizmos)
	{
		Target.TransformProxy->bSetPivotMode = bEnableSetPivot;
	}
}



void UEditPivotTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
	//	TEXT("ToggleSnapDrag"),
	//	LOCTEXT("TransformToggleSnapDrag", "Toggle SnapDrag"),
	//	LOCTEXT("TransformToggleSnapDragTooltip", "Toggle SnapDrag on and off"),
	//	EModifierKey::None, EKeys::D,
	//	[this]() { this->TransformProps->bEnableSnapDragging = !this->TransformProps->bEnableSnapDragging; });

	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 3,
	//	TEXT("CycleTransformMode"),
	//	LOCTEXT("TransformCycleTransformMode", "Next Transform Mode"),
	//	LOCTEXT("TransformCycleTransformModeTooltip", "Cycle through available Transform Modes"),
	//	EModifierKey::None, EKeys::A,
	//	[this]() { this->TransformProps->TransformMode = (EEditPivotTransformMode)(((uint8)TransformProps->TransformMode+1) % (uint8)EEditPivotTransformMode::LastValue); });


	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 4,
	//	TEXT("CycleSourceMode"),
	//	LOCTEXT("TransformCycleSourceMode", "Next SnapDrag Source Mode"),
	//	LOCTEXT("TransformCycleSourceModeTooltip", "Cycle through available SnapDrag Source Modes"),
	//	EModifierKey::None, EKeys::W,
	//	[this]() { this->TransformProps->SnapDragSource = (EEditPivotSnapDragSource)(((uint8)TransformProps->SnapDragSource + 1) % (uint8)EEditPivotSnapDragSource::LastValue); });

	//ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 5,
	//	TEXT("CycleRotationMode"),
	//	LOCTEXT("TransformCycleRotationMode", "Next SnapDrag Rotation Mode"),
	//	LOCTEXT("TransformCycleRotationModeTooltip", "Cycle through available SnapDrag Rotation Modes"),
	//	EModifierKey::None, EKeys::E,
	//	[this]() { this->TransformProps->RotationMode = (EEditPivotSnapDragRotationMode)(((uint8)TransformProps->RotationMode + 1) % (uint8)EEditPivotSnapDragRotationMode::LastValue); });


}




void UEditPivotTool::SetActiveGizmos_Single(bool bLocalRotations)
{
	check(ActiveGizmos.Num() == 0);

	FEditPivotTarget Transformable;
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


void UEditPivotTool::ResetActiveGizmos()
{
	GizmoManager->DestroyAllGizmosByOwner(this);
	ActiveGizmos.Reset();
}



// does not make sense that CanBeginClickDragSequence() returns a RayHit? Needs to be an out-argument...
FInputRayHit UEditPivotTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
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

void UEditPivotTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	FInputRayHit HitPos = CanBeginClickDragSequence(PressPos);
	check(HitPos.bHit);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("TransformToolTransformTxnName", "SnapDrag"));

	FEditPivotTarget& ActiveTarget = ActiveGizmos[0];
	USceneComponent* GizmoComponent = ActiveTarget.TransformGizmo->GetGizmoActor()->GetRootComponent();
	StartDragTransform = GizmoComponent->GetComponentToWorld();

	if (TransformProps->SnapDragSource == EEditPivotSnapDragSource::ClickPoint)
	{
		StartDragFrameWorld = FFrame3d(PressPos.WorldRay.PointAt(HitPos.HitDepth), HitPos.HitNormal);
	}
	else
	{
		StartDragFrameWorld = FFrame3d(StartDragTransform);
	}

}


void UEditPivotTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams CollisionParams = FCollisionQueryParams::DefaultQueryParam;

	//bool bApplyToPivot = true;
	//if (bApplyToPivot == false)
	//{
	//	int IgnoreIndex = -1;
	//	for (int k = 0; k < ComponentTargets.Num(); ++k)
	//	{
	//		if (IgnoreIndex == -1 || k == IgnoreIndex)
	//		{
	//			CollisionParams.AddIgnoredComponent(ComponentTargets[k]->GetOwnerComponent());
	//		}
	//	}
	//}


	bool bRotate = (TransformProps->RotationMode != EEditPivotSnapDragRotationMode::Ignore);
	float NormalSign = (TransformProps->RotationMode == EEditPivotSnapDragRotationMode::AlignFlipped) ? -1.0f : 1.0f;

	FHitResult Result;
	bool bWorldHit = TargetWorld->LineTraceSingleByObjectType(Result, DragPos.WorldRay.Origin, DragPos.WorldRay.PointAt(999999), ObjectQueryParams, CollisionParams);
	if (bWorldHit == false)
	{
		return;
	}


	FVector HitPos = Result.ImpactPoint;
	FVector TargetNormal = (-NormalSign) * Result.Normal;

	FQuaterniond AlignRotation = (bRotate) ?
		FQuaterniond(FVector3d::UnitZ(), TargetNormal) : FQuaterniond::Identity();

	FTransform NewTransform = StartDragTransform;
	NewTransform.SetRotation((FQuat)AlignRotation);
	NewTransform.SetTranslation(HitPos);

	FEditPivotTarget& ActiveTarget = ActiveGizmos[0];
	USceneComponent* GizmoComponent = ActiveTarget.TransformGizmo->GetGizmoActor()->GetRootComponent();
	GizmoComponent->SetWorldTransform(NewTransform);


}


void UEditPivotTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	OnTerminateDragSequence();
}

void UEditPivotTool::OnTerminateDragSequence()
{
	FEditPivotTarget& ActiveTarget = ActiveGizmos[0];
	USceneComponent* GizmoComponent = ActiveTarget.TransformGizmo->GetGizmoActor()->GetRootComponent();
	FTransform EndDragtransform = GizmoComponent->GetComponentToWorld();

	TUniquePtr<FComponentWorldTransformChange> Change = MakeUnique<FComponentWorldTransformChange>(StartDragTransform, EndDragtransform);
	GetToolManager()->EmitObjectChange(GizmoComponent, MoveTemp(Change),
		LOCTEXT("TransformToolTransformTxnName", "SnapDrag"));

	GetToolManager()->EndUndoTransaction();

	ActiveSnapDragIndex = -1;
}




void UEditPivotTool::UpdateAssets(const FFrame3d& NewPivotWorldFrame)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("EditPivotToolTransactionName", "Edit Pivot"));

	FTransform NewWorldTransform = NewPivotWorldFrame.ToFTransform();

	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		FTransform3d ComponentToWorld(ComponentTargets[ComponentIdx]->GetWorldTransform());

		ComponentTargets[ComponentIdx]->CommitMesh([&](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			// AAAAHHHH we can apply this xform directly to MeshDescription if we had the suitable function!

			FMeshDescriptionToDynamicMesh ToDynamicMesh;
			FDynamicMesh3 Mesh;
			ToDynamicMesh.Convert(CommitParams.MeshDescription, Mesh);

			MeshTransforms::ApplyTransform(Mesh, ComponentToWorld);
			MeshTransforms::WorldToFrameCoords(Mesh, NewPivotWorldFrame);

			FDynamicMeshToMeshDescription Converter;
			Converter.Update(&Mesh, *CommitParams.MeshDescription, true, false);
		});

		UPrimitiveComponent* Component = ComponentTargets[ComponentIdx]->GetOwnerComponent();
		Component->Modify();
		Component->SetWorldTransform(NewWorldTransform);
	}

	GetToolManager()->EndUndoTransaction();
}





#undef LOCTEXT_NAMESPACE
