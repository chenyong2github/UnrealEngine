// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditPivotTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "Mechanics/DragAlignmentMechanic.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshAdapterTransforms.h"
#include "MeshDescriptionAdapter.h"
#include "MeshTransforms.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "ToolSceneQueriesUtil.h"
#include "Physics/ComponentCollisionUtil.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/AssetBackedTarget.h"
#include "ToolTargetManager.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UEditPivotTool"

/*
 * ToolBuilder
 */


const FToolTargetTypeRequirements& UEditPivotToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UAssetBackedTarget::StaticClass()
	});
	return TypeRequirements;
}

bool UEditPivotToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) >= 1;
}

UInteractiveTool* UEditPivotToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UEditPivotTool* NewTool = NewObject<UEditPivotTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
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

	DragAlignmentMechanic = NewObject<UDragAlignmentMechanic>(this);
	DragAlignmentMechanic->Setup(this);
	DragAlignmentMechanic->AddToGizmo(ActiveGizmos[0].TransformGizmo);

	Precompute();

	FText AllTheWarnings = LOCTEXT("EditPivotWarning", "WARNING: This Tool will Modify the selected StaticMesh Assets! If you do not wish to modify the original Assets, please make copies in the Content Browser first!");

	// detect and warn about any meshes in selection that correspond to same source data
	bool bSharesSources = GetMapToSharedSourceData(MapToFirstOccurrences);
	if (bSharesSources)
	{
		AllTheWarnings = FText::Format(FTextFormat::FromString("{0}\n\n{1}"), AllTheWarnings, LOCTEXT("EditPivotSharedAssetsWarning", "WARNING: Multiple meshes in your selection use the same source asset!  This is not supported -- each asset can only have one baked pivot."));
	}

	GetToolManager()->DisplayMessage(AllTheWarnings, EToolMessageLevel::UserWarning);

	SetToolDisplayName(LOCTEXT("ToolName", "Edit Pivot"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "This tool edits the Pivot (Origin) of the input assets. Hold Ctrl while using the gizmo to align to scene. Enable Snap Dragging and click+drag to place gizmo directly into clicked position."),
		EToolMessageLevel::UserNotification);
}



void UEditPivotTool::Shutdown(EToolShutdownType ShutdownType)
{
	DragAlignmentMechanic->Shutdown();

	FFrame3d CurPivotFrame(ActiveGizmos[0].TransformProxy->GetTransform());

	GizmoManager->DestroyAllGizmosByOwner(this);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		UpdateAssets(CurPivotFrame);
	}
}


void VertexIteration(const FMeshDescription* Mesh, TFunctionRef<void(int32, const FVector&)> ApplyFunc)
{
	TArrayView<const FVector3f> VertexPositions = Mesh->GetVertexPositions().GetRawArray();

	for (const FVertexID VertexID : Mesh->Vertices().GetElementIDs())
	{
		const FVector Position = VertexPositions[VertexID];
		ApplyFunc(VertexID.GetValue(), Position);
	}
}


void UEditPivotTool::Precompute()
{
	ObjectBounds = FAxisAlignedBox3d::Empty();
	WorldBounds = FAxisAlignedBox3d::Empty();

	int NumComponents = Targets.Num();
	if (NumComponents == 1)
	{
		Transform = UE::Geometry::FTransform3d(TargetComponentInterface(0)->GetWorldTransform());

		FMeshDescription* Mesh = TargetMeshProviderInterface(0)->GetMeshDescription();
		VertexIteration(Mesh, [&](int32 VertexID, const FVector& Position) {
			ObjectBounds.Contain((FVector3d)Position);
			WorldBounds.Contain(Transform.TransformPosition((FVector3d)Position));
		});
	}
	else
	{
		Transform = UE::Geometry::FTransform3d::Identity();
		for (int k = 0; k < NumComponents; ++k)
		{
			IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(k);
			IMeshDescriptionProvider* TargetMeshProvider = TargetMeshProviderInterface(k);
			UE::Geometry::FTransform3d CurTransform(TargetComponent->GetWorldTransform());
			FMeshDescription* Mesh = TargetMeshProvider->GetMeshDescription();
			VertexIteration(Mesh, [&](int32 VertexID, const FVector& Position) {
				ObjectBounds.Contain(CurTransform.TransformPosition((FVector3d)Position));
				WorldBounds.Contain(CurTransform.TransformPosition((FVector3d)Position));
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
	DragAlignmentMechanic->Render(RenderAPI);
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

		case EEditPivotToolActions::WorldOrigin:
			SetPivotToWorldOrigin();
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


void UEditPivotTool::SetPivotToWorldOrigin()
{
	ActiveGizmos[0].TransformGizmo->SetNewGizmoTransform(FTransform());
}


void UEditPivotTool::SetActiveGizmos_Single(bool bLocalRotations)
{
	check(ActiveGizmos.Num() == 0);

	FEditPivotTarget Transformable;
	Transformable.TransformProxy = NewObject<UTransformProxy>(this);
	Transformable.TransformProxy->bRotatePerObject = bLocalRotations;

	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		Transformable.TransformProxy->AddComponent(TargetComponentInterface(ComponentIdx)->GetOwnerComponent());
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

	FHitResult Result;
	bool bWorldHit = ToolSceneQueriesUtil::FindNearestVisibleObjectHit(TargetWorld, Result, PressPos.WorldRay);

	if (!bWorldHit)
	{
		return FInputRayHit();
	}
	return FInputRayHit(Result.Distance, Result.ImpactNormal);
}

void UEditPivotTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	FInputRayHit HitPos = CanBeginClickDragSequence(PressPos);
	check(HitPos.bHit);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("TransformToolTransformTxnName", "SnapDrag"));

	FEditPivotTarget& ActiveTarget = ActiveGizmos[0];
	USceneComponent* GizmoComponent = ActiveTarget.TransformGizmo->GetGizmoActor()->GetRootComponent();
	StartDragTransform = GizmoComponent->GetComponentToWorld();
}


void UEditPivotTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
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
		FQuaterniond(FVector3d::UnitZ(), (FVector3d)TargetNormal) : FQuaterniond::Identity();

	FTransform NewTransform = StartDragTransform;
	NewTransform.SetRotation((FQuat)AlignRotation);
	NewTransform.SetTranslation(HitPos);

	FEditPivotTarget& ActiveTarget = ActiveGizmos[0];
	ActiveTarget.TransformGizmo->SetNewGizmoTransform(NewTransform);
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
}




void UEditPivotTool::UpdateAssets(const FFrame3d& NewPivotWorldFrame)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("EditPivotToolTransactionName", "Edit Pivot"));

	FTransform NewWorldTransform = NewPivotWorldFrame.ToFTransform();
	FTransform NewWorldInverse = NewWorldTransform.Inverse();
	TArray<FTransform> OriginalTransforms;
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		OriginalTransforms.Add(TargetComponentInterface(ComponentIdx)->GetWorldTransform());
	}
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(ComponentIdx);
		if (MapToFirstOccurrences[ComponentIdx] == ComponentIdx)
		{
			UE::Geometry::FTransform3d ToBake(OriginalTransforms[ComponentIdx] * NewWorldInverse);

			UPrimitiveComponent* Component = TargetComponent->GetOwnerComponent();
			Component->Modify();

			// transform simple collision geometry
			UE::Geometry::TransformSimpleCollision(Component, ToBake);

			TargetMeshCommitterInterface(ComponentIdx)->CommitMeshDescription([&ToBake](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
			{
				FMeshDescriptionEditableTriangleMeshAdapter EditableMeshDescAdapter(CommitParams.MeshDescriptionOut);
				MeshAdapterTransforms::ApplyTransform(EditableMeshDescAdapter, ToBake);
			});

			Component->SetWorldTransform(NewWorldTransform);
		}
		else
		{
			UPrimitiveComponent* Component = TargetComponent->GetOwnerComponent();
			Component->Modify();
			// try to invert baked transform
			FTransform Baked = OriginalTransforms[MapToFirstOccurrences[ComponentIdx]] * NewWorldInverse;
			Component->SetWorldTransform(Baked.Inverse() * OriginalTransforms[ComponentIdx]);
		}
		TargetComponent->GetOwnerActor()->MarkComponentsRenderStateDirty();
	}

	// hack to ensure user sees the updated pivot immediately: request re-select of the original selection
	FSelectedOjectsChangeList NewSelection;
	NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
	for (int OrigMeshIdx = 0; OrigMeshIdx < Targets.Num(); OrigMeshIdx++)
	{
		IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(OrigMeshIdx);
		NewSelection.Actors.Add(TargetComponent->GetOwnerActor());
	}
	GetToolManager()->RequestSelectionChange(NewSelection);

	GetToolManager()->EndUndoTransaction();
}





#undef LOCTEXT_NAMESPACE
