// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshBoolean

#include "Operations/MeshBoolean.h"
#include "Operations/MeshMeshCut.h"

#include "Async/ParallelFor.h"
#include "MeshTransforms.h"

#include "Algo/RemoveIf.h"

#include "DynamicMeshAABBTree3.h"

bool FMeshBoolean::Compute()
{
	// copy meshes
	FDynamicMesh3 CutMeshB(*Meshes[1]);
	*Result = *Meshes[0];
	FDynamicMesh3* CutMesh[2]{ Result, &CutMeshB }; // just an alias to keep things organized

	// transform the copies to a shared space (centered at the origin)
	// TODO: also rescale the meshes to a standard size (?)
	FAxisAlignedBox3d CombinedAABB(CutMesh[0]->GetCachedBounds(), Transforms[0]);
	FAxisAlignedBox3d MeshB_AABB(CutMesh[1]->GetCachedBounds(), Transforms[1]);
	CombinedAABB.Contain(MeshB_AABB);
	for (int MeshIdx = 0; MeshIdx < 2; MeshIdx++)
	{
		FTransform3d CenteredTransform = Transforms[MeshIdx];
		CenteredTransform.SetTranslation(CenteredTransform.GetTranslation() - CombinedAABB.Center());
		MeshTransforms::ApplyTransform(*CutMesh[MeshIdx], CenteredTransform);
	}
	ResultTransform = FTransform3d(CombinedAABB.Center());

	if (Cancelled())
	{
		return false;
	}

	// build spatial data and use it to find intersections
	FDynamicMeshAABBTree3 Spatial[2]{ CutMesh[0], CutMesh[1] };
	MeshIntersection::FIntersectionsQueryResult Intersections = Spatial[0].FindAllIntersections(Spatial[1]);

	if (Cancelled())
	{
		return false;
	}

	// cut the meshes
	FMeshMeshCut Cut(CutMesh[0], CutMesh[1]);
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
		for (int MeshIdx = 0; MeshIdx < 2; MeshIdx++)
		{
			// convert vertex chains to edge IDs to simplify logic of finding remaining candidate edges after collapses
			TArray<int> EIDs;
			for (int ChainIdx = 0; ChainIdx < Cut.VertexChains[MeshIdx].Num();)
			{
				int ChainLen = Cut.VertexChains[MeshIdx][ChainIdx];
				int ChainEnd = ChainIdx + 1 + ChainLen;
				for (int ChainSubIdx = ChainIdx + 1; ChainSubIdx + 1 < ChainEnd; ChainSubIdx++)
				{
					int VID[2]{ Cut.VertexChains[MeshIdx][ChainSubIdx], Cut.VertexChains[MeshIdx][ChainSubIdx + 1] };
					if (CutMesh[MeshIdx]->GetVertex(VID[0]).DistanceSquared(CutMesh[MeshIdx]->GetVertex(VID[1])) < DegenerateEdgeTolSq)
					{
						EIDs.Add(CutMesh[MeshIdx]->FindEdge(VID[0], VID[1]));
					}
				}
				ChainIdx = ChainEnd;
			}
			for (int EID : EIDs)
			{
				if (!ensure(CutMesh[MeshIdx]->IsEdge(EID)))
				{
					continue;
				}
				FVector3d A, B;
				CutMesh[MeshIdx]->GetEdgeV(EID, A, B);
				if (A.DistanceSquared(B) > DegenerateEdgeTolSq)
				{
					continue;
				}
				FIndex2i EV = CutMesh[MeshIdx]->GetEdgeV(EID);

				// if the vertex we'd remove is on a seam, try removing the other one instead
				if (CutMesh[MeshIdx]->HasAttributes() && CutMesh[MeshIdx]->Attributes()->IsSeamVertex(EV.B, false))
				{
					Swap(EV.A, EV.B);
					// if they were both on seams, then collapse should not happen?  (& would break OnCollapseEdge assumptions in overlay)
					if (CutMesh[MeshIdx]->HasAttributes() && CutMesh[MeshIdx]->Attributes()->IsSeamVertex(EV.B, false))
					{
						continue;
					}
				}
				FDynamicMesh3::FEdgeCollapseInfo CollapseInfo;
				EMeshResult CollapseResult = CutMesh[MeshIdx]->CollapseEdge(EV.A, EV.B, CollapseInfo);
			}
		}
	}

	if (Cancelled())
	{
		return false;
	}

	// edges that will become new boundary edges after the boolean op removes triangles on each mesh
	TArray<int> CutBoundaryEdges[2];
	// Vertices on the cut boundary that *may* not have a corresonding vertex on the other mesh
	TSet<int> PossUnmatchedBdryVerts[2];

	// delete geometry according to boolean rules, tracking the boundary edges
	{ // (just for scope)
		// first decide what triangles to delete for both meshes (*before* deleting anything so winding doesn't get messed up!)
		TArray<bool> KeepTri[2];
		for (int MeshIdx = 0; MeshIdx < 2; MeshIdx++)
		{
			TFastWindingTree<FDynamicMesh3> Winding(&Spatial[1 - MeshIdx]);
			FDynamicMeshAABBTree3& OtherSpatial = Spatial[1 - MeshIdx];
			FDynamicMesh3& ProcessMesh = *CutMesh[MeshIdx];
			int MaxTriID = ProcessMesh.MaxTriangleID();
			KeepTri[MeshIdx].SetNumUninitialized(MaxTriID);
			bool bCoplanarKeepSameDir = Operation != EBooleanOp::Difference;
			bool bRemoveInside = 1; // whether to remove the inside triangles (e.g. for union) or the outside ones (e.g. for intersection)
			if (Operation == EBooleanOp::Intersect || (Operation == EBooleanOp::Difference && MeshIdx == 1))
			{
				bRemoveInside = 0;
			}
			ParallelFor(MaxTriID, [this, &KeepTri, &MeshIdx, &Winding, &OtherSpatial, &ProcessMesh, bCoplanarKeepSameDir, bRemoveInside](int TID)
				{
					if (!ProcessMesh.IsTriangle(TID))
					{
						return;
					}
					FVector3d Centroid = ProcessMesh.GetTriCentroid(TID);
					double WindingNum = Winding.FastWindingNumber(Centroid) > WindingThreshold;
					if (WindingNum > -.0001 && WindingNum < 1.0001) // TODO tune these / don't hardcode?
					{
						double DSq;
						int OtherTID = OtherSpatial.FindNearestTriangle(Centroid, DSq, SnapTolerance);
						if (OtherTID > -1) // only consider it coplanar if there is a matching tri
						{
							FVector3d OtherNormal = OtherSpatial.GetMesh()->GetTriNormal(OtherTID);
							FVector3d Normal = ProcessMesh.GetTriNormal(TID);
							double DotNormals = OtherNormal.Dot(Normal);
							// don't consider it actually coplanar unless the tris are actually somewhat aligned
							// TODO: tweak this tolerance?  Or maybe even map back to normals pre-mesh-cut?  Degenerate tris can mess this up.
							if (FMath::Abs(DotNormals) > .9)
							{
								// To be extra sure it's a coplanar match, check the vertices are *also* on the other mesh (w/in SnapTolerance)
								FTriangle3d Tri;
								ProcessMesh.GetTriVertices(TID, Tri.V[0], Tri.V[1], Tri.V[2]);
								bool bAllTrisOnOtherMesh = true;
								for (int Idx = 0; Idx < 3; Idx++)
								{
									if (OtherSpatial.FindNearestTriangle(Tri.V[Idx], DSq, SnapTolerance) == FDynamicMesh3::InvalidID)
									{
										bAllTrisOnOtherMesh = false;
										break;
									}
								}
								if (bAllTrisOnOtherMesh)
								{
									if (MeshIdx != 0) // for coplanar tris favor the first mesh; just delete from the other mesh
									{
										KeepTri[MeshIdx][TID] = false;
										return;
									}
									else // for the first mesh, logic depends on orientation of matching tri
									{
										KeepTri[MeshIdx][TID] = DotNormals > 0 == bCoplanarKeepSameDir;
										return;
									}
								}
							}
						}
					}

					KeepTri[MeshIdx][TID] = (WindingNum > WindingThreshold) != bRemoveInside;
				});
			for (int EID : ProcessMesh.EdgeIndicesItr())
			{
				FIndex2i TriPair = ProcessMesh.GetEdgeT(EID);
				if (TriPair.B == IndexConstants::InvalidID || KeepTri[MeshIdx][TriPair.A] == KeepTri[MeshIdx][TriPair.B])
				{
					continue;
				}

				CutBoundaryEdges[MeshIdx].Add(EID);
				FIndex2i VertPair = ProcessMesh.GetEdgeV(EID);
				PossUnmatchedBdryVerts[MeshIdx].Add(VertPair.A);
				PossUnmatchedBdryVerts[MeshIdx].Add(VertPair.B);
			}
		}
		// now go ahead and delete from both meshes
		for (int MeshIdx = 0; MeshIdx < 2; MeshIdx++)
		{
			FDynamicMesh3& ProcessMesh = *CutMesh[MeshIdx];

			for (int TID = 0; TID < KeepTri[MeshIdx].Num(); TID++)
			{
				if (ProcessMesh.IsTriangle(TID) && !KeepTri[MeshIdx][TID])
				{
					ProcessMesh.RemoveTriangle(TID, true, false);
				}
			}
		}
	}

	if (Cancelled())
	{
		return false;
	}

	// Hash boundary verts for faster search
	TArray<TPointHashGrid3d<int>> PointHashes;
	for (int MeshIdx = 0; MeshIdx < 2; MeshIdx++)
	{
		PointHashes.Emplace(CutMesh[MeshIdx]->GetCachedBounds().MaxDim() / 64, -1);
		for (int BoundaryVID : PossUnmatchedBdryVerts[MeshIdx])
		{
			PointHashes[MeshIdx].InsertPointUnsafe(BoundaryVID, CutMesh[MeshIdx]->GetVertex(BoundaryVID));
		}
	}

	// ensure segments that are now on boundaries have 1:1 vertex correspondence across meshes
	TMap<int, int> AllVIDMatches; // mapping of matched vertex IDs from cutmesh 0 to cutmesh 1
	for (int MeshIdx = 0; MeshIdx < 2; MeshIdx++)
	{
		int OtherMeshIdx = 1 - MeshIdx;
		FDynamicMesh3& OtherMesh = *CutMesh[OtherMeshIdx];

		// mapping from OtherMesh VIDs to ProcessMesh VIDs
		// used to ensure we only keep the best match, in cases where multiple boundary vertices map to a given vertex on the other mesh boundary
		TMap<int, int> FoundMatches;

		for (int BoundaryVID : PossUnmatchedBdryVerts[MeshIdx])
		{
			FVector3d Pos = CutMesh[MeshIdx]->GetVertex(BoundaryVID);
			TPair<int, double> VIDDist = PointHashes[OtherMeshIdx].FindNearestInRadius(Pos, SnapTolerance, [&Pos, &OtherMesh](int VID)
				{
					return Pos.DistanceSquared(OtherMesh.GetVertex(VID));
				});
			int NearestVID = VIDDist.Key; // ID of nearest vertex on other mesh
			double DSq = VIDDist.Value;   // square distance to that vertex

			if (NearestVID != FDynamicMesh3::InvalidID)
			{

				int* Match = FoundMatches.Find(NearestVID);
				if (Match)
				{
					double OldDSq = CutMesh[MeshIdx]->GetVertex(*Match).DistanceSquared(OtherMesh.GetVertex(NearestVID));
					if (DSq < OldDSq) // new vertex is a better match than the old one
					{
						int OldVID = *Match; // copy old VID out of match before updating the TMap
						FoundMatches.Add(NearestVID, BoundaryVID); // new VID is recorded as best match

						// old VID is swapped in as the one to consider as unmatched
						// it will now be matched below
						BoundaryVID = OldVID;
						Pos = CutMesh[MeshIdx]->GetVertex(BoundaryVID);
						DSq = OldDSq;
					}
					NearestVID = FDynamicMesh3::InvalidID; // one of these vertices will be unmatched
				}
				else
				{
					FoundMatches.Add(NearestVID, BoundaryVID);
				}
			}

			// if we didn't find a valid match, try to split the nearest edge to create a match
			if (NearestVID == FDynamicMesh3::InvalidID)
			{
				// vertex had no match -- try to split edge to match it
				int OtherEID = FindNearestEdge(OtherMesh, CutBoundaryEdges[OtherMeshIdx], Pos);
				if (OtherEID != FDynamicMesh3::InvalidID)
				{
					FVector3d EdgePts[2];
					OtherMesh.GetEdgeV(OtherEID, EdgePts[0], EdgePts[1]);
					FSegment3d Seg(EdgePts[0], EdgePts[1]);
					double Along = Seg.ProjectUnitRange(Pos);
					FDynamicMesh3::FEdgeSplitInfo SplitInfo;
					if (ensure(EMeshResult::Ok == OtherMesh.SplitEdge(OtherEID, SplitInfo, Along)))
					{
						FoundMatches.Add(SplitInfo.NewVertex, BoundaryVID);
						OtherMesh.SetVertex(SplitInfo.NewVertex, Pos);
						CutBoundaryEdges[OtherMeshIdx].Add(SplitInfo.NewEdges.A);
						// Note: Do not update PossUnmatchedBdryVerts with the new vertex, because it is already matched by construction
						// Likewise do not update the pointhash -- we don't want it to find vertices that were already perfectly matched
					}
				}
			}
		}

		// actually snap the positions together for final matches
		for (TPair<int, int>& Match : FoundMatches)
		{
			CutMesh[MeshIdx]->SetVertex(Match.Value, OtherMesh.GetVertex(Match.Key));

			// Copy match to AllVIDMatches; note this is always mapping from CutMesh 0 to 1
			int VIDs[2]{ Match.Key, Match.Value }; // just so we can access by index
			AllVIDMatches.Add(VIDs[1 - MeshIdx], VIDs[MeshIdx]);
		}
	}

	if (Operation == EBooleanOp::Difference)
	{
		// TODO: implement a way to flip all the triangles in the mesh without building this AllTID array
		TArray<int> AllTID;
		for (int TID : CutMesh[1]->TriangleIndicesItr())
		{
			AllTID.Add(TID);
		}
		FDynamicMeshEditor FlipEditor(CutMesh[1]);
		FlipEditor.ReverseTriangleOrientations(AllTID, true);
	}

	if (Cancelled())
	{
		return false;
	}

	FDynamicMeshEditor Editor(Result);
	FMeshIndexMappings IndexMaps;
	Editor.AppendMesh(CutMesh[1], IndexMaps);

	bool bWeldSuccess = MergeEdges(IndexMaps, CutMesh, CutBoundaryEdges, AllVIDMatches);

	return bWeldSuccess;
}


bool FMeshBoolean::MergeEdges(const FMeshIndexMappings& IndexMaps, FDynamicMesh3* CutMesh[2], const TArray<int> CutBoundaryEdges[2], const TMap<int, int>& AllVIDMatches)
{
	// translate the edge IDs from CutMesh[1] over to Result mesh edge IDs
	TArray<int> OtherMeshEdges;
	for (int OldMeshEID : CutBoundaryEdges[1])
	{
		FIndex2i OtherEV = CutMesh[1]->GetEdgeV(OldMeshEID);
		int MappedEID = Result->FindEdge(IndexMaps.GetNewVertex(OtherEV.A), IndexMaps.GetNewVertex(OtherEV.B));
		if (ensure(Result->IsBoundaryEdge(MappedEID)))
		{
			OtherMeshEdges.Add(MappedEID);
		}
	}

	// find "easy" match candidates using the already-made vertex correspondence
	TArray<FIndex2i> CandidateMatches;
	for (int EID : CutBoundaryEdges[0])
	{
		if (!ensure(Result->IsBoundaryEdge(EID)))
		{
			continue;
		}
		FIndex2i VIDs = Result->GetEdgeV(EID);
		const int* OtherA = AllVIDMatches.Find(VIDs.A);
		const int* OtherB = AllVIDMatches.Find(VIDs.B);
		if (OtherA && OtherB)
		{
			int MapOtherA = IndexMaps.GetNewVertex(*OtherA);
			int MapOtherB = IndexMaps.GetNewVertex(*OtherB);
			int OtherEID = Result->FindEdge(MapOtherA, MapOtherB);
			if (OtherEID != FDynamicMesh3::InvalidID)
			{
				CandidateMatches.Add(FIndex2i(EID, OtherEID));
			}
		}
	}

	// merge the easy matches
	TArray<int> UnmatchedEdges;
	for (FIndex2i Candidate : CandidateMatches)
	{
		if (!Result->IsEdge(Candidate.A) || !Result->IsBoundaryEdge(Candidate.A))
		{
			continue;
		}

		FDynamicMesh3::FMergeEdgesInfo MergeInfo;
		EMeshResult EdgeMergeResult = Result->MergeEdges(Candidate.A, Candidate.B, MergeInfo);
		if (EdgeMergeResult != EMeshResult::Ok)
		{
			UnmatchedEdges.Add(Candidate.A);
		}
	}

	// filter matched edges from the edge array for the other mesh
	OtherMeshEdges.SetNum(Algo::RemoveIf(OtherMeshEdges, [this](int EID)
		{
			return !Result->IsEdge(EID) || !Result->IsBoundaryEdge(EID);
		}));

	// see if we can match anything else
	bool bAllMatched = CutBoundaryEdges[0].Num() == CutBoundaryEdges[1].Num();
	if (UnmatchedEdges.Num() > 0)
	{
		// greedily match within snap tolerance
		double SnapToleranceSq = SnapTolerance * SnapTolerance;
		for (int OtherEID : OtherMeshEdges)
		{
			if (!Result->IsEdge(OtherEID) || !Result->IsBoundaryEdge(OtherEID))
			{
				continue;
			}
			FVector3d OA, OB;
			Result->GetEdgeV(OtherEID, OA, OB);
			for (int UnmatchedIdx = 0; UnmatchedIdx < UnmatchedEdges.Num(); UnmatchedIdx++)
			{
				int EID = UnmatchedEdges[UnmatchedIdx];
				if (!Result->IsEdge(EID) || !Result->IsBoundaryEdge(EID))
				{
					UnmatchedEdges.RemoveAtSwap(UnmatchedIdx, 1, false);
					UnmatchedIdx--;
					continue;
				}
				FVector3d A, B;
				Result->GetEdgeV(EID, A, B);
				if (OA.DistanceSquared(A) < SnapToleranceSq && OB.DistanceSquared(B) < SnapToleranceSq)
				{
					FDynamicMesh3::FMergeEdgesInfo MergeInfo;
					EMeshResult EdgeMergeResult = Result->MergeEdges(EID, OtherEID, MergeInfo);
					if (EdgeMergeResult == EMeshResult::Ok)
					{
						UnmatchedEdges.RemoveAtSwap(UnmatchedIdx, 1, false);
						break;
					}
				}
			}
		}

		// store the failure cases from the first mesh's array
		for (int EID : UnmatchedEdges)
		{
			if (Result->IsEdge(EID) && Result->IsBoundaryEdge(EID))
			{
				CreatedBoundaryEdges.Add(EID);
				bAllMatched = false;
			}
		}
	}
	// store the failure cases from the second mesh's array
	for (int OtherEID : OtherMeshEdges)
	{
		if (Result->IsEdge(OtherEID) && Result->IsBoundaryEdge(OtherEID))
		{
			CreatedBoundaryEdges.Add(OtherEID);
			bAllMatched = false;
		}
	}
	return bAllMatched;
}


int FMeshBoolean::FindNearestEdge(const FDynamicMesh3& OnMesh, const TArray<int>& EIDs, FVector3d Pos)
{
	int NearEID = FDynamicMesh3::InvalidID;
	double NearSqr = SnapTolerance * SnapTolerance;
	FVector3d EdgePts[2];
	for (int EID : EIDs) {
		OnMesh.GetEdgeV(EID, EdgePts[0], EdgePts[1]);

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
