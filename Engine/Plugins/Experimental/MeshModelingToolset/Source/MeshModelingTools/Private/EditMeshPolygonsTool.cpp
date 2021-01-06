// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditMeshPolygonsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "CompGeom/PolygonTriangulation.h"
#include "SegmentTypes.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "ToolSceneQueriesUtil.h"
#include "Intersection/IntersectionUtil.h"
#include "Transforms/MultiTransformer.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "Operations/MeshPlaneCut.h"
#include "Selections/MeshEdgeSelection.h"
#include "Selections/MeshFaceSelection.h"
#include "Selections/MeshConnectedComponents.h"
#include "FaceGroupUtil.h"
#include "DynamicMeshEditor.h"
#include "DynamicMeshChangeTracker.h"
#include "Changes/MeshChange.h"
#include "MeshIndexUtil.h"
#include "MeshRegionBoundaryLoops.h"

#include "Operations/OffsetMeshRegion.h"
#include "Operations/InsetMeshRegion.h"
#include "Operations/SimpleHoleFiller.h"
#include "MeshTransforms.h"

#include "Algo/ForEach.h"
#include "Async/ParallelFor.h"
#include "Containers/BitArray.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "UEditMeshPolygonsTool"



/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UEditMeshPolygonsToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UEditMeshPolygonsTool* EditPolygonsTool = NewObject<UEditMeshPolygonsTool>(SceneState.ToolManager);
	if (bTriangleMode)
	{
		EditPolygonsTool->EnableTriangleMode();
	}
	return EditPolygonsTool;
}



void UEditMeshPolygonsToolActionPropertySet::PostAction(EEditMeshPolygonsToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}

/*
* Tool methods
*/

UEditMeshPolygonsTool::UEditMeshPolygonsTool()
{
	SetToolDisplayName(LOCTEXT("EditMeshPolygonsToolName", "Edit PolyGroups Tool"));
}

void UEditMeshPolygonsTool::EnableTriangleMode()
{
	check(DynamicMeshComponent == nullptr);		// must not have been initialized!
	bTriangleMode = true;
}

void UEditMeshPolygonsTool::Setup()
{
	UMeshSurfacePointTool::Setup();

	// register click behavior
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());
	WorldTransform = FTransform3d(DynamicMeshComponent->GetComponentTransform());

	// set materials
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	// configure secondary render material
	UMaterialInterface* SelectionMaterial = ToolSetupUtil::GetSelectionMaterial(FLinearColor::Yellow, GetToolManager());
	if (SelectionMaterial != nullptr)
	{
		DynamicMeshComponent->SetSecondaryRenderMaterial(SelectionMaterial);
	}

	// enable secondary triangle buffers
	DynamicMeshComponent->EnableSecondaryTriangleBuffers(
		[this](const FDynamicMesh3* Mesh, int32 TriangleID)
	{
		return SelectionMechanic->GetActiveSelection().IsSelectedTriangle(Mesh, Topology.Get(), TriangleID);
	});


	// dynamic mesh configuration settings
	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	FMeshNormals::QuickComputeVertexNormals(*DynamicMeshComponent->GetMesh());
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshChanged.Add(
		FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UEditMeshPolygonsTool::OnDynamicMeshComponentChanged));



	// add properties
	CommonProps = NewObject<UPolyEditCommonProperties>(this);
	CommonProps->RestoreProperties(this);
	AddToolPropertySource(CommonProps);
	CommonProps->WatchProperty(CommonProps->LocalFrameMode,
								  [this](ELocalFrameMode) { UpdateMultiTransformerFrame(); });
	CommonProps->WatchProperty(CommonProps->bLockRotation,
								  [this](bool)
								  {
									  LockedTransfomerFrame = LastTransformerFrame; UpdateMultiTransformerFrame();
								  });
	// We are going to SilentUpdate here because otherwise the Watches above will immediately fire (why??)
	// and cause UpdateMultiTransformerFrame() to be called for each, emitting two spurious Transform changes. 
	CommonProps->SilentUpdateWatched();

	// set up SelectionMechanic
	SelectionMechanic = NewObject<UPolygonSelectionMechanic>(this);
	SelectionMechanic->Setup(this);
	SelectionMechanic->Properties->RestoreProperties(this);
	SelectionMechanic->OnSelectionChanged.AddUObject(this, &UEditMeshPolygonsTool::OnSelectionModifiedEvent);
	if (bTriangleMode)
	{
		SelectionMechanic->PolyEdgesRenderer.LineThickness = 1.0;
	}

	// initialize AABBTree
	MeshSpatial.SetMesh(DynamicMeshComponent->GetMesh());
	PrecomputeTopology();

	// Set UV Scale factor based on initial mesh bounds
	float BoundsMaxDim = DynamicMeshComponent->GetMesh()->GetBounds().MaxDim();
	if (BoundsMaxDim > 0)
	{
		UVScaleFactor = 1.0 / BoundsMaxDim;
	}

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// init state flags flags
	bInDrag = false;

	MultiTransformer = NewObject<UMultiTransformer>(this);
	MultiTransformer->Setup(GetToolManager()->GetPairedGizmoManager(), GetToolManager());
	MultiTransformer->OnTransformStarted.AddUObject(this, &UEditMeshPolygonsTool::OnMultiTransformerTransformBegin);
	MultiTransformer->OnTransformUpdated.AddUObject(this, &UEditMeshPolygonsTool::OnMultiTransformerTransformUpdate);
	MultiTransformer->OnTransformCompleted.AddUObject(this, &UEditMeshPolygonsTool::OnMultiTransformerTransformEnd);
	MultiTransformer->SetSnapToWorldGridSourceFunc([this]() {
		return CommonProps->bSnapToWorldGrid
			&& GetToolManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem() == EToolContextCoordinateSystem::World;
	});
	MultiTransformer->SetGizmoVisibility(false);

	if (bTriangleMode == false)
	{
		EditActions = NewObject<UEditMeshPolygonsToolActions>();
		EditActions->Initialize(this);
		AddToolPropertySource(EditActions);

		EditEdgeActions = NewObject<UEditMeshPolygonsToolEdgeActions>();
		EditEdgeActions->Initialize(this);
		AddToolPropertySource(EditEdgeActions);

		EditUVActions = NewObject<UEditMeshPolygonsToolUVActions>();
		EditUVActions->Initialize(this);
		AddToolPropertySource(EditUVActions);
	}
	else
	{
		EditActions_Triangles = NewObject<UEditMeshPolygonsToolActions_Triangles>();
		EditActions_Triangles->Initialize(this);
		AddToolPropertySource(EditActions_Triangles);

		EditEdgeActions_Triangles = NewObject<UEditMeshPolygonsToolEdgeActions_Triangles>();
		EditEdgeActions_Triangles->Initialize(this);
		AddToolPropertySource(EditEdgeActions_Triangles);
	}

	ExtrudeProperties = NewObject<UPolyEditExtrudeProperties>();
	ExtrudeProperties->RestoreProperties(this);
	AddToolPropertySource(ExtrudeProperties);
	SetToolPropertySourceEnabled(ExtrudeProperties, false);
	ExtrudeProperties->WatchProperty(ExtrudeProperties->Direction,
									 [this](EPolyEditExtrudeDirection){ RestartExtrude(); });

	OffsetProperties = NewObject<UPolyEditOffsetProperties>();
	OffsetProperties->RestoreProperties(this);
	AddToolPropertySource(OffsetProperties);
	SetToolPropertySourceEnabled(OffsetProperties, false);

	InsetProperties = NewObject<UPolyEditInsetProperties>();
	InsetProperties->RestoreProperties(this);
	AddToolPropertySource(InsetProperties);
	SetToolPropertySourceEnabled(InsetProperties, false);

	OutsetProperties = NewObject<UPolyEditOutsetProperties>();
	OutsetProperties->RestoreProperties(this);
	AddToolPropertySource(OutsetProperties);
	SetToolPropertySourceEnabled(OutsetProperties, false);

	CutProperties = NewObject<UPolyEditCutProperties>();
	CutProperties->RestoreProperties(this);
	AddToolPropertySource(CutProperties);
	SetToolPropertySourceEnabled(CutProperties, false);

	SetUVProperties = NewObject<UPolyEditSetUVProperties>();
	SetUVProperties->RestoreProperties(this);
	AddToolPropertySource(SetUVProperties);
	SetToolPropertySourceEnabled(SetUVProperties, false);


	if (bTriangleMode)
	{
		SetToolDisplayName(LOCTEXT("EditMeshTrianglesToolName", "Edit Triangles Tool"));
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartEditMeshPolygonsTool_TriangleMode", "Select Triangles to edit mesh. Q to toggle Gizmo Orientation Lock."),
			EToolMessageLevel::UserNotification);
	}
	else
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartEditMeshPolygonsTool", "Select PolyGroups to edit mesh. Q to toggle Gizmo Orientation Lock."),
			EToolMessageLevel::UserNotification);
	}

	if (Topology->Groups.Num() < 2)
	{
		GetToolManager()->DisplayMessage( LOCTEXT("NoGroupsWarning", "This object has a single PolyGroup. Use the PolyGroups or Select Tool to assign PolyGroups."), EToolMessageLevel::UserWarning);
	}
}

void UEditMeshPolygonsTool::Shutdown(EToolShutdownType ShutdownType)
{
	CommonProps->SaveProperties(this);
	ExtrudeProperties->SaveProperties(this);
	OffsetProperties->SaveProperties(this);
	InsetProperties->SaveProperties(this);
	CutProperties->SaveProperties(this);
	SetUVProperties->SaveProperties(this);
	SelectionMechanic->Properties->SaveProperties(this);

	MultiTransformer->Shutdown();
	SelectionMechanic->Shutdown();
	if (EditPreview != nullptr)
	{
		EditPreview->Disconnect();
		EditPreview = nullptr;
	}

	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// may need to compact the mesh if we did undo on a mesh edit, then vertices will be dense but compact checks will fail...
			if (bWasTopologyEdited)
			{
				DynamicMeshComponent->GetMesh()->CompactInPlace();
			}

			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("EditMeshPolygonsToolTransactionName", "Deform Mesh"));
			ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FConversionToMeshDescriptionOptions ConversionOptions;
				bool bModifiedTopology = (ModifiedTopologyCounter > 0);
				ConversionOptions.bSetPolyGroups = bModifiedTopology;
				DynamicMeshComponent->Bake(CommitParams.MeshDescription, bModifiedTopology, ConversionOptions);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}




void UEditMeshPolygonsTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 2,
		TEXT("ToggleLockRotation"),
		LOCTEXT("ToggleLockRotationUIName", "Lock Rotation"),
		LOCTEXT("ToggleLockRotationTooltip", "Toggle Frame Rotation Lock on and off"),
		EModifierKey::None, EKeys::Q,
		[this]() { CommonProps->bLockRotation = !CommonProps->bLockRotation; });
}


void UEditMeshPolygonsTool::RequestAction(EEditMeshPolygonsToolActions ActionType)
{
	if (PendingAction != EEditMeshPolygonsToolActions::NoAction)
	{
		return;
	}

	PendingAction = ActionType;
}





FDynamicMeshAABBTree3& UEditMeshPolygonsTool::GetSpatial()
{
	if (bSpatialDirty)
	{
		MeshSpatial.Build();
		bSpatialDirty = false;
	}
	return MeshSpatial;
}







bool UEditMeshPolygonsTool::HitTest(const FRay& WorldRay, FHitResult& OutHit)
{
	if (CurrentToolMode != ECurrentToolMode::TransformSelection)
	{
		OutHit.Distance = 100.0;
		OutHit.ImpactPoint = WorldRay.PointAt(100.0);
		return true;
	}

	// disable hit test
	return SelectionMechanic->TopologyHitTest(WorldRay, OutHit);
}



FInputRayHit UEditMeshPolygonsTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FHitResult OutHit;
	if (HitTest(ClickPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	// background capture, if nothing else is hit
	return FInputRayHit(TNumericLimits<float>::Max());
}

void UEditMeshPolygonsTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	if ( CurrentToolMode == ECurrentToolMode::ExtrudeSelection )
	{
		ApplyExtrude(false);
		return;
	}
	if (CurrentToolMode == ECurrentToolMode::OffsetSelection)
	{
		ApplyExtrude(true);
		return;
	}
	else if (CurrentToolMode == ECurrentToolMode::InsetSelection || CurrentToolMode == ECurrentToolMode::OutsetSelection)
	{
		ApplyInset(CurrentToolMode == ECurrentToolMode::OutsetSelection);
		return;
	}
	else if (CurrentToolMode == ECurrentToolMode::CutSelection)
	{
		if (SurfacePathMechanic->TryAddPointFromRay(ClickPos.WorldRay))
		{
			if (SurfacePathMechanic->IsDone())
			{
				ApplyCutFaces();
			}
		}
		return;
	}
	else if (CurrentToolMode == ECurrentToolMode::SetUVs)
	{
		if (SurfacePathMechanic->TryAddPointFromRay(ClickPos.WorldRay))
		{
			if (SurfacePathMechanic->IsDone())
			{
				ApplySetUVs();
			}
		}
		return;
	}

	// update selection
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PolyMeshSelectionChange", "Selection"));
	SelectionMechanic->BeginChange();
	FVector3d LocalHitPosition, LocalHitNormal;
	bool bSelectionModified = SelectionMechanic->UpdateSelection(ClickPos.WorldRay, LocalHitPosition, LocalHitNormal);
	if (bSelectionModified) 
	{
		FFrame3d LocalFrame(LocalHitPosition, LocalHitNormal);
		LastGeometryFrame = SelectionMechanic->GetSelectionFrame(true, &LocalFrame);
		UpdateMultiTransformerFrame();
	}
	SelectionMechanic->EndChangeAndEmitIfModified();
	GetToolManager()->EndUndoTransaction();
}


void UEditMeshPolygonsTool::UpdateMultiTransformerFrame(const FFrame3d* UseFrame)
{
	FFrame3d SetFrame = LastTransformerFrame;
	if (UseFrame == nullptr)
	{
		if (CommonProps->LocalFrameMode == ELocalFrameMode::FromGeometry)
		{
			SetFrame = LastGeometryFrame;
		}
		else
		{
			SetFrame = FFrame3d(LastGeometryFrame.Origin, WorldTransform.GetRotation());
		}
	}
	else
	{
		SetFrame = *UseFrame;
	}

	if (CommonProps->bLockRotation)
	{
		SetFrame.Rotation = LockedTransfomerFrame.Rotation;
	}

	LastTransformerFrame = SetFrame;
	//MultiTransformer->UpdateGizmoPositionFromWorldFrame(SetFrame, true);
	MultiTransformer->InitializeGizmoPositionFromWorldFrame(SetFrame, true);
}


void UEditMeshPolygonsTool::OnSelectionModifiedEvent()
{
	bSelectionStateDirty = true;
}




FInputRayHit UEditMeshPolygonsTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	// disable this for now
	return FInputRayHit();
	//return UMeshSurfacePointTool::CanBeginClickDragSequence(PressPos);
}



void UEditMeshPolygonsTool::OnBeginDrag(const FRay& WorldRay)
{
}



void UEditMeshPolygonsTool::OnUpdateDrag(const FRay& Ray)
{
	check(false);
}

void UEditMeshPolygonsTool::OnEndDrag(const FRay& Ray)
{
	check(false);
}



void UEditMeshPolygonsTool::OnMultiTransformerTransformBegin()
{
	SelectionMechanic->ClearHighlight();
	UpdateDeformerFromSelection( SelectionMechanic->GetActiveSelection() );
	InitialGizmoFrame = MultiTransformer->GetCurrentGizmoFrame();
	InitialGizmoScale = MultiTransformer->GetCurrentGizmoScale();
	BeginChange();
}

void UEditMeshPolygonsTool::OnMultiTransformerTransformUpdate()
{
	if (MultiTransformer->InGizmoEdit())
	{
		CacheUpdate_Gizmo();
	}
}

void UEditMeshPolygonsTool::OnMultiTransformerTransformEnd()
{
	bGizmoUpdatePending = false;
	bSpatialDirty = true;
	SelectionMechanic->NotifyMeshChanged(false);

	MultiTransformer->ResetScale();

	// close change record
	EndChange();
}



bool UEditMeshPolygonsTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (CurrentToolMode == ECurrentToolMode::ExtrudeSelection)
	{
		ExtrudeHeightMechanic->UpdateCurrentDistance(DevicePos.WorldRay);
		bPreviewUpdatePending = true;
		return true;
	}
	else if (CurrentToolMode == ECurrentToolMode::OffsetSelection)
	{
		ExtrudeHeightMechanic->UpdateCurrentDistance(DevicePos.WorldRay);
		bPreviewUpdatePending = true;
		return true;
	}
	else if (CurrentToolMode == ECurrentToolMode::InsetSelection || CurrentToolMode == ECurrentToolMode::OutsetSelection)
	{
		CurveDistMechanic->UpdateCurrentDistance(DevicePos.WorldRay);
		bPreviewUpdatePending = true;
		return true;
	}
	else if (CurrentToolMode == ECurrentToolMode::CutSelection)
	{
		SurfacePathMechanic->UpdatePreviewPoint(DevicePos.WorldRay);
		return true;
	}
	else if (CurrentToolMode == ECurrentToolMode::SetUVs)
	{
		SurfacePathMechanic->UpdatePreviewPoint(DevicePos.WorldRay);
		bPreviewUpdatePending = true;
		return true;
	}

	if (ActiveVertexChange == nullptr && MultiTransformer->InGizmoEdit() == false )
	{
		SelectionMechanic->UpdateHighlight(DevicePos.WorldRay);
	}
	return true;
}


void UEditMeshPolygonsTool::OnEndHover()
{
	SelectionMechanic->ClearHighlight();
}




void UEditMeshPolygonsTool::UpdateDeformerFromSelection(const FGroupTopologySelection& Selection)
{
	//Determine which of the following (corners, edges or faces) has been selected by counting the associated feature's IDs
	if (Selection.SelectedCornerIDs.Num() > 0)
	{
		//Add all the the Corner's adjacent poly-groups (NbrGroups) to the ongoing array of groups.
		LinearDeformer.SetActiveHandleCorners(Selection.SelectedCornerIDs.Array());
	}
	else if (Selection.SelectedEdgeIDs.Num() > 0)
	{
		//Add all the the edge's adjacent poly-groups (NbrGroups) to the ongoing array of groups.
		LinearDeformer.SetActiveHandleEdges(Selection.SelectedEdgeIDs.Array());
	}
	else if (Selection.SelectedGroupIDs.Num() > 0)
	{
		LinearDeformer.SetActiveHandleFaces(Selection.SelectedGroupIDs.Array());
	}
}



void UEditMeshPolygonsTool::CacheUpdate_Gizmo()
{
	LastUpdateGizmoFrame = MultiTransformer->GetCurrentGizmoFrame();
	LastUpdateGizmoScale = MultiTransformer->GetCurrentGizmoScale();
	GetToolManager()->PostInvalidation();
	bGizmoUpdatePending = true;
}

void UEditMeshPolygonsTool::ComputeUpdate_Gizmo()
{
	if (SelectionMechanic->HasSelection() == false || bGizmoUpdatePending == false)
	{
		return;
	}
	bGizmoUpdatePending = false;

	FFrame3d CurFrame = LastUpdateGizmoFrame;
	FVector3d CurScale = LastUpdateGizmoScale;
	FVector3d TranslationDelta = CurFrame.Origin - InitialGizmoFrame.Origin;
	FQuaterniond RotateDelta = CurFrame.Rotation - InitialGizmoFrame.Rotation;
	FVector3d CurScaleDelta = CurScale - InitialGizmoScale;
	FVector3d LocalTranslation = WorldTransform.InverseTransformVector((FVector)TranslationDelta);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	if (TranslationDelta.SquaredLength() > 0.0001 || RotateDelta.SquaredLength() > 0.0001 || CurScaleDelta.SquaredLength() > 0.0001)
	{
		LinearDeformer.UpdateSolution(Mesh, [&](FDynamicMesh3* TargetMesh, int VertIdx)
		{
			FVector3d PosLocal = TargetMesh->GetVertex(VertIdx);
			FVector3d PosWorld = WorldTransform.TransformPosition(PosLocal);
			FVector3d PosGizmo = InitialGizmoFrame.ToFramePoint(PosWorld);
			PosGizmo = CurScale * PosGizmo;
			FVector3d NewPosWorld = CurFrame.FromFramePoint(PosGizmo);
			FVector3d NewPosLocal = WorldTransform.InverseTransformPosition(NewPosWorld);
			return NewPosLocal;
		});
	}
	else
	{
		// Reset mesh to initial positions.
		LinearDeformer.ClearSolution(Mesh);
	}
	DynamicMeshComponent->FastNotifyPositionsUpdated(true);
	GetToolManager()->PostInvalidation();
}



void UEditMeshPolygonsTool::OnTick(float DeltaTime)
{
	MultiTransformer->Tick(DeltaTime);

	if (bGizmoUpdatePending)
	{
		ComputeUpdate_Gizmo();
	}

	if (bSelectionStateDirty)
	{
		// update color highlights
		DynamicMeshComponent->FastNotifySecondaryTrianglesChanged();

		if (SelectionMechanic->HasSelection())
		{
			MultiTransformer->SetGizmoVisibility(true);

			// update frame because we might be here due to an undo event/etc, rather than an explicit selection change
			LastGeometryFrame = SelectionMechanic->GetSelectionFrame(true, &LastGeometryFrame);
			UpdateMultiTransformerFrame();
		}
		else
		{
			MultiTransformer->SetGizmoVisibility(false);
		}

		bSelectionStateDirty = false;
	}

	if (PendingAction != EEditMeshPolygonsToolActions::NoAction)
	{
		CancelMeshEditChange();

		if (PendingAction == EEditMeshPolygonsToolActions::Extrude || PendingAction == EEditMeshPolygonsToolActions::Offset)
		{
			GetToolManager()->EmitObjectChange(this, MakeUnique<FBeginInteractivePolyEditChange>(CurrentOperationTimestamp), LOCTEXT("PolyMeshEditBeginExtrude", "Extrude"));
			BeginExtrude( (PendingAction == EEditMeshPolygonsToolActions::Offset) );
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::Inset)
		{
			GetToolManager()->EmitObjectChange(this, MakeUnique<FBeginInteractivePolyEditChange>(CurrentOperationTimestamp), LOCTEXT("PolyMeshEditBeginInset", "Begin Inset"));
			BeginInset(false);
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::Outset)
		{
			GetToolManager()->EmitObjectChange(this, MakeUnique<FBeginInteractivePolyEditChange>(CurrentOperationTimestamp), LOCTEXT("PolyMeshEditBeginOutset", "Begin Outset"));
			BeginInset(true);
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::CutFaces)
		{
			GetToolManager()->EmitObjectChange(this, MakeUnique<FBeginInteractivePolyEditChange>(CurrentOperationTimestamp), LOCTEXT("PolyMeshEditBeginCutFaces", "Cut Faces"));
			BeginCutFaces();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::PlanarProjectionUV)
		{
			GetToolManager()->EmitObjectChange(this, MakeUnique<FBeginInteractivePolyEditChange>(CurrentOperationTimestamp), LOCTEXT("PolyMeshEditBeginUVPlanarProjection", "Set UVs"));
			BeginSetUVs();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::Merge)
		{
			ApplyMerge();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::Delete)
		{
			ApplyDelete();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::RecalculateNormals)
		{
			ApplyRecalcNormals();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::FlipNormals)
		{
			ApplyFlipNormals();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::CollapseEdge)
		{
			ApplyCollapseEdge();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::WeldEdges)
		{
			ApplyWeldEdges();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::StraightenEdge)
		{
			ApplyStraightenEdges();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::FillHole)
		{
			ApplyFillHole();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::Retriangulate)
		{
			ApplyRetriangulate();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::Decompose)
		{
			ApplyDecompose();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::Disconnect)
		{
			ApplyDisconnect();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::PokeSingleFace)
		{
			ApplyPokeSingleFace();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::SplitSingleEdge)
		{
			ApplySplitSingleEdge();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::CollapseSingleEdge)
		{
			ApplyCollapseSingleEdge();
		}
		else if (PendingAction == EEditMeshPolygonsToolActions::FlipSingleEdge)
		{
			ApplyFlipSingleEdge();
		}

		PendingAction = EEditMeshPolygonsToolActions::NoAction;
	}


	// todo: convert to ValueWatcher
	if (CurrentToolMode == ECurrentToolMode::SetUVs)
	{
		EPreviewMaterialType WantMaterial = (SetUVProperties->bShowMaterial) ? EPreviewMaterialType::SourceMaterials : EPreviewMaterialType::UVMaterial;
		if (CurrentPreviewMaterial != WantMaterial)
		{
			UpdateEditPreviewMaterials(WantMaterial);
		}
	}


	if (bPreviewUpdatePending)
	{
		if (CurrentToolMode == ECurrentToolMode::ExtrudeSelection)
		{
			EditPreview->UpdateExtrudeType(ExtrudeHeightMechanic->CurrentHeight);
		}
		else if (CurrentToolMode == ECurrentToolMode::OffsetSelection)
		{
			if (OffsetProperties->bUseFaceNormals)
			{
				EditPreview->UpdateExtrudeType_FaceNormalAvg(ExtrudeHeightMechanic->CurrentHeight);
			}
			else
			{
				EditPreview->UpdateExtrudeType(ExtrudeHeightMechanic->CurrentHeight, true);
			}
		}
		else if (CurrentToolMode == ECurrentToolMode::InsetSelection || CurrentToolMode == ECurrentToolMode::OutsetSelection)
		{
			bool bOutset = (CurrentToolMode == ECurrentToolMode::OutsetSelection);
			double Sign = bOutset ? -1.0 : 1.0;
			bool bReproject = (bOutset) ? false : InsetProperties->bReproject;
			double Softness = (bOutset) ? OutsetProperties->Softness : InsetProperties->Softness;
			bool bBoundaryOnly = (bOutset) ? OutsetProperties->bBoundaryOnly : InsetProperties->bBoundaryOnly;
			double AreaCorrection = (bOutset) ? OutsetProperties->AreaScale : InsetProperties->AreaScale;
			EditPreview->UpdateInsetType(Sign* CurveDistMechanic->CurrentDistance, bReproject, Softness, AreaCorrection, bBoundaryOnly);
		}
		else if (CurrentToolMode == ECurrentToolMode::SetUVs)
		{
			UpdateSetUVS();
		}
		bPreviewUpdatePending = false;
	}
}


void UEditMeshPolygonsTool::PrecomputeTopology()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	Topology = (bTriangleMode) ? MakeUnique<FTriangleGroupTopology>(Mesh, false) : MakeUnique<FGroupTopology>(Mesh, false);
	Topology->RebuildTopology();

	// update selection mechanic
	SelectionMechanic->Initialize(DynamicMeshComponent, Topology.Get(),
		[this]() { return &GetSpatial(); },
		[this]() { return GetShiftToggle(); }
		);

	LinearDeformer.Initialize(Mesh, Topology.Get());
}




void UEditMeshPolygonsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
	DynamicMeshComponent->bExplicitShowWireframe = CommonProps->bShowWireframe;

	SelectionMechanic->Render(RenderAPI);

	if (ExtrudeHeightMechanic != nullptr)
	{
		ExtrudeHeightMechanic->Render(RenderAPI);
	}
	if (CurveDistMechanic != nullptr)
	{
		CurveDistMechanic->Render(RenderAPI);
	}
	if (SurfacePathMechanic != nullptr)
	{
		SurfacePathMechanic->Render(RenderAPI);
	}
}




//
// Change Tracking
//


void UEditMeshPolygonsTool::UpdateChangeFromROI(bool bFinal)
{
	if (ActiveVertexChange == nullptr)
	{
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	ActiveVertexChange->SaveVertices(Mesh, LinearDeformer.GetModifiedVertices(), !bFinal);
	ActiveVertexChange->SaveOverlayNormals(Mesh, LinearDeformer.GetModifiedOverlayNormals(), !bFinal);
}


void UEditMeshPolygonsTool::BeginChange()
{
	if (ActiveVertexChange == nullptr)
	{
		ActiveVertexChange = new FMeshVertexChangeBuilder(EMeshVertexChangeComponents::VertexPositions | EMeshVertexChangeComponents::OverlayNormals);
		UpdateChangeFromROI(false);
	}
}


void UEditMeshPolygonsTool::EndChange()
{
	if (ActiveVertexChange != nullptr)
	{
		UpdateChangeFromROI(true);
		GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(ActiveVertexChange->Change), LOCTEXT("PolyMeshDeformationChange", "PolyMesh Edit"));
	}

	delete ActiveVertexChange;
	ActiveVertexChange = nullptr;

	CurrentOperationTimestamp++;
}


void UEditMeshPolygonsTool::OnDynamicMeshComponentChanged()
{
	bSpatialDirty = true;
	SelectionMechanic->NotifyMeshChanged(false);
}

void UEditMeshPolygonsTool::AfterTopologyEdit()
{
	bSpatialDirty = true;
	bWasTopologyEdited = true;
	SelectionMechanic->NotifyMeshChanged(true);

	DynamicMeshComponent->NotifyMeshUpdated();
	MeshSpatial.SetMesh(DynamicMeshComponent->GetMesh(), true);
	PrecomputeTopology();
}




void UEditMeshPolygonsTool::ApplyPlaneCut()
{
	FFrame3d PlaneFrame;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FMeshPlaneCut Cut(Mesh, PlaneFrame.Origin, PlaneFrame.Z());
	Cut.UVScaleFactor = UVScaleFactor;

	FMeshEdgeSelection Edges(Mesh);
	const FGroupTopologySelection& ActiveSelection = SelectionMechanic->GetActiveSelection();
	if (ActiveSelection.SelectedGroupIDs.Num() > 0)
	{
		for (int32 GroupID : ActiveSelection.SelectedGroupIDs)
		{
			Edges.SelectTriangleEdges( Topology->GetGroupTriangles(GroupID) );
		}
		Cut.EdgeFilterFunc = [&](int EdgeID) { return Edges.IsSelected(EdgeID); };
	}

	Cut.SplitEdgesOnly(true);

	DynamicMeshComponent->NotifyMeshUpdated();
	MeshSpatial.SetMesh(DynamicMeshComponent->GetMesh(), true);
	PrecomputeTopology();
}





void UEditMeshPolygonsTool::BeginExtrude(bool bIsNormalOffset)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	if (bIsNormalOffset)
	{
		// yikes...
	}
	if (BeginMeshFaceEditChangeWithPreview() == false)
	{
		return;
	}

	ActiveSelectionFrameWorld.AlignAxis(2, GetExtrudeDirection());
	EditPreview->InitializeExtrudeType(Mesh, ActiveTriangleSelection, ActiveSelectionFrameWorld.Z(), &WorldTransform, true);
	// move world extrude frame to point on surface
	ActiveSelectionFrameWorld.Origin = EditPreview->GetInitialPatchMeshSpatial().FindNearestPoint(ActiveSelectionFrameWorld.Origin);

	// make inifinite-extent hit-test mesh
	FDynamicMesh3 ExtrudeHitTargetMesh;
	EditPreview->MakeExtrudeTypeHitTargetMesh(ExtrudeHitTargetMesh);

	ExtrudeHeightMechanic = NewObject<UPlaneDistanceFromHitMechanic>(this);
	ExtrudeHeightMechanic->Setup(this);

	ExtrudeHeightMechanic->WorldHitQueryFunc = [this](const FRay& WorldRay, FHitResult& HitResult)
	{
		return ToolSceneQueriesUtil::FindNearestVisibleObjectHit(DynamicMeshComponent->GetWorld(), HitResult, WorldRay);
	};
	ExtrudeHeightMechanic->WorldPointSnapFunc = [this](const FVector3d& WorldPos, FVector3d& SnapPos)
	{
		return CommonProps->bSnapToWorldGrid && ToolSceneQueriesUtil::FindWorldGridSnapPoint(this, WorldPos, SnapPos);
	};
	ExtrudeHeightMechanic->CurrentHeight = 1.0f;  // initialize to something non-zero...prob should be based on polygon bounds maybe?

	ExtrudeHeightMechanic->Initialize(MoveTemp(ExtrudeHitTargetMesh), ActiveSelectionFrameWorld, true);
	CurrentToolMode = (bIsNormalOffset) ? ECurrentToolMode::OffsetSelection : ECurrentToolMode::ExtrudeSelection;

	if (bIsNormalOffset == false)
	{
		SetToolPropertySourceEnabled(ExtrudeProperties, true);
	}
	else
	{
		SetToolPropertySourceEnabled(OffsetProperties, true);
	}
	SetActionButtonPanelsVisible(false);
}



void UEditMeshPolygonsTool::ApplyExtrude(bool bIsOffset)
{
	check(ExtrudeHeightMechanic != nullptr && EditPreview != nullptr);

	FVector3d ExtrudeDir = WorldTransform.InverseTransformVector(ActiveSelectionFrameWorld.Z());
	double ExtrudeDist = ExtrudeHeightMechanic->CurrentHeight;

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FOffsetMeshRegion Extruder(Mesh);
	Extruder.UVScaleFactor = UVScaleFactor;
	Extruder.Triangles = ActiveTriangleSelection;
	TSet<int32> TriangleSet(ActiveTriangleSelection);
	Extruder.OffsetPositionFunc = [&](const FVector3d& Pos, const FVector3f& Normal, int32 VertexID) {
		return Pos + ExtrudeDist * (bIsOffset ? (FVector3d)Normal : ExtrudeDir);
	};
	Extruder.bIsPositiveOffset = (ExtrudeDist > 0);
	Extruder.bUseFaceNormals = (bIsOffset && OffsetProperties->bUseFaceNormals);
	Extruder.bOffsetFullComponentsAsSolids = bIsOffset || ExtrudeProperties->bShellsToSolids;
	Extruder.ChangeTracker = MakeUnique<FDynamicMeshChangeTracker>(Mesh);
	Extruder.ChangeTracker->BeginChange();
	Extruder.Apply();

	FMeshNormals::QuickComputeVertexNormalsForTriangles(*Mesh, Extruder.AllModifiedTriangles);

	// construct new selection
	FGroupTopologySelection NewSelection;
	for (const FOffsetMeshRegion::FOffsetInfo& Info : Extruder.OffsetRegions)
	{
		for (int32 gid : Info.OffsetGroups)
		{
			NewSelection.SelectedGroupIDs.Add(gid);
		}
	}

	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(Extruder.ChangeTracker->EndChange());
	CompleteMeshEditChange( (bIsOffset) ? LOCTEXT("PolyMeshOffsetChange", "Offset") : LOCTEXT("PolyMeshExtrudeChange", "Extrude"),
		MoveTemp(MeshChange), NewSelection);

	ExtrudeHeightMechanic = nullptr;
	CurrentToolMode = ECurrentToolMode::TransformSelection;

	SetToolPropertySourceEnabled(ExtrudeProperties, false);
	SetToolPropertySourceEnabled(OffsetProperties, false);
	SetActionButtonPanelsVisible(true);
}


void UEditMeshPolygonsTool::RestartExtrude()
{
	if (CurrentToolMode == ECurrentToolMode::ExtrudeSelection)
	{
		CancelMeshEditChange();
		BeginExtrude(false);
	}
}


FVector3d UEditMeshPolygonsTool::GetExtrudeDirection() const
{
	switch (ExtrudeProperties->Direction)
	{
	default:
	case EPolyEditExtrudeDirection::SelectionNormal:
		return ActiveSelectionFrameWorld.Z();
	case EPolyEditExtrudeDirection::WorldX:
		return FVector3d::UnitX();
	case EPolyEditExtrudeDirection::WorldY:
		return FVector3d::UnitY();
	case EPolyEditExtrudeDirection::WorldZ:
		return FVector3d::UnitZ();
	case EPolyEditExtrudeDirection::LocalX:
		return WorldTransform.GetRotation().AxisX();
	case EPolyEditExtrudeDirection::LocalY:
		return WorldTransform.GetRotation().AxisY();
	case EPolyEditExtrudeDirection::LocalZ:
		return WorldTransform.GetRotation().AxisZ();
	}
	return ActiveSelectionFrameWorld.Z();
}




void UEditMeshPolygonsTool::BeginInset(bool bOutset)
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	if (BeginMeshFaceEditChangeWithPreview() == false)
	{
		return;
	}

	EditPreview->InitializeInsetType(Mesh, ActiveTriangleSelection, &WorldTransform);

	// make infinite-extent hit-test mesh
	FDynamicMesh3 InsetHitTargetMesh;
	EditPreview->MakeInsetTypeTargetMesh(InsetHitTargetMesh);

	CurveDistMechanic = NewObject<USpatialCurveDistanceMechanic>(this);
	CurveDistMechanic->Setup(this);
	CurveDistMechanic->WorldPointSnapFunc = [this](const FVector3d& WorldPos, FVector3d& SnapPos)
	{
		return CommonProps->bSnapToWorldGrid && ToolSceneQueriesUtil::FindWorldGridSnapPoint(this, WorldPos, SnapPos);
	};
	CurveDistMechanic->CurrentDistance = 1.0f;  // initialize to something non-zero...prob should be based on polygon bounds maybe?

	FMeshBoundaryLoops Loops(&InsetHitTargetMesh);
	TArray<FVector3d> LoopVertices;
	Loops.Loops[0].GetVertices(LoopVertices);
	CurveDistMechanic->InitializePolyLoop(LoopVertices, FTransform3d::Identity());
	CurrentToolMode = (bOutset) ? ECurrentToolMode::OutsetSelection : ECurrentToolMode::InsetSelection;

	SetToolPropertySourceEnabled((bOutset) ? 
		(UInteractiveToolPropertySet*)OutsetProperties : (UInteractiveToolPropertySet*)InsetProperties, true);
	SetActionButtonPanelsVisible(false);
}




void UEditMeshPolygonsTool::ApplyInset(bool bOutset)
{
	check(CurveDistMechanic != nullptr && EditPreview != nullptr);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FInsetMeshRegion Inset(Mesh);
	Inset.UVScaleFactor = UVScaleFactor;
	Inset.Triangles = ActiveTriangleSelection;
	Inset.InsetDistance = (bOutset) ? -CurveDistMechanic->CurrentDistance : CurveDistMechanic->CurrentDistance;
	Inset.bReproject = (bOutset) ? false : InsetProperties->bReproject;
	Inset.Softness = (bOutset) ? OutsetProperties->Softness : InsetProperties->Softness;
	Inset.bSolveRegionInteriors = (bOutset) ? (!OutsetProperties->bBoundaryOnly) : (!InsetProperties->bBoundaryOnly);
	Inset.AreaCorrection = (bOutset) ? OutsetProperties->AreaScale : InsetProperties->AreaScale;

	Inset.ChangeTracker = MakeUnique<FDynamicMeshChangeTracker>(Mesh);
	Inset.ChangeTracker->BeginChange();
	Inset.Apply();

	FMeshNormals::QuickComputeVertexNormalsForTriangles(*Mesh, Inset.AllModifiedTriangles);

	// emit undo
	FGroupTopologySelection CurSelection = SelectionMechanic->GetActiveSelection();
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(Inset.ChangeTracker->EndChange());
	CompleteMeshEditChange( bOutset ? LOCTEXT("PolyMeshOutsetChange", "Outset") : LOCTEXT("PolyMeshInsetChange", "Inset"), MoveTemp(MeshChange), CurSelection);

	CurveDistMechanic = nullptr;
	CurrentToolMode = ECurrentToolMode::TransformSelection;

	SetToolPropertySourceEnabled((bOutset) ?
		(UInteractiveToolPropertySet*)OutsetProperties : (UInteractiveToolPropertySet*)InsetProperties, false);
	SetActionButtonPanelsVisible(true);
}




void UEditMeshPolygonsTool::BeginCutFaces()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	if (BeginMeshFaceEditChangeWithPreview() == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnCutFacesFailedMessage", "Cannot Cut Current Selection"),
			EToolMessageLevel::UserWarning);
		return;
	}
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnBeginCutFacesMessage", "Click twice on selected face to define cut line"),
		EToolMessageLevel::UserMessage);


	EditPreview->InitializeStaticType(Mesh, ActiveTriangleSelection, &WorldTransform);

	FDynamicMesh3 StaticHitTargetMesh;
	EditPreview->MakeInsetTypeTargetMesh(StaticHitTargetMesh);

	SurfacePathMechanic = NewObject<UCollectSurfacePathMechanic>(this);
	SurfacePathMechanic->Setup(this);
	SurfacePathMechanic->InitializeMeshSurface(MoveTemp(StaticHitTargetMesh));
	SurfacePathMechanic->SetFixedNumPointsMode(2);
	SurfacePathMechanic->bSnapToTargetMeshVertices = true;
	double SnapTol = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD();
	SurfacePathMechanic->SpatialSnapPointsFunc = [this, SnapTol](FVector3d Position1, FVector3d Position2)
	{
		return CutProperties->bSnapToVertices && ToolSceneQueriesUtil::PointSnapQuery(this->CameraState, Position1, Position2, SnapTol);
	};

	CurrentToolMode = ECurrentToolMode::CutSelection;
	SetToolPropertySourceEnabled(CutProperties, true);
	SetActionButtonPanelsVisible(false);
}

void UEditMeshPolygonsTool::ApplyCutFaces()
{
	check(SurfacePathMechanic != nullptr && EditPreview != nullptr);

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	// construct cut plane normal from line points
	FFrame3d Point0(SurfacePathMechanic->HitPath[0]), Point1(SurfacePathMechanic->HitPath[1]);
	FVector3d PlaneNormal;
	if (CutProperties->Orientation == EPolyEditCutPlaneOrientation::ViewDirection)
	{
		FVector3d Direction0 = (Point0.Origin - CameraState.Position).Normalized();
		FVector3d Direction1 = (Point1.Origin - CameraState.Position).Normalized();
		PlaneNormal = Direction1.Cross(Direction0);
	}
	else
	{
		FVector3d LineDirection = (Point1.Origin - Point0.Origin).Normalized();
		FVector3d UpVector = (Point0.Z() + Point1.Z()).Normalized();
		PlaneNormal = LineDirection.Cross(UpVector);
	}
	FVector3d PlaneOrigin = 0.5 * (Point0.Origin + Point1.Origin);
	// map into local space of target mesh
	PlaneOrigin = WorldTransform.InverseTransformPosition(PlaneOrigin);
	PlaneNormal = WorldTransform.InverseTransformNormal(PlaneNormal);
	PlaneNormal.Normalize();

	// track changes
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	TArray<int32> VertexSelection;
	MeshIndexUtil::TriangleToVertexIDs(Mesh, ActiveTriangleSelection, VertexSelection);
	ChangeTracker.SaveVertexOneRingTriangles(VertexSelection, true);

	// apply the cut to edges of selected triangles
	FGroupTopologySelection OutputSelection;
	FMeshPlaneCut Cut(Mesh, PlaneOrigin, PlaneNormal);
	FMeshEdgeSelection Edges(Mesh);
	Edges.SelectTriangleEdges(ActiveTriangleSelection);
	Cut.EdgeFilterFunc = [&](int EdgeID) { return Edges.IsSelected(EdgeID); };
	if (Cut.SplitEdgesOnly(true))
	{
		for (FMeshPlaneCut::FCutResultRegion& Region : Cut.ResultRegions)
		{
			OutputSelection.SelectedGroupIDs.Add(Region.GroupID);
		}
	}

	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshCutFacesChange", "Cut Faces"), MoveTemp(MeshChange), OutputSelection);

	SurfacePathMechanic = nullptr;
	CurrentToolMode = ECurrentToolMode::TransformSelection;
	SetToolPropertySourceEnabled(CutProperties, false);
	SetActionButtonPanelsVisible(true);
}





void UEditMeshPolygonsTool::BeginSetUVs()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	if (BeginMeshFaceEditChangeWithPreview() == false)
	{
		GetToolManager()->DisplayMessage( LOCTEXT("OnSetUVsFailedMesssage", "Cannot Set UVs for Current Selection"), EToolMessageLevel::UserWarning);
		return;
	}
	GetToolManager()->DisplayMessage( LOCTEXT("OnBeginSetUVsMessage", "Click on the face to Set UVs"), EToolMessageLevel::UserMessage);

	EditPreview->InitializeStaticType(Mesh, ActiveTriangleSelection, &WorldTransform);
	UpdateEditPreviewMaterials((SetUVProperties->bShowMaterial) ? EPreviewMaterialType::SourceMaterials : EPreviewMaterialType::UVMaterial);

	FDynamicMesh3 StaticHitTargetMesh;
	EditPreview->MakeInsetTypeTargetMesh(StaticHitTargetMesh);

	SurfacePathMechanic = NewObject<UCollectSurfacePathMechanic>(this);
	SurfacePathMechanic->Setup(this);
	SurfacePathMechanic->InitializeMeshSurface(MoveTemp(StaticHitTargetMesh));
	SurfacePathMechanic->SetFixedNumPointsMode(2);
	SurfacePathMechanic->bSnapToTargetMeshVertices = true;
	double SnapTol = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD();
	SurfacePathMechanic->SpatialSnapPointsFunc = [this, SnapTol](FVector3d Position1, FVector3d Position2)
	{
		return ToolSceneQueriesUtil::PointSnapQuery(this->CameraState, Position1, Position2, SnapTol);
	};

	CurrentToolMode = ECurrentToolMode::SetUVs;
	SetToolPropertySourceEnabled(SetUVProperties, true);
	SetActionButtonPanelsVisible(false);
}

void UEditMeshPolygonsTool::UpdateSetUVS()
{
	// align projection frame to line user is drawing out from plane origin
	FFrame3d PlanarFrame = SurfacePathMechanic->PreviewPathPoint;
	double UVScale = 1.0 / ActiveSelectionBounds.MaxDim();
	if (SurfacePathMechanic->HitPath.Num() == 1)
	{
		SurfacePathMechanic->InitializePlaneSurface(PlanarFrame);

		FVector3d Delta = PlanarFrame.Origin - SurfacePathMechanic->HitPath[0].Origin;
		double Dist = Delta.Normalize();
		UVScale *= FMathd::Lerp(1.0, 25.0, Dist / ActiveSelectionBounds.MaxDim());
		PlanarFrame = SurfacePathMechanic->HitPath[0];
		PlanarFrame.ConstrainedAlignAxis(0, Delta, PlanarFrame.Z());
	}

	EditPreview->UpdateStaticType([&](FDynamicMesh3& Mesh)
	{
		FDynamicMeshEditor Editor(&Mesh);
		TArray<int32> AllTriangles;
		for (int32 tid : Mesh.TriangleIndicesItr())
		{
			AllTriangles.Add(tid);
		}
		Editor.SetTriangleUVsFromProjection(AllTriangles, PlanarFrame, UVScale, FVector2f::Zero(), false);
	}, false);

}


void UEditMeshPolygonsTool::ApplySetUVs()
{
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FGroupTopologySelection ActiveSelection = SelectionMechanic->GetActiveSelection();

	// align projection frame to line user drew
	FFrame3d PlanarFrame = SurfacePathMechanic->HitPath[0];
	double UVScale = 1.0 / ActiveSelectionBounds.MaxDim();
	FVector3d Delta = SurfacePathMechanic->HitPath[1].Origin - PlanarFrame.Origin;
	double Dist = Delta.Normalize();
	UVScale *= FMathd::Lerp(1.0, 25.0, Dist / ActiveSelectionBounds.MaxDim());
	PlanarFrame.ConstrainedAlignAxis(0, Delta, PlanarFrame.Z());

	// transform to local, use 3D point to transfer UV scale value
	FVector3d ScalePt = PlanarFrame.Origin + UVScale * PlanarFrame.Z();
	FTransform3d ToLocalXForm(WorldTransform.Inverse());
	PlanarFrame.Transform(ToLocalXForm);
	ScalePt = ToLocalXForm.TransformPosition(ScalePt);
	UVScale = ScalePt.Distance(PlanarFrame.Origin);

	// track changes
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(ActiveTriangleSelection, true);
	FDynamicMeshEditor Editor(Mesh);
	Editor.SetTriangleUVsFromProjection(ActiveTriangleSelection, PlanarFrame, UVScale, FVector2f::Zero(), false, 0);

	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshSetUVsChange", "Set UVs"), MoveTemp(MeshChange), ActiveSelection);

	SurfacePathMechanic = nullptr;
	CurrentToolMode = ECurrentToolMode::TransformSelection;
	SetToolPropertySourceEnabled(SetUVProperties, false);
	SetActionButtonPanelsVisible(true);
}




void UEditMeshPolygonsTool::ApplyMerge()
{
	if (BeginMeshFaceEditChangeWithPreview() == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnMergeFailedMessage", "Cannot Merge Current Selection"),
			EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(ActiveTriangleSelection, false);
	FMeshConnectedComponents Components(Mesh);
	Components.FindConnectedTriangles(ActiveTriangleSelection);
	FGroupTopologySelection NewSelection;
	for (const FMeshConnectedComponents::FComponent& Component : Components)
	{
		int32 NewGroupID = Mesh->AllocateTriangleGroup();
		FaceGroupUtil::SetGroupID(*Mesh, Component.Indices, NewGroupID);
		NewSelection.SelectedGroupIDs.Add(NewGroupID);
	}
	
	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshMergeChange", "Merge"), MoveTemp(MeshChange), NewSelection);

	CurrentToolMode = ECurrentToolMode::TransformSelection;
}






void UEditMeshPolygonsTool::ApplyDelete()
{
	if (BeginMeshFaceEditChange() == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnDeleteFailedMessage", "Cannot Delete Current Selection"),
			EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(ActiveTriangleSelection, true);
	FDynamicMeshEditor Editor(Mesh);
	Editor.RemoveTriangles(ActiveTriangleSelection, true);

	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	FGroupTopologySelection NewSelection;
	CompleteMeshEditChange(LOCTEXT("PolyMeshDeleteChange", "Delete"), MoveTemp(MeshChange), NewSelection);

	CurrentToolMode = ECurrentToolMode::TransformSelection;
}



void UEditMeshPolygonsTool::ApplyRecalcNormals()
{
	if (BeginMeshFaceEditChange() == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnRecalcNormalsFailedMessage", "Cannot Recalculate Normals for Current Selection"),
			EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	FDynamicMeshEditor Editor(Mesh);
	FGroupTopologySelection ActiveSelection = SelectionMechanic->GetActiveSelection();
	for (int32 GroupID : ActiveSelection.SelectedGroupIDs)
	{
		ChangeTracker.SaveTriangles(Topology->GetGroupTriangles(GroupID), true);
		Editor.SetTriangleNormals(Topology->GetGroupTriangles(GroupID));
	}

	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshRecalcNormalsChange", "Recalc Normals"), MoveTemp(MeshChange), ActiveSelection);

	CurrentToolMode = ECurrentToolMode::TransformSelection;
}


void UEditMeshPolygonsTool::ApplyFlipNormals()
{
	if (BeginMeshFaceEditChange() == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnFlipNormalsFailedMessage", "Cannot Flip Normals for Current  Selection"),
			EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	FDynamicMeshEditor Editor(Mesh);
	FGroupTopologySelection ActiveSelection = SelectionMechanic->GetActiveSelection();
	for (int32 GroupID : ActiveSelection.SelectedGroupIDs)
	{
		for ( int32 tid : Topology->GetGroupTriangles(GroupID) )
		{ 
			ChangeTracker.SaveTriangle(tid, true);
			Mesh->ReverseTriOrientation(tid);
		}
	}

	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshFlipNormalsChange", "Flip Normals"), MoveTemp(MeshChange), ActiveSelection);

	CurrentToolMode = ECurrentToolMode::TransformSelection;
}


void UEditMeshPolygonsTool::ApplyRetriangulate()
{
	if (BeginMeshFaceEditChange() == false)
	{
		GetToolManager()->DisplayMessage( LOCTEXT("OnRetriangulateFailed", "Cannot Retriangulate Current Selection"), EToolMessageLevel::UserWarning);
		return;
	}

	int32 nCompleted = 0;
	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	FDynamicMeshEditor Editor(Mesh);
	FGroupTopologySelection ActiveSelection = SelectionMechanic->GetActiveSelection();
	for (int32 GroupID : ActiveSelection.SelectedGroupIDs)
	{
		const TArray<int32>& Triangles = Topology->GetGroupTriangles(GroupID);
		ChangeTracker.SaveTriangles(Triangles, true);
		FMeshRegionBoundaryLoops RegionLoops(Mesh, Triangles, true);
		if (!RegionLoops.bFailed && RegionLoops.Loops.Num() == 1 && Triangles.Num() > 1)
		{
			TArray<FMeshRegionBoundaryLoops::VidOverlayMap<FVector2f>> VidUVMaps;
			if (Mesh->HasAttributes())
			{
				const FDynamicMeshAttributeSet* Attributes = Mesh->Attributes();
				for (int i = 0; i < Attributes->NumUVLayers(); ++i)
				{
					VidUVMaps.Emplace();
					RegionLoops.GetLoopOverlayMap(RegionLoops.Loops[0], *Attributes->GetUVLayer(i), VidUVMaps.Last());
				}
			}

			// We don't want to remove isolated vertices while removing triangles because we don't
			// want to throw away boundary verts. However, this means that we'll have to go back
			// through these vertices later to throw away isolated internal verts.
			TArray<int32> OldVertices;
			MeshIndexUtil::TriangleToVertexIDs(Mesh, Triangles, OldVertices);
			Editor.RemoveTriangles(Topology->GetGroupTriangles(GroupID), false);

			RegionLoops.Loops[0].Reverse();
			FSimpleHoleFiller Filler(Mesh, RegionLoops.Loops[0]);
			Filler.FillType = FSimpleHoleFiller::EFillType::PolygonEarClipping;
			Filler.Fill(GroupID);

			// Throw away any of the old verts that are still isolated (they were in the interior of the group)
			Algo::ForEachIf(OldVertices, [Mesh](int32 Vid) { return !Mesh->IsReferencedVertex(Vid); },
				[Mesh](int32 Vid) { Mesh->RemoveVertex(Vid, false, false); } // Don't try to remove attached tris, don't care about bowties
			);

			if (Mesh->HasAttributes())
			{
				const FDynamicMeshAttributeSet* Attributes = Mesh->Attributes();
				for (int i = 0; i < Attributes->NumUVLayers(); ++i)
				{
					RegionLoops.UpdateLoopOverlayMapValidity(VidUVMaps[i], *Attributes->GetUVLayer(i));
				}
				Filler.UpdateAttributes(VidUVMaps);
			}

			nCompleted++;
		}
	}
	if (nCompleted != ActiveSelection.SelectedGroupIDs.Num())
	{
		GetToolManager()->DisplayMessage(LOCTEXT("OnRetriangulateFailures", "Some faces could not be retriangulated"), EToolMessageLevel::UserWarning);
	}

	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshRetriangulateChange", "Retriangulate"), MoveTemp(MeshChange), ActiveSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}




void UEditMeshPolygonsTool::ApplyDecompose()
{
	if (BeginMeshFaceEditChange() == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("OnDecomposeFailed", "Cannot Decompose Current Selection"), EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	FGroupTopologySelection NewSelection;
	for (int32 GroupID : SelectionMechanic->GetActiveSelection().SelectedGroupIDs)
	{
		const TArray<int32>& Triangles = Topology->GetGroupTriangles(GroupID);
		ChangeTracker.SaveTriangles(Triangles, false);
		for (int32 tid : Triangles)
		{
			int32 NewGroupID = Mesh->AllocateTriangleGroup();
			Mesh->SetTriangleGroup(tid, NewGroupID);
			NewSelection.SelectedGroupIDs.Add(NewGroupID);
		}
	}


	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshDecomposeChange", "Decompose"), MoveTemp(MeshChange), NewSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}


void UEditMeshPolygonsTool::ApplyDisconnect()
{
	if (BeginMeshFaceEditChange() == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("OnDisconnectFailed", "Cannot Disconnect Current Selection"), EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	FGroupTopologySelection ActiveSelection = SelectionMechanic->GetActiveSelection();
	TArray<int32> AllTriangles;
	for (int32 GroupID : ActiveSelection.SelectedGroupIDs)
	{
		AllTriangles.Append(Topology->GetGroupTriangles(GroupID));
	}
	ChangeTracker.SaveTriangles(AllTriangles, true);
	FDynamicMeshEditor Editor(Mesh);
	Editor.DisconnectTriangles(AllTriangles, false);

	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshDisconnectChange", "Disconnect"), MoveTemp(MeshChange), ActiveSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}




void UEditMeshPolygonsTool::ApplyCollapseEdge()
{
	// AAAHHH cannot do because of overlays!
	return;

	if (SelectionMechanic->GetActiveSelection().SelectedEdgeIDs.Num() != 1 || BeginMeshEdgeEditChange() == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnEdgeColllapseFailed", "Cannot Collapse current selection"),
			EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	//const TArray<int32>& EdgeIDs = ActiveEdgeSelection[0].EdgeIDs;
	//for (int32 eid : EdgeIDs)
	//{
	//	if (Mesh->IsEdge(eid))
	//	{
	//		FIndex2i EdgeVerts = Mesh->GetEdgeV(eid);
	//		ChangeTracker.SaveVertexOneRingTriangles(EdgeVerts.A, true);
	//		ChangeTracker.SaveVertexOneRingTriangles(EdgeVerts.B, true);
	//		FDynamicMesh3::FEdgeCollapseInfo CollapseInfo;
	//		Mesh->CollapseEdge()
	//	}
	//}

	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	FGroupTopologySelection NewSelection;
	CompleteMeshEditChange(LOCTEXT("PolyMeshEdgeCollapseChange", "Collapse"), MoveTemp(MeshChange), NewSelection);

	CurrentToolMode = ECurrentToolMode::TransformSelection;
}




void UEditMeshPolygonsTool::ApplyWeldEdges()
{
	bool bValidInput = SelectionMechanic->GetActiveSelection().SelectedEdgeIDs.Num() == 2 && BeginMeshBoundaryEdgeEditChange(true);
	bValidInput = bValidInput && ActiveEdgeSelection.Num() == 2;		// one of the initial edges may not have been valid
	if ( bValidInput == false )
	{
		GetToolManager()->DisplayMessage( LOCTEXT("OnWeldEdgesFailed", "Cannot Weld current selection"), EToolMessageLevel::UserWarning);
		CancelMeshEditChange();
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();

	int32 EdgeIDA = Topology->GetGroupEdgeEdges(ActiveEdgeSelection[0].EdgeTopoID)[0];
	int32 EdgeIDB = Topology->GetGroupEdgeEdges(ActiveEdgeSelection[1].EdgeTopoID)[0];
	FIndex2i EdgeVerts[2] = { Mesh->GetEdgeV(EdgeIDA), Mesh->GetEdgeV(EdgeIDB) };
	for (int j = 0; j < 2; ++j)
	{
		ChangeTracker.SaveVertexOneRingTriangles(EdgeVerts[j].A, true);
		ChangeTracker.SaveVertexOneRingTriangles(EdgeVerts[j].B, true);
	}

	FDynamicMesh3::FMergeEdgesInfo MergeInfo;
	EMeshResult Result = Mesh->MergeEdges(EdgeIDB, EdgeIDA, MergeInfo);
	if (Result != EMeshResult::Ok)
	{
		GetToolManager()->DisplayMessage( LOCTEXT("OnWeldEdgesFailed", "Cannot Weld current selection"), EToolMessageLevel::UserWarning);
		CancelMeshEditChange();
		return;
	}

	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	FGroupTopologySelection NewSelection;
	CompleteMeshEditChange(LOCTEXT("PolyMeshWeldEdgeChange", "Weld Edges"), MoveTemp(MeshChange), NewSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}


void UEditMeshPolygonsTool::ApplyStraightenEdges()
{
	if (BeginMeshEdgeEditChange() == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("OnStraightenEdgesFailed", "Cannot Straighten current selection"), EToolMessageLevel::UserWarning);
		CancelMeshEditChange();
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();

	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();

	for (const FSelectedEdge& Edge : ActiveEdgeSelection)
	{
		const TArray<int32>& EdgeVerts = Topology->GetGroupEdgeVertices(Edge.EdgeTopoID);
		int32 NumV = EdgeVerts.Num();
		if ( NumV > 2 )
		{
			ChangeTracker.SaveVertexOneRingTriangles(EdgeVerts, true);
			FVector3d A(Mesh->GetVertex(EdgeVerts[0])), B(Mesh->GetVertex(EdgeVerts[NumV-1]));
			TArray<double> VtxArcLengths;
			double EdgeArcLen = Topology->GetEdgeArcLength(Edge.EdgeTopoID, &VtxArcLengths);
			for (int k = 1; k < NumV-1; ++k)
			{
				double t = VtxArcLengths[k] / EdgeArcLen;
				Mesh->SetVertex(EdgeVerts[k], FVector3d::Lerp(A, B, t));
			}
		}
	}

	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	FGroupTopologySelection NewSelection;
	CompleteMeshEditChange(LOCTEXT("PolyMeshStraightenEdgeChange", "Straighten Edges"), MoveTemp(MeshChange), NewSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}



void UEditMeshPolygonsTool::ApplyFillHole()
{
	if (BeginMeshBoundaryEdgeEditChange(false) == false)
	{
		GetToolManager()->DisplayMessage( LOCTEXT("OnEdgeFillFailed", "Cannot Fill current selection"), EToolMessageLevel::UserWarning);
		CancelMeshEditChange();
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	FGroupTopologySelection NewSelection;
	for (FSelectedEdge& FillEdge : ActiveEdgeSelection)
	{
		if (Mesh->IsBoundaryEdge(FillEdge.EdgeIDs[0]))		// may no longer be boundary due to previous fill
		{
			FMeshBoundaryLoops BoundaryLoops(Mesh);
			int32 LoopID = BoundaryLoops.FindLoopContainingEdge(FillEdge.EdgeIDs[0]);
			if (LoopID >= 0)
			{
				FEdgeLoop& Loop = BoundaryLoops.Loops[LoopID];
				FSimpleHoleFiller Filler(Mesh, Loop);
				Filler.FillType = FSimpleHoleFiller::EFillType::PolygonEarClipping;
				int32 NewGroupID = Mesh->AllocateTriangleGroup();
				Filler.Fill(NewGroupID);
				NewSelection.SelectedGroupIDs.Add(NewGroupID);

				// Compute normals and UVs
				if (Mesh->HasAttributes())
				{
					TArray<FVector3d> VertexPositions;
					Loop.GetVertices(VertexPositions);
					FVector3d PlaneOrigin;
					FVector3d PlaneNormal;
					PolygonTriangulation::ComputePolygonPlane<double>(VertexPositions, PlaneNormal, PlaneOrigin);

					FDynamicMeshEditor Editor(Mesh);
					FFrame3d ProjectionFrame(PlaneOrigin, PlaneNormal);
					Editor.SetTriangleNormals(Filler.NewTriangles);
					Editor.SetTriangleUVsFromProjection(Filler.NewTriangles, ProjectionFrame, UVScaleFactor);
				}
			}
		}
	}

	// emit undo
	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshFillHoleChange", "Fill Hole"), MoveTemp(MeshChange), NewSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}




void UEditMeshPolygonsTool::ApplyPokeSingleFace()
{
	if (BeginMeshFaceEditChange() == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("OnPokeFailedMessage", "Cannot Poke Current Selection"), EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(ActiveTriangleSelection, false);
	FGroupTopologySelection NewSelection;
	for (int32 tid : ActiveTriangleSelection)
	{
		FDynamicMesh3::FPokeTriangleInfo PokeInfo;
		NewSelection.SelectedGroupIDs.Add(tid);
		if (Mesh->PokeTriangle(tid, PokeInfo) == EMeshResult::Ok)
		{
			NewSelection.SelectedGroupIDs.Add(PokeInfo.NewTriangles.A);
			NewSelection.SelectedGroupIDs.Add(PokeInfo.NewTriangles.B);
		}
	}

	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshPokeChange", "Poke Faces"), MoveTemp(MeshChange), NewSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}



void UEditMeshPolygonsTool::ApplyFlipSingleEdge()
{
	if (BeginMeshEdgeEditChange() == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("OnFlipFailedMessage", "Cannot Flip Current Selection"), EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FGroupTopologySelection ActiveSelection = SelectionMechanic->GetActiveSelection();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	for (FSelectedEdge& Edge : ActiveEdgeSelection)
	{
		int32 eid = Edge.EdgeIDs[0];
		if (Mesh->IsEdge(eid) && Mesh->IsBoundaryEdge(eid) == false && Mesh->Attributes()->IsSeamEdge(eid) == false)
		{
			FIndex2i et = Mesh->GetEdgeT(eid);
			ChangeTracker.SaveTriangle(et.A, true);
			ChangeTracker.SaveTriangle(et.B, true);
			FDynamicMesh3::FEdgeFlipInfo FlipInfo;
			Mesh->FlipEdge(eid, FlipInfo);
		}
	}

	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshFlipChange", "Flip Edges"), MoveTemp(MeshChange), ActiveSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}

void UEditMeshPolygonsTool::ApplyCollapseSingleEdge()
{
	if (BeginMeshEdgeEditChange() == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("OnCollapseFailedMessage", "Cannot Collapse Current Selection"), EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FGroupTopologySelection ActiveSelection = SelectionMechanic->GetActiveSelection();
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	TSet<int32> ValidEdgeIDs;
	for (FSelectedEdge& Edge : ActiveEdgeSelection)
	{
		int32 eid = Edge.EdgeIDs[0];
		if (Mesh->IsEdge(eid) && Mesh->Attributes()->IsSeamEdge(eid) == false)
		{
			ValidEdgeIDs.Add(eid);
		}
	}
	TSet<int32> DoneEdgeIDs;
	for (int32 eid : ValidEdgeIDs)
	{
		if (DoneEdgeIDs.Contains(eid) == false && Mesh->IsEdge(eid))
		{
			FIndex2i ev = Mesh->GetEdgeV(eid);
			ChangeTracker.SaveVertexOneRingTriangles(ev.A, true);
			ChangeTracker.SaveVertexOneRingTriangles(ev.B, true);
			FDynamicMesh3::FEdgeCollapseInfo CollapseInfo;
			if (Mesh->CollapseEdge(ev.A, ev.B, CollapseInfo) == EMeshResult::Ok)
			{
				DoneEdgeIDs.Add(eid);
				DoneEdgeIDs.Add(CollapseInfo.RemovedEdges.A);
				DoneEdgeIDs.Add(CollapseInfo.RemovedEdges.B);
			}
		}
	}

	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshCollapseChange", "Collapse Edges"), MoveTemp(MeshChange), FGroupTopologySelection());
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}

void UEditMeshPolygonsTool::ApplySplitSingleEdge()
{
	if (BeginMeshEdgeEditChange() == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("OnSplitFailedMessage", "Cannot Split Current Selection"), EToolMessageLevel::UserWarning);
		return;
	}

	FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	FGroupTopologySelection NewSelection;
	FDynamicMeshChangeTracker ChangeTracker(Mesh);
	ChangeTracker.BeginChange();
	for (FSelectedEdge& Edge : ActiveEdgeSelection)
	{
		int32 eid = Edge.EdgeIDs[0];
		if (Mesh->IsEdge(eid))
		{
			FIndex2i et = Mesh->GetEdgeT(eid);
			ChangeTracker.SaveTriangle(et.A, true);
			NewSelection.SelectedGroupIDs.Add(et.A);
			if (et.B != FDynamicMesh3::InvalidID)
			{
				ChangeTracker.SaveTriangle(et.B, true);
				NewSelection.SelectedGroupIDs.Add(et.B);
			}
			FDynamicMesh3::FEdgeSplitInfo SplitInfo;
			if (Mesh->SplitEdge(eid, SplitInfo) == EMeshResult::Ok)
			{
				NewSelection.SelectedGroupIDs.Add(SplitInfo.NewTriangles.A);
				if (SplitInfo.NewTriangles.B != FDynamicMesh3::InvalidID)
				{
					NewSelection.SelectedGroupIDs.Add(SplitInfo.NewTriangles.A);
				}
			}
		}
	}

	TUniquePtr<FMeshChange> MeshChange = MakeUnique<FMeshChange>(ChangeTracker.EndChange());
	CompleteMeshEditChange(LOCTEXT("PolyMeshSplitChange", "Split Edges"), MoveTemp(MeshChange), NewSelection);
	CurrentToolMode = ECurrentToolMode::TransformSelection;
}




bool UEditMeshPolygonsTool::BeginMeshFaceEditChange()
{
	check(EditPreview == nullptr);

	ActiveTriangleSelection.Reset();

	// need some selected faces
	const FGroupTopologySelection& ActiveSelection = SelectionMechanic->GetActiveSelection();
	Topology->GetSelectedTriangles(ActiveSelection, ActiveTriangleSelection);
	if (ActiveSelection.SelectedGroupIDs.Num() == 0 || ActiveTriangleSelection.Num() == 0)
	{
		return false;
	}

	const FDynamicMesh3* Mesh = DynamicMeshComponent->GetMesh();
	ActiveSelectionBounds = FAxisAlignedBox3d::Empty();
	for (int tid : ActiveTriangleSelection)
	{
		ActiveSelectionBounds.Contain(Mesh->GetTriBounds(tid));
	}

	// world and local frames
	ActiveSelectionFrameLocal = Topology->GetSelectionFrame(ActiveSelection);
	ActiveSelectionFrameWorld = ActiveSelectionFrameLocal;
	ActiveSelectionFrameWorld.Transform(WorldTransform);

	return true;
}


bool UEditMeshPolygonsTool::BeginMeshFaceEditChangeWithPreview()
{
	bool bOK = BeginMeshFaceEditChange();
	if (bOK)
	{
		EditPreview = NewObject<UPolyEditPreviewMesh>(this);
		EditPreview->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), FTransform::Identity);
		UpdateEditPreviewMaterials(EPreviewMaterialType::PreviewMaterial);
		EditPreview->EnableWireframe(true);

		// hide gizmo and selected triangles
		MultiTransformer->SetGizmoVisibility(false);
		DynamicMeshComponent->SetSecondaryBuffersVisibility(false);
	}
	return bOK;
}


void UEditMeshPolygonsTool::CompleteMeshEditChange(
	const FText& TransactionLabel, 
	TUniquePtr<FToolCommandChange> EditChange,
	const FGroupTopologySelection& OutputSelection)
{
	// open top-level transaction
	GetToolManager()->BeginUndoTransaction(TransactionLabel);

	// clear current selection
	SelectionMechanic->BeginChange();
	SelectionMechanic->ClearSelection();
	GetToolManager()->EmitObjectChange(SelectionMechanic, SelectionMechanic->EndChange(), LOCTEXT("PolyMeshExtrudeChangeClearSelection", "ClearSelection"));

	// emit the pre-edit change
	GetToolManager()->EmitObjectChange(this, MakeUnique<FEditPolygonsTopologyPreEditChange>(), LOCTEXT("PolyMeshExtrudeChangePreEdit", "PreEdit"));

	// emit the mesh change
	GetToolManager()->EmitObjectChange(DynamicMeshComponent, MoveTemp(EditChange), TransactionLabel);

	// emit the post-edit change
	GetToolManager()->EmitObjectChange(this, MakeUnique<FEditPolygonsTopologyPostEditChange>(), TransactionLabel);
	// call this (PostEditChange will do this)
	AfterTopologyEdit();
	// increment topology-change counter
	ModifiedTopologyCounter++;

	// set output selection
	if (OutputSelection.IsEmpty() == false)
	{
		SelectionMechanic->BeginChange();
		SelectionMechanic->SetSelection(OutputSelection);
		GetToolManager()->EmitObjectChange(SelectionMechanic, SelectionMechanic->EndChange(), LOCTEXT("PolyMeshExtrudeChangeSetSelection", "SetSelection"));
	}

	// complete the transaction
	GetToolManager()->EndUndoTransaction();

	// clean up preview mesh, hiding of things, etc
	if (EditPreview != nullptr)
	{
		EditPreview->Disconnect();
		EditPreview = nullptr;
	}
	DynamicMeshComponent->SetSecondaryBuffersVisibility(true);

	CurrentOperationTimestamp++;
}


bool UEditMeshPolygonsTool::BeginMeshEdgeEditChange()
{
	return BeginMeshEdgeEditChange([](int32) {return true; });
}

bool UEditMeshPolygonsTool::BeginMeshBoundaryEdgeEditChange(bool bOnlySimple)
{
	if (bOnlySimple)
	{
		return BeginMeshEdgeEditChange(
			[&](int32 GroupEdgeID) { return Topology->IsBoundaryEdge(GroupEdgeID) && Topology->IsSimpleGroupEdge(GroupEdgeID); });
	}
	else
	{
		return BeginMeshEdgeEditChange(
			[&](int32 GroupEdgeID) { return Topology->IsBoundaryEdge(GroupEdgeID); });
	}
}

bool UEditMeshPolygonsTool::BeginMeshEdgeEditChange(TFunctionRef<bool(int32)> GroupEdgeIDFilterFunc)
{
	check(EditPreview == nullptr);

	ActiveEdgeSelection.Reset();

	const FGroupTopologySelection& ActiveSelection = SelectionMechanic->GetActiveSelection();
	int NumEdges = ActiveSelection.SelectedEdgeIDs.Num();
	if (NumEdges == 0)
	{
		return false;
	}
	ActiveEdgeSelection.Reserve(NumEdges);
	for (int32 EdgeID : ActiveSelection.SelectedEdgeIDs)
	{
		if (GroupEdgeIDFilterFunc(EdgeID))
		{
			FSelectedEdge& Edge = ActiveEdgeSelection.Emplace_GetRef();
			Edge.EdgeTopoID = EdgeID;
			Edge.EdgeIDs = Topology->GetGroupEdgeEdges(EdgeID);
		}
	}

	return ActiveEdgeSelection.Num() > 0;
}



void UEditMeshPolygonsTool::CancelMeshEditChange()
{
	if (EditPreview != nullptr)
	{
		EditPreview->Disconnect();
		EditPreview = nullptr;
	}
	DynamicMeshComponent->SetSecondaryBuffersVisibility(true);

	// disable any mechanics
	ExtrudeHeightMechanic = nullptr;
	CurveDistMechanic = nullptr;
	SurfacePathMechanic = nullptr;

	// hide properties that might be visible
	SetToolPropertySourceEnabled(ExtrudeProperties, false);
	SetToolPropertySourceEnabled(OffsetProperties, false);
	SetToolPropertySourceEnabled(InsetProperties, false);
	SetToolPropertySourceEnabled(OutsetProperties, false);
	SetToolPropertySourceEnabled(CutProperties, false);
	SetToolPropertySourceEnabled(SetUVProperties, false);
	SetActionButtonPanelsVisible(true);

	CurrentToolMode = ECurrentToolMode::TransformSelection;
}




void UEditMeshPolygonsTool::UpdateEditPreviewMaterials(EPreviewMaterialType MaterialType)
{
	if (EditPreview != nullptr)
	{
		if (MaterialType == EPreviewMaterialType::SourceMaterials)
		{
			EditPreview->ClearOverrideRenderMaterial();
			EditPreview->SetMaterials(DynamicMeshComponent->GetMaterials());
		}
		else if (MaterialType == EPreviewMaterialType::PreviewMaterial)
		{
			EditPreview->ClearOverrideRenderMaterial();
			EditPreview->SetMaterial(
				ToolSetupUtil::GetSelectionMaterial(FLinearColor(0.8f, 0.75f, 0.0f), GetToolManager()));
		}
		else if (MaterialType == EPreviewMaterialType::UVMaterial)
		{
			UMaterial* CheckerMaterialBase = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/CheckerMaterial"));
			if (CheckerMaterialBase != nullptr)
			{
				UMaterialInstanceDynamic* CheckerMaterial = UMaterialInstanceDynamic::Create(CheckerMaterialBase, NULL);
				CheckerMaterial->SetScalarParameterValue("Density", 1);
				EditPreview->SetOverrideRenderMaterial(CheckerMaterial);
			}
		}

		CurrentPreviewMaterial = MaterialType;
	}
}





void UEditMeshPolygonsTool::SetActionButtonPanelsVisible(bool bVisible)
{
	if (bTriangleMode == false)
	{
		if (EditActions)
		{
			SetToolPropertySourceEnabled(EditActions, bVisible);
		}
		if (EditEdgeActions)
		{
			SetToolPropertySourceEnabled(EditEdgeActions, bVisible);
		}
		if (EditUVActions)
		{
			SetToolPropertySourceEnabled(EditUVActions, bVisible);
		}
	}
	else
	{
		if (EditActions_Triangles)
		{
			SetToolPropertySourceEnabled(EditActions_Triangles, bVisible);
		}
		if (EditEdgeActions_Triangles)
		{
			SetToolPropertySourceEnabled(EditEdgeActions_Triangles, bVisible);
		}
	}
}




void FEditPolygonsTopologyPreEditChange::Apply(UObject* Object)
{
}
void FEditPolygonsTopologyPreEditChange::Revert(UObject* Object)
{
	Cast<UEditMeshPolygonsTool>(Object)->AfterTopologyEdit();
	Cast<UEditMeshPolygonsTool>(Object)->ModifiedTopologyCounter--;
}
FString FEditPolygonsTopologyPreEditChange::ToString() const 
{ 
	return TEXT("FEditPolygonsTopologyPreEditChange"); 
}


void FEditPolygonsTopologyPostEditChange::Apply(UObject* Object)
{
	Cast<UEditMeshPolygonsTool>(Object)->AfterTopologyEdit();
	Cast<UEditMeshPolygonsTool>(Object)->ModifiedTopologyCounter++;
}
void FEditPolygonsTopologyPostEditChange::Revert(UObject* Object)
{
}
FString FEditPolygonsTopologyPostEditChange::ToString() const 
{ 
	return TEXT("FEditPolygonsTopologyPostEditChange"); 
}



void FBeginInteractivePolyEditChange::Revert(UObject* Object)
{
	Cast<UEditMeshPolygonsTool>(Object)->CancelMeshEditChange();
	bHaveDoneUndo = true;
}
bool FBeginInteractivePolyEditChange::HasExpired(UObject* Object) const
{
	return bHaveDoneUndo || (Cast<UEditMeshPolygonsTool>(Object)->CheckInOperation(OperationTimestamp) == false);
}
FString FBeginInteractivePolyEditChange::ToString() const
{
	return TEXT("FBeginInteractivePolyEditChange");
}


#undef LOCTEXT_NAMESPACE
