// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/GroupTopologyStorableSelection.h"

#include "DynamicMesh3.h"
#include "Selection/ToolSelectionUtil.h"
#include "Util/CompactMaps.h"

#include "GroupTopology.h"

namespace StorableGroupTopologySelectionLocals
{
	// Helper function for GetGroupEdgeRepresentativeVerts(). Given a loop as a list of vids, with
	// the first and last vid the same, returns the lowest vid and its lower-vid neighbor.
	FIndex2i GetLoopRepresentativeVerts(const TArray<int32>& Verts)
	{
		int32 NumVerts = Verts.Num();

		int32 MinVid = Verts[0];
		// Last vert is a repeat of first vert, so neighbor is second to last.
		int32 MinNeighbor = FMath::Min(Verts[1], Verts[NumVerts - 2]);

		for (int32 i = 1; i < NumVerts - 1; ++i)
		{
			if (Verts[i] < MinVid)
			{
				MinVid = Verts[i];
				MinNeighbor = FMath::Min(Verts[i - 1], Verts[i + 1]);
			}
		}
		return FIndex2i(MinVid, MinNeighbor); // we know that MinVid is smaller
	}
}//end namespace StorableGroupTopologySelectionLocals
using namespace StorableGroupTopologySelectionLocals;

void UGroupTopologyStorableSelection::SetSelection(const FGroupTopology& TopologyIn, const FGroupTopologySelection& SelectionIn)
{
	GroupIDs = SelectionIn.SelectedGroupIDs.Array();
	CornerVids.Reset();
	GroupEdgeRepresentativeVerts.Reset();

	for (int32 CornerID : SelectionIn.SelectedCornerIDs)
	{
		CornerVids.Add(TopologyIn.GetCornerVertexID(CornerID));
	}
	for (int32 EdgeID : SelectionIn.SelectedEdgeIDs)
	{
		GroupEdgeRepresentativeVerts.Add(GetGroupEdgeRepresentativeVerts(TopologyIn, EdgeID));
	}
}

void UGroupTopologyStorableSelection::SetSelection(const FGroupTopology& TopologyIn, const FGroupTopologySelection& SelectionIn,
	const FCompactMaps& CompactMaps)
{
	GroupIDs = SelectionIn.SelectedGroupIDs.Array();
	CornerVids.Reset();
	GroupEdgeRepresentativeVerts.Reset();

	for (int32 CornerID : SelectionIn.SelectedCornerIDs)
	{
		CornerVids.Add(CompactMaps.GetVertex(TopologyIn.GetCornerVertexID(CornerID)));
	}
	for (int32 EdgeID : SelectionIn.SelectedEdgeIDs)
	{
		GroupEdgeRepresentativeVerts.Add(GetGroupEdgeRepresentativeVerts(TopologyIn, EdgeID, CompactMaps));
	}
}

void UGroupTopologyStorableSelection::ExtractIntoSelectionObject(const FGroupTopology& TopologyIn, FGroupTopologySelection& SelectionOut) const
{
	SelectionOut.Clear();

	const FDynamicMesh3* Mesh = TopologyIn.GetMesh();
	if (!Mesh)
	{
		ensureMsgf(false, TEXT("FStoredGroupTopologySelection::ExtractIntoSelectionObject: target topology must have valid underlying mesh. "));
		return;
	}

	SelectionOut.SelectedGroupIDs = TSet<int32>(GroupIDs);

	for (int32 Vid : CornerVids)
	{
		if (!Mesh->IsVertex(Vid))
		{
			ensureMsgf(false, TEXT("FStoredGroupTopologySelection::ExtractIntoSelectionObject: target topology's mesh was missing a vertex ID. "
				"Perhaps the mesh was compacted without updating the stored selection?"));
			continue;
		}
		int32 CornerID = TopologyIn.GetCornerIDFromVertexID(Vid);
		if (CornerID == IndexConstants::InvalidID)
		{
			ensureMsgf(false, TEXT("FStoredGroupTopologySelection::ExtractIntoSelectionObject: target topology did not have an expected vert as a corner. "
				"Is the topology initialized, and based on the same mesh?"));
			continue;
		}
		SelectionOut.SelectedCornerIDs.Add(CornerID);
	}
	for (const FIndex2i& EdgeVerts : GroupEdgeRepresentativeVerts)
	{
		if (!Mesh->IsVertex(EdgeVerts.A) || !Mesh->IsVertex(EdgeVerts.B))
		{
			ensureMsgf(false, TEXT("FStoredGroupTopologySelection::ExtractIntoSelectionObject: target topology's mesh was missing a vertex ID. "
				"Perhaps the mesh was compacted without updating the stored selection?"));
			continue;
		}
		int32 Eid = Mesh->FindEdge(EdgeVerts.A, EdgeVerts.B);
		if (Eid == IndexConstants::InvalidID)
		{
			ensureMsgf(false, TEXT("FStoredGroupTopologySelection::ExtractIntoSelectionObject: target topology's mesh was missing an expected edge."));
			continue;
		}
		int32 GroupEdgeID = TopologyIn.FindGroupEdgeID(Eid);
		if (Eid == IndexConstants::InvalidID)
		{
			ensureMsgf(false, TEXT("FStoredGroupTopologySelection::ExtractIntoSelectionObject: target topology did not have an expected group edge."
				"Is the topology initialized, and based on the same mesh?"));
			continue;
		}

		SelectionOut.SelectedEdgeIDs.Add(GroupEdgeID);
	}
}

FIndex2i UGroupTopologyStorableSelection::GetGroupEdgeRepresentativeVerts(const FGroupTopology& TopologyIn, int GroupEdgeID, const FCompactMaps& CompactMaps)
{
	check(GroupEdgeID >= 0 && GroupEdgeID < TopologyIn.Edges.Num());

	const FGroupTopology::FGroupEdge& GroupEdge = TopologyIn.Edges[GroupEdgeID];
	const TArray<int32>& Verts = GroupEdge.Span.Vertices;

	if (GroupEdge.EndpointCorners.A != IndexConstants::InvalidID)
	{
		// Use remapped vids
		int32 FirstVid = CompactMaps.GetVertex(Verts[0]);
		int32 FirstNeighbor = CompactMaps.GetVertex(Verts[1]);
		int32 LastVid = CompactMaps.GetVertex(Verts.Last());
		int32 LastNeighbor = CompactMaps.GetVertex(Verts[Verts.Num() - 2]);

		return FirstVid < LastVid ?
			FIndex2i(FMath::Min(FirstVid, FirstNeighbor), FMath::Max(FirstVid, FirstNeighbor))
			: FIndex2i(FMath::Min(LastVid, LastNeighbor), FMath::Max(LastVid, LastNeighbor));
	}
	else
	{
		TArray<int32> RemappedVerts;
		RemappedVerts.SetNum(Verts.Num());
		for (int32 i = 0; i < Verts.Num(); ++i)
		{
			RemappedVerts[i] = CompactMaps.GetVertex(Verts[i]);
		}
		return GetLoopRepresentativeVerts(RemappedVerts);
	}
}

FIndex2i UGroupTopologyStorableSelection::GetGroupEdgeRepresentativeVerts(const FGroupTopology& TopologyIn, int GroupEdgeID)
{
	check(GroupEdgeID >= 0 && GroupEdgeID < TopologyIn.Edges.Num());

	const FGroupTopology::FGroupEdge& GroupEdge = TopologyIn.Edges[GroupEdgeID];
	const TArray<int32>& Verts = GroupEdge.Span.Vertices;

	if (GroupEdge.EndpointCorners.A != IndexConstants::InvalidID)
	{
		int32 FirstVid = Verts[0];
		int32 FirstNeighbor = Verts[1];
		int32 LastVid = Verts.Last();
		int32 LastNeighbor = Verts[Verts.Num() - 2];

		return FirstVid < LastVid ?
			FIndex2i(FMath::Min(FirstVid, FirstNeighbor), FMath::Max(FirstVid, FirstNeighbor))
			: FIndex2i(FMath::Min(LastVid, LastNeighbor), FMath::Max(LastVid, LastNeighbor));
	}
	else
	{
		return GetLoopRepresentativeVerts(Verts);
	}
}