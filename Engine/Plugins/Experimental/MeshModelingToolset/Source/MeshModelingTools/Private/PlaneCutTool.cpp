// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PlaneCutTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "Drawing/MeshDebugDrawing.h"
#include "AssetGenerationUtil.h"

#include "CuttingOps/PlaneCutOp.h"

#define LOCTEXT_NAMESPACE "UPlaneCutTool"


/*
 * ToolBuilder
 */


bool UPlaneCutToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UPlaneCutToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UPlaneCutTool* NewTool = NewObject<UPlaneCutTool>(SceneState.ToolManager);

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
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}



/*
 * Tool
 */

UPlaneCutToolProperties::UPlaneCutToolProperties()
{
	bDiscardAttributes = false;
	bKeepBothHalves = false;
	bFillCutHole = true;
	SpacingBetweenHalves = 1;
	bShowPreview = true;
	bFillSpans = false;
}



UPlaneCutTool::UPlaneCutTool()
{
	CutPlaneOrigin = FVector::ZeroVector;
	CutPlaneOrientation = FQuat::Identity;
}

void UPlaneCutTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UPlaneCutTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponents
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	// Convert input mesh descriptions to dynamic mesh
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		TSharedPtr<FDynamicMesh3> OriginalDynamicMesh = MakeShared<FDynamicMesh3>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.bPrintDebugMessages = true;
		Converter.Convert(ComponentTarget->GetMesh(), *OriginalDynamicMesh);
		OriginalDynamicMeshes.Add(OriginalDynamicMesh);
	}

	// click to set plane behavior
	FSelectClickedAction* SetPlaneAction = new FSelectClickedAction();
	SetPlaneAction->World = this->TargetWorld;
	SetPlaneAction->OnClickedPositionFunc = [this](const FHitResult& Hit)
	{
		SetCutPlaneFromWorldPos(Hit.ImpactPoint, Hit.ImpactNormal);
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->InvalidateResult();
		}
	};
	SetPointInWorldConnector = SetPlaneAction;

	USingleClickInputBehavior* ClickToSetPlaneBehavior = NewObject<USingleClickInputBehavior>();
	ClickToSetPlaneBehavior->ModifierCheckFunc = FInputDeviceState::IsCtrlKeyDown;
	ClickToSetPlaneBehavior->Initialize(SetPointInWorldConnector);
	AddInputBehavior(ClickToSetPlaneBehavior);

	// create proxy and gizmo (but don't attach yet)
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformGizmo = GizmoManager->Create3AxisTransformGizmo(this);

	// initialize our properties
	BasicProperties = NewObject<UPlaneCutToolProperties>(this, TEXT("Plane Cut Settings"));
	AddToolPropertySource(BasicProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	UpdateNumPreviews();

	// set initial cut plane (also attaches gizmo/proxy)
	FBox CombinedBounds; CombinedBounds.Init();
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		FVector ComponentOrigin, ComponentExtents;
		ComponentTarget->GetOwnerActor()->GetActorBounds(false, ComponentOrigin, ComponentExtents);
		CombinedBounds += FBox::BuildAABB(ComponentOrigin, ComponentExtents);
	}
	SetCutPlaneFromWorldPos(CombinedBounds.GetCenter(), FVector::UpVector);
	// hook up callback so further changes trigger recut
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UPlaneCutTool::TransformChanged);

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPlaneCutTool::UpdateNumPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 NumSourceMeshes = OriginalDynamicMeshes.Num();
	int32 TargetNumPreview = (BasicProperties->bKeepBothHalves ? 2 : 1) * NumSourceMeshes;
	if (TargetNumPreview < CurrentNumPreview)
	{
		for (int32 PreviewIdx = CurrentNumPreview - 1; PreviewIdx >= TargetNumPreview; PreviewIdx--)
		{
			Previews[PreviewIdx]->Cancel();
		}
		Previews.SetNum(TargetNumPreview);
	}
	else
	{
		for (int32 PreviewIdx = CurrentNumPreview; PreviewIdx < TargetNumPreview; PreviewIdx++)
		{
			UPlaneCutOperatorFactory *CutSide = NewObject<UPlaneCutOperatorFactory>();
			CutSide->CutTool = this;
			CutSide->bCutBackSide = PreviewIdx >= NumSourceMeshes;
			int32 SrcIdx = PreviewIdx % NumSourceMeshes;
			CutSide->ComponentIndex = SrcIdx;
			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(CutSide, "Preview"));
			Preview->Setup(this->TargetWorld, CutSide);

			FComponentMaterialSet MaterialSet;
			ComponentTargets[SrcIdx]->GetMaterialSet(MaterialSet);
			Preview->ConfigureMaterials(MaterialSet.Materials,
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);

			// set initial preview to un-processed mesh, so stuff doesn't just disappear if the first cut takes a while
			Preview->PreviewMesh->UpdatePreview(OriginalDynamicMeshes[SrcIdx].Get());
			Preview->PreviewMesh->SetTransform(ComponentTargets[SrcIdx]->GetWorldTransform());
			Preview->SetVisibility(BasicProperties->bShowPreview);
			
		}
	}
}


void UPlaneCutTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	TArray<FDynamicMeshOpResult> Results;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Results.Emplace(Preview->Shutdown());
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}

	if (SetPointInWorldConnector != nullptr)
	{
		delete SetPointInWorldConnector;
	}
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->DestroyAllGizmosByOwner(this);
}

void UPlaneCutTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UPlaneCutOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FPlaneCutOp> CutOp = MakeUnique<FPlaneCutOp>();
	CutOp->bDiscardAttributes = CutTool->BasicProperties->bDiscardAttributes;
	CutOp->bFillCutHole = CutTool->BasicProperties->bFillCutHole;
	CutOp->bFillSpans = CutTool->BasicProperties->bFillSpans;

	FTransform LocalToWorld = CutTool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	FTransform WorldToLocal = LocalToWorld.Inverse();
	FVector LocalOrigin = WorldToLocal.TransformPosition(CutTool->CutPlaneOrigin);
	FVector WorldNormal = CutTool->CutPlaneOrientation.GetAxisZ();
	FVector LocalNormal = WorldToLocal.TransformVectorNoScale(WorldNormal); // TODO: correct for nonuniform scaling?
	if (bCutBackSide)
	{
		LocalNormal = -LocalNormal;
	}
	CutOp->LocalPlaneOrigin = LocalOrigin;
	CutOp->LocalPlaneNormal = LocalNormal;
	CutOp->OriginalMesh = CutTool->OriginalDynamicMeshes[ComponentIndex];
	
	if (bCutBackSide)
	{
		LocalToWorld *= FTransform(CutTool->BasicProperties->SpacingBetweenHalves * WorldNormal);
	}
	CutOp->SetTransform(LocalToWorld);

	return CutOp;
}



void UPlaneCutTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FColor GridColor(128, 128, 128, 32);
	float GridThickness = 0.5f;
	float GridLineSpacing = 25.0f;   // @todo should be relative to view
	int NumGridLines = 10;
	
	FFrame3f DrawFrame(CutPlaneOrigin, CutPlaneOrientation);
	MeshDebugDraw::DrawSimpleGrid(DrawFrame, NumGridLines, GridLineSpacing, GridThickness, GridColor, false, PDI, FTransform::Identity);
}

void UPlaneCutTool::Tick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}


#if WITH_EDITOR
void UPlaneCutTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}
#endif

void UPlaneCutTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPlaneCutToolProperties, bShowPreview)))
	{
		for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
		{
			ComponentTarget->SetOwnerVisibility(!BasicProperties->bShowPreview);
		}
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->SetVisibility(BasicProperties->bShowPreview);
		}
	}

	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPlaneCutTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	// TODO: if multi-select is re-enabled, only invalidate the preview that actually needs it?
	CutPlaneOrientation = Transform.GetRotation();
	CutPlaneOrigin = (FVector)Transform.GetTranslation();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPlaneCutTool::SetCutPlaneFromWorldPos(const FVector& Position, const FVector& Normal)
{
	CutPlaneOrigin = Position;

	FFrame3f CutPlane(Position, Normal);
	CutPlaneOrientation = CutPlane.Rotation;

	PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy);
	PlaneTransformGizmo->SetNewGizmoTransform(CutPlane.ToFTransform());
}


bool UPlaneCutTool::HasAccept() const
{
	return true;
}

bool UPlaneCutTool::CanAccept() const
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		if (!Preview->HaveValidResult())
		{
			return false;
		}
	}
	return true;
}


void UPlaneCutTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	if (Results.Num() == 0 
		|| Results[0].Mesh.IsValid() == false
		|| Results[0].Mesh->TriangleCount() == 0 )
	{
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("PlaneCutToolTransactionName", "Plane Cut Tool"));
	

	// currently in-place replaces the first half, and adds a new actor for the second half (if it was generated)
	// TODO: options to support other choices re what should be a new actor
	ensure(Results.Num() > 0);
	int32 NumSourceMeshes = OriginalDynamicMeshes.Num();
	for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
	{
		check(Results[OrigMeshIdx].Mesh.Get() != nullptr);
		ComponentTargets[OrigMeshIdx]->CommitMesh([&Results, OrigMeshIdx](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(Results[OrigMeshIdx].Mesh.Get(), *CommitParams.MeshDescription);
		});
	}

	// The method for creating a new mesh (AssetGenerationUtil::GenerateStaticMeshActor) is editor-only; just creating the other half if not in editor
#if WITH_EDITOR
	
	if (Results.Num() > NumSourceMeshes)
	{
		ensure(Results.Num() == NumSourceMeshes * 2);

		FSelectedOjectsChangeList NewSelection;
		NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
		for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
		{
			NewSelection.Actors.Add(ComponentTarget->GetOwnerActor());
		}
		
		for (int32 AddedMeshIdx = NumSourceMeshes; AddedMeshIdx < Results.Num(); AddedMeshIdx++)
		{
			check(Results[AddedMeshIdx].Mesh.Get() != nullptr);

			TArray<UMaterialInterface*> Materials;
			TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[AddedMeshIdx - NumSourceMeshes];
			for (int MaterialIdx = 0, NumMaterials = ComponentTarget->GetNumMaterials(); MaterialIdx < NumMaterials; MaterialIdx++)
			{
				Materials.Add(ComponentTarget->GetMaterial(MaterialIdx));
			}

			AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
				AssetAPI, TargetWorld,
				Results[AddedMeshIdx].Mesh.Get(), Results[AddedMeshIdx].Transform, TEXT("PlaneCutOtherHalf"),
				AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath(), Materials);
			NewSelection.Actors.Add(NewActor);
		}
		GetToolManager()->RequestSelectionChange(NewSelection);
	}
#endif

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
