// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/PolygonSelectionMechanic.h"
#include "InteractiveToolManager.h"
#include "Util/ColorConstants.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UPolygonSelectionMechanic"

UPolygonSelectionMechanic::~UPolygonSelectionMechanic()
{
	checkf(PreviewGeometryActor == nullptr, TEXT("Shutdown() should be called before UPolygonSelectionMechanic is destroyed."));
}

void UPolygonSelectionMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	Properties = NewObject<UPolygonSelectionMechanicProperties>(this);
	if (bAddSelectionFilterPropertiesToParentTool)
	{
		AddToolPropertySource(Properties);
	}

	// set up visualizers
	PolyEdgesRenderer.LineColor = FLinearColor::Red;
	PolyEdgesRenderer.LineThickness = 2.0;
	HilightRenderer.LineColor = FLinearColor::Green;
	HilightRenderer.LineThickness = 4.0f;
	SelectionRenderer.LineColor = LinearColors::Gold3f();
	SelectionRenderer.LineThickness = 4.0f;

	float HighlightedFaceDepthOffset = 2.0f;
	HighlightedFaceMaterial = ToolSetupUtil::GetSelectionMaterial(FLinearColor::Green, ParentToolIn->GetToolManager(), HighlightedFaceDepthOffset);
	// The rest of the highlighting setup has to be done in Initialize(), since we need the world to set up our drawing component.
}

void UPolygonSelectionMechanic::Shutdown()
{
	if (PreviewGeometryActor)
	{
		PreviewGeometryActor->Destroy();
		PreviewGeometryActor = nullptr;
	}
}

void UPolygonSelectionMechanic::Initialize(
	const FDynamicMesh3* MeshIn,
	FTransform TargetTransformIn,
	UWorld* WorldIn,
	const FGroupTopology* TopologyIn,
	TFunction<FDynamicMeshAABBTree3 * ()> GetSpatialSourceFuncIn,
	TFunction<bool(void)> GetAddToSelectionModifierStateFuncIn)
{
	this->Mesh = MeshIn;
	this->Topology = TopologyIn;
	this->TargetTransform = FTransform3d(TargetTransformIn);

	TopoSelector.Initialize(Mesh, Topology);
	this->GetSpatialFunc = GetSpatialSourceFuncIn;
	TopoSelector.SetSpatialSource(GetSpatialFunc);
	TopoSelector.PointsWithinToleranceTest = [this](const FVector3d& Position1, const FVector3d& Position2) {
		if (CameraState.bIsOrthographic)
		{
			// We could just always use ToolSceneQueriesUtil::PointSnapQuery. But in ortho viewports, we happen to know
			// that the only points that we will ever give this function will be the closest points between a ray and
			// some geometry, meaning that the vector between them will be orthogonal to the view ray. With this knowledge,
			// we can do the tolerance computation more efficiently than PointSnapQuery can, since we don't need to project
			// down to the view plane.
			// As in PointSnapQuery, we convert our angle-based tolerance to one we can use in an ortho viewport (instead of
			// dividing our field of view into 90 visual angle degrees, we divide the plane into 90 units).
			float OrthoTolerance = ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD() * CameraState.OrthoWorldCoordinateWidth / 90.0;
			return Position1.DistanceSquared(Position2) < OrthoTolerance * OrthoTolerance;
		}
		else
		{
			return ToolSceneQueriesUtil::PointSnapQuery(CameraState,
				TargetTransform.TransformPosition(Position1), TargetTransform.TransformPosition(Position2));
		}
		
	};

	GetAddToSelectionModifierStateFunc = GetAddToSelectionModifierStateFuncIn;

	// Set up the component we use to draw highlighted triangles. Only needs to be done once, not when the mesh
	// changes (we are assuming that we won't swap worlds without creating a new mechanic).
	if (PreviewGeometryActor == nullptr)
	{
		FRotator Rotation(0.0f, 0.0f, 0.0f);
		FActorSpawnParameters SpawnInfo;;
		PreviewGeometryActor = WorldIn->SpawnActor<APreviewGeometryActor>(FVector::ZeroVector, Rotation, SpawnInfo);

		DrawnTriangleSetComponent = NewObject<UTriangleSetComponent>(PreviewGeometryActor);
		PreviewGeometryActor->SetRootComponent(DrawnTriangleSetComponent);
		DrawnTriangleSetComponent->RegisterComponent();
	}

	PreviewGeometryActor->SetActorTransform(TargetTransformIn);

	DrawnTriangleSetComponent->Clear();
	CurrentlyHighlightedGroups.Empty();
}

void UPolygonSelectionMechanic::Initialize(
	USimpleDynamicMeshComponent* MeshComponentIn,
	const FGroupTopology* TopologyIn,
	TFunction<FDynamicMeshAABBTree3 * ()> GetSpatialSourceFuncIn,
	TFunction<bool()> GetAddToSelectionModifierStateFuncIn)
{

	Initialize(MeshComponentIn->GetMesh(),
		MeshComponentIn->GetComponentTransform(),
		MeshComponentIn->GetWorld(),
		TopologyIn,
		GetSpatialSourceFuncIn,
		GetAddToSelectionModifierStateFuncIn);
}

void UPolygonSelectionMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	// Cache the view camera state so we can use for snapping/etc.
	// This should not happen in Render() though...
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);


	FViewCameraState RenderCameraState = RenderAPI->GetCameraState();

	const FDynamicMesh3* TargetMesh = this->Mesh;
	FTransform Transform = (FTransform)TargetTransform;

	PolyEdgesRenderer.BeginFrame(RenderAPI, RenderCameraState);
	PolyEdgesRenderer.SetTransform(Transform);
	for (const FGroupTopology::FGroupEdge& Edge : Topology->Edges)
	{
		FVector3d A, B;
		for (int32 eid : Edge.Span.Edges)
		{
			TargetMesh->GetEdgeV(eid, A, B);
			PolyEdgesRenderer.DrawLine(A, B);
		}
	}
	PolyEdgesRenderer.EndFrame();

	if (PersistentSelection.IsEmpty() == false)
	{
		SelectionRenderer.BeginFrame(RenderAPI, RenderCameraState);
		SelectionRenderer.SetTransform(Transform);
		TopoSelector.DrawSelection(PersistentSelection, &SelectionRenderer, &RenderCameraState);
		SelectionRenderer.EndFrame();
	}

	HilightRenderer.BeginFrame(RenderAPI, RenderCameraState);
	HilightRenderer.SetTransform(Transform);
	TopoSelector.DrawSelection(HilightSelection, &HilightRenderer, &RenderCameraState);
	HilightRenderer.EndFrame();
}





void UPolygonSelectionMechanic::ClearHighlight()
{
	checkf(DrawnTriangleSetComponent != nullptr, TEXT("Initialize() not called on UPolygonSelectionMechanic."));

	HilightSelection.Clear();
	DrawnTriangleSetComponent->Clear();
	CurrentlyHighlightedGroups.Empty();
}


void UPolygonSelectionMechanic::NotifyMeshChanged(bool bTopologyModified)
{
	ClearHighlight();
	TopoSelector.Invalidate(true, bTopologyModified);
	if (bTopologyModified)
	{
		PersistentSelection.Clear();
		SelectionTimestamp++;
		OnSelectionChanged.Broadcast();
	}
}


bool UPolygonSelectionMechanic::TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit)
{
	FGroupTopologySelection Selection;
	return TopologyHitTest(WorldRay, OutHit, Selection);
}

bool UPolygonSelectionMechanic::TopologyHitTest(const FRay& WorldRay, FHitResult& OutHit, FGroupTopologySelection& OutSelection)
{
	FRay3d LocalRay(TargetTransform.InverseTransformPosition(WorldRay.Origin),
		TargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	UpdateTopoSelector();

	FVector3d LocalPosition, LocalNormal;
	if (TopoSelector.FindSelectedElement(LocalRay, OutSelection, LocalPosition, LocalNormal) == false)
	{
		return false;
	}

	if (OutSelection.SelectedCornerIDs.Num() > 0)
	{
		OutHit.FaceIndex = OutSelection.SelectedCornerIDs[0];
		OutHit.Distance = LocalRay.Project(LocalPosition);
		OutHit.ImpactPoint = (FVector)TargetTransform.TransformPosition(LocalRay.PointAt(OutHit.Distance));
	}
	else if (OutSelection.SelectedEdgeIDs.Num() > 0)
	{
		OutHit.FaceIndex = OutSelection.SelectedEdgeIDs[0];
		OutHit.Distance = LocalRay.Project(LocalPosition);
		OutHit.ImpactPoint = (FVector)TargetTransform.TransformPosition(LocalRay.PointAt(OutHit.Distance));
	}
	else
	{
		FDynamicMeshAABBTree3* Spatial = GetSpatialFunc();
		int HitTID = Spatial->FindNearestHitTriangle(LocalRay);
		if (HitTID != IndexConstants::InvalidID)
		{
			FTriangle3d Triangle;
			Spatial->GetMesh()->GetTriVertices(HitTID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
			FIntrRay3Triangle3d Query(LocalRay, Triangle);
			if (Query.Find())
			{
				OutHit.FaceIndex = HitTID;
				OutHit.Distance = (float)Query.RayParameter;
				OutHit.Normal = (FVector)TargetTransform.TransformVectorNoScale(Spatial->GetMesh()->GetTriNormal(HitTID));
				OutHit.ImpactPoint = (FVector)TargetTransform.TransformPosition(LocalRay.PointAt(Query.RayParameter));
			}
			else
			{
				return false;
			}
		}
	}
	return true;
}



void UPolygonSelectionMechanic::UpdateTopoSelector(bool bUseOrthoSettings)
{
	bool bFaces = Properties->bSelectFaces;
	bool bEdges = Properties->bSelectEdges;
	bool bVertices = Properties->bSelectVertices;

	if (PersistentSelection.IsEmpty() == false && GetAddToSelectionModifierStateFunc() == true)
	{
		bFaces = bFaces && PersistentSelection.SelectedGroupIDs.Num() > 0;
		bEdges = bEdges && PersistentSelection.SelectedEdgeIDs.Num() > 0;
		bVertices = bVertices && PersistentSelection.SelectedCornerIDs.Num() > 0;
	}

	TopoSelector.UpdateEnableFlags(bFaces, bEdges, bVertices);
	if (bUseOrthoSettings)
	{
		TopoSelector.UpdateSelectionModeFlags(Properties->bPreferProjectedElement, Properties->bSelectDownRay, Properties->bIgnoreOcclusion);
	}
	else
	{
		TopoSelector.UpdateSelectionModeFlags(false, false, false);
	}
}




bool UPolygonSelectionMechanic::UpdateHighlight(const FRay& WorldRay)
{
	checkf(DrawnTriangleSetComponent != nullptr, TEXT("Initialize() not called on UPolygonSelectionMechanic."));

	FRay3d LocalRay(TargetTransform.InverseTransformPosition(WorldRay.Origin),
		TargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	HilightSelection.Clear();
	UpdateTopoSelector(CameraState.bIsOrthographic);
	FVector3d LocalPosition, LocalNormal;
	bool bHit = TopoSelector.FindSelectedElement(LocalRay, HilightSelection, LocalPosition, LocalNormal);

	// Currently we draw highlighted edges/vertices differently from highlighted faces. Edges/vertices
	// get drawn in the Render() call, so it is sufficient to just update HighlightSelection above.
	// Faces, meanwhile, get placed into a Component that is rendered through the normal rendering system.
	// So, we need to update the component when the highlighted selection changes.

	// Put hovered groups in a set to easily compare to current
	TSet<int> NewlyHighlightedGroups;
	NewlyHighlightedGroups.Append(HilightSelection.SelectedGroupIDs);

	// See if we're currently highlighting any groups that we're not supposed to
	if (!NewlyHighlightedGroups.Includes(CurrentlyHighlightedGroups))
	{
		DrawnTriangleSetComponent->Clear();
		CurrentlyHighlightedGroups.Empty();
	}

	// See if we need to add any groups
	if (!CurrentlyHighlightedGroups.Includes(NewlyHighlightedGroups))
	{
		// We'll need to add new triangles. For their normals, we may need to use the normal overlay.
		const FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
		if (Mesh->HasAttributes() && Mesh->Attributes()->NumNormalLayers() > 0)
		{
			NormalOverlay = Mesh->Attributes()->GetNormalLayer(0);
		}

		// Add triangles for each new group
		for (int Gid : HilightSelection.SelectedGroupIDs)
		{
			if (!CurrentlyHighlightedGroups.Contains(Gid))
			{
				for (int32 Tid : Topology->GetGroupTriangles(Gid))
				{
					FIndex3i VertIndices = Mesh->GetTriangle(Tid);

					FVector3f NormalA;
					FVector3f NormalB;
					FVector3f NormalC;

					if (NormalOverlay)
					{
						NormalOverlay->GetTriElements(Tid, NormalA, NormalB, NormalC);
					}
					else
					{
						NormalA = Mesh->GetVertexNormal(VertIndices.A);
						NormalB = Mesh->GetVertexNormal(VertIndices.B);
						NormalC = Mesh->GetVertexNormal(VertIndices.C);
					}
					// If the mesh had neither a normal overlay or vertex normals, then results are undefined.

					DrawnTriangleSetComponent->AddTriangle(FRenderableTriangle(HighlightedFaceMaterial,
						FRenderableTriangleVertex((FVector)Mesh->GetVertex(VertIndices.A), (FVector2D)Mesh->GetVertexUV(VertIndices.A), (FVector)NormalA, (FColor)Mesh->GetVertexColor(VertIndices.A)),
						FRenderableTriangleVertex((FVector)Mesh->GetVertex(VertIndices.B), (FVector2D)Mesh->GetVertexUV(VertIndices.B), (FVector)NormalB, (FColor)Mesh->GetVertexColor(VertIndices.B)),
						FRenderableTriangleVertex((FVector)Mesh->GetVertex(VertIndices.C), (FVector2D)Mesh->GetVertexUV(VertIndices.C), (FVector)NormalC, (FColor)Mesh->GetVertexColor(VertIndices.C))));
				}

				CurrentlyHighlightedGroups.Add(Gid);
			}
		}//end iterating through groups
	}//end if groups need to be added

	return bHit;
}




bool UPolygonSelectionMechanic::HasSelection() const
{
	return PersistentSelection.IsEmpty() == false;
}


bool UPolygonSelectionMechanic::UpdateSelection(const FRay& WorldRay, FVector3d& LocalHitPositionOut, FVector3d& LocalHitNormalOut)
{
	FRay3d LocalRay(TargetTransform.InverseTransformPosition(WorldRay.Origin),
		TargetTransform.InverseTransformVector(WorldRay.Direction));
	LocalRay.Direction.Normalize();

	UpdateTopoSelector(CameraState.bIsOrthographic);

	bool bSelectionModified = false;
	FVector3d LocalPosition, LocalNormal;
	FGroupTopologySelection Selection;
	if (TopoSelector.FindSelectedElement(LocalRay, Selection, LocalPosition, LocalNormal))
	{
		LocalHitPositionOut = LocalPosition;
		LocalHitNormalOut = LocalNormal;
		if (GetAddToSelectionModifierStateFunc())
		{
			PersistentSelection.Toggle(Selection);
		}
		else
		{
			PersistentSelection = Selection;
		}
		bSelectionModified = true;
	}
	else
	{
		bSelectionModified = (PersistentSelection.IsEmpty() == false);
		PersistentSelection.Clear();
	}

	if (bSelectionModified)
	{
		SelectionTimestamp++;
		OnSelectionChanged.Broadcast();
	}

	return bSelectionModified;
}


void UPolygonSelectionMechanic::SetSelection(const FGroupTopologySelection& Selection)
{
	PersistentSelection = Selection;
	SelectionTimestamp++;
	OnSelectionChanged.Broadcast();
}


void UPolygonSelectionMechanic::ClearSelection()
{
	PersistentSelection.Clear();
	SelectionTimestamp++;
	OnSelectionChanged.Broadcast();
}



void UPolygonSelectionMechanic::BeginChange()
{
	check(ActiveChange.IsValid() == false);
	ActiveChange = MakeUnique<FPolygonSelectionMechanicSelectionChange>();
	ActiveChange->Before = PersistentSelection;
	ActiveChange->Timestamp = SelectionTimestamp;
}

TUniquePtr<FToolCommandChange> UPolygonSelectionMechanic::EndChange()
{
	check(ActiveChange.IsValid() == true);
	ActiveChange->After = PersistentSelection;
	if (SelectionTimestamp != ActiveChange->Timestamp)
	{
		return MoveTemp(ActiveChange);
	}
	ActiveChange = TUniquePtr<FPolygonSelectionMechanicSelectionChange>();
	return TUniquePtr<FToolCommandChange>();
}

bool UPolygonSelectionMechanic::EndChangeAndEmitIfModified()
{
	check(ActiveChange.IsValid() == true);
	ActiveChange->After = PersistentSelection;
	if (SelectionTimestamp != ActiveChange->Timestamp)
	{
		GetParentTool()->GetToolManager()->EmitObjectChange(this, MoveTemp(ActiveChange),
			LOCTEXT("SelectionChange", "Selection Change"));
		return true;
	}
	ActiveChange = TUniquePtr<FPolygonSelectionMechanicSelectionChange>();
	return false;
}


FFrame3d UPolygonSelectionMechanic::GetSelectionFrame(bool bWorld, FFrame3d* InitialLocalFrame) const
{
	FFrame3d UseFrame;
	if (PersistentSelection.IsEmpty() == false)
	{
		UseFrame = Topology->GetSelectionFrame(PersistentSelection, InitialLocalFrame);
	}

	if (bWorld)
	{
		UseFrame.Transform(TargetTransform);
	}

	return UseFrame;
}



void FPolygonSelectionMechanicSelectionChange::Apply(UObject* Object)
{
	UPolygonSelectionMechanic* Mechanic = Cast<UPolygonSelectionMechanic>(Object);
	if (Mechanic)
	{
		Mechanic->PersistentSelection = After;
		Mechanic->OnSelectionChanged.Broadcast();
	}
}
void FPolygonSelectionMechanicSelectionChange::Revert(UObject* Object)
{
	UPolygonSelectionMechanic* Mechanic = Cast<UPolygonSelectionMechanic>(Object);
	if (Mechanic)
	{
		Mechanic->PersistentSelection = Before;
		Mechanic->OnSelectionChanged.Broadcast();
	}
}
FString FPolygonSelectionMechanicSelectionChange::ToString() const
{
	return TEXT("FPolygonSelectionMechanicSelectionChange");
}



#undef LOCTEXT_NAMESPACE