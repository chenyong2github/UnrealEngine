// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroupEdgeInsertionTool.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "CuttingOps/GroupEdgeInsertionOp.h"
#include "DynamicMeshToMeshDescription.h"
#include "InteractiveToolManager.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "ToolBuilderUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UGroupEdgeInsertionTool"

bool GetSharedBoundary(const FGroupTopology& Topology,
	const FGroupEdgeInserter::FGroupEdgeSplitPoint& StartPoint, int32 StartTopologyID, bool bStartIsCorner,
	const FGroupEdgeInserter::FGroupEdgeSplitPoint& EndPoint, int32 EndTopologyID, bool bEndIsCorner,
	int32& GroupIDOut, int32& BoundaryIndexOut);
bool DoesBoundaryContainPoint(const FGroupTopology& Topology,
	const FGroupTopology::FGroupBoundary& Boundary, int32 PointTopologyID, bool bPointIsCorner);


bool UGroupEdgeInsertionToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UGroupEdgeInsertionToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UGroupEdgeInsertionTool* NewTool = NewObject<UGroupEdgeInsertionTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	UPrimitiveComponent* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}

TUniquePtr<FDynamicMeshOperator> UGroupEdgeInsertionOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FGroupEdgeInsertionOp> Op = MakeUnique<FGroupEdgeInsertionOp>();

	Op->OriginalMesh = Tool->CurrentMesh;
	Op->OriginalTopology = Tool->CurrentTopology;
	Op->SetTransform(Tool->ComponentTarget->GetWorldTransform());

	if (Tool->bShowingBaseMesh)
	{
		Op->bShowingBaseMesh = true;
		return Op; // No inputs necessary- just showing the base mesh.
	}

	if (Tool->Settings->InsertionMode == EGroupEdgeInsertionMode::PlaneCut)
	{
		Op->Mode = FGroupEdgeInserter::EInsertionMode::PlaneCut;
	}
	else
	{
		Op->Mode = FGroupEdgeInserter::EInsertionMode::Retriangulate;
	}

	Op->VertexTolerance = Tool->Settings->VertexTolerance;

	Op->StartPoint = Tool->StartPoint;
	Op->EndPoint = Tool->EndPoint;
	Op->CommonGroupID = Tool->CommonGroupID;
	Op->CommonBoundaryIndex = Tool->CommonBoundaryIndex;

	return Op;
}

void UGroupEdgeInsertionTool::Setup()
{
	USingleSelectionTool::Setup();

	if (!ComponentTarget)
	{
		return;
	}

	GetToolManager()->DisplayMessage(
		LOCTEXT("GroupEdgeInsertionToolDescription", "Click two points on the boundary of a face to insert a new edge between the points and split the face."),
		EToolMessageLevel::UserNotification);

	// Initialize the mesh that we'll be operating on
	CurrentMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), *CurrentMesh);
	CurrentTopology = MakeShared<FGroupTopology>(CurrentMesh.Get(), true);
	MeshSpatial.SetMesh(CurrentMesh.Get(), true);

	// Set up properties
	Settings = NewObject<UGroupEdgeInsertionProperties>(this);
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

	// These draw the group edges and the loops to be inserted
	ExistingEdgesRenderer.LineColor = FLinearColor::Red;
	ExistingEdgesRenderer.LineThickness = 2.0;
	PreviewEdgeRenderer.LineColor = FLinearColor::Green;
	PreviewEdgeRenderer.LineThickness = 4.0;
	PreviewEdgeRenderer.PointColor = FLinearColor::Green;
	PreviewEdgeRenderer.PointSize = 8.0;
	PreviewEdgeRenderer.bDepthTested = false;

	// Set up the topology selector, which we use to select the endpoints
	TopologySelector.Initialize(CurrentMesh.Get(), CurrentTopology.Get());
	TopologySelector.SetSpatialSource([this]() {return &MeshSpatial; });
	TopologySelector.PointsWithinToleranceTest = [this](const FVector3d& Position1, const FVector3d& Position2, double TolScale) {
		FTransform3d Transform(ComponentTarget->GetWorldTransform());
		return ToolSceneQueriesUtil::PointSnapQuery(CameraState, Transform.TransformPosition(Position1), Transform.TransformPosition(Position2),
			ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD() * TolScale);
	};
	TopologySelectorSettings.bEnableEdgeHits = true;
	TopologySelectorSettings.bEnableCornerHits = true;
	TopologySelectorSettings.bEnableFaceHits = false;
}

void UGroupEdgeInsertionTool::SetupPreview()
{
	UGroupEdgeInsertionOperatorFactory* OpFactory = NewObject<UGroupEdgeInsertionOperatorFactory>();
	OpFactory->Tool = this;

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory);
	Preview->Setup(TargetWorld, OpFactory);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials(MaterialSet.Materials, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));

	// Whenever we get a new result from the op, we need to extract the preview edges so that
	// we can draw them if we want to.
	Preview->OnOpCompleted.AddLambda([this](const FDynamicMeshOperator* UncastOp) {
		const FGroupEdgeInsertionOp* Op = static_cast<const FGroupEdgeInsertionOp*>(UncastOp);

		bLastComputeSucceeded = Op->bSucceeded;
		LatestOpTopologyResult.Reset();

		PreviewEdges.Reset();
		if (bLastComputeSucceeded)
		{
			Op->GetEdgeLocations(PreviewEdges);
			LatestOpTopologyResult = Op->ResultTopology;
		}
		else
		{
			// Don't show the broken preview, since we wouldn't accept it on click.
			Preview->PreviewMesh->UpdatePreview(CurrentMesh.Get());
		}
		});

	Preview->OnOpCompleted.AddLambda([this](const FDynamicMeshOperator*) {
		if (!bLastComputeSucceeded)
		{
			// Don't show the broken preview, since we wouldn't accept it on click.
			Preview->PreviewMesh->UpdatePreview(CurrentMesh.Get());
		}
		});

	// Set initial preview to unprocessed mesh, so that things don't disappear initially
	Preview->PreviewMesh->UpdatePreview(CurrentMesh.Get());
	Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	Preview->PreviewMesh->EnableWireframe(Settings->bWireframe);
	Preview->SetVisibility(true);
	ClearPreview();

	ComponentTarget->SetOwnerVisibility(false);
}

void UGroupEdgeInsertionTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	Preview->Shutdown();
	ComponentTarget->SetOwnerVisibility(true);
	CurrentMesh.Reset();
	CurrentTopology.Reset();
	ExpireChanges();
}

void UGroupEdgeInsertionTool::OnTick(float DeltaTime)
{
	if (Preview)
	{
		Preview->Tick(DeltaTime);

		if (ToolState == EToolState::WaitingForInsertComplete && Preview->HaveValidResult())
		{
			if (bLastComputeSucceeded)
			{
				// Apply the insertion
				GetToolManager()->BeginUndoTransaction(LOCTEXT("GroupEdgeInsertionTransactionName", "Group Edge Insertion"));

				GetToolManager()->EmitObjectChange(this, MakeUnique<FGroupEdgeInsertionChangeBookend>(CurrentChangeStamp, true), LOCTEXT("GroupEdgeInsertion", "Group Edge Insertion"));
				ComponentTarget->CommitMesh([this](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
					{
						FDynamicMeshToMeshDescription Converter;
						Converter.Convert(Preview->PreviewMesh->GetMesh(), *CommitParams.MeshDescription);
					});
				GetToolManager()->EmitObjectChange(this, MakeUnique<FGroupEdgeInsertionChangeBookend>(CurrentChangeStamp, false), LOCTEXT("GroupEdgeInsertion", "Group Edge Insertion"));

				GetToolManager()->EndUndoTransaction();

				// Update current mesh and topology
				CurrentMesh->Copy(*Preview->PreviewMesh->GetMesh(), true, true, true, true);
				*CurrentTopology = *LatestOpTopologyResult;
				CurrentTopology->RetargetOnClonedMesh(CurrentMesh.Get());
				MeshSpatial.Build();
				TopologySelector.Invalidate(true, true);

				ToolState = EToolState::GettingStart;
			}
			else
			{
				ToolState = EToolState::GettingEnd;
			}

			PreviewEdges.Reset();
		}
	}
}

void UGroupEdgeInsertionTool::Render(IToolsContextRenderAPI* RenderAPI)
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

	// Draw the preview edges and points
	PreviewEdgeRenderer.BeginFrame(RenderAPI, RenderCameraState);
	PreviewEdgeRenderer.SetTransform(Preview->PreviewMesh->GetTransform());
	for (const TPair<FVector3d, FVector3d>& EdgeVerts : PreviewEdges)
	{
		PreviewEdgeRenderer.DrawLine(EdgeVerts.Key, EdgeVerts.Value);
	}
	for (const FVector3d& Point : PreviewPoints)
	{
		PreviewEdgeRenderer.DrawPoint(Point);
	}
	PreviewEdgeRenderer.EndFrame();
}

void UGroupEdgeInsertionTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	PreviewEdges.Reset();
	Preview->PreviewMesh->EnableWireframe(Settings->bWireframe);
	Preview->InvalidateResult();
}

void UGroupEdgeInsertionTool::ClearPreview(bool bClearDrawnElements, bool bForce)
{
	// We don't seem to have a way to cancel the background op on a mesh without shutting down
	// the entire preview, hence us clearing the preview this way. When we know that the op is
	// not running, we can instead use UpdatePreview() to reset the mesh to the original mesh.

	if (!bShowingBaseMesh || bForce)
	{
		bShowingBaseMesh = true;
		Preview->InvalidateResult();
	}
	if (bClearDrawnElements)
	{
		PreviewEdges.Reset();
		PreviewPoints.Reset();
	}
}

void UGroupEdgeInsertionTool::ConditionallyUpdatePreview(
	const FGroupEdgeInserter::FGroupEdgeSplitPoint& NewEndPoint, int32 NewEndTopologyID, bool bNewEndIsCorner, 
	int32 NewCommonGroupID, int32 NewBoundaryIndex)
{
	if (bShowingBaseMesh 
		|| bEndIsCorner != bNewEndIsCorner || EndTopologyID != NewEndTopologyID
		|| EndPoint.bIsVertex != NewEndPoint.bIsVertex || EndPoint.ElementID != NewEndPoint.ElementID
		|| (!NewEndPoint.bIsVertex && NewEndPoint.EdgeTValue != EndPoint.EdgeTValue)
		|| CommonGroupID != NewCommonGroupID || CommonBoundaryIndex != NewBoundaryIndex)
	{
		// Update the end variables, since they are apparently different
		EndPoint = NewEndPoint;
		EndTopologyID = NewEndTopologyID;
		bEndIsCorner = bNewEndIsCorner;
		CommonGroupID = NewCommonGroupID;
		CommonBoundaryIndex = NewBoundaryIndex;

		// If either endpoint is a corner, we need to calculate its tangent. This will differ based on which
		// boundary it is a part of.
		if (bStartIsCorner)
		{
			GetCornerTangent(StartTopologyID, CommonGroupID, CommonBoundaryIndex, StartPoint.Tangent);
		}
		if (bEndIsCorner)
		{
			GetCornerTangent(EndTopologyID, CommonGroupID, CommonBoundaryIndex, EndPoint.Tangent);
		}

		bShowingBaseMesh = false;
		PreviewEdges.Reset();
		Preview->InvalidateResult();
	}
}

FInputRayHit UGroupEdgeInsertionTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FInputRayHit Hit;
	switch (ToolState)
	{
	case EToolState::WaitingForInsertComplete:
		break; // Keep hit invalid

	case EToolState::GettingStart:
	{
		PreviewPoints.Reset();
		FVector3d RayPoint;
		if (TopologyHitTest(PressPos.WorldRay, RayPoint))
		{
			Hit = FInputRayHit(PressPos.WorldRay.GetParameter((FVector)RayPoint));
		}
	}
	case EToolState::GettingEnd:
	{
		FVector3d RayPoint;
		FRay3d LocalRay;
		if (TopologyHitTest(PressPos.WorldRay, RayPoint, &LocalRay))
		{
			Hit = FInputRayHit(PressPos.WorldRay.GetParameter((FVector)RayPoint));
		}
		else
		{
			// If we don't hit a valid element, we still do a hover if we hit the mesh.
			// We still do the topology check in the first place because it accepts missing
			// rays that are close enough to snap.
			double RayT = 0;
			int32 Tid = FDynamicMesh3::InvalidID;
			if (MeshSpatial.FindNearestHitTriangle(LocalRay, RayT, Tid))
			{
				Hit = FInputRayHit(RayT);
			}
		}
	}
	}

	return Hit;
}

bool UGroupEdgeInsertionTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	switch (ToolState)
	{
	case EToolState::WaitingForInsertComplete:
		return false; // Do nothing.

	case EToolState::GettingStart:
	{
		// Update start variables and show a preview of a point if it's on an edge or corner
		PreviewPoints.Reset();
		FVector3d PreviewPoint;
		if (GetHoveredItem(DevicePos.WorldRay, StartPoint, StartTopologyID, bStartIsCorner, PreviewPoint))
		{
			PreviewPoints.Add(PreviewPoint);
			return true;
		}
		return false;
	}
	case EToolState::GettingEnd:
	{
		check(PreviewPoints.Num() > 0);
		PreviewPoints.SetNum(1); // Keep the first element, which is the start point

		// Don't update the end variables right away so that we can check if they actually changed (they
		// won't when we snap to the same corner as before).
		FGroupEdgeInserter::FGroupEdgeSplitPoint SnappedPoint;
		int32 PointTopologyID, GroupID, BoundaryIndex;
		bool bPointIsCorner;
		FVector3d PreviewPoint;
		FRay3d LocalRay;
		if (GetHoveredItem(DevicePos.WorldRay, SnappedPoint, PointTopologyID, bPointIsCorner, PreviewPoint, &LocalRay))
		{
			// See if the point is not on the same vertex/edge but is on the same boundary
			if (!(SnappedPoint.bIsVertex == StartPoint.bIsVertex && SnappedPoint.ElementID == StartPoint.ElementID)
				&& GetSharedBoundary(*CurrentTopology, StartPoint, StartTopologyID, bStartIsCorner,
					SnappedPoint, PointTopologyID, bPointIsCorner, GroupID, BoundaryIndex))
			{
				ConditionallyUpdatePreview(SnappedPoint, PointTopologyID, bPointIsCorner, GroupID, BoundaryIndex);
			}
			else
			{
				PreviewEdges.Reset(); // TODO: Maybe we should show a different color edge on a fail, rather than hiding it?
			}
			PreviewPoints.Add(PreviewPoint);
			
			return true;
		}

		// If we don't have a valid endpoint, draw a line to the current hit location.
		if (!bShowingBaseMesh)
		{
			ClearPreview(false);
		}
		PreviewEdges.Reset();
		double RayT = 0;
		int32 Tid = FDynamicMesh3::InvalidID;
		if (MeshSpatial.FindNearestHitTriangle(LocalRay, RayT, Tid))
		{
			PreviewEdges.Emplace(PreviewPoints[0], LocalRay.PointAt(RayT));
			return true;
		}
		return false;
	}
	}

	check(false); // Each case has its own return, so shouldn't get here
	return false;
}

void UGroupEdgeInsertionTool::OnEndHover()
{
	switch (ToolState)
	{
	case EToolState::WaitingForInsertComplete:
	case EToolState::GettingStart:
		ClearPreview(true);
		break;
	case EToolState::GettingEnd:
		// Keep the first preview point.
		ClearPreview(false);
		PreviewPoints.SetNum(1);
		PreviewEdges.Reset();
	}
}

FInputRayHit UGroupEdgeInsertionTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FInputRayHit Hit;
	switch (ToolState)
	{
	case EToolState::WaitingForInsertComplete:
		break; // Keep hit invalid

	// Same requirement for the other two cases: the click should go on an edge
	case EToolState::GettingStart:
	case EToolState::GettingEnd:
	{
		FVector3d RayPoint;
		if (TopologyHitTest(ClickPos.WorldRay, RayPoint))
		{
			Hit = FInputRayHit(ClickPos.WorldRay.GetParameter((FVector)RayPoint));
		}
		break;
	}
	}
	return Hit;
}

void UGroupEdgeInsertionTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	switch (ToolState)
	{
	case EToolState::WaitingForInsertComplete:
		break; // Do nothing

	case EToolState::GettingStart:
	{
		// Update start variables and switch state if successful
		FVector3d PreviewPoint;
		if (GetHoveredItem(ClickPos.WorldRay, StartPoint, StartTopologyID, bStartIsCorner, PreviewPoint))
		{
			PreviewPoints.Reset();
			PreviewPoints.Add(PreviewPoint);
			ToolState = EToolState::GettingEnd;

			GetToolManager()->BeginUndoTransaction(LOCTEXT("GroupEdgeStartTransactionName", "Group Edge Start"));
			GetToolManager()->EmitObjectChange(this, MakeUnique<FGroupEdgeInsertionFirstPointChange>(CurrentChangeStamp), 
				LOCTEXT("GroupEdgeStart", "Group Edge Start"));
			GetToolManager()->EndUndoTransaction();
		}
		break;
	}
	case EToolState::GettingEnd:
	{
		// Don't update the end variables right away so that we can check if they actually changed (they
		// won't when we snap to the same corner as before).
		FVector3d PreviewPoint;
		FGroupEdgeInserter::FGroupEdgeSplitPoint SnappedPoint;
		int32 PointTopologyID, GroupID, BoundaryIndex;
		bool bPointIsCorner;
		if (GetHoveredItem(ClickPos.WorldRay, SnappedPoint, PointTopologyID, bPointIsCorner, PreviewPoint))
		{
			// See if the point is not on the same vertex/edge but is on the same boundary
			if (!(SnappedPoint.bIsVertex == StartPoint.bIsVertex && SnappedPoint.ElementID == StartPoint.ElementID) 
				&& GetSharedBoundary(*CurrentTopology, StartPoint, StartTopologyID, bStartIsCorner,
					SnappedPoint, PointTopologyID, bPointIsCorner, GroupID, BoundaryIndex))
			{
				ConditionallyUpdatePreview(SnappedPoint, PointTopologyID, bPointIsCorner, GroupID, BoundaryIndex);
				ToolState = EToolState::WaitingForInsertComplete;
			}
			else
			{
				ClearPreview(false);
			}
		}
		break;
	}
	}
}

bool UGroupEdgeInsertionTool::TopologyHitTest(const FRay& WorldRay, 
	FVector3d& RayPositionOut, FRay3d* LocalRayOut)
{
	FTransform LocalToWorld = ComponentTarget->GetWorldTransform();
	FRay3d LocalRay(LocalToWorld.InverseTransformPosition(WorldRay.Origin),
		LocalToWorld.InverseTransformVector(WorldRay.Direction), false);

	if (LocalRayOut)
	{
		*LocalRayOut = LocalRay;
	}

	FGroupTopologySelection Selection;
	FVector3d Position, Normal;
	if (TopologySelector.FindSelectedElement(TopologySelectorSettings,
		LocalRay, Selection, Position, Normal))
	{
		RayPositionOut =  LocalToWorld.TransformPosition((FVector)Position);
		return true;
	}
	return false;
}

bool UGroupEdgeInsertionTool::GetHoveredItem(const FRay& WorldRay, 
	FGroupEdgeInserter::FGroupEdgeSplitPoint& PointOut, 
	int32& TopologyElementIDOut, bool& bIsCornerOut, FVector3d& PositionOut,
	FRay3d* LocalRayOut)
{
	TopologyElementIDOut = FDynamicMesh3::InvalidID;
	PointOut.ElementID = FDynamicMesh3::InvalidID;

	// Cast the ray to see what we hit.
	FTransform LocalToWorld = ComponentTarget->GetWorldTransform();
	FRay3d LocalRay(LocalToWorld.InverseTransformPosition(WorldRay.Origin),
		LocalToWorld.InverseTransformVector(WorldRay.Direction), false);
	if (LocalRayOut)
	{
		*LocalRayOut = LocalRay;
	}
	FGroupTopologySelection Selection;
	FVector3d Position, Normal;
	int32 EdgeSegmentID;
	if (!TopologySelector.FindSelectedElement(
		TopologySelectorSettings, LocalRay, Selection, Position, Normal, &EdgeSegmentID))
	{
		return false; // Didn't hit anything
	}
	else if (Selection.SelectedCornerIDs.Num() > 0)
	{
		// Point is a corner
		TopologyElementIDOut = Selection.GetASelectedCornerID();
		bIsCornerOut = true;
		PointOut.bIsVertex = true;
		PointOut.ElementID = CurrentTopology->GetCornerVertexID(TopologyElementIDOut);
		// We can't initialize the tangent yet because the tangent of a corner will
		// depend on which boundary it is a part of.

		PositionOut = CurrentMesh->GetVertex(PointOut.ElementID);
	}
	else 
	{
		// Point is an edge. We'll need to calculate the t value and some other things.
		check(Selection.SelectedEdgeIDs.Num() > 0);

		TopologyElementIDOut = Selection.GetASelectedEdgeID();
		bIsCornerOut = false;

		const FGroupTopology::FGroupEdge& GroupEdge = CurrentTopology->Edges[TopologyElementIDOut];

		int32 Eid = GroupEdge.Span.Edges[EdgeSegmentID];
		int32 StartVid = GroupEdge.Span.Vertices[EdgeSegmentID];
		int32 EndVid = GroupEdge.Span.Vertices[EdgeSegmentID + 1];
		FVector3d StartVert = CurrentMesh->GetVertex(StartVid);
		FVector3d EndVert = CurrentMesh->GetVertex(EndVid);
		FVector3d EdgeVector = EndVert - StartVert;
		double EdgeLength = EdgeVector.Length();
		check(EdgeLength > 0);

		PointOut.Tangent = EdgeVector / EdgeLength;

		FRay EdgeRay((FVector)StartVert, (FVector)PointOut.Tangent, true);
		double DistDownEdge = EdgeRay.GetParameter((FVector)Position);

		PositionOut = EdgeRay.PointAt(DistDownEdge);

		// See if the point is at a vertex in the group edge span.
		if (DistDownEdge <= Settings->VertexTolerance)
		{
			PointOut.bIsVertex = true;
			PointOut.ElementID = StartVid;
			if (EdgeSegmentID > 0)
			{
				// Average with previous normalized edge vector
				PointOut.Tangent += (StartVert - CurrentMesh->GetVertex(GroupEdge.Span.Vertices[EdgeSegmentID - 1])).Normalized();
				PointOut.Tangent.Normalize();
			}
		}
		else if (abs(DistDownEdge - EdgeLength) <= Settings->VertexTolerance)
		{
			PointOut.bIsVertex = true;
			PointOut.ElementID = EndVid;
			if (EdgeSegmentID + 2 < GroupEdge.Span.Vertices.Num())
			{
				PointOut.Tangent += (CurrentMesh->GetVertex(GroupEdge.Span.Vertices[EdgeSegmentID + 2]) - EndVert).Normalized();
				PointOut.Tangent.Normalize();
			}
		}
		else
		{
			PointOut.bIsVertex = false;
			PointOut.ElementID = Eid;
			PointOut.EdgeTValue = DistDownEdge / EdgeLength;
			if (CurrentMesh->GetEdgeV(Eid).A != StartVid)
			{
				PointOut.EdgeTValue = 1 - PointOut.EdgeTValue;
			}
		}
	}
	return true;
}


void UGroupEdgeInsertionTool::GetCornerTangent(int32 CornerID, int32 GroupID, int32 BoundaryIndex, FVector3d& TangentOut)
{
	TangentOut = FVector3d::Zero();

	int32 CornerVid = CurrentTopology->GetCornerVertexID(CornerID);
	check(CornerVid != FDynamicMesh3::InvalidID);

	const FGroupTopology::FGroup* Group = CurrentTopology->FindGroupByID(GroupID);
	check(Group && BoundaryIndex >= 0 && BoundaryIndex < Group->Boundaries.Num());

	const FGroupTopology::FGroupBoundary& Boundary = Group->Boundaries[BoundaryIndex];
	TArray<FVector3d> AdjacentPoints;
	for (int32 GroupEdgeID : Boundary.GroupEdges)
	{
		TArray<int32> Vertices = CurrentTopology->Edges[GroupEdgeID].Span.Vertices;
		if (Vertices[0] == CornerVid)
		{
			AdjacentPoints.Add(CurrentMesh->GetVertex(Vertices[1]));
		}
		else if (Vertices.Last() == CornerVid)
		{
			AdjacentPoints.Add(CurrentMesh->GetVertex(Vertices[Vertices.Num()-2]));
		}
	}
	check(AdjacentPoints.Num() == 2);

	FVector3d CornerPosition = CurrentMesh->GetVertex(CornerVid);
	TangentOut = (CornerPosition - AdjacentPoints[0]).Normalized();
	TangentOut += (AdjacentPoints[1] - CornerPosition).Normalized();
	TangentOut.Normalize();
}

bool GetSharedBoundary(const FGroupTopology& Topology,
	const FGroupEdgeInserter::FGroupEdgeSplitPoint& StartPoint, int32 StartTopologyID, bool bStartIsCorner,
	const FGroupEdgeInserter::FGroupEdgeSplitPoint& EndPoint, int32 EndTopologyID, bool bEndIsCorner,
	int32& GroupIDOut, int32& BoundaryIndexOut)
{
	// The start and endpoints could be on the same boundary of multiple groups at
	// the same time, and sometimes we won't be able to resolve the ambiguity
	// (one example is a sphere split into two equal groups, but could even happen
	// with more than two groups when endpoints are corners).
	// Sometimes there are things we can do to eliminate some contenders- the best
	// approach is probably trying to do a plane cut for all of the options and 
	// removing those that fail. However, it's worth noting that such issues won't
	// arise in the standard application of this tool for low-poly modeling, where
	// groups are planar, so it's not worth the bother.
	// Instead, we'll just take one of the results arbitrarily, though we will try to
	// take one that has a single boundary (this will prefer a cylinder cap over
	// a cylinder side).
	// TODO: The code would be simpler if we didn't even want to do that filtering- we'd
	// just return the first result we found. Should we consider doing that?

	GroupIDOut = FDynamicMesh3::InvalidID;
	BoundaryIndexOut = FDynamicMesh3::InvalidID;

	TArray<TPair<int32,int32>> CandidateGroupIDsAndBoundaryIndices;
	if (bStartIsCorner)
	{
		// Go through all neighboring groups and their boundaries to find a shared one.
		const FGroupTopology::FCorner& StartCorner = Topology.Corners[StartTopologyID];
		for (int32 GroupID : StartCorner.NeighbourGroupIDs)
		{
			const FGroupTopology::FGroup* Group = Topology.FindGroupByID(GroupID);
			for (int32 i = 0; i < Group->Boundaries.Num(); ++i)
			{
				const FGroupTopology::FGroupBoundary& Boundary = Group->Boundaries[i];
				if (DoesBoundaryContainPoint(Topology, Boundary, EndTopologyID, bEndIsCorner)
					&& DoesBoundaryContainPoint(Topology, Boundary, StartTopologyID, bStartIsCorner))
				{
					CandidateGroupIDsAndBoundaryIndices.Emplace(GroupID, i);
					break; // Can't share more than one boundary in the same group
				}
			}
		}
	}
	else
	{
		// Start is on an edge, so there are fewer boundaries to look through.
		const FGroupTopology::FGroupEdge& GroupEdge = Topology.Edges[StartTopologyID];
		const FGroupTopology::FGroup* Group = Topology.FindGroupByID(GroupEdge.Groups.A);
		for (int32 i = 0; i < Group->Boundaries.Num(); ++i)
		{
			const FGroupTopology::FGroupBoundary& Boundary = Group->Boundaries[i];

			if (DoesBoundaryContainPoint(Topology, Boundary, EndTopologyID, bEndIsCorner)
				&& DoesBoundaryContainPoint(Topology, Boundary, StartTopologyID, bStartIsCorner))
			{
				CandidateGroupIDsAndBoundaryIndices.Emplace(GroupEdge.Groups.A, i);
				break;
			}
		}
		if (GroupEdge.Groups.B != FDynamicMesh3::InvalidID)
		{
			Group = Topology.FindGroupByID(GroupEdge.Groups.B);
			for (int32 i = 0; i < Group->Boundaries.Num(); ++i)
			{
				const FGroupTopology::FGroupBoundary& Boundary = Group->Boundaries[i];

				if (DoesBoundaryContainPoint(Topology, Boundary, EndTopologyID, bEndIsCorner)
					&& DoesBoundaryContainPoint(Topology, Boundary, StartTopologyID, bStartIsCorner))
				{
					CandidateGroupIDsAndBoundaryIndices.Emplace(GroupEdge.Groups.B, i);
					break;
				}
			}
		}
	}

	if (CandidateGroupIDsAndBoundaryIndices.Num() == 0)
	{
		return false;
	}

	// Prefer a result that has a single boundary if there are multiple.
	if (CandidateGroupIDsAndBoundaryIndices.Num() > 1)
	{
		for (const TPair<int32, int32>& GroupIDBoundaryIdxPair : CandidateGroupIDsAndBoundaryIndices)
		{
			if (Topology.FindGroupByID(GroupIDBoundaryIdxPair.Key)->Boundaries.Num() == 1)
			{
				GroupIDOut = GroupIDBoundaryIdxPair.Key;
				BoundaryIndexOut = 0;
				return true;
			}
		}
	}

	GroupIDOut = CandidateGroupIDsAndBoundaryIndices[0].Key;
	BoundaryIndexOut = CandidateGroupIDsAndBoundaryIndices[0].Value;
	return true;
}

bool DoesBoundaryContainPoint(const FGroupTopology& Topology,
	const FGroupTopology::FGroupBoundary& Boundary, int32 PointTopologyID, bool bPointIsCorner)
{
	for (int32 GroupEdgeID : Boundary.GroupEdges)
	{
		if (!bPointIsCorner && GroupEdgeID == PointTopologyID)
		{
			return true;
		}

		const FGroupTopology::FGroupEdge& GroupEdge = Topology.Edges[GroupEdgeID];
		if (bPointIsCorner && (GroupEdge.EndpointCorners.A == PointTopologyID
			|| GroupEdge.EndpointCorners.B == PointTopologyID))
		{
			return true;
		}
	}
	return false;
}


// Undo/redo support

void FGroupEdgeInsertionFirstPointChange::Revert(UObject* Object)
{
	UGroupEdgeInsertionTool* Tool = Cast<UGroupEdgeInsertionTool>(Object);

	check(Tool->ToolState == UGroupEdgeInsertionTool::EToolState::GettingEnd);
	Tool->ToolState = UGroupEdgeInsertionTool::EToolState::GettingStart;

	Tool->ClearPreview();

	bHaveDoneUndo = true;
}

void FGroupEdgeInsertionChangeBookend::Revert(UObject* Object)
{
	if (bBeforeChange)
	{
		// Load from the component, which has been updated
		UGroupEdgeInsertionTool* Tool = Cast<UGroupEdgeInsertionTool>(Object);
		Tool->CurrentMesh->Clear();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(Tool->ComponentTarget->GetMesh(), *Tool->CurrentMesh);
		Tool->CurrentTopology->RebuildTopology();
		Tool->MeshSpatial.Build();
		Tool->TopologySelector.Invalidate(true, true);

		Tool->ClearPreview(false, true);

		check(Tool->ToolState == UGroupEdgeInsertionTool::EToolState::GettingStart);
		// If we were doing full undo/redo of the start point insertions, instead of just
		// letting the user back out of their latest one, then we would set the state
		// to GettingEnd here. Instead we go all the way back to GettingStart.
	}
}

void FGroupEdgeInsertionChangeBookend::Apply(UObject* Object)
{
	if (!bBeforeChange)
	{
		// Load from the component, which has been updated
		UGroupEdgeInsertionTool* Tool = Cast<UGroupEdgeInsertionTool>(Object);
		Tool->CurrentMesh->Clear();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(Tool->ComponentTarget->GetMesh(), *Tool->CurrentMesh);
		Tool->CurrentTopology->RebuildTopology();
		Tool->MeshSpatial.Build();
		Tool->TopologySelector.Invalidate(true, true);

		Tool->ClearPreview();

		// Since we always go all the way back to GettingStart on revert, we would expect
		// this to be the state on redo.
		check(Tool->ToolState == UGroupEdgeInsertionTool::EToolState::GettingStart);
	}
}

#undef LOCTEXT_NAMESPACE