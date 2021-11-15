// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/MeshSelectionMechanic.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/SingleClickOrDragBehavior.h"
#include "Drawing/TriangleSetComponent.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/PointSetComponent.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Intersection/IntrTriangle2AxisAlignedBox2.h"
#include "Intersection/IntersectionQueries2.h"
#include "InteractiveToolManager.h"
#include "Polyline3.h"
#include "Selections/MeshConnectedComponents.h"
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
	
	FDynamicMeshSelection::EType ToCompatibleDynamicMeshSelectionType(const EMeshSelectionMechanicMode& Mode)
	{
		switch (Mode)
		{
			case EMeshSelectionMechanicMode::Mesh:
			case EMeshSelectionMechanicMode::Component:
			case EMeshSelectionMechanicMode::Triangle:
				return FDynamicMeshSelection::EType::Triangle;
			case EMeshSelectionMechanicMode::Edge:
				return FDynamicMeshSelection::EType::Edge;
			case EMeshSelectionMechanicMode::Vertex:
				return FDynamicMeshSelection::EType::Vertex;
		}
		checkNoEntry();
		return FDynamicMeshSelection::EType::Vertex;
	}

	// Returns the marquee selection rectangle, obtained from the given CameraRectangle, projected to the XY plane
	FAxisAlignedBox2d GetRectangleXY(const FCameraRectangle& CameraRectangle)
	{
		ensure(CameraRectangle.bIsInitialized);
		FAxisAlignedBox2d Result;
		
		double Offset = CameraRectangle.SelectionDomain.Plane.DistanceTo(FVector::ZeroVector);
		FCameraRectangle::FRectangleInPlane Domain = CameraRectangle.ProjectSelectionDomain(Offset);
		
		// This works because we know the UV axes are aligned with the XY axes, see the comment in UUVEditorMode::InitializeTargets
		const FVector MinPoint3D = CameraRectangle.PointUVToPoint3D(Domain.Plane, Domain.Rectangle.Min);
		const FVector MaxPoint3D = CameraRectangle.PointUVToPoint3D(Domain.Plane, Domain.Rectangle.Max);
		Result.Contain(FVector2d{MinPoint3D.X, MinPoint3D.Y}); // Convert to 2D and convert to double
		Result.Contain(FVector2d{MaxPoint3D.X, MaxPoint3D.Y});
	
		return Result;
	}
	
	FVector2d XY(const FVector3d& Point)
	{
		return {Point.X, Point.Y};
	}

	void AppendVertexIDs(const FDynamicMesh3& MeshXY0, int TriangleID, TArray<int>& VertexIDs)
	{
		const FIndex3i& Triangle = MeshXY0.GetTriangleRef(TriangleID);
		VertexIDs.Add(Triangle.A);
		VertexIDs.Add(Triangle.B);
		VertexIDs.Add(Triangle.C);
	}

	void AppendVertexIDsIfIntersected(const FDynamicMesh3& MeshXY0, const FAxisAlignedBox2d& RectangleXY, int TriangleID, TArray<int>& VertexIDs)
	{
		const FIndex3i& Triangle = MeshXY0.GetTriangleRef(TriangleID);
		if (RectangleXY.Contains(XY(MeshXY0.GetVertex(Triangle.A))))
		{
			VertexIDs.Add(Triangle.A);
		}
		
		if (RectangleXY.Contains(XY(MeshXY0.GetVertex(Triangle.B))))
		{
			VertexIDs.Add(Triangle.B);
		}
		
		if (RectangleXY.Contains(XY(MeshXY0.GetVertex(Triangle.C))))
		{
			VertexIDs.Add(Triangle.C);
		}
	}
	
	void AppendEdgeIDs(const FDynamicMesh3& MeshXY0, int TriangleID, TArray<int>& EdgeIDs)
	{
		const FIndex3i& Edges = MeshXY0.GetTriEdgesRef(TriangleID);
		EdgeIDs.Add(Edges.A);
		EdgeIDs.Add(Edges.B);
		EdgeIDs.Add(Edges.C);
	}
	
	void AppendEdgeIDsIfIntersected(const FDynamicMesh3& MeshXY0, const FAxisAlignedBox2d& RectangleXY, int TriangleID, TArray<int>& EdgeIDs)
	{
		const FIndex3i& Edges = MeshXY0.GetTriEdgesRef(TriangleID);

		const FIndex2i& EdgeA = MeshXY0.GetEdgeRef(Edges.A).Vert;
		const FSegment2d SegmentA(XY(MeshXY0.GetVertex(EdgeA.A)), XY(MeshXY0.GetVertex(EdgeA.B)));
		if (TestIntersection(SegmentA, RectangleXY))
		{
			EdgeIDs.Add(Edges.A);
		}
		
		const FIndex2i& EdgeB = MeshXY0.GetEdgeRef(Edges.B).Vert;
		const FSegment2d SegmentB(XY(MeshXY0.GetVertex(EdgeB.A)), XY(MeshXY0.GetVertex(EdgeB.B)));
		if (TestIntersection(SegmentB, RectangleXY))
		{
			EdgeIDs.Add(Edges.B);
		}
		
		const FIndex2i& EdgeC = MeshXY0.GetEdgeRef(Edges.C).Vert;
		const FSegment2d SegmentC(XY(MeshXY0.GetVertex(EdgeC.A)), XY(MeshXY0.GetVertex(EdgeC.B)));
		if (TestIntersection(SegmentC, RectangleXY))
		{
			EdgeIDs.Add(Edges.C);
		}
	}
	
	void AppendTriangleID(const FDynamicMesh3&, int TriangleID, TArray<int>& TriangleIDs)
	{
		TriangleIDs.Add(TriangleID);
	}

	void AppendTriangleIDIfIntersected(const FDynamicMesh3& MeshXY0, const FAxisAlignedBox2d& RectangleXY, int TriangleID, TArray<int>& TriangleIDs)
	{
		const FIndex3i& Triangle = MeshXY0.GetTriangleRef(TriangleID);
		const FTriangle2d TriangleXY(XY(MeshXY0.GetVertex(Triangle.A)),
									 XY(MeshXY0.GetVertex(Triangle.B)),
									 XY(MeshXY0.GetVertex(Triangle.C)));
		
		// Check with bTriangleIsOriented = false since some triangles maybe oriented away from the camera
		if (FIntrTriangle2AxisAlignedBox2d Intersects(TriangleXY, RectangleXY, false); Intersects.Test())
		{
			TriangleIDs.Add(TriangleID);
		}
	}
	
	// Returns indices, collected by the given functions, from triangles which are intersected by the given rectangle.
	// TreeXY0 must contain a mesh with vertices in the XY plane (have zero Z coordinate)
	template<typename IDsFromTriangleF, typename IDsFromTriangleIfIntersectedF>
	TArray<int32> FindAllIntersectionsAxisAlignedBox2(const FDynamicMeshAABBTree3& TreeXY0,
													  const FAxisAlignedBox2d& RectangleXY,
													  IDsFromTriangleF AppendIDs,
													  IDsFromTriangleIfIntersectedF AppendIDsIfIntersected)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FindAllIntersectionsAxisAlignedBox2);
		
		check(TreeXY0.GetMesh());
		
		TArray<int32> Result;
		FAxisAlignedBox2d TreeRectangleXY;
		TreeRectangleXY.Contain(XY(TreeXY0.GetBoundingBox().Min));
		TreeRectangleXY.Contain(XY(TreeXY0.GetBoundingBox().Max));
		if (RectangleXY.Contains(TreeRectangleXY))
		{
			// Early out selecting everything
			Result.Reserve(TreeXY0.GetMesh()->TriangleCount());
			for (int TriangleID : TreeXY0.GetMesh()->TriangleIndicesItr())
			{
				AppendIDs(*TreeXY0.GetMesh(), TriangleID, Result);
			}
			return Result;
		}
		
		int SelectAllDepth = TNumericLimits<int>::Max();
		int CurrentDepth = -1;
		
		// Traversal is depth first
		FDynamicMeshAABBTree3::FTreeTraversal Traversal;
		
		Traversal.NextBoxF =
			[&RectangleXY, &SelectAllDepth, &CurrentDepth](const FAxisAlignedBox3d& Box, int Depth)
		{
			CurrentDepth = Depth;
			if (Depth > SelectAllDepth)
			{
				// We are deeper than the depth whose AABB was first detected to be contained in the RectangleXY,
				// descend and collect all leaf triangles
				return true;
			}
			
			SelectAllDepth = TNumericLimits<int>::Max();
			
			const FAxisAlignedBox2d BoxXY(XY(Box.Min), XY(Box.Max));
			if (RectangleXY.Intersects(BoxXY))
			{
				if (RectangleXY.Contains(BoxXY))
				{
					SelectAllDepth = Depth;
				}
				
				return true;		
			}
			return false;
		};
		
		Traversal.NextTriangleF =
			[&RectangleXY, &SelectAllDepth, &CurrentDepth, &TreeXY0, &Result, &AppendIDs, &AppendIDsIfIntersected]
			(int TriangleID)
		{
			if (CurrentDepth >= SelectAllDepth)
			{
				// This TriangleID is entirely contained in the selection rectangle so we can skip intersection testing
				return AppendIDs(*TreeXY0.GetMesh(), TriangleID, Result);
			}
			return AppendIDsIfIntersected(*TreeXY0.GetMesh(), RectangleXY, TriangleID, Result);
		};
		
		TreeXY0.DoTraversal(Traversal);

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
	// TODO(Performance) :DynamicMarqueeSelection It would be cool to have the marquee selection update dynamically as
	//  the rectangle gets changed, right now this isn't interactive for large meshes so we disabled it
	// MarqueeMechanic->OnDragRectangleChanged.AddUObject(this, &UMeshSelectionMechanic::OnDragRectangleChanged);
	MarqueeMechanic->OnDragRectangleFinished.AddUObject(this, &UMeshSelectionMechanic::OnDragRectangleFinished);

	USingleClickOrDragInputBehavior* ClickOrDragBehavior = NewObject<USingleClickOrDragInputBehavior>();
	ClickOrDragBehavior->Initialize(this, MarqueeMechanic);
	ClickOrDragBehavior->Modifiers.RegisterModifier(ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	ClickOrDragBehavior->Modifiers.RegisterModifier(CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	ParentTool->AddInputBehavior(ClickOrDragBehavior);

	ClearCurrentSelection();

	TriangleSet = NewObject<UTriangleSetComponent>();
	TriangleSetMaterial = ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(GetParentTool()->GetToolManager(), TriangleColor, TriangleDepthBias, TriangleOpacity);
	
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
	TriangleSet->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	PreviewGeometryActor->SetRootComponent(TriangleSet);
	if (TriangleSet->IsRegistered())
	{
		TriangleSet->ReregisterComponent();
	}
	else
	{
		TriangleSet->RegisterComponent();
	}

	LineSet->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	LineSet->AttachToComponent(TriangleSet, FAttachmentTransformRules::KeepWorldTransform);
	if (LineSet->IsRegistered())
	{
		LineSet->ReregisterComponent();
	}
	else
	{
		LineSet->RegisterComponent();
	}

	PointSet->Rename(nullptr, PreviewGeometryActor); // Changes the "outer"
	PointSet->AttachToComponent(TriangleSet, FAttachmentTransformRules::KeepWorldTransform);
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

void UMeshSelectionMechanic::ChangeSelectionColor(const FColor& TriangleColorIn, float TriangleOpacityIn, const FColor& LineColorIn, const FColor& PointColorIn)
{
	TriangleColor = TriangleColorIn;
	TriangleOpacity = TriangleOpacityIn;
	LineColor = LineColorIn;
	PointColor = PointColorIn;

	if (TriangleSetMaterial) {
		TriangleSetMaterial->SetVectorParameterValue(TEXT("Color"), TriangleColorIn);
		TriangleSetMaterial->SetScalarParameterValue(TEXT("Opacity"), TriangleOpacityIn);
	}
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

	if (bEmitChange && OriginalSelection != CurrentSelection)
	{
		EmitSelectionChange(OriginalSelection, CurrentSelection, bBroadcast);
	}
	if (bBroadcast)
	{
		OnSelectionChanged.Broadcast();
	}

	// Rebuild after broadcast in case the outside world wants to adjust things like color...
	RebuildDrawnElements(FTransform(CurrentSelectionCentroid));
}

void UMeshSelectionMechanic::RebuildDrawnElements(const FTransform& StartTransform)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_RebuildDrawnElements);

	TriangleSet->Clear();
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
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_RebuildDrawnElements_Triangle);
		
		TriangleSet->ReserveTriangles(CurrentSelection.SelectedIDs.Num());
		LineSet->ReserveLines(CurrentSelection.SelectedIDs.Num() * 3);
		for (int32 Tid : CurrentSelection.SelectedIDs)
		{
			FIndex3i Vids = CurrentSelection.Mesh->GetTriangle(Tid);
			FVector Points[3];
			for (int i = 0; i < 3; ++i)
			{
				Points[i] = TransformToApply(CurrentSelection.Mesh->GetVertex(Vids[i]));
			}
			TriangleSet->AddTriangle(Points[0], Points[1], Points[2], FVector(0, 0, 1), TriangleColor, TriangleSetMaterial);
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
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_RebuildDrawnElements_Edge);

		LineSet->ReserveLines(CurrentSelection.SelectedIDs.Num());
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
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_RebuildDrawnElements_Vertex);

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
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_UpdateCentroid);

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


void UMeshSelectionMechanic::ChangeSelectionMode(const EMeshSelectionMechanicMode& TargetMode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode);
	
	if (CurrentSelection.SelectedIDs.IsEmpty())
	{
		SelectionMode = TargetMode;
		return;
	}
	
	const FDynamicMeshSelection OriginalSelection = CurrentSelection;
	
	auto VerticesToEdges = [this, &OriginalSelection]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_VerticesToEdges);
		
		CurrentSelection.SelectedIDs.Empty();
		CurrentSelection.Type = FDynamicMeshSelection::EType::Edge;

		for (int32 Vid : OriginalSelection.SelectedIDs)
		{
			for (int32 Eid : OriginalSelection.Mesh->VtxEdgesItr(Vid))
			{
				if (!CurrentSelection.SelectedIDs.Contains(Eid))
				{
					FIndex2i Verts = OriginalSelection.Mesh->GetEdgeV(Eid);
					if (OriginalSelection.SelectedIDs.Contains(Verts.A) &&
						OriginalSelection.SelectedIDs.Contains(Verts.B))
					{
						CurrentSelection.SelectedIDs.Add(Eid);	
					}
				}
			}
		}
	};

	auto VerticesToTriangles = [this, &OriginalSelection]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_VerticesToTriangles);
		
		CurrentSelection.SelectedIDs.Empty();
		CurrentSelection.Type = FDynamicMeshSelection::EType::Triangle;
		
		for (int32 Vid : OriginalSelection.SelectedIDs)
		{
			for (int32 Tid : OriginalSelection.Mesh->VtxTrianglesItr(Vid))
			{
				if (!CurrentSelection.SelectedIDs.Contains(Tid))
				{
					FIndex3i Verts = OriginalSelection.Mesh->GetTriangle(Tid);
					if (OriginalSelection.SelectedIDs.Contains(Verts.A) &&
						OriginalSelection.SelectedIDs.Contains(Verts.B) &&
						OriginalSelection.SelectedIDs.Contains(Verts.C))
					{
						CurrentSelection.SelectedIDs.Add(Tid);	
					}
				}
			}
		}
	};

	auto EdgesToVertices = [this, &OriginalSelection]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_EdgesToVertices);
		
		CurrentSelection.SelectedIDs.Empty();
		CurrentSelection.Type = FDynamicMeshSelection::EType::Vertex;
		
		for (int32 Eid : OriginalSelection.SelectedIDs)
		{
			FIndex2i Verts = OriginalSelection.Mesh->GetEdgeV(Eid);
			CurrentSelection.SelectedIDs.Add(Verts.A);
			CurrentSelection.SelectedIDs.Add(Verts.B);
		}
	};

	// Triangles with two selected edges will be selected
	auto EdgesToTriangles = [this, &OriginalSelection]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_EdgesToTriangles);
		
		CurrentSelection.SelectedIDs.Empty();
		CurrentSelection.Type = FDynamicMeshSelection::EType::Triangle;
		
		TArray<int32> FoundTriangles;
		for (int32 Eid : OriginalSelection.SelectedIDs)
		{
			FIndex2i Tris = OriginalSelection.Mesh->GetEdgeT(Eid);
			FoundTriangles.Add(Tris.A);
			if (Tris.B != IndexConstants::InvalidID)
			{
				FoundTriangles.Add(Tris.B);
			}
		}

		if (FoundTriangles.Num() < 2)
		{
			return;
		}

		Algo::Sort(FoundTriangles);

		for (int I = 0; I < FoundTriangles.Num() - 1; I++)
		{
			if (FoundTriangles[I] == FoundTriangles[I + 1])
			{
				CurrentSelection.SelectedIDs.Add(FoundTriangles[I]);
				I++;
			}	
		}
	};

	auto TrianglesToVertices = [this, &OriginalSelection]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_TrianglesToVertices);
		
		CurrentSelection.SelectedIDs.Empty();
		CurrentSelection.Type = FDynamicMeshSelection::EType::Vertex;

		for (int32 Tid : OriginalSelection.SelectedIDs)
		{
			FIndex3i Verts = OriginalSelection.Mesh->GetTriangle(Tid);
			CurrentSelection.SelectedIDs.Add(Verts.A);
			CurrentSelection.SelectedIDs.Add(Verts.B);
			CurrentSelection.SelectedIDs.Add(Verts.C);
		}
	};

	auto TrianglesToEdges = [this, &OriginalSelection]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_TrianglesToEdges);
			
		CurrentSelection.SelectedIDs.Empty();
		CurrentSelection.Type = FDynamicMeshSelection::EType::Edge;
	
		for (int32 Tid : OriginalSelection.SelectedIDs)
		{
			FIndex3i Edges = OriginalSelection.Mesh->GetTriEdgesRef(Tid);
			CurrentSelection.SelectedIDs.Add(Edges.A);
			CurrentSelection.SelectedIDs.Add(Edges.B);
			CurrentSelection.SelectedIDs.Add(Edges.C);
		}
	};

	switch (SelectionMode)
	{
	case EMeshSelectionMechanicMode::Vertex:
		switch (TargetMode)
		{
		case EMeshSelectionMechanicMode::Vertex:
			// Do nothing
			break;

		case EMeshSelectionMechanicMode::Edge:
			VerticesToEdges();
			break;
			
		case EMeshSelectionMechanicMode::Triangle:
		case EMeshSelectionMechanicMode::Component:
		case EMeshSelectionMechanicMode::Mesh:
			VerticesToTriangles();
			break;
		}
		break; // SelectionMode
	
	case EMeshSelectionMechanicMode::Edge:
		switch (TargetMode)
		{
		case EMeshSelectionMechanicMode::Vertex:
			EdgesToVertices();
			break;

		case EMeshSelectionMechanicMode::Edge:
			// Do nothing
			break;

		case EMeshSelectionMechanicMode::Mesh:
		case EMeshSelectionMechanicMode::Triangle:
		case EMeshSelectionMechanicMode::Component:
			EdgesToTriangles();
			break;
		}
		break; // SelectionMode
	
	case EMeshSelectionMechanicMode::Mesh:
	case EMeshSelectionMechanicMode::Triangle:
	case EMeshSelectionMechanicMode::Component:
		switch (TargetMode)
		{
		case EMeshSelectionMechanicMode::Vertex:
			TrianglesToVertices();
			break;

		case EMeshSelectionMechanicMode::Edge:
			TrianglesToEdges();
			break;

		case EMeshSelectionMechanicMode::Mesh:
		case EMeshSelectionMechanicMode::Triangle:
		case EMeshSelectionMechanicMode::Component:
			// Do nothing
			break;
		}
		break; // SelectionMode
	}

	if (CurrentSelection != OriginalSelection)
	{
		UpdateCentroid();
		RebuildDrawnElements(FTransform(GetCurrentSelectionCentroid()));
		EmitSelectionChange(OriginalSelection, CurrentSelection, true);
		OnSelectionChanged.Broadcast();
	}

	SelectionMode = TargetMode;
	return;
}


void UMeshSelectionMechanic::UpdateCurrentSelection(const TSet<int32>& NewSelection, bool CalledFromOnDragRectangleChanged)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_UpdateCurrentSelection);
	
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
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnClicked);
	
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
				TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnClicked_Component);

				FMeshConnectedComponents MeshSelectedComponent(CurrentSelection.Mesh);
				TArray<int32> SeedTriangles;
				SeedTriangles.Add(HitTid);
				MeshSelectedComponent.FindTrianglesConnectedToSeeds(SeedTriangles);
				ensure(MeshSelectedComponent.Components.Num() == 1); // Expect each triangle to only be in a single component
				ClickSelectedIDs.Append(MoveTemp(MeshSelectedComponent.Components[0].Indices));
			}
			else if (SelectionMode == EMeshSelectionMechanicMode::Edge)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnClicked_Edge);
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
				TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnClicked_Vertex);
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
				TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnClicked_Triangle);

				ClickSelectedIDs = TSet<int32>{HitTid};
			}
			else if (SelectionMode == EMeshSelectionMechanicMode::Mesh)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnClicked_Mesh);

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
		EmitSelectionChange(OriginalSelection, CurrentSelection, true);
		OnSelectionChanged.Broadcast();

		// Rebuild after broadcast in case the outside world wants to adjust things like color...
		RebuildDrawnElements(FTransform(GetCurrentSelectionCentroid()));
	}
}

void UMeshSelectionMechanic::OnDragRectangleStarted()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleStarted); // Mark start of drag sequence
	
	if (CurrentSelection.Type != MeshSelectionMechanicLocals::ToCompatibleDynamicMeshSelectionType(SelectionMode))
	{
		ClearCurrentSelection();	
	}
	
	PreDragSelection = CurrentSelection;
}

void UMeshSelectionMechanic::OnDragRectangleChanged(const FCameraRectangle& CurrentRectangle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged);
	
	auto FindIDsInsideCurrentRectangle = [this](auto&& AddIDsToSelection)
	{
		TSet<int32> RectangleSelectedIDs;
		
		if (CurrentSelectionIndex == IndexConstants::InvalidID)
		{
			for (int32 MeshIndex = 0; MeshIndex < MeshSpatials.Num(); ++MeshIndex)
			{
				// TODO UMeshSelectionMechanic is currently assuming that the provided mesh transform is identity, which is
				//  the case in the UV editor, this restriction should be lifted. When we do this we should also apply the
				//  (inverse) transform to the CurrentRectangle query rays. Search :ApplyTransformToQuery
				ensure(MeshTransforms[MeshIndex].Identical(&FTransform::Identity, 0));
				AddIDsToSelection(*MeshSpatials[MeshIndex], RectangleSelectedIDs);
	
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

			// TODO See :ApplyTransformToQuery
			ensure(MeshTransforms[CurrentSelectionIndex].Identical(&FTransform::Identity, 0));
			AddIDsToSelection(*MeshSpatials[CurrentSelectionIndex], RectangleSelectedIDs);
		}

		return RectangleSelectedIDs;
	};
	using namespace MeshSelectionMechanicLocals;
	FAxisAlignedBox2d RectangleXY = GetRectangleXY(CurrentRectangle);
	
	TSet<int32> RectangleSelectedIDs;
	if (SelectionMode == EMeshSelectionMechanicMode::Vertex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Vertex);
	
		auto AddVertexIDsToSelection = [this, &RectangleXY](const FDynamicMeshAABBTree3& Tree, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SelectedIDs =
				FindAllIntersectionsAxisAlignedBox2(Tree, RectangleXY, AppendVertexIDs, AppendVertexIDsIfIntersected);
			OutSelectedIDs.Append(MoveTemp(SelectedIDs));
		};
	
		RectangleSelectedIDs = FindIDsInsideCurrentRectangle(AddVertexIDsToSelection);
	}
	else if (SelectionMode == EMeshSelectionMechanicMode::Edge)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Edge);
		
		auto AddEdgeIDsToSelection = [this, &RectangleXY](const FDynamicMeshAABBTree3& Tree, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SelectedIDs =
				FindAllIntersectionsAxisAlignedBox2(Tree, RectangleXY, AppendEdgeIDs, AppendEdgeIDsIfIntersected);
			OutSelectedIDs.Append(MoveTemp(SelectedIDs));
		};

		RectangleSelectedIDs = FindIDsInsideCurrentRectangle(AddEdgeIDsToSelection);
	}
	else if (SelectionMode == EMeshSelectionMechanicMode::Triangle)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Triangle);
		
		auto AddTriangleIDsToSelection = [this, &RectangleXY] (const FDynamicMeshAABBTree3& Tree, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SelectedIDs =
				FindAllIntersectionsAxisAlignedBox2(Tree, RectangleXY, AppendTriangleID, AppendTriangleIDIfIntersected);
			OutSelectedIDs.Append(MoveTemp(SelectedIDs));
		};
	
		RectangleSelectedIDs = FindIDsInsideCurrentRectangle(AddTriangleIDsToSelection);
	}
	else if (SelectionMode == EMeshSelectionMechanicMode::Component)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Component);
		
		auto AddTriangleIDsToSelection = [this, &RectangleXY](const FDynamicMeshAABBTree3& Tree, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SeedTriangles =
				FindAllIntersectionsAxisAlignedBox2(Tree, RectangleXY, AppendTriangleID, AppendTriangleIDIfIntersected);

			// TODO(Performance) For large meshes and selections following code is MUCH slower than AABB traversal,
			//  consider precomputing the connected components an only updating them when the mesh topology changes
			//  rather than every time the selection changes.
			FMeshConnectedComponents MeshSelectedComponent(Tree.GetMesh());
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
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Mesh);
		
		auto AddAllMeshTriangleIDsToSelection = [this, &RectangleXY] (const FDynamicMeshAABBTree3& Tree, TSet<int32>& OutSelectedIDs)
		{
			TArray<int32> SelectedIDs =
				FindAllIntersectionsAxisAlignedBox2(Tree, RectangleXY, AppendTriangleID, AppendTriangleIDIfIntersected);
			if (!SelectedIDs.IsEmpty())
			{
				for (int32 Tid : Tree.GetMesh()->TriangleIndicesItr())
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
	
	// TODO(Performance) With large meshes and selections this call is much slower than AABB traversal (4x)
	UpdateCentroid();
	
	// TODO(Performance) With large meshes and selections this call is MUCH slower than AABB traversal (60x)
	RebuildDrawnElements(FTransform(GetCurrentSelectionCentroid()));
}

void UMeshSelectionMechanic::OnDragRectangleFinished(const FCameraRectangle& CurrentRectangle, bool bCancelled)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleFinished); // Mark end of drag sequence

	// TODO(Performance) :DynamicMarqueeSelection Remove this call when marquee selection is fast enough to update
	//  dynamically for large meshes
	OnDragRectangleChanged(CurrentRectangle);

	if (!bCancelled && (PreDragSelection != CurrentSelection))
	{
		UpdateCentroid();		
		EmitSelectionChange(PreDragSelection, CurrentSelection, true);
		OnSelectionChanged.Broadcast();

		// Rebuild after broadcast in case the outside world wants to adjust things like color...
		RebuildDrawnElements(FTransform(GetCurrentSelectionCentroid()));
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
	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE

