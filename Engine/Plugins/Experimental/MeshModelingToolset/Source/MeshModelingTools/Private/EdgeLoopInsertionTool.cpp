// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdgeLoopInsertionTool.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "CuttingOps/EdgeLoopInsertionOp.h"
#include "DynamicMeshToMeshDescription.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "InteractiveToolManager.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Operations/GroupEdgeInserter.h"
#include "ToolBuilderUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UEdgeLoopInsertionTool"

USingleSelectionMeshEditingTool* UEdgeLoopInsertionToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UEdgeLoopInsertionTool>(SceneState.ToolManager);
}

TUniquePtr<FDynamicMeshOperator> UEdgeLoopInsertionOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FEdgeLoopInsertionOp> Op = MakeUnique<FEdgeLoopInsertionOp>();

	Op->OriginalMesh = Tool->CurrentMesh;
	Op->OriginalTopology = Tool->CurrentTopology;
	Op->SetTransform(Cast<IPrimitiveComponentBackedTarget>(Tool->Target)->GetWorldTransform());

	if (Tool->bShowingBaseMesh)
	{
		// Return op with no input lengths so that we get the original mesh back.
		return Op;
	}

	if (Tool->Settings->InsertionMode == EEdgeLoopInsertionMode::PlaneCut)
	{
		Op->Mode = FGroupEdgeInserter::EInsertionMode::PlaneCut;
	}
	else
	{
		Op->Mode = FGroupEdgeInserter::EInsertionMode::Retriangulate;
	}

	Op->VertexTolerance = Tool->Settings->VertexTolerance;
	
	Op->GroupEdgeID = Tool->InputGroupEdgeID;

	Op->StartCornerID = Tool->Settings->bFlipOffsetDirection ?
		Op->OriginalTopology->Edges[Op->GroupEdgeID].EndpointCorners.B
		: Op->OriginalTopology->Edges[Op->GroupEdgeID].EndpointCorners.A;

	// Set up the inputs
	if (Tool->Settings->PositionMode == EEdgeLoopPositioningMode::Even)
	{
		int32 NumLoops = Tool->Settings->NumLoops;
		for (int32 i = 0; i < NumLoops; ++i)
		{
			Op->InputLengths.Add((i + 1.0) / (NumLoops + 1.0));
		}
	}
	else if (Tool->Settings->bInteractive)
	{
		Op->InputLengths.Add(Tool->InteractiveInputLength);
	}
	else if (Tool->Settings->PositionMode == EEdgeLoopPositioningMode::ProportionOffset)
	{
		Op->InputLengths.Add(Tool->Settings->ProportionOffset);
	}
	else
	{
		Op->InputLengths.Add(Tool->Settings->DistanceOffset);
	}

	Op->bInputsAreProportions = (Tool->Settings->PositionMode == EEdgeLoopPositioningMode::Even 
		|| Tool->Settings->PositionMode == EEdgeLoopPositioningMode::ProportionOffset);

	return Op;
}

void UEdgeLoopInsertionTool::Setup()
{
	USingleSelectionTool::Setup();

	if (!Target)
	{
		return;
	}

	SetToolDisplayName(LOCTEXT("ToolName", "Insert PolyLoop"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("EdgeLoopInsertionToolDescription", "Click an edge to insert an edge loop passing across that edge. Edge loops follow a sequence of quad-like polygroups."),
		EToolMessageLevel::UserNotification);

	// Initialize the mesh that we'll be operating on
	CurrentMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(Cast<IMeshDescriptionProvider>(Target)->GetMeshDescription(), *CurrentMesh);
	CurrentTopology = MakeShared<FGroupTopology, ESPMode::ThreadSafe>(CurrentMesh.Get(), true);
	MeshSpatial.SetMesh(CurrentMesh.Get(), true);

	// Set up properties
	Settings = NewObject<UEdgeLoopInsertionProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	// Register ourselves to receive clicks and hover
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	SetupPreview();

	// Draws the old group topology
	ExistingEdgesRenderer.LineColor = FLinearColor::Red;
	ExistingEdgesRenderer.LineThickness = 2.0;

	// Draws the new group edges that are added
	PreviewEdgeRenderer.LineColor = FLinearColor::Green;
	PreviewEdgeRenderer.LineThickness = 2.0;

	// Highlights non-quad groups that stop the loop;
	ProblemTopologyRenderer.LineColor = FLinearColor::Red;
	ProblemTopologyRenderer.LineThickness = 3.0;
	ProblemTopologyRenderer.DepthBias = 1.0;

	// Set up the topology selector, which we use to select the edges where we insert the loops
	TopologySelector.Initialize(CurrentMesh.Get(), CurrentTopology.Get());
	TopologySelector.SetSpatialSource([this]() {return &MeshSpatial; });
	TopologySelector.PointsWithinToleranceTest = [this](const FVector3d& Position1, const FVector3d& Position2, double TolScale) {
		UE::Geometry::FTransform3d Transform(Cast<IPrimitiveComponentBackedTarget>(Target)->GetWorldTransform());
		return ToolSceneQueriesUtil::PointSnapQuery(CameraState, Transform.TransformPosition(Position1), Transform.TransformPosition(Position2),
			ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD() * TolScale);
	};
	TopologySelectorSettings.bEnableEdgeHits = true;
	TopologySelectorSettings.bEnableFaceHits = false;
	TopologySelectorSettings.bEnableCornerHits = false;
}

void UEdgeLoopInsertionTool::SetupPreview()
{
	UEdgeLoopInsertionOperatorFactory* OpFactory = NewObject<UEdgeLoopInsertionOperatorFactory>();
	OpFactory->Tool = this;

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory);
	Preview->Setup(TargetWorld, OpFactory);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::AutoCalculated);

	FComponentMaterialSet MaterialSet;
	Cast<IMaterialProvider>(Target)->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials(MaterialSet.Materials, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));

	// Whenever we get a new result from the op, we need to extract the preview edges so that
	// we can draw them if we want to, and the additional outputs we need (changed triangles and
	// topology).
	Preview->OnOpCompleted.AddLambda([this](const FDynamicMeshOperator* UncastOp) {
		const FEdgeLoopInsertionOp* Op = static_cast<const FEdgeLoopInsertionOp*>(UncastOp);

		bLastComputeSucceeded = Op->bSucceeded;
		LatestOpTopologyResult.Reset();
		PreviewEdges.Reset();
		LatestOpChangedTids.Reset();

		if (bLastComputeSucceeded)
		{
			Op->GetLoopEdgeLocations(PreviewEdges);
			LatestOpTopologyResult = Op->ResultTopology;
			LatestOpChangedTids = Op->ChangedTids;
		}

		// Regardless of success, extract things for highlighting any non-quads that stopped our loop.
		ProblemTopologyEdges.Reset();
		ProblemTopologyVerts.Reset();
		for (int32 GroupEdgeID : Op->ProblemGroupEdgeIDs)
		{
			for (int32 Eid : CurrentTopology->GetGroupEdgeEdges(GroupEdgeID))
			{
				TPair<FVector3d, FVector3d> Endpoints;
				CurrentMesh->GetEdgeV(Eid, Endpoints.Key, Endpoints.Value);
				ProblemTopologyEdges.Add(MoveTemp(Endpoints));
			}
			FGroupTopology::FGroupEdge& GroupEdge = CurrentTopology->Edges[GroupEdgeID];
			if (GroupEdge.EndpointCorners.A != FDynamicMesh3::InvalidID)
			{
				ProblemTopologyVerts.AddUnique(CurrentMesh->GetVertex(CurrentTopology->Corners[GroupEdge.EndpointCorners.A].VertexID));
				ProblemTopologyVerts.AddUnique(CurrentMesh->GetVertex(CurrentTopology->Corners[GroupEdge.EndpointCorners.B].VertexID));
			}
		}
		});

	// In case of failure, we want to hide the broken preview, since we wouldn't accept it on
	// a click. Note that this can't be fired OnOpCompleted because the preview is updated
	// with the op result after that callback, which would undo the reset. The preview edge
	// extraction can't be lumped in here because it needs the op rather than the preview object.
	Preview->OnMeshUpdated.AddLambda([this]( UMeshOpPreviewWithBackgroundCompute*) {
		if (!bLastComputeSucceeded)
		{
			Preview->PreviewMesh->UpdatePreview(CurrentMesh.Get());
		}
		});

	// Set initial preview to unprocessed mesh, so that things don't disappear initially
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	Preview->PreviewMesh->UpdatePreview(CurrentMesh.Get());
	Preview->PreviewMesh->SetTransform(TargetComponent->GetWorldTransform());
	Preview->PreviewMesh->EnableWireframe(Settings->bWireframe);
	Preview->SetVisibility(true);
	ClearPreview();

	TargetComponent->SetOwnerVisibility(false);
}

void UEdgeLoopInsertionTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Set visibility before committing so that it doesn't get saved as false.
	Cast<IPrimitiveComponentBackedTarget>(Target)->SetOwnerVisibility(true);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("EdgeLoopInsertionToolTransactionName", "Edge Loop Tool"));
		Cast<IMeshDescriptionCommitter>(Target)->CommitMeshDescription([this](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(CurrentMesh.Get(), *CommitParams.MeshDescriptionOut);
		});
		GetToolManager()->EndUndoTransaction();
	}

	Settings->SaveProperties(this);
	Preview->Shutdown();
	CurrentMesh.Reset();
	CurrentTopology.Reset();
	ExpireChanges();
}

void UEdgeLoopInsertionTool::OnTick(float DeltaTime)
{
	if (Preview)
	{
		Preview->Tick(DeltaTime);

		if (bWaitingForInsertionCompletion && Preview->HaveValidResult())
		{
			if (bLastComputeSucceeded)
			{
				FDynamicMeshChangeTracker ChangeTracker(CurrentMesh.Get());
				ChangeTracker.BeginChange();
				ChangeTracker.SaveTriangles(*LatestOpChangedTids, true /*bSaveVertices*/);

				// Update current mesh and topology
				CurrentMesh->Copy(*Preview->PreviewMesh->GetMesh(), true, true, true, true);
				*CurrentTopology = *LatestOpTopologyResult;
				CurrentTopology->RetargetOnClonedMesh(CurrentMesh.Get());
				MeshSpatial.Build();
				TopologySelector.Invalidate(true, true);

				// Emit transaction
				GetToolManager()->BeginUndoTransaction(LOCTEXT("EdgeLoopInsertionTransactionName", "Edge Loop Insertion"));
				GetToolManager()->EmitObjectChange(this, MakeUnique<FEdgeLoopInsertionChange>(ChangeTracker.EndChange(), CurrentChangeStamp), 
					LOCTEXT("EdgeLoopInsertion", "Edge Loop Insertion"));
				GetToolManager()->EndUndoTransaction();
			}

			PreviewEdges.Reset();
			ProblemTopologyEdges.Reset();
			ProblemTopologyVerts.Reset();

			bWaitingForInsertionCompletion = false;
		}
	}
}

void UEdgeLoopInsertionTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	// Draw the existing group edges
	FViewCameraState RenderCameraState = RenderAPI->GetCameraState();
	ExistingEdgesRenderer.BeginFrame(RenderAPI, RenderCameraState);
	ExistingEdgesRenderer.SetTransform(Preview->PreviewMesh->GetTransform());

	for (const FGroupTopology::FGroupEdge& Edge : CurrentTopology->Edges)
	{
		FVector3d A, B;
		for (int32 eid : Edge.Span.Edges)
		{
			CurrentMesh->GetEdgeV(eid, A, B);
			ExistingEdgesRenderer.DrawLine(A, B);
		}
	}
	ExistingEdgesRenderer.EndFrame();

	// Draw the preview edges
	PreviewEdgeRenderer.BeginFrame(RenderAPI, RenderCameraState);
	PreviewEdgeRenderer.SetTransform(Preview->PreviewMesh->GetTransform());
	for (TPair<FVector3d, FVector3d>& EdgeVerts : PreviewEdges)
	{
		PreviewEdgeRenderer.DrawLine(EdgeVerts.Key, EdgeVerts.Value);
	}
	PreviewEdgeRenderer.EndFrame();

	if (Settings->bHighlightProblemGroups)
	{
		// Highlight any non-quad groups that stopped the loop.
		ProblemTopologyRenderer.BeginFrame(RenderAPI, RenderCameraState);
		ProblemTopologyRenderer.SetTransform(Preview->PreviewMesh->GetTransform());
		for (TPair<FVector3d, FVector3d>& EdgeVerts : ProblemTopologyEdges)
		{
			ProblemTopologyRenderer.DrawLine(EdgeVerts.Key, EdgeVerts.Value);
		}
		for (FVector3d& Vert : ProblemTopologyVerts)
		{
			ProblemTopologyRenderer.DrawViewFacingX(Vert, ProblemVertTickWidth);
		}
		ProblemTopologyRenderer.EndFrame();
	}
}

bool UEdgeLoopInsertionTool::CanAccept() const
{
	return !bWaitingForInsertionCompletion;
}

void UEdgeLoopInsertionTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	PreviewEdges.Reset();
	Preview->PreviewMesh->EnableWireframe(Settings->bWireframe);
	Preview->InvalidateResult();
}

FInputRayHit UEdgeLoopInsertionTool::HitTest(const FRay& WorldRay)
{
	FInputRayHit Hit;

	// See if we hit an edge
	FTransform3d LocalToWorld(Cast<IPrimitiveComponentBackedTarget>(Target)->GetWorldTransform());
	FRay3d LocalRay(LocalToWorld.InverseTransformPosition((FVector3d)WorldRay.Origin),
		LocalToWorld.InverseTransformVector((FVector3d)WorldRay.Direction), false);
	FGroupTopologySelection Selection;
	FVector3d Position, Normal;
	if (TopologySelector.FindSelectedElement(
		TopologySelectorSettings, LocalRay, Selection, Position, Normal))
	{
		// TODO: We could check here that the edge has some quad-like neighbor. For now we
		// just check that the edge isn't a loop unto itself (in which case the neighbor groups
		// are definitely not quad-like).
		int32 GroupEdgeID = Selection.GetASelectedEdgeID();
		const FGroupTopology::FGroupEdge& GroupEdge = CurrentTopology->Edges[GroupEdgeID];
		if (GroupEdge.EndpointCorners.A != FDynamicMesh3::InvalidID)
		{
			Hit = FInputRayHit(LocalRay.Project(Position));
		}
	}

	return Hit;
}

bool UEdgeLoopInsertionTool::UpdateHoveredItem(const FRay& WorldRay)
{
	// Check that we hit an edge
	FTransform3d LocalToWorld(Cast<IPrimitiveComponentBackedTarget>(Target)->GetWorldTransform());
	FRay3d LocalRay(LocalToWorld.InverseTransformPosition((FVector3d)WorldRay.Origin),
		LocalToWorld.InverseTransformVector((FVector3d)WorldRay.Direction), false);

	FGroupTopologySelection Selection;
	FVector3d Position, Normal;
	int32 EdgeSegmentID;
	if (!TopologySelector.FindSelectedElement(
		TopologySelectorSettings, LocalRay, Selection, Position, Normal, &EdgeSegmentID))
	{
		ClearPreview();
		return false; // Didn't hit anything
	}

	// Check that the edge has endpoints
	int32 GroupEdgeID = Selection.GetASelectedEdgeID();
	FGroupTopology::FGroupEdge GroupEdge = CurrentTopology->Edges[GroupEdgeID];
	if (GroupEdge.EndpointCorners.A == FDynamicMesh3::InvalidID)
	{
		ClearPreview();
		return false; // Edge definitely doesn't have quad-like neighbors
	}

	if (Settings->PositionMode == EEdgeLoopPositioningMode::Even)
	{
		// In even mode and non-interactive mode, all that matters is the group edge 
		// that we're hovering, not where our pointer is exactly.
		ConditionallyUpdatePreview(GroupEdgeID);
		return true;
	}
	if (!Settings->bInteractive)
	{
		// Don't try to insert a loop when our inputs don't make sense.
		double TotalLength = CurrentTopology->GetEdgeArcLength(GroupEdgeID);
		if (Settings->PositionMode == EEdgeLoopPositioningMode::DistanceOffset)
		{
			if (Settings->DistanceOffset > TotalLength || Settings->DistanceOffset <= Settings->VertexTolerance)
			{
				ClearPreview();
				return false;
			}
		}
		else if (Settings->PositionMode == EEdgeLoopPositioningMode::ProportionOffset)
		{
			if (abs(Settings->ProportionOffset * TotalLength - TotalLength) <= Settings->VertexTolerance)
			{
				ClearPreview();
				return false;
			}
		}

		ConditionallyUpdatePreview(GroupEdgeID);
		return true;
	}

	// Otherwise, we need to figure out where along the edge we are hovering.
	double NewInputLength = 0;
	int32 StartVid = GroupEdge.Span.Vertices[EdgeSegmentID];
	int32 EndVid = GroupEdge.Span.Vertices[EdgeSegmentID + 1];
	FVector3d StartVert = CurrentMesh->GetVertex(StartVid);
	FVector3d EndVert = CurrentMesh->GetVertex(EndVid);

	FRay EdgeRay((FVector)StartVert, (FVector)(EndVert - StartVert), false);

	double DistDownEdge = EdgeRay.GetParameter((FVector)Position);

	TArray<double> PerVertexLengths;
	double TotalLength = CurrentTopology->GetEdgeArcLength(GroupEdgeID, &PerVertexLengths);

	NewInputLength = PerVertexLengths[EdgeSegmentID] + DistDownEdge;
	if (Settings->bFlipOffsetDirection)
	{
		// If we flipped start corner, we should be measuring from the opposite direction
		NewInputLength = TotalLength - NewInputLength;
	}
	// We avoid trying to insert loops that are guaranteed to follow existing group edges.
	// Distance offset with total length may work if the group widens on the other side.
	// Though it's worth noting that this filter as a whole is assuming straight group edges...
	if (NewInputLength <= Settings->VertexTolerance || 
		(Settings->PositionMode == EEdgeLoopPositioningMode::ProportionOffset 
			&& abs(NewInputLength - TotalLength) <= Settings->VertexTolerance))
	{
		ClearPreview();
		return false;
	}
	if (Settings->PositionMode == EEdgeLoopPositioningMode::ProportionOffset)
	{
		NewInputLength /= TotalLength;
	}
	ConditionallyUpdatePreview(GroupEdgeID, &NewInputLength);
	return true;
}

void UEdgeLoopInsertionTool::ClearPreview()
{
	// We don't seem to have a way to cancel the background op on a mesh without shutting down
	// the entire preview, hence us clearing the preview this way. When we know that the op is
	// not running, we can instead use UpdatePreview() to reset the mesh to the original mesh.
	bShowingBaseMesh = true;
	PreviewEdges.Reset();
	Preview->InvalidateResult();
}

void UEdgeLoopInsertionTool::ConditionallyUpdatePreview(int32 NewGroupID, double *NewInteractiveInputLength)
{
	if (bShowingBaseMesh || InputGroupEdgeID != NewGroupID 
		|| (NewInteractiveInputLength && Settings->PositionMode != EEdgeLoopPositioningMode::Even 
		&& *NewInteractiveInputLength != InteractiveInputLength))
	{
		InputGroupEdgeID = NewGroupID;
		if (NewInteractiveInputLength)
		{
			InteractiveInputLength = *NewInteractiveInputLength;
		}
		bShowingBaseMesh = false;
		PreviewEdges.Reset();
		Preview->InvalidateResult();
	}
}

FInputRayHit UEdgeLoopInsertionTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	if (bWaitingForInsertionCompletion)
	{
		return FInputRayHit();
	}

	return HitTest(PressPos.WorldRay);
}

bool UEdgeLoopInsertionTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (bWaitingForInsertionCompletion)
	{
		return false;
	}

	return UpdateHoveredItem(DevicePos.WorldRay);
}

void UEdgeLoopInsertionTool::OnEndHover()
{
	if (!bWaitingForInsertionCompletion)
	{
		ClearPreview();
	}
}

FInputRayHit UEdgeLoopInsertionTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FInputRayHit Hit;
	if (bWaitingForInsertionCompletion)
	{
		return Hit;
	}

	return HitTest(ClickPos.WorldRay);
}

void UEdgeLoopInsertionTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (bWaitingForInsertionCompletion)
	{
		return;
	}

	if (UpdateHoveredItem(ClickPos.WorldRay))
	{
		bWaitingForInsertionCompletion = true;
	}

}


// Undo/redo support

void FEdgeLoopInsertionChange::Apply(UObject* Object)
{
	UEdgeLoopInsertionTool* Tool = Cast<UEdgeLoopInsertionTool>(Object);
	MeshChange->Apply(Tool->CurrentMesh.Get(), false);
	Tool->MeshSpatial.Build();
	Tool->TopologySelector.Invalidate(true, true);
	Tool->CurrentTopology->RebuildTopology();
	Tool->ClearPreview();
}

void FEdgeLoopInsertionChange::Revert(UObject* Object)
{
	UEdgeLoopInsertionTool* Tool = Cast<UEdgeLoopInsertionTool>(Object);
	MeshChange->Apply(Tool->CurrentMesh.Get(), true);
	Tool->MeshSpatial.Build();
	Tool->TopologySelector.Invalidate(true, true);
	Tool->CurrentTopology->RebuildTopology();
	Tool->ClearPreview();
}

#undef LOCTEXT_NAMESPACE