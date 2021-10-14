// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/MeshSelectionMechanic.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/SingleClickOrDragBehavior.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/PointSetComponent.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Intersection/IntrTriangle2AxisAlignedBox2.h"
#include "Intersection/IntersectionQueries2.h"
#include "InteractiveToolManager.h"
#include "Polyline3.h"
#include "Selections/MeshConnectedComponents.h"
#include "SceneManagement.h"
#include "Spatial/GeometrySet3.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UMeshSelectionMechanic"

namespace MeshSelectionMechanicLocals
{
	/**
	 * Class that can be used to apply/revert selection changes. Expects that the mesh selection
	 * mechanic is the associated UObject that is passed to Apply/Revert.
	 */
	class FMeshSelectionMechanicSelectionChange : public FToolCommandChange
	{
	public:
		FMeshSelectionMechanicSelectionChange(const FDynamicMeshSelection& OldSelection,
			const FDynamicMeshSelection& NewSelection, bool bBroadcastOnSelectionChangedIn)
			: Before(OldSelection)
			, After(NewSelection)
			, bBroadcastOnSelectionChanged(bBroadcastOnSelectionChangedIn)
		{
		}

		virtual void Apply(UObject* Object) override
		{
			UMeshSelectionMechanic* Mechanic = Cast<UMeshSelectionMechanic>(Object);
			if (Mechanic)
			{
				Mechanic->SetSelection(After, bBroadcastOnSelectionChanged, false);
			}
		}

		virtual void Revert(UObject* Object) override
		{
			UMeshSelectionMechanic* Mechanic = Cast<UMeshSelectionMechanic>(Object);
			if (Mechanic)
			{
				Mechanic->SetSelection(Before, bBroadcastOnSelectionChanged, false);
			}
		}

		virtual FString ToString() const override
		{
			return TEXT("MeshSelectionMechanicLocals::FMeshSelectionMechanicSelectionChange");
		}

	protected:
		FDynamicMeshSelection Before;
		FDynamicMeshSelection After;
		bool bBroadcastOnSelectionChanged;
	};

	template <typename InElementType>
	void ToggleItem(TSet<InElementType>& Set, InElementType Item)
	{
		if (Set.Remove(Item) == 0)
		{
			Set.Add(Item);
		}
	}
	
	UE::Geometry::FDynamicMeshSelection::EType ToCompatibleDynamicMeshSelectionType(const EMeshSelectionMechanicMode& Mode)
	{
		switch (Mode)
		{
			case EMeshSelectionMechanicMode::Mesh:
			case EMeshSelectionMechanicMode::Component:
			case EMeshSelectionMechanicMode::Triangle:
				return UE::Geometry::FDynamicMeshSelection::EType::Triangle;
			case EMeshSelectionMechanicMode::Edge:
				return UE::Geometry::FDynamicMeshSelection::EType::Edge;
			case EMeshSelectionMechanicMode::Vertex:
				return UE::Geometry::FDynamicMeshSelection::EType::Vertex;
		}
		checkNoEntry();
		return UE::Geometry::FDynamicMeshSelection::EType::Vertex;
	}

	template <typename Real>
	TVector2<FVector::FReal> XY(const TVector<Real>& Point)
	{
		return {static_cast<FVector::FReal>(Point.X), static_cast<FVector::FReal>(Point.Y)};
	}

	// Returns the marquee selection rectangle, obtained from the given CameraRectangle, projected to the XY plane
	FCameraRectangle::FAxisAlignedBox2 GetRectangleXY(const FCameraRectangle& CameraRectangle)
	{
		ensure(CameraRectangle.bIsInitialized);
		
		double Offset = CameraRectangle.SelectionDomain.Plane.DistanceTo(FVector::ZeroVector);
		FCameraRectangle::FRectangleInPlane Domain = CameraRectangle.ProjectSelectionDomain(Offset);
		FCameraRectangle::FAxisAlignedBox2 RectangleXY;
		
		// This works because we know the UV axes are aligned with the XY axes, see the comment in UUVEditorMode::InitializeTargets
		RectangleXY.Contain(XY(CameraRectangle.PointUVToPoint3D(Domain.Plane, Domain.Rectangle.Min)));
		RectangleXY.Contain(XY(CameraRectangle.PointUVToPoint3D(Domain.Plane, Domain.Rectangle.Max)));
	
		return RectangleXY;
	}

	// Returns the points of the given mesh that lie inside the given rectangle. Assumptions:
	// - Mesh vertices lie in the XY plane (have zero Z coordinate)
	// - The camera is above the mesh and looking in the -Z direction (so no clip testing is needed)
	TArray<int32> FindProjectedVerticesInRectangleXY0(const FCameraRectangle& CameraRectangle, const FDynamicMesh3& MeshXY0)
	{
		FCameraRectangle::FAxisAlignedBox2 RectangleXY = GetRectangleXY(CameraRectangle);
		
		TArray<int32> Result;
		for (int32 VertexIndex : MeshXY0.VertexIndicesItr())
		{
			if (RectangleXY.Contains(XY(MeshXY0.GetVertex(VertexIndex))))
			{
				Result.Add(VertexIndex);
			}
		}

		return Result;
	}
	
	// Returns the edges of the given mesh that lie inside the given rectangle. Assumptions:
	// - Mesh vertices lie in the XY plane (have zero Z coordinate)
	// - The camera is above the mesh and looking in the -Z direction (so no clip testing is needed)
	TArray<int32> FindProjectedEdgesInRectangleXY0(const FCameraRectangle& CameraRectangle, const FDynamicMesh3& MeshXY0)
	{
		FCameraRectangle::FAxisAlignedBox2 RectangleXY = GetRectangleXY(CameraRectangle);
		
		TArray<int32> Result;
		for (int32 EdgeIndex : MeshXY0.EdgeIndicesItr())
		{
			const FDynamicMesh3::FEdge& Edge = MeshXY0.GetEdgeRef(EdgeIndex);
			FCameraRectangle::FSegment2 SegmentXY(XY(MeshXY0.GetVertex(Edge.Vert.A)),
												  XY(MeshXY0.GetVertex(Edge.Vert.B)));
			if (TestIntersection(SegmentXY, RectangleXY))
			{
				Result.Add(EdgeIndex);
			}
		}
		return Result;
	}
	
	// Returns the triangles of the given mesh that lie inside the given rectangle. Assumptions:
	// - Mesh vertices lie in the XY plane (have zero Z coordinate)
	// - The camera is above the mesh and looking in the -Z direction (so no clip testing is needed)
	TArray<int32> FindProjectedTrianglesInRectangleXY0(const FCameraRectangle& CameraRectangle, const FDynamicMesh3& MeshXY0)
	{
		FCameraRectangle::FAxisAlignedBox2 RectangleXY = GetRectangleXY(CameraRectangle);
		
		TArray<int32> Result;
		for (int32 TriangleIndex : MeshXY0.TriangleIndicesItr())
		{
			const FIndex3i& Triangle = MeshXY0.GetTriangleRef(TriangleIndex);
			const TTriangle2<FVector::FReal> TriangleXY(XY(MeshXY0.GetVertex(Triangle.A)),
														XY(MeshXY0.GetVertex(Triangle.B)),
														XY(MeshXY0.GetVertex(Triangle.C)));

			// Check with bTriangleIsOriented = false since some triangles maybe oriented away from the camera
			if (TIntrTriangle2AxisAlignedBox2<FVector::FReal> Intersects(TriangleXY, RectangleXY, false); Intersects.Test())
			{
				Result.Add(TriangleIndex);
			}
		}
		return Result;
	}
} // namespace MeshSelectionMechanicLocals

void UMeshSelectionMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	// This will be the target for the click drag behavior below
	MarqueeMechanic = NewObject<URectangleMarqueeMechanic>();
	MarqueeMechanic->bUseExternalClickDragBehavior = true;
	MarqueeMechanic->Setup(ParentToolIn);
	MarqueeMechanic->OnDragRectangleStarted.AddUObject(this, &UMeshSelectionMechanic::OnDragRectangleStarted);
	MarqueeMechanic->OnDragRectangleChanged.AddUObject(this, &UMeshSelectionMechanic::OnDragRectangleChanged);
	MarqueeMechanic->OnDragRectangleFinished.AddUObject(this, &UMeshSelectionMechanic::OnDragRectangleFinished);

	USingleClickOrDragInputBehavior* ClickOrDragBehavior = NewObject<USingleClickOrDragInputBehavior>();
	ClickOrDragBehavior->Initialize(this, MarqueeMechanic);
	ClickOrDragBehavior->Modifiers.RegisterModifier(ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	ClickOrDragBehavior->Modifiers.RegisterModifier(CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	ParentTool->AddInputBehavior(ClickOrDragBehavior);

	ClearCurrentSelection();

	LineSet = NewObject<ULineSetComponent>();
	LineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(
		GetParentTool()->GetToolManager(), /*bDepthTested*/ true));

	PointSet = NewObject<UPointSetComponent>();
	PointSet->SetPointMaterial(ToolSetupUtil::GetDefaultPointComponentMaterial(
		GetParentTool()->GetToolManager(), /*bDepthTested*/ true));

	// Add default selection change emitter if one was not provided.
	if (!EmitSelectionChange)
	{
		EmitSelectionChange = [this](const FDynamicMeshSelection& OldSelection, const FDynamicMeshSelection& NewSelection, bool bBroadcastOnSelectionChanged)
		{
			GetParentTool()->GetToolManager()->EmitObjectChange(this, 
				MakeUnique<MeshSelectionMechanicLocals::FMeshSelectionMechanicSelectionChange>(OldSelection, NewSelection, bBroadcastOnSelectionChanged),
				LOCTEXT("SelectionChangeMessage", "Selection Change"));
		};
	}
}

void UMeshSelectionMechanic::SetWorld(UWorld* World)
{
	// It may be unreasonable to worry about SetWorld being called more than once, but let's be safe anyway
	if (PreviewGeometryActor)
	{
		PreviewGeometryActor->Destroy();
	}

	// We need the world so we can create the geometry actor in the right place.
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters SpawnInfo;
	PreviewGeometryActor = World->SpawnActor<APreviewGeometryActor>(FVector::ZeroVector, Rotation, SpawnInfo);

	// Attach the rendering component to the actor
	LineSet->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	PreviewGeometryActor->SetRootComponent(LineSet);
	if (LineSet->IsRegistered())
	{
		LineSet->ReregisterComponent();
	}
	else
	{
		LineSet->RegisterComponent();
	}

	PointSet->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	PointSet->AttachToComponent(LineSet, FAttachmentTransformRules::KeepWorldTransform);
	if (PointSet->IsRegistered())
	{
		PointSet->ReregisterComponent();
	}
	else
	{
		PointSet->RegisterComponent();
	}
}

void UMeshSelectionMechanic::Shutdown()
{
	if (PreviewGeometryActor)
	{
		PreviewGeometryActor->Destroy();
		PreviewGeometryActor = nullptr;
	}
}

void UMeshSelectionMechanic::AddSpatial(TSharedPtr<FDynamicMeshAABBTree3> SpatialIn, const FTransform& TransformIn)
{
	MeshSpatials.Add(SpatialIn);
	MeshTransforms.Add(TransformIn);
}

const FDynamicMeshSelection& UMeshSelectionMechanic::GetCurrentSelection() const
{
	return CurrentSelection;
}

void UMeshSelectionMechanic::SetSelection(const FDynamicMeshSelection& Selection, bool bBroadcast, bool bEmitChange)
{
	FDynamicMeshSelection OriginalSelection = CurrentSelection;
	CurrentSelection = Selection;

	// Adjust the mesh in which the selection lives
	if (CurrentSelection.Mesh == nullptr)
	{
		// We're actually clearing the selection
		if (!ensure(CurrentSelection.SelectedIDs.IsEmpty()))
		{
			CurrentSelection.SelectedIDs.Empty();
		}
		CurrentSelectionIndex = IndexConstants::InvalidID;
	}
	else if (CurrentSelectionIndex == IndexConstants::InvalidID 
		|| MeshSpatials[CurrentSelectionIndex]->GetMesh() != Selection.Mesh)
	{
		CurrentSelectionIndex = IndexConstants::InvalidID;
		for (int32 i = 0; i < MeshSpatials.Num(); ++i)
		{
			if (MeshSpatials[i]->GetMesh() == Selection.Mesh)
			{
				CurrentSelectionIndex = i;
				break;
			}
		}
		ensure(CurrentSelectionIndex != IndexConstants::InvalidID);
	}

	UpdateCentroid();
	RebuildDrawnElements(FTransform(CurrentSelectionCentroid));

	if (bEmitChange && OriginalSelection != CurrentSelection)
	{
		EmitSelectionChange(OriginalSelection, CurrentSelection, bBroadcast);
	}
	if (bBroadcast)
	{
		OnSelectionChanged.Broadcast();
	}
}

void UMeshSelectionMechanic::RebuildDrawnElements(const FTransform& StartTransform)
{
	LineSet->Clear();
	PointSet->Clear();
	PreviewGeometryActor->SetActorTransform(StartTransform);

	if (CurrentSelectionIndex == IndexConstants::InvalidID)
	{
		return;
	}

	// For us to end up with the StartTransform, we have to invert it, but only after applying
	// the mesh transform to begin with.
	auto TransformToApply = [&StartTransform, this](const FVector& VectorIn)
	{
		return StartTransform.InverseTransformPosition(
			MeshTransforms[CurrentSelectionIndex].TransformPosition(VectorIn));
	};

	if (CurrentSelection.Type == FDynamicMeshSelection::EType::Triangle)
	{
		for (int32 Tid : CurrentSelection.SelectedIDs)
		{
			FIndex3i Vids = CurrentSelection.Mesh->GetTriangle(Tid);
			FVector Points[3];
			for (int i = 0; i < 3; ++i)
			{
				Points[i] = TransformToApply(CurrentSelection.Mesh->GetVertex(Vids[i]));
			}
			for (int i = 0; i < 3; ++i)
			{
				int NextIndex = (i + 1) % 3;
				LineSet->AddLine(Points[i], Points[NextIndex],
					LineColor, LineThickness, DepthBias);
			}
		}
	}
	else if (CurrentSelection.Type == FDynamicMeshSelection::EType::Edge)
	{
		for (int32 Eid : CurrentSelection.SelectedIDs)
		{
			FIndex2i EdgeVids = CurrentSelection.Mesh->GetEdgeV(Eid);
			LineSet->AddLine(
				TransformToApply(CurrentSelection.Mesh->GetVertex(EdgeVids.A)),
				TransformToApply(CurrentSelection.Mesh->GetVertex(EdgeVids.B)),
					LineColor, LineThickness, DepthBias);
		}
	}
	else if (CurrentSelection.Type == FDynamicMeshSelection::EType::Vertex)
	{
		PointSet->ReservePoints(CurrentSelection.SelectedIDs.Num());
		for (int32 Vid : CurrentSelection.SelectedIDs)
		{
			FRenderablePoint PointToRender(TransformToApply(CurrentSelection.Mesh->GetVertex(Vid)),
				                           PointColor,
				                           PointThickness);
			PointSet->AddPoint(PointToRender);
		}
	}
}

void UMeshSelectionMechanic::UpdateCentroid()
{
	CurrentSelectionCentroid = FVector3d(0);
	if (CurrentSelection.IsEmpty())
	{
		return;
	}

	if (CurrentSelection.Type == FDynamicMeshSelection::EType::Edge)
	{
		for (int32 Eid : CurrentSelection.SelectedIDs)
		{
			CurrentSelectionCentroid += CurrentSelection.Mesh->GetEdgePoint(Eid, 0.5);
		}
		CurrentSelectionCentroid /= CurrentSelection.SelectedIDs.Num();
	}
	else if (CurrentSelection.Type == FDynamicMeshSelection::EType::Triangle)
	{
		for (int32 Tid : CurrentSelection.SelectedIDs)
		{
			CurrentSelectionCentroid += CurrentSelection.Mesh->GetTriCentroid(Tid);
		}
		CurrentSelectionCentroid /= CurrentSelection.SelectedIDs.Num();
	}
	if (CurrentSelection.Type == FDynamicMeshSelection::EType::Vertex)
	{
		for (int32 Vid : CurrentSelection.SelectedIDs)
		{
			CurrentSelectionCentroid += CurrentSelection.Mesh->GetVertex(Vid);
		}
		CurrentSelectionCentroid /= CurrentSelection.SelectedIDs.Num();
	}

	// disable centroid caching if mesh does not have shape changestamp enabled
	CentroidTimestamp = CurrentSelection.Mesh->HasShapeChangeStampEnabled() ?
		CurrentSelection.Mesh->GetShapeChangeStamp() : TNumericLimits<uint32>::Max();
}

FVector3d UMeshSelectionMechanic::GetCurrentSelectionCentroid()
{
	if (!CurrentSelection.Mesh)
	{
		return FVector3d(0);
	}
	else if (CurrentSelection.Mesh->GetShapeChangeStamp() != CentroidTimestamp)
	{
		UpdateCentroid();
	}

	return CurrentSelectionCentroid;
}


void UMeshSelectionMechanic::SetDrawnElementsTransform(const FTransform& Transform)
{
	PreviewGeometryActor->SetActorTransform(Transform);
}

void UMeshSelectionMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	// Cache the camera state
	MarqueeMechanic->Render(RenderAPI);
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
}

void UMeshSelectionMechanic::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	MarqueeMechanic->DrawHUD(Canvas, RenderAPI);
}

FInputRayHit UMeshSelectionMechanic::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	// TODO We could account for what modifiers would do and return an accurate depth here but for now this simple code works fine

	// Return a hit so we always capture and can clear the selection
	FInputRayHit Hit; 
	Hit.bHit = true;
	return Hit;	
}

void UMeshSelectionMechanic::ClearCurrentSelection()
{
	CurrentSelection.SelectedIDs.Empty();
	CurrentSelection.Mesh = nullptr;
	CurrentSelection.Type = MeshSelectionMechanicLocals::ToCompatibleDynamicMeshSelectionType(SelectionMode);
	CurrentSelectionIndex = IndexConstants::InvalidID;
}

void UMeshSelectionMechanic::UpdateCurrentSelection(const TSet<int32>& NewSelection, bool CalledFromOnDragRectangleChanged)
{
	if (!ShouldRestartSelection() && CalledFromOnDragRectangleChanged)
	{
		// If we're modifying (adding/removing/toggling, but not restarting) the selection, we should start with the
		// selection cached in OnDragRectangleStarted, otherwise multiple changes get accumulated as the rectangle is
		// swept around
		CurrentSelection.SelectedIDs = PreDragSelection.SelectedIDs;
	}
	
	if (ShouldRestartSelection())
	{
		CurrentSelection.SelectedIDs = NewSelection;
	}
	else if (ShouldAddToSelection())
	{
		CurrentSelection.SelectedIDs.Append(NewSelection);
	}
	else if (ShouldToggleFromSelection())
	{
		for (int32 Index : NewSelection)
		{
			MeshSelectionMechanicLocals::ToggleItem(CurrentSelection.SelectedIDs, Index);
		}
	}
	else if (ShouldRemoveFromSelection())
	{
		CurrentSelection.SelectedIDs = CurrentSelection.SelectedIDs.Difference(NewSelection);
	}
	else
	{
		checkNoEntry();
	}

	if (CurrentSelection.SelectedIDs.IsEmpty())
	{
		ClearCurrentSelection();
	}
	
	CurrentSelection.Type = MeshSelectionMechanicLocals::ToCompatibleDynamicMeshSelectionType(SelectionMode);
}

void UMeshSelectionMechanic::OnClicked(const FInputDeviceRay& ClickPos)
{
	FDynamicMeshSelection OriginalSelection = CurrentSelection;
	
	if (CurrentSelection.Type != MeshSelectionMechanicLocals::ToCompatibleDynamicMeshSelectionType(SelectionMode))
	{
		ClearCurrentSelection();
	}

	TSet<int32> ClickSelectedIDs; // TODO Maybe using a TVariant would have been better here...
	{
		int32 HitTid = IndexConstants::InvalidID;
		for (int32 MeshIndex = 0; MeshIndex < MeshSpatials.Num(); ++MeshIndex)
		{
			FRay LocalRay(
				MeshTransforms[MeshIndex].InverseTransformPosition(ClickPos.WorldRay.Origin),
				MeshTransforms[MeshIndex].InverseTransformVector(ClickPos.WorldRay.Direction));

			double RayT = 0; 
			if (MeshSpatials[MeshIndex]->FindNearestHitTriangle(LocalRay, RayT, HitTid))
			{
				CurrentSelection.Mesh = MeshSpatials[MeshIndex]->GetMesh();
				CurrentSelection.TopologyTimestamp = CurrentSelection.Mesh->GetTopologyChangeStamp();
				CurrentSelectionIndex = MeshIndex;
				break;
			}
		}

		if (HitTid != IndexConstants::InvalidID)
		{
			if (SelectionMode == EMeshSelectionMechanicMode::Component)
			{
				FMeshConnectedComponents MeshSelectedComponent(CurrentSelection.Mesh);
				TArray<int32> SeedTriangles;
				SeedTriangles.Add(HitTid);
				MeshSelectedComponent.FindTrianglesConnectedToSeeds(SeedTriangles);
				ensure(MeshSelectedComponent.Components.Num() == 1); // Expect each triangle to only be in a single component
				ClickSelectedIDs.Append(MoveTemp(MeshSelectedComponent.Components[0].Indices));
			}
			else if (SelectionMode == EMeshSelectionMechanicMode::Edge)
			{
				// TODO: We'll need the ability to hit occluded triangles to see if there is a better edge to snap to.

				// Try to snap to one of the edges.
				FIndex3i Eids = CurrentSelection.Mesh->GetTriEdges(HitTid);

				FGeometrySet3 GeometrySet;
				for (int i = 0; i < 3; ++i)
				{
					FIndex2i Vids = CurrentSelection.Mesh->GetEdgeV(Eids[i]);
					FPolyline3d Polyline(CurrentSelection.Mesh->GetVertex(Vids.A), CurrentSelection.Mesh->GetVertex(Vids.B));
					GeometrySet.AddCurve(Eids[i], Polyline);
				}
				FGeometrySet3::FNearest Result;
				if (GeometrySet.FindNearestCurveToRay(ClickPos.WorldRay, Result, 
					[this](const FVector3d& Position1, const FVector3d& Position2) {
						return ToolSceneQueriesUtil::PointSnapQuery(CameraState,
							Position1, Position2,
							ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD()); }))
				{
					ClickSelectedIDs = TSet<int32>{Result.ID};
				}
			}
			else if (SelectionMode == EMeshSelectionMechanicMode::Vertex)
			{
				// TODO: Improve this to handle super narrow, sliver triangles better, where testing near vertices can be difficult.

				// Try to snap to one of the vertices
				FIndex3i Vids = CurrentSelection.Mesh->GetTriangle(HitTid);

				FGeometrySet3 GeometrySet;
				for (int i = 0; i < 3; ++i)
				{
					GeometrySet.AddPoint(Vids[i], CurrentSelection.Mesh->GetTriVertex(HitTid, i));
				}
				FGeometrySet3::FNearest Result;
				if (GeometrySet.FindNearestPointToRay(ClickPos.WorldRay, Result,
					[this](const FVector3d& Position1, const FVector3d& Position2) {
						return ToolSceneQueriesUtil::PointSnapQuery(CameraState,
							Position1, Position2,
							ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD()); }))
				{
					ClickSelectedIDs = TSet<int32>{Result.ID};
				}
			}
			else if (SelectionMode == EMeshSelectionMechanicMode::Triangle)
			{
				ClickSelectedIDs = TSet<int32>{HitTid};
			}
			else if (SelectionMode == EMeshSelectionMechanicMode::Mesh)
			{
				for (int32 Tid : CurrentSelection.Mesh->TriangleIndicesItr())
				{
					ClickSelectedIDs.Add(Tid);
				}
			}
			else
			{
				checkNoEntry();
			}
		}
	}

	// TODO Perhaps selection clearing should happen only if the click occurs further than some threshold from the meshes
	UpdateCurrentSelection(ClickSelectedIDs);

	if (OriginalSelection != CurrentSelection)
	{
		UpdateCentroid();
		RebuildDrawnElements(FTransform(GetCurrentSelectionCentroid()));
		EmitSelectionChange(OriginalSelection, CurrentSelection, true);
		OnSelectionChanged.Broadcast();
	}
}

void UMeshSelectionMechanic::OnDragRectangleStarted()
{
	if (CurrentSelection.Type != MeshSelectionMechanicLocals::ToCompatibleDynamicMeshSelectionType(SelectionMode))
	{
		ClearCurrentSelection();	
	}
	
	PreDragSelection = CurrentSelection;
}

void UMeshSelectionMechanic::OnDragRectangleChanged(const FCameraRectangle& CurrentRectangle)
{
	auto FindIDsInsideCurrentRectangle = [this](auto&& AddIDsToSelection)
	{
		TSet<int32> RectangleSelectedIDs;
		
		if (CurrentSelectionIndex == IndexConstants::InvalidID)
		{
			for (int32 MeshIndex = 0; MeshIndex < MeshSpatials.Num(); ++MeshIndex)
			{
				const FDynamicMesh3& Mesh = *(MeshSpatials[MeshIndex]->GetMesh());

				// TODO UMeshSelectionMechanic is currently assuming that the provided mesh transform is identity, which is
				//  the case in the UV editor, this restriction should be lifted. When we do this we should also apply the
				//  (inverse) tranform to the CurrentRectangle query rays. Search :ApplyTransformToQuery
				ensure(MeshTransforms[MeshIndex].Identical(&FTransform::Identity, 0));
				AddIDsToSelection(Mesh, MeshTransforms[MeshIndex], RectangleSelectedIDs);
	
				if (!RectangleSelectedIDs.IsEmpty())
				{
					// Pick the first selectable mesh for now, maybe we should try to be smarter
					CurrentSelectionIndex = MeshIndex;
					CurrentSelection.Mesh = MeshSpatials[CurrentSelectionIndex]->GetMesh();
					break;
				}
			}
		}
		else
		{
			ensure(CurrentSelectionIndex != IndexConstants::InvalidID);
			ensure(CurrentSelection.Mesh == MeshSpatials[CurrentSelectionIndex]->GetMesh());

			const FDynamicMesh3& Mesh = *(MeshSpatials[CurrentSelectionIndex]->GetMesh());

			// TODO See :ApplyTransformToQuery
			ensure(MeshTransforms[CurrentSelectionIndex].Identical(&FTransform::Identity, 0));
			AddIDsToSelection(Mesh, MeshTransforms[CurrentSelectionIndex], RectangleSelectedIDs);
		}

		return RectangleSelectedIDs;
	};
	
	TSet<int32> RectangleSelectedIDs;
	if (SelectionMode == EMeshSelectionMechanicMode::Vertex)
	{
		auto AddVertexIDsToSelection = [this, &CurrentRectangle]
			(const FDynamicMesh3& Mesh, const FTransform& Transform, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SelectedIDs = MeshSelectionMechanicLocals::FindProjectedVerticesInRectangleXY0(CurrentRectangle, Mesh);
			OutSelectedIDs.Append(MoveTemp(SelectedIDs));
		};
	
		RectangleSelectedIDs = FindIDsInsideCurrentRectangle(AddVertexIDsToSelection);
	}
	else if (SelectionMode == EMeshSelectionMechanicMode::Edge)
	{
		auto AddEdgeIDsToSelection = [this, &CurrentRectangle]
			(const FDynamicMesh3& Mesh, const FTransform& Transform, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SelectedIDs = MeshSelectionMechanicLocals::FindProjectedEdgesInRectangleXY0(CurrentRectangle, Mesh);
			OutSelectedIDs.Append(MoveTemp(SelectedIDs));
		};

		RectangleSelectedIDs = FindIDsInsideCurrentRectangle(AddEdgeIDsToSelection);
	}
	else if (SelectionMode == EMeshSelectionMechanicMode::Triangle)
	{
		auto AddTriangleIDsToSelection = [this, &CurrentRectangle]
			(const FDynamicMesh3& Mesh, const FTransform& Transform, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SelectedIDs = MeshSelectionMechanicLocals::FindProjectedTrianglesInRectangleXY0(CurrentRectangle, Mesh);
			OutSelectedIDs.Append(MoveTemp(SelectedIDs));
		};
	
		RectangleSelectedIDs = FindIDsInsideCurrentRectangle(AddTriangleIDsToSelection);
	}
	else if (SelectionMode == EMeshSelectionMechanicMode::Component)
	{
		auto AddTriangleIDsToSelection = [this, &CurrentRectangle]
			(const FDynamicMesh3& Mesh, const FTransform& Transform, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SeedTriangles = MeshSelectionMechanicLocals::FindProjectedTrianglesInRectangleXY0(CurrentRectangle, Mesh);
			FMeshConnectedComponents MeshSelectedComponent(&Mesh);
			MeshSelectedComponent.FindTrianglesConnectedToSeeds(SeedTriangles);
			for (int ComponentIndex = 0; ComponentIndex < MeshSelectedComponent.Components.Num(); ComponentIndex++)
			{
				OutSelectedIDs.Append(MoveTemp(MeshSelectedComponent.Components[ComponentIndex].Indices));
			}
		};
	
		RectangleSelectedIDs = FindIDsInsideCurrentRectangle(AddTriangleIDsToSelection);
	}
	else if (SelectionMode == EMeshSelectionMechanicMode::Mesh)
	{
		auto AddAllMeshTriangleIDsToSelection = [this, &CurrentRectangle]
			(const FDynamicMesh3& Mesh, const FTransform& Transform, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SelectedIDs = MeshSelectionMechanicLocals::FindProjectedTrianglesInRectangleXY0(CurrentRectangle, Mesh);
			if (!SelectedIDs.IsEmpty())
			{
				for (int32 Tid : Mesh.TriangleIndicesItr())
				{
					OutSelectedIDs.Add(Tid);
				}
			}
		};

		RectangleSelectedIDs = FindIDsInsideCurrentRectangle(AddAllMeshTriangleIDsToSelection);
	}
	else
	{
		checkNoEntry();
	}

	UpdateCurrentSelection(RectangleSelectedIDs, true);
	
	UpdateCentroid();
	RebuildDrawnElements(FTransform(GetCurrentSelectionCentroid()));
}

void UMeshSelectionMechanic::OnDragRectangleFinished(const FCameraRectangle& Rectangle, bool bCancelled)
{
	if (!bCancelled && (PreDragSelection != CurrentSelection))
	{
		UpdateCentroid();
		RebuildDrawnElements(FTransform(GetCurrentSelectionCentroid()));
		EmitSelectionChange(PreDragSelection, CurrentSelection, true);
		OnSelectionChanged.Broadcast();
	}
}

void UMeshSelectionMechanic::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	switch (ModifierID)
	{
	case ShiftModifierID:
		bShiftToggle = bIsOn;
		break;
	case CtrlModifierID:
		bCtrlToggle = bIsOn;
		break;
	}
}

#undef LOCTEXT_NAMESPACE
