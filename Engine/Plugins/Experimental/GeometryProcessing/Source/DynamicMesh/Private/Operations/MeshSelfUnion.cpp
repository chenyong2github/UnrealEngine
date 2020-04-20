// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshBoolean

#include "Operations/MeshSelfUnion.h"
#include "Operations/MeshMeshCut.h"

#include "Selections/MeshConnectedComponents.h"

#include "MeshNormals.h"

#include "Async/ParallelFor.h"
#include "MeshTransforms.h"

#include "Algo/RemoveIf.h"

#include "DynamicMeshAABBTree3.h"

bool FMeshSelfUnion::Compute()
{
	// build spatial data and use it to find intersections
	FDynamicMeshAABBTree3 Spatial(Mesh);
	MeshIntersection::FIntersectionsQueryResult Intersections = Spatial.FindAllSelfIntersections();

	if (Cancelled())
	{
		return false;
	}

	// cut the meshes
	FMeshSelfCut Cut(Mesh);
	Cut.bTrackInsertedVertices = bCollapseDegenerateEdgesOnCut; // to collect candidates to collapse
	Cut.Cut(Intersections);

	if (Cancelled())
	{
		return false;
	}

	// collapse tiny edges along cut boundary
	if (bCollapseDegenerateEdgesOnCut)
	{
		double DegenerateEdgeTolSq = DegenerateEdgeTol * DegenerateEdgeTol;

		// convert vertex chains to edge IDs to simplify logic of finding remaining candidate edges after collapses
		TArray<int> EIDs;
		for (int ChainIdx = 0; ChainIdx < Cut.VertexChains.Num();)
		{
			int ChainLen = Cut.VertexChains[ChainIdx];
			int ChainEnd = ChainIdx + 1 + ChainLen;
			for (int ChainSubIdx = ChainIdx + 1; ChainSubIdx + 1 < ChainEnd; ChainSubIdx++)
			{
				int VID[2]{ Cut.VertexChains[ChainSubIdx], Cut.VertexChains[ChainSubIdx + 1] };
				if (Mesh->GetVertex(VID[0]).DistanceSquared(Mesh->GetVertex(VID[1])) < DegenerateEdgeTolSq)
				{
					EIDs.Add(Mesh->FindEdge(VID[0], VID[1]));
				}
			}
			ChainIdx = ChainEnd;
		}
		for (int EID : EIDs)
		{
			if (!ensure(Mesh->IsEdge(EID)))
			{
				continue;
			}
			FVector3d A, B;
			Mesh->GetEdgeV(EID, A, B);
			if (A.DistanceSquared(B) > DegenerateEdgeTolSq)
			{
				continue;
			}
			FIndex2i EV = Mesh->GetEdgeV(EID);

			// if the vertex we'd remove is on a seam, try removing the other one instead
			if (Mesh->HasAttributes() && Mesh->Attributes()->IsSeamVertex(EV.B, false))
			{
				Swap(EV.A, EV.B);
				// if they were both on seams, then collapse should not happen?  (& would break OnCollapseEdge assumptions in overlay)
				if (Mesh->HasAttributes() && Mesh->Attributes()->IsSeamVertex(EV.B, false))
				{
					continue;
				}
			}
			FDynamicMesh3::FEdgeCollapseInfo CollapseInfo;
			EMeshResult CollapseResult = Mesh->CollapseEdge(EV.A, EV.B, CollapseInfo);
		}
	}

	if (Cancelled())
	{
		return false;
	}

	// edges that will become new boundary edges after the boolean op removes triangles on each mesh
	TArray<int> CutBoundaryEdges;
	// Vertices on the cut boundary that *may* not have a corresonding vertex on the other mesh
	TSet<int> PossUnmatchedBdryVerts;

	FMeshNormals Normals(Mesh);
	Normals.ComputeTriangleNormals();

	FMeshConnectedComponents ConnectedComponents(Mesh);
	ConnectedComponents.FindConnectedTriangles();
	TArray<int> TriToComponentID;  TriToComponentID.Init(-1, Mesh->MaxTriangleID());
	for (int ComponentIdx = 0; ComponentIdx < ConnectedComponents.Num(); ComponentIdx++)
	{
		const FMeshConnectedComponents::FComponent& Component = ConnectedComponents.GetComponent(ComponentIdx);
		for (int TID : Component.Indices)
		{
			TriToComponentID[TID] = ComponentIdx;
		}
	}
	// remap component IDs so they are ordered corresponding to the order of their first triangles in the mesh
	TArray<int> ComponentIDRemap; ComponentIDRemap.Init(-1, ConnectedComponents.Num());
	int RemapIdx = 0;
	for (int TID = 0; TID < Mesh->MaxTriangleID(); TID++)
	{
		int ComponentIdx = TriToComponentID[TID];
		if (ComponentIdx != -1 && ComponentIDRemap[ComponentIdx] == -1)
		{
			ComponentIDRemap[ComponentIdx] = RemapIdx++;
		}
	}
	for (int TID = 0; TID < Mesh->MaxTriangleID(); TID++)
	{
		int& ComponentIdx = TriToComponentID[TID];
		if (ComponentIdx > -1)
		{
			ComponentIdx = ComponentIDRemap[ComponentIdx];
		}
	}

	// delete geometry according to boolean rules, tracking the boundary edges
	{ // (just for scope)
		// decide what triangles to delete
		TArray<bool> KeepTri;
		TFastWindingTree<FDynamicMesh3> Winding(&Spatial);
		int MaxTriID = Mesh->MaxTriangleID();
		KeepTri.SetNumUninitialized(MaxTriID);
		
		ParallelFor(MaxTriID, [this, &Spatial, &Normals, &TriToComponentID, &KeepTri, &Winding](int TID)
		{
			if (!Mesh->IsTriangle(TID))
			{
				return;
			}
			FVector3d Centroid = Mesh->GetTriCentroid(TID);

			double WindingNum = Winding.FastWindingNumber(Centroid + Normals[TID] * NormalOffset) > WindingThreshold;
			
			if (WindingNum > -.0001 && WindingNum < 1.0001) // TODO tune these / don't hardcode?
			{
				double DSq;
				int MyComponentID = TriToComponentID[TID];
				IMeshSpatial::FQueryOptions QueryOptions(SnapTolerance,
					[&TriToComponentID, MyComponentID](int OtherTID)
					{
						return TriToComponentID[OtherTID] != MyComponentID;
					}
				);
				int OtherTID = Spatial.FindNearestTriangle(Centroid, DSq, QueryOptions);
				if (OtherTID > -1) // only consider it coplanar if there is a matching tri
				{
					double DotNormals = Normals[OtherTID].Dot(Normals[TID]);
					// don't consider it actually coplanar unless the tris are actually somewhat aligned
					// TODO: tweak this tolerance?  Or maybe even map back to normals pre-mesh-cut?  Degenerate tris can mess this up.
					if (FMath::Abs(DotNormals) > .9)
					{
						// To be extra sure it's a coplanar match, check the vertices are *also* on the other mesh (w/in SnapTolerance)
						FTriangle3d Tri;
						Mesh->GetTriVertices(TID, Tri.V[0], Tri.V[1], Tri.V[2]);
						bool bAllTrisOnOtherMesh = true;
						for (int Idx = 0; Idx < 3; Idx++)
						{
							if (Spatial.FindNearestTriangle(Tri.V[Idx], DSq, QueryOptions) == FDynamicMesh3::InvalidID)
							{
								bAllTrisOnOtherMesh = false;
								break;
							}
						}
						if (bAllTrisOnOtherMesh)
						{
							if (DotNormals < 0)
							{
								KeepTri[TID] = false;
							}
							else
							{
								int OtherComponentID = TriToComponentID[OtherTID];
								bool bKeep = MyComponentID < OtherComponentID;
								KeepTri[TID] = bKeep;
							}
							return;
						}
					}
				}
			}

			KeepTri[TID] = WindingNum < WindingThreshold;
		});

		// track where we will create new boundary edges
		for (int EID : Mesh->EdgeIndicesItr())
		{
			FIndex2i TriPair = Mesh->GetEdgeT(EID);
			if (TriPair.B == IndexConstants::InvalidID || KeepTri[TriPair.A] == KeepTri[TriPair.B])
			{
				continue;
			}

			CutBoundaryEdges.Add(EID);
			FIndex2i VertPair = Mesh->GetEdgeV(EID);
			PossUnmatchedBdryVerts.Add(VertPair.A);
			PossUnmatchedBdryVerts.Add(VertPair.B);
		}
		
		// actually delete triangles
		for (int TID = 0; TID < KeepTri.Num(); TID++)
		{
			if (Mesh->IsTriangle(TID) && !KeepTri[TID])
			{
				Mesh->RemoveTriangle(TID, true, false);
			}
		}
	}

	if (Cancelled())
	{
		return false;
	}

	// Hash boundary verts for faster search
	TPointHashGrid3d<int> PointHash(Mesh->GetCachedBounds().MaxDim() / 64, -1);
	for (int BoundaryVID : PossUnmatchedBdryVerts)
	{
		PointHash.InsertPointUnsafe(BoundaryVID, Mesh->GetVertex(BoundaryVID));
	}
	
	// mapping of all accepted correspondences of boundary vertices (both ways -- so if A is connected to B we add both A->B and B->A) 
	TMap<int, int> FoundMatches;

	{ // for scope
		TArray<int> BoundaryNbrEdges;
		TArray<int> ExcludeVertices;
		for (int BoundaryVID : PossUnmatchedBdryVerts)
		{
			// skip vertices that we've already matched up
			if (FoundMatches.Contains(BoundaryVID))
			{
				continue;
			}

			FVector3d Pos = Mesh->GetVertex(BoundaryVID);

			// Find a neighborhood of topologically-connected vertices that we can't match to
			// TODO: in theory we should walk SnapTolerance away on the connected boundary edges to build the full exclusion set
			// (in practice just filtering the immediate neighbors should usually be ok?)
			BoundaryNbrEdges.Reset();
			ExcludeVertices.Reset();
			ExcludeVertices.Add(BoundaryVID);
			Mesh->GetAllVtxBoundaryEdges(BoundaryVID, BoundaryNbrEdges);
			for (int EID : BoundaryNbrEdges)
			{
				FIndex2i EdgeVID = Mesh->GetEdgeV(EID);
				ExcludeVertices.Add(EdgeVID.A == BoundaryVID ? EdgeVID.B : EdgeVID.A);
			}

			TPair<int, double> VIDDist = PointHash.FindNearestInRadius(
				Pos, SnapTolerance,
				[this, &Pos](int VID)
				{
					return Pos.DistanceSquared(Mesh->GetVertex(VID));
				},
				[&ExcludeVertices](int VID)
				{
					return ExcludeVertices.Contains(VID);
				}
			);
			int NearestVID = VIDDist.Key; // ID of nearest vertex on other mesh
			double DSq = VIDDist.Value;   // square distance to that vertex

			if (NearestVID != FDynamicMesh3::InvalidID)
			{

				int* Match = FoundMatches.Find(NearestVID);
				if (Match)
				{
					double OldDSq = Mesh->GetVertex(*Match).DistanceSquared(Mesh->GetVertex(NearestVID));
					if (DSq < OldDSq) // new vertex is a better match than the old one
					{
						int OldVID = *Match; // copy old VID out of match before updating the TMap
						FoundMatches.Add(NearestVID, BoundaryVID); // new VID is recorded as best match
						FoundMatches.Add(BoundaryVID, NearestVID);
						FoundMatches.Remove(OldVID);

						// old VID is swapped in as the one to consider as unmatched
						// it will now be matched below
						BoundaryVID = OldVID;
						Mesh->GetAllVtxBoundaryEdges(BoundaryVID, BoundaryNbrEdges);
						Pos = Mesh->GetVertex(BoundaryVID);
						DSq = OldDSq;
					}
					NearestVID = FDynamicMesh3::InvalidID; // one of these vertices will be unmatched
				}
				else
				{
					FoundMatches.Add(NearestVID, BoundaryVID);
					FoundMatches.Add(BoundaryVID, NearestVID);
				}
			}

			// if we didn't find a valid match, try to split the nearest edge to create a match
			if (NearestVID == FDynamicMesh3::InvalidID)
			{
				// vertex had no match -- try to split edge to match it
				int OtherEID = FindNearestEdge(CutBoundaryEdges, BoundaryNbrEdges, Pos);
				if (OtherEID != FDynamicMesh3::InvalidID)
				{
					FVector3d EdgePts[2];
					Mesh->GetEdgeV(OtherEID, EdgePts[0], EdgePts[1]);
					FSegment3d Seg(EdgePts[0], EdgePts[1]);
					double Along = Seg.ProjectUnitRange(Pos);
					FDynamicMesh3::FEdgeSplitInfo SplitInfo;
					if (ensure(EMeshResult::Ok == Mesh->SplitEdge(OtherEID, SplitInfo, Along)))
					{
						FoundMatches.Add(SplitInfo.NewVertex, BoundaryVID);
						FoundMatches.Add(BoundaryVID, SplitInfo.NewVertex);
						Mesh->SetVertex(SplitInfo.NewVertex, Pos);
						CutBoundaryEdges.Add(SplitInfo.NewEdges.A);
						// Note: Do not update PossUnmatchedBdryVerts with the new vertex, because it is already matched by construction
						// Likewise do not update the pointhash -- we don't want it to find vertices that were already perfectly matched
					}
				}
			}
		}
	}

	// actually snap the positions together for final matches
	for (TPair<int, int>& Match : FoundMatches)
	{
		if (Match.Value < Match.Key)
		{
			checkSlow(FoundMatches[Match.Value] == Match.Key);
			continue; // everything is in the map twice, so we only process the Key<Value entries
		}
		Mesh->SetVertex(Match.Value, Mesh->GetVertex(Match.Key));
	}

	if (Cancelled())
	{
		return false;
	}

	bool bWeldSuccess = MergeEdges(CutBoundaryEdges, FoundMatches);

	return bWeldSuccess;
}


bool FMeshSelfUnion::MergeEdges(const TArray<int>& CutBoundaryEdges, const TMap<int, int>& FoundMatches)
{	
	// find "easy" match candidates using the already-made vertex correspondence
	TArray<FIndex2i> CandidateMatches;
	for (int EID : CutBoundaryEdges)
	{
		if (!ensure(Mesh->IsBoundaryEdge(EID)))
		{
			continue;
		}
		FIndex2i VIDs = Mesh->GetEdgeV(EID);
		const int* OtherA = FoundMatches.Find(VIDs.A);
		const int* OtherB = FoundMatches.Find(VIDs.B);
		if (OtherA && OtherB)
		{
			int OtherEID = Mesh->FindEdge(*OtherA, *OtherB);
			// because FoundMatches includes both directions of each mapping
			// only accept the mapping w/ EID < OtherEID (This also excludes OtherEID == InvalidID)
			if (OtherEID > EID)
			{
				checkSlow(OtherEID != FDynamicMesh3::InvalidID);
				CandidateMatches.Add(FIndex2i(EID, OtherEID));
			}
		}
	}

	// merge the easy matches
	for (FIndex2i Candidate : CandidateMatches)
	{
		if (!Mesh->IsEdge(Candidate.A) || !Mesh->IsBoundaryEdge(Candidate.A))
		{
			continue;
		}

		FDynamicMesh3::FMergeEdgesInfo MergeInfo;
		EMeshResult EdgeMergeResult = Mesh->MergeEdges(Candidate.A, Candidate.B, MergeInfo);
	}

	// collect remaining unmatched edges
	TArray<int> UnmatchedEdges;
	for (int EID : CutBoundaryEdges)
	{
		if (Mesh->IsEdge(EID) && Mesh->IsBoundaryEdge(EID))
		{
			UnmatchedEdges.Add(EID);
		}
	}

	// try to greedily match remaining edges within snap tolerance
	double SnapToleranceSq = SnapTolerance * SnapTolerance;
	for (int Idx = 0; Idx + 1 < UnmatchedEdges.Num(); Idx++)
	{
		int EID = UnmatchedEdges[Idx];
		if (!Mesh->IsEdge(EID) || !Mesh->IsBoundaryEdge(EID))
		{
			continue;
		}
		FVector3d A, B;
		Mesh->GetEdgeV(EID, A, B);
		for (int OtherIdx = Idx + 1; OtherIdx < UnmatchedEdges.Num(); OtherIdx++)
		{
			int OtherEID = UnmatchedEdges[OtherIdx];
			if (!Mesh->IsEdge(OtherEID) || !Mesh->IsBoundaryEdge(OtherEID))
			{
				UnmatchedEdges.RemoveAtSwap(OtherIdx, 1, false);
				OtherIdx--;
				continue;
			}
			FVector3d OA, OB;
			Mesh->GetEdgeV(OtherEID, OA, OB);

			if (OA.DistanceSquared(A) < SnapToleranceSq && OB.DistanceSquared(B) < SnapToleranceSq)
			{
				FDynamicMesh3::FMergeEdgesInfo MergeInfo;
				EMeshResult EdgeMergeResult = Mesh->MergeEdges(EID, OtherEID, MergeInfo);
				if (EdgeMergeResult == EMeshResult::Ok)
				{
					UnmatchedEdges.RemoveAtSwap(OtherIdx, 1, false);
					break;
				}
			}
		}
	}

	// store the failure cases
	bool bAllMatched = true;
	for (int EID : UnmatchedEdges)
	{
		if (Mesh->IsEdge(EID) && Mesh->IsBoundaryEdge(EID))
		{
			CreatedBoundaryEdges.Add(EID);
			bAllMatched = false;
		}
	}
	
	return bAllMatched;
}


int FMeshSelfUnion::FindNearestEdge(const TArray<int>& EIDs, const TArray<int>& BoundaryNbrEdges, FVector3d Pos)
{
	int NearEID = FDynamicMesh3::InvalidID;
	double NearSqr = SnapTolerance * SnapTolerance;
	FVector3d EdgePts[2];
	for (int EID : EIDs) {
		if (BoundaryNbrEdges.Contains(EID))
		{
			continue;
		}
		Mesh->GetEdgeV(EID, EdgePts[0], EdgePts[1]);

		FSegment3d Seg(EdgePts[0], EdgePts[1]);
		double DSqr = Seg.DistanceSquared(Pos);
		if (DSqr < NearSqr)
		{
			NearEID = EID;
			NearSqr = DSqr;
		}
	}
	return NearEID;
}
