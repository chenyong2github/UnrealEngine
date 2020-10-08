// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditPivotTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshAdapterTransforms.h"
#include "MeshDescriptionAdapter.h"
#include "MeshTransforms.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "ToolSceneQueriesUtil.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "Components/PrimitiveComponent.h"
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




void UEditPivotToolActionPropertySet::PostAction(EEditPivotToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
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

	EditPivotActions = NewObject<UEditPivotToolActionPropertySet>(this);
	EditPivotActions->Initialize(this);
	AddToolPropertySource(EditPivotActions);

	ResetActiveGizmos();
	SetActiveGizmos_Single(false);
	UpdateSetPivotModes(true);

	Precompute();

	FText AllTheWarnings = LOCTEXT("EditPivotWarning", "WARNING: This Tool will Modify the selected StaticMesh Assets! If you do not wish to modify the original Assets, please make copies in the Content Browser first!");

	// detect and warn about any meshes in selection that correspond to same source data
	bool bSharesSources = GetMapToFirstComponentsSharingSourceData(MapToFirstOccurrences);
	if (bSharesSources)
	{
		AllTheWarnings = FText::Format(FTextFormat::FromString("{0}\n\n{1}"), AllTheWarnings, LOCTEXT("EditPivotSharedAssetsWarning", "WARNING: Multiple meshes in your selection use the same source asset!  This is not supported -- each asset can only have one baked pivot."));
	}

	GetToolManager()->DisplayMessage(AllTheWarnings, EToolMessageLevel::UserWarning);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "This Tool edits the Pivot (Origin) of the input objects. Use the Gizmo or enable Snap Dragging and click-drag on the surface to reposition."),
		EToolMessageLevel::UserNotification);
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


void VertexIteration(const FMeshDescription* Mesh, TFunction<void(int32, const FVector&)> ApplyFunc)
{
	const FVertexArray& VertexIDs = Mesh->Vertices();
	TVertexAttributesConstRef<FVector> VertexPositions =
		Mesh->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	for (const FVertexID VertexID : VertexIDs.GetElementIDs())
	{
		const FVector Position = VertexPositions.Get(VertexID);
		ApplyFunc(VertexID.GetValue(), Position);
	}
}


void UEditPivotTool::Precompute()
{
	ObjectBounds = FAxisAlignedBox3d::Empty();
	WorldBounds = FAxisAlignedBox3d::Empty();

	int NumComponents = ComponentTargets.Num();
	if (NumComponents == 1)
	{
		Transform = FTransform3d(ComponentTargets[0]->GetWorldTransform());

		FMeshDescription* Mesh = ComponentTargets[0]->GetMesh();
		VertexIteration(Mesh, [&](int32 VertexID, const FVector& Position) {
			ObjectBounds.Contain(Position);
			WorldBounds.Contain(Transform.TransformPosition(Position));
		});
	}
	else
	{
		Transform = FTransform3d::Identity();
		for (int k = 0; k < ComponentTargets.Num(); ++k)
		{
			TUniquePtr<FPrimitiveComponentTarget>& Target = ComponentTargets[k];
			FTransform3d CurTransform(Target->GetWorldTransform());
			FMeshDescription* Mesh = Target->GetMesh();
			VertexIteration(Mesh, [&](int32 VertexID, const FVector& Position) {
				ObjectBounds.Contain(CurTransform.TransformPosition(Position));
				WorldBounds.Contain(CurTransform.TransformPosition(Position));
			});
		}
	}
}



void UEditPivotTool::RequestAction(EEditPivotToolActions ActionType)
{
	if (PendingAction == EEditPivotToolActions::NoAction)
	{
		PendingAction = ActionType;
	}
}


void UEditPivotTool::OnTick(float DeltaTime)
{
	if (PendingAction != EEditPivotToolActions::NoAction)
	{
		ApplyAction(PendingAction);
		PendingAction = EEditPivotToolActions::NoAction;
	}
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
}


void UEditPivotTool::ApplyAction(EEditPivotToolActions ActionType)
{
	switch (ActionType)
	{
		case EEditPivotToolActions::Center:
		case EEditPivotToolActions::Bottom:
		case EEditPivotToolActions::Top:
		case EEditPivotToolActions::Left:
		case EEditPivotToolActions::Right:
		case EEditPivotToolActions::Front:
		case EEditPivotToolActions::Back:
			SetPivotToBoxPoint(ActionType);
			break;
	}
}


void UEditPivotTool::SetPivotToBoxPoint(EEditPivotToolActions ActionPoint)
{
	FAxisAlignedBox3d UseBox = (EditPivotActions->bUseWorldBox) ? WorldBounds : ObjectBounds;
	FVector3d Point = UseBox.Center();

	if (ActionPoint == EEditPivotToolActions::Bottom || ActionPoint == EEditPivotToolActions::Top)
	{
		Point.Z = (ActionPoint == EEditPivotToolActions::Bottom) ? UseBox.Min.Z : UseBox.Max.Z;
	}
	else if (ActionPoint == EEditPivotToolActions::Left || ActionPoint == EEditPivotToolActions::Right)
	{
		Point.Y = (ActionPoint == EEditPivotToolActions::Left) ? UseBox.Min.Y : UseBox.Max.Y;
	}
	else if (ActionPoint == EEditPivotToolActions::Back || ActionPoint == EEditPivotToolActions::Front)
	{
		Point.X = (ActionPoint == EEditPivotToolActions::Front) ? UseBox.Min.X : UseBox.Max.X;
	}

	FTransform NewTransform;
	if (EditPivotActions->bUseWorldBox == false)
	{
		FFrame3d LocalFrame(Point);
		LocalFrame.Transform(Transform);
		NewTransform = LocalFrame.ToFTransform();
	}
	else
	{
		NewTransform = FTransform((FVector)Point);
	}

	ActiveGizmos[0].TransformGizmo->SetNewGizmoTransform(NewTransform);
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
	Transformable.TransformGizmo = GizmoManager->CreateCustomTransformGizmo(
		ETransformGizmoSubElements::StandardTranslateRotate, this
	);
	Transformable.TransformGizmo->SetActiveTarget(Transformable.TransformProxy, GetToolManager());

	Transformable.TransformGizmo->bUseContextCoordinateSystem = false;
	Transformable.TransformGizmo->CurrentCoordinateSystem = EToolContextCoordinateSystem::Local;

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

	//if (TransformProps->SnapDragSource == EEditPivotSnapDragSource::ClickPoint)
	//{
		StartDragFrameWorld = FFrame3d(PressPos.WorldRay.PointAt(HitPos.HitDepth), HitPos.HitNormal);
	//}
	//else
	//{
	//	StartDragFrameWorld = FFrame3d(StartDragTransform);
	//}

}


void UEditPivotTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
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
	bool bWorldHit = ToolSceneQueriesUtil::FindNearestVisibleObjectHit(TargetWorld, Result, DragPos.WorldRay);
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
	FTransform NewWorldInverse = NewWorldTransform.Inverse();
	TArray<FTransform> OriginalTransforms;
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		OriginalTransforms.Add(ComponentTargets[ComponentIdx]->GetWorldTransform());
	}
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		if (MapToFirstOccurrences[ComponentIdx] == ComponentIdx)
		{
			FTransform3d ToBake(OriginalTransforms[ComponentIdx] * NewWorldInverse);

			ComponentTargets[ComponentIdx]->CommitMesh([&ToBake](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FMeshDescriptionEditableTriangleMeshAdapter EditableMeshDescAdapter(CommitParams.MeshDescription);
				MeshAdapterTransforms::ApplyTransform(EditableMeshDescAdapter, ToBake);
			});

			UPrimitiveComponent* Component = ComponentTargets[ComponentIdx]->GetOwnerComponent();
			Component->Modify();
			Component->SetWorldTransform(NewWorldTransform);
		}
		else
		{
			UPrimitiveComponent* Component = ComponentTargets[ComponentIdx]->GetOwnerComponent();
			Component->Modify();
			// try to invert baked transform
			FTransform Baked = OriginalTransforms[MapToFirstOccurrences[ComponentIdx]] * NewWorldInverse;
			Component->SetWorldTransform(Baked.Inverse() * OriginalTransforms[ComponentIdx]);
		}
		ComponentTargets[ComponentIdx]->GetOwnerActor()->MarkComponentsRenderStateDirty();
	}

	// hack to ensure user sees the updated pivot immediately: request re-select of the original selection
	FSelectedOjectsChangeList NewSelection;
	NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
	for (int OrigMeshIdx = 0; OrigMeshIdx < ComponentTargets.Num(); OrigMeshIdx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[OrigMeshIdx];
		NewSelection.Actors.Add(ComponentTarget->GetOwnerActor());
	}
	GetToolManager()->RequestSelectionChange(NewSelection);

	GetToolManager()->EndUndoTransaction();
}





#undef LOCTEXT_NAMESPACE
