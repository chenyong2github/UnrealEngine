// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PlaneCutTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshTriangleAttribute.h"
#include "DynamicMeshEditor.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "Drawing/MeshDebugDrawing.h"
#include "AssetGenerationUtil.h"

#include "Changes/ToolCommandChangeSequence.h"

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
	for (int Idx = 0; Idx < ComponentTargets.Num(); Idx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[Idx];
		FDynamicMesh3* OriginalDynamicMesh = new FDynamicMesh3;
		FMeshDescriptionToDynamicMesh Converter;
		Converter.bPrintDebugMessages = true;

		
		Converter.Convert(ComponentTarget->GetMesh(), *OriginalDynamicMesh);
		OriginalDynamicMesh->EnableAttributes();
		TDynamicMeshScalarTriangleAttribute<int>* SubObjectIDs = new TDynamicMeshScalarTriangleAttribute<int>(OriginalDynamicMesh);
		SubObjectIDs->Initialize(0);
		int AttribIndex = OriginalDynamicMesh->Attributes()->AttachAttribute(SubObjectIDs);

		/// fill in the MeshesToCut array
		UDynamicMeshReplacementChangeTarget* Target = MeshesToCut.Add_GetRef(NewObject<UDynamicMeshReplacementChangeTarget>());
		MeshSubObjectAttribIndices.Add(AttribIndex);
		check(MeshSubObjectAttribIndices.Num() == MeshesToCut.Num());
		// store a UV scale based on the original mesh bounds (we don't want to recompute this between cuts b/c we want consistent UV scale)
		MeshUVScaleFactor.Add(1.0 / OriginalDynamicMesh->GetBounds().MaxDim());

		// Set callbacks so previews are invalidated on undo/redo changing the meshes
		Target->SetMesh(TSharedPtr<const FDynamicMesh3>(OriginalDynamicMesh));
		Target->OnMeshChanged.AddLambda([this, Idx]() { Previews[Idx]->InvalidateResult(); });
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
	ToolPropertyObjects.Add(this);
	BasicProperties = NewObject<UPlaneCutToolProperties>(this, TEXT("Plane Cut Settings"));
	AddToolPropertySource(BasicProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreviews();

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


void UPlaneCutTool::SetupPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 NumSourceMeshes = MeshesToCut.Num();
	int32 TargetNumPreview = NumSourceMeshes;
	for (int32 PreviewIdx = CurrentNumPreview; PreviewIdx < TargetNumPreview; PreviewIdx++)
	{
		UPlaneCutOperatorFactory *CutSide = NewObject<UPlaneCutOperatorFactory>();
		CutSide->CutTool = this;
		CutSide->ComponentIndex = PreviewIdx;
		UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(CutSide, "Preview"));
		Preview->Setup(this->TargetWorld, CutSide);

		FComponentMaterialSet MaterialSet;
		ComponentTargets[PreviewIdx]->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		// set initial preview to un-processed mesh, so stuff doesn't just disappear if the first cut takes a while
		Preview->PreviewMesh->UpdatePreview(MeshesToCut[PreviewIdx]->GetMesh().Get());
		Preview->PreviewMesh->SetTransform(ComponentTargets[PreviewIdx]->GetWorldTransform());
		Preview->SetVisibility(BasicProperties->bShowPreview);
	}
}



void UPlaneCutTool::DoCut()
{
	if (!CanAccept())
	{
		return;
	}

	

	TUniquePtr<FToolCommandChangeSequence> ChangeSeq = MakeUnique<FToolCommandChangeSequence>();

	TArray<FDynamicMeshOpResult> Results;
	for (int Idx = 0, N = MeshesToCut.Num(); Idx < N; Idx++)
	{
		UMeshOpPreviewWithBackgroundCompute* Preview = Previews[Idx];
		TUniquePtr<FDynamicMesh3> ResultMesh = Preview->PreviewMesh->ExtractPreviewMesh();
		ChangeSeq->AppendChange(MeshesToCut[Idx], MeshesToCut[Idx]->ReplaceMesh(
			TSharedPtr<const FDynamicMesh3>(ResultMesh.Release()))
		);
	}

	// emit combined change sequence
	GetToolManager()->EmitObjectChange(this, MoveTemp(ChangeSeq), LOCTEXT("MeshPlaneCut", "Cut Mesh with Plane"));
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
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
	CutOp->bFillCutHole = CutTool->BasicProperties->bFillCutHole;
	CutOp->bFillSpans = CutTool->BasicProperties->bFillSpans;

	FTransform LocalToWorld = CutTool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	FTransform WorldToLocal = LocalToWorld.Inverse();
	FVector LocalOrigin = WorldToLocal.TransformPosition(CutTool->CutPlaneOrigin);
	FVector WorldNormal = CutTool->CutPlaneOrientation.GetAxisZ();
	FVector LocalNormal = WorldToLocal.TransformVectorNoScale(WorldNormal); // TODO: correct for nonuniform scaling?
	CutOp->LocalPlaneOrigin = LocalOrigin;
	CutOp->LocalPlaneNormal = LocalNormal;
	CutOp->OriginalMesh = CutTool->MeshesToCut[ComponentIndex]->GetMesh();
	CutOp->bKeepBothHalves = CutTool->BasicProperties->bKeepBothHalves;
	CutOp->OtherHalfOffsetDistance = CutTool->BasicProperties->SpacingBetweenHalves;
	CutOp->UVScaleFactor = CutTool->MeshUVScaleFactor[ComponentIndex];
	CutOp->SubObjectsAttribIndex = CutTool->MeshSubObjectAttribIndices[ComponentIndex];
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

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPlaneCutTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
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
	if (Results.Num() == 0)
	{
		return;
	}

	for (int ResIdx = 0; ResIdx < Results.Num(); ResIdx++)
	{
		if (   Results[ResIdx].Mesh.IsValid() == false
			|| Results[ResIdx].Mesh->TriangleCount() == 0)
		{
			// TODO handle the "some meshes are fully deleted" case in some better way than this.
			// (we cannot currently save them as empty assets, as that will break undo and possibly other downstream things that make bad assumptions)
			return;
		}
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("PlaneCutToolTransactionName", "Plane Cut Tool"));
	

	// currently in-place replaces the first half, and adds a new actor for the second half (if it was generated)
	// TODO: options to support other choices re what should be a new actor

	ensure(Results.Num() > 0);
	int32 NumSourceMeshes = MeshesToCut.Num();
	TArray<TArray<FDynamicMesh3>> AllSplitMeshes;
	

	for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
	{
		FDynamicMesh3* UseMesh = Results[OrigMeshIdx].Mesh.Get();
		check(UseMesh != nullptr);

		TDynamicMeshScalarTriangleAttribute<int>* SubMeshIDs = 
			static_cast<TDynamicMeshScalarTriangleAttribute<int>*>(UseMesh->Attributes()->GetAttachedAttribute(
				MeshSubObjectAttribIndices[OrigMeshIdx]));
		TArray<FDynamicMesh3>& SplitMeshes = AllSplitMeshes.Emplace_GetRef();
		bool bWasSplit = FDynamicMeshEditor::SplitMesh(UseMesh, SplitMeshes, [SubMeshIDs](int TID)
		{
			return SubMeshIDs->GetValue(TID);
		}
		);
		if (bWasSplit)
		{
			// split mesh did something but has no meshes in the output array??
			if (!ensure(SplitMeshes.Num() > 0))
			{
				continue;
			}
			UseMesh = &SplitMeshes[0];
		}

		ComponentTargets[OrigMeshIdx]->CommitMesh([&UseMesh](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(UseMesh, *CommitParams.MeshDescription);
		});
	}

	// The method for creating a new mesh (AssetGenerationUtil::GenerateStaticMeshActor) is editor-only
#if WITH_EDITOR
	bool bNeedToAdd = false;
	for (int MeshIdx = 0; MeshIdx < NumSourceMeshes; MeshIdx++)
	{
		TArray<FDynamicMesh3>& SplitMeshes = AllSplitMeshes[MeshIdx];
		if (SplitMeshes.Num() < 2)
		{
			continue;
		}

		bNeedToAdd = true;
	}

	if (bNeedToAdd)
	{
		FSelectedOjectsChangeList NewSelection;
		NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
		for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
		{
			NewSelection.Actors.Add(ComponentTarget->GetOwnerActor());
		}

		for (int OrigMeshIdx = 0; OrigMeshIdx < NumSourceMeshes; OrigMeshIdx++)
		{
			TArray<FDynamicMesh3>& SplitMeshes = AllSplitMeshes[OrigMeshIdx];
			if (SplitMeshes.Num() < 2)
			{
				continue;
			}

			// build array of materials from the original
			TArray<UMaterialInterface*> Materials;
			TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[OrigMeshIdx];
			for (int MaterialIdx = 0, NumMaterials = ComponentTarget->GetNumMaterials(); MaterialIdx < NumMaterials; MaterialIdx++)
			{
				Materials.Add(ComponentTarget->GetMaterial(MaterialIdx));
			}

			// add all the additional meshes
			for (int AddMeshIdx = 1; AddMeshIdx < SplitMeshes.Num(); AddMeshIdx++)
			{
				AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
					AssetAPI, TargetWorld,
					&SplitMeshes[AddMeshIdx], Results[OrigMeshIdx].Transform, TEXT("PlaneCutOtherPart"),
					AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath(), Materials);
				NewSelection.Actors.Add(NewActor);
			}
		}

		GetToolManager()->RequestSelectionChange(NewSelection);
	}

#endif

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
