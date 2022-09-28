// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/PolygonSelectionMechanic.h"
#include "Selection/GroupTopologySelector.h"
#include "Selection/PersistentMeshSelection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PolygonSelectionMechanic)

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UPolygonSelectionMechanic"

void UPolygonSelectionMechanic::Initialize(
	const FDynamicMesh3* MeshIn,
	FTransform3d TargetTransformIn,
	UWorld* WorldIn,
	const FGroupTopology* TopologyIn,
	TFunction<FDynamicMeshAABBTree3* ()> GetSpatialSourceFuncIn)
{
	Topology = TopologyIn;
	TopoSelector = MakeShared<FGroupTopologySelector>(MeshIn, TopologyIn);

	UMeshTopologySelectionMechanic::Initialize(MeshIn, TargetTransformIn, WorldIn, GetSpatialSourceFuncIn);
}

void UPolygonSelectionMechanic::Initialize(
	UDynamicMeshComponent* MeshComponentIn,
	const FGroupTopology* TopologyIn,
	TFunction<FDynamicMeshAABBTree3* ()> GetSpatialSourceFuncIn)
{
	Initialize(MeshComponentIn->GetMesh(),
		(FTransform3d)MeshComponentIn->GetComponentTransform(),
		MeshComponentIn->GetWorld(),
		TopologyIn,
		GetSpatialSourceFuncIn);
}

// TODO: Remove this function when Properties is removed after deprecation
void UPolygonSelectionMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UMeshTopologySelectionMechanic::Setup(ParentToolIn);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Properties_DEPRECATED = NewObject<UDEPRECATED_PolygonSelectionMechanicProperties>(this);
	Properties_DEPRECATED->Initialize(this);
	Properties_DEPRECATED->WatchProperty(Properties_DEPRECATED->bSelectVertices, [this](bool bSelectVertices) {
		UpdateMarqueeEnabled();
	});
	Properties_DEPRECATED->WatchProperty(Properties_DEPRECATED->bSelectEdges, [this](bool bSelectVertices) {
		UpdateMarqueeEnabled();
	});
	Properties_DEPRECATED->WatchProperty(Properties_DEPRECATED->bSelectFaces, [this](bool bSelectFaces) {
		UpdateMarqueeEnabled();
	});
	Properties_DEPRECATED->WatchProperty(Properties_DEPRECATED->bEnableMarquee, [this](bool bEnableMarquee) {
		UpdateMarqueeEnabled();
	});
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}


void UPolygonSelectionMechanic::GetSelection(UPersistentMeshSelection& SelectionOut, const FCompactMaps* CompactMapsToApply) const
{
	SelectionOut.SetSelection(*Topology, PersistentSelection, CompactMapsToApply);
}

void UPolygonSelectionMechanic::LoadSelection(const UPersistentMeshSelection& SelectionIn)
{
	SelectionIn.ExtractIntoSelectionObject(*Topology, PersistentSelection);
}

bool UPolygonSelectionMechanic::UpdateHighlight(const FRay& WorldRay)
{
	checkf(DrawnTriangleSetComponent != nullptr, TEXT("Initialize() not called on UMeshTopologySelectionMechanic."));

	FRay3d LocalRay(TargetTransform.InverseTransformPosition((FVector3d)WorldRay.Origin),
		TargetTransform.InverseTransformVector((FVector3d)WorldRay.Direction));
	UE::Geometry::Normalize(LocalRay.Direction);

	HilightSelection.Clear();
	FVector3d LocalPosition, LocalNormal;
	FGroupTopologySelector::FSelectionSettings TopoSelectorSettings = GetTopoSelectorSettings(CameraState.bIsOrthographic);
	bool bHit = TopoSelector->FindSelectedElement(TopoSelectorSettings, LocalRay, HilightSelection, LocalPosition, LocalNormal);

	TSharedPtr<FGroupTopologySelector> GroupTopoSelector = StaticCastSharedPtr<FGroupTopologySelector>(TopoSelector);

	if (HilightSelection.SelectedEdgeIDs.Num() > 0 && Properties->bSelectEdgeRings && ShouldSelectEdgeRingsFunc())
	{
		GroupTopoSelector->ExpandSelectionByEdgeRings(HilightSelection);
	}
	if (HilightSelection.SelectedEdgeIDs.Num() > 0 && Properties->bSelectEdgeLoops && ShouldSelectEdgeLoopsFunc())
	{
		GroupTopoSelector->ExpandSelectionByEdgeLoops(HilightSelection);
	}

	// Don't hover highlight a selection that we already selected, because people didn't like that
	if (PersistentSelection.Contains(HilightSelection))
	{
		HilightSelection.Clear();
	}

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
		// Add triangles for each new group
		for (int Gid : HilightSelection.SelectedGroupIDs)
		{
			if (!CurrentlyHighlightedGroups.Contains(Gid))
			{
				for (int32 Tid : Topology->GetGroupTriangles(Gid))
				{
					// We use the triangle normals because the normal overlay isn't guaranteed to be valid as we edit the mesh
					FVector3d TriangleNormal = Mesh->GetTriNormal(Tid);

					// The UV's and colors here don't currently get used by HighlightedFaceMaterial, but we set them anyway
					FIndex3i VertIndices = Mesh->GetTriangle(Tid);
					DrawnTriangleSetComponent->AddTriangle(FRenderableTriangle(HighlightedFaceMaterial,
						FRenderableTriangleVertex((FVector)Mesh->GetVertex(VertIndices.A), (FVector2D)Mesh->GetVertexUV(VertIndices.A), (FVector)TriangleNormal, FLinearColor(Mesh->GetVertexColor(VertIndices.A)).ToFColor(true)),
						FRenderableTriangleVertex((FVector)Mesh->GetVertex(VertIndices.B), (FVector2D)Mesh->GetVertexUV(VertIndices.B), (FVector)TriangleNormal, FLinearColor(Mesh->GetVertexColor(VertIndices.B)).ToFColor(true)),
						FRenderableTriangleVertex((FVector)Mesh->GetVertex(VertIndices.C), (FVector2D)Mesh->GetVertexUV(VertIndices.C), (FVector)TriangleNormal, FLinearColor(Mesh->GetVertexColor(VertIndices.C)).ToFColor(true))));
				}

				CurrentlyHighlightedGroups.Add(Gid);
			}
		}//end iterating through groups
	}//end if groups need to be added

	return bHit;
}


bool UPolygonSelectionMechanic::UpdateSelection(const FRay& WorldRay, FVector3d& LocalHitPositionOut, FVector3d& LocalHitNormalOut)
{
	FRay3d LocalRay(TargetTransform.InverseTransformPosition((FVector3d)WorldRay.Origin),
		TargetTransform.InverseTransformVector((FVector3d)WorldRay.Direction));
	UE::Geometry::Normalize(LocalRay.Direction);

	const FGroupTopologySelection PreviousSelection = PersistentSelection;

	FVector3d LocalPosition, LocalNormal;
	FGroupTopologySelection Selection;
	FGroupTopologySelector::FSelectionSettings TopoSelectorSettings = GetTopoSelectorSettings(CameraState.bIsOrthographic);
	if (TopoSelector->FindSelectedElement(TopoSelectorSettings, LocalRay, Selection, LocalPosition, LocalNormal))
	{
		LocalHitPositionOut = LocalPosition;
		LocalHitNormalOut = LocalNormal;

		TSharedPtr<FGroupTopologySelector> GroupTopoSelector = StaticCastSharedPtr<FGroupTopologySelector>(TopoSelector);

		if (Selection.SelectedEdgeIDs.Num() > 0 && Properties->bSelectEdgeRings && ShouldSelectEdgeRingsFunc())
		{
			GroupTopoSelector->ExpandSelectionByEdgeRings(Selection);
		}
		if (Selection.SelectedEdgeIDs.Num() > 0 && Properties->bSelectEdgeLoops && ShouldSelectEdgeLoopsFunc())
		{
			GroupTopoSelector->ExpandSelectionByEdgeLoops(Selection);
		}
	}

	if (ShouldAddToSelectionFunc())
	{
		if (ShouldRemoveFromSelectionFunc())
		{
			PersistentSelection.Toggle(Selection);
		}
		else
		{
			PersistentSelection.Append(Selection);
		}
	}
	else if (ShouldRemoveFromSelectionFunc())
	{
		PersistentSelection.Remove(Selection);
	}
	else
	{
		PersistentSelection = Selection;
	}

	if (PersistentSelection != PreviousSelection)
	{
		SelectionTimestamp++;
		OnSelectionChanged.Broadcast();
		return true;
	}

	return false;
}


#undef LOCTEXT_NAMESPACE
