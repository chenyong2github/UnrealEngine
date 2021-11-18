// Copyright Epic Games, Inc. All Rights Reserved.

#include "Polygroups/PolygroupsGenerator.h"
#include "DynamicMesh/MeshNormals.h"
#include "Selections/MeshConnectedComponents.h"
#include "Util/IndexUtil.h"
#include "Parameterization/MeshDijkstra.h"
#include "Parameterization/IncrementalMeshDijkstra.h"
#include "Parameterization/MeshRegionGraph.h"
#include "Async/ParallelFor.h"

#include "Curve/DynamicGraph3.h"

using namespace UE::Geometry;

FPolygroupsGenerator::FPolygroupsGenerator(FDynamicMesh3* MeshIn)
{
	Mesh = MeshIn;
}



bool FPolygroupsGenerator::FindPolygroupsFromUVIslands(int32 UVLayer)
{
	const FDynamicMeshUVOverlay* UV = Mesh->Attributes()->GetUVLayer(UVLayer);
	if (!UV)
	{
		return false;
	}

	FMeshConnectedComponents Components(Mesh);
	Components.FindConnectedTriangles([&UV](int32 TriIdx0, int32 TriIdx1)
	{
		return UV->AreTrianglesConnected(TriIdx0, TriIdx1);
	});

	int32 NumComponents = Components.Num();
	for (int32 ci = 0; ci < NumComponents; ++ci)
	{
		FoundPolygroups.Add(Components.GetComponent(ci).Indices);
	}

	if (bApplyPostProcessing)
	{
		PostProcessPolygroups(false);
	}
	if (bCopyToMesh)
	{
		CopyPolygroupsToMesh();
	}

	return (FoundPolygroups.Num() > 0);
}


bool FPolygroupsGenerator::FindPolygroupsFromHardNormalSeams()
{
	const FDynamicMeshNormalOverlay* Normals = Mesh->Attributes()->GetNormalLayer(0);
	if (!Normals)
	{
		return false;
	}

	FMeshConnectedComponents Components(Mesh);
	Components.FindConnectedTriangles([Normals](int32 TriIdx0, int32 TriIdx1)
	{
		return Normals->AreTrianglesConnected(TriIdx0, TriIdx1);
	});

	int32 NumComponents = Components.Num();
	for (int32 ci = 0; ci < NumComponents; ++ci)
	{
		FoundPolygroups.Add(Components.GetComponent(ci).Indices);
	}

	if (bApplyPostProcessing)
	{
		PostProcessPolygroups(false);
	}
	if (bCopyToMesh)
	{
		CopyPolygroupsToMesh();
	}

	return (FoundPolygroups.Num() > 0);
}


bool FPolygroupsGenerator::FindPolygroupsFromConnectedTris()
{

	FMeshConnectedComponents Components(Mesh);
	Components.FindConnectedTriangles([this](int32 TriIdx0, int32 TriIdx1)
	{
		FIndex3i NbrTris = Mesh->GetTriNeighbourTris(TriIdx0);
		int NbrIndex = IndexUtil::FindTriIndex(TriIdx1, NbrTris);
		return (NbrIndex != IndexConstants::InvalidID);
	});

	int32 NumComponents = Components.Num();
	for (int32 ci = 0; ci < NumComponents; ++ci)
	{
		FoundPolygroups.Add(Components.GetComponent(ci).Indices);
	}

	if (bApplyPostProcessing)
	{
		PostProcessPolygroups(false);
	}
	if (bCopyToMesh)
	{
		CopyPolygroupsToMesh();
	}

	return (FoundPolygroups.Num() > 0);
}



bool FPolygroupsGenerator::FindPolygroupsFromFaceNormals(double DotTolerance)
{
	DotTolerance = 1.0 - DotTolerance;

	// compute face normals
	FMeshNormals Normals(Mesh);
	Normals.ComputeTriangleNormals();

	TArray<bool> DoneTriangle;
	DoneTriangle.SetNum(Mesh->MaxTriangleID());

	TArray<int> Stack;

	// grow outward from vertices until we have no more left
	for (int TriID : Mesh->TriangleIndicesItr())
	{
		if (DoneTriangle[TriID] == true)
		{
			continue;
		}

		TArray<int> Polygroup;
		Polygroup.Add(TriID);
		DoneTriangle[TriID] = true;

		Stack.SetNum(0);
		Stack.Add(TriID);
		while (Stack.Num() > 0)
		{
			int CurTri = Stack.Pop(false);
			FIndex3i NbrTris = Mesh->GetTriNeighbourTris(CurTri);
			for (int j = 0; j < 3; ++j)
			{
				if (NbrTris[j] >= 0
					&& DoneTriangle[NbrTris[j]] == false)
				{
					double Dot = Normals[CurTri].Dot(Normals[NbrTris[j]]);
					if (Dot > DotTolerance)
					{
						Polygroup.Add(NbrTris[j]);
						Stack.Add(NbrTris[j]);
						DoneTriangle[NbrTris[j]] = true;
					}
				}
			}
		}

		FoundPolygroups.Add(Polygroup);
	}

	if (bApplyPostProcessing)
	{
		PostProcessPolygroups(true);
	}
	if (bCopyToMesh)
	{
		CopyPolygroupsToMesh();
	}

	return (FoundPolygroups.Num() > 0);
}



/**
 * Dual graph of mesh faces, ie graph of edges across faces between face centers.
 * Normals and Areas are tracked for each point.
 */
class FMeshFaceDualGraph : public FDynamicGraph3d
{
public:
	TDynamicVectorN<double, 3> Normals;
	TDynamicVector<double> Areas;

	int AppendVertex(const FVector3d& Centroid, const FVector3d& Normal, double Area)
	{
		int vid = FDynamicGraph3d::AppendVertex(Centroid);
		Normals.InsertAt({ {Normal.X, Normal.Y, Normal.Z} }, vid);
		Areas.InsertAt(Area, vid);
		return vid;
	}

	FVector3d GetNormal(int32 vid) const
	{
		return Normals.AsVector3(vid);
	}

	/** Build a Face Dual Graph for a triangle mesh */
	static void MakeFaceDualGraphForMesh(
		FDynamicMesh3* Mesh, 
		FMeshFaceDualGraph& FaceGraph,
		TFunctionRef<bool(int,int)> TrisConnectedPredicate )
	{
		// if not true, code below needs updating
		check(Mesh->IsCompactT());

		for (int32 tid : Mesh->TriangleIndicesItr())
		{
			FVector3d Normal, Centroid;
			double Area;
			Mesh->GetTriInfo(tid, Normal, Area, Centroid);

			int32 newid = FaceGraph.AppendVertex(Centroid, Normal, Area);
			check(newid == tid);
		}

		for (int32 tid : Mesh->TriangleIndicesItr())
		{
			FIndex3i NbrT = Mesh->GetTriNeighbourTris(tid);
			for (int32 j = 0; j < 3; ++j)
			{
				if (Mesh->IsTriangle(NbrT[j]) && TrisConnectedPredicate(tid, NbrT[j]) )
				{
					FaceGraph.AppendEdge(tid, NbrT[j]);
				}
			}
		}
	}

};






bool FPolygroupsGenerator::FindPolygroupsFromFurthestPointSampling(
	int32 NumPoints, 
	EWeightingType WeightingType, 
	FVector3d WeightingCoeffs,
	FPolygroupSet* StartingGroups)
{
	NumPoints = FMath::Min(NumPoints, Mesh->VertexCount());

	// cannot seem to use auto or TUniqueFunction here...
	TFunction<bool(int,int)> TrisConnectedPredicate = [](int, int) -> bool { return true; };
	if (StartingGroups != nullptr)
	{
		TrisConnectedPredicate = [StartingGroups](int a, int b) -> bool { return StartingGroups->GetGroup(a) == StartingGroups->GetGroup(b) ? true : false; };
	}

	FMeshFaceDualGraph FaceGraph;
	FMeshFaceDualGraph::MakeFaceDualGraphForMesh(Mesh, FaceGraph, TrisConnectedPredicate);

	TIncrementalMeshDijkstra<FMeshFaceDualGraph> FurthestPoints(&FaceGraph);

	TArray<int32> SeedIndices;

	// need to add at least one seed point for each mesh connected component, so that all triangles are assigned a group
	// TODO: two seed points for components that have no boundary?
	FMeshConnectedComponents Components(Mesh);
	Components.FindConnectedTriangles(TrisConnectedPredicate);
	int32 NumConnected = Components.Num();
	for (int32 k = 0; k < NumConnected; ++k)
	{
		SeedIndices.Add(Components[k].Indices[0]);
	}

	// initial incremental update from per-component points
	TArray<TIncrementalMeshDijkstra<FMeshFaceDualGraph>::FSeedPoint> ComponentSeeds;
	for (int32 vid : SeedIndices)
	{
		ComponentSeeds.Add(TIncrementalMeshDijkstra<FMeshFaceDualGraph>::FSeedPoint{ vid, vid, 0.0 });
	}
	FurthestPoints.AddSeedPoints(ComponentSeeds);

	// TODO: can (approximately) bound the size of a region based on mesh area. Then can pass that in
	// with seed point as upper distance bound. This will change the result as it will initially grow on
	// the 'front' (and possibly no guarantee that mesh is covered?)
	//    (initial furthest-points would be any with an invalid value...then we can ensure coverage)
	// FindMaxGraphDistancePointID() seems like it might be somewhat expensive...

	// incrementally add furthest-points
	while ( SeedIndices.Num() < NumPoints)
	{
		int32 NextPointID = FurthestPoints.FindMaxGraphDistancePointID();
		if (NextPointID >= 0)
		{
			TIncrementalMeshDijkstra<FMeshFaceDualGraph>::FSeedPoint NextSeedPoint{ NextPointID, NextPointID, 0.0 };
			FurthestPoints.AddSeedPoints({ NextSeedPoint });
			SeedIndices.Add(NextPointID);
		}
		else
		{
			break;
		}
	}


	// Now that we have furthest point set, recompute a Dijkstra propagation with optional weighting
	// (unweighted version should be the same as the FurthestPoints dijkstra though...could re-use?)

	TMeshDijkstra<FMeshFaceDualGraph> SuperPixels(&FaceGraph);
	
	if (WeightingType == EWeightingType::NormalDeviation)
	{
		SuperPixels.bEnableDistanceWeighting = true;
		SuperPixels.GetWeightedDistanceFunc = [this, WeightingCoeffs, &FaceGraph](int32 FromVID, int32 ToVID, int32 SeedVID, double Distance)
		{
			FVector3d NA = FaceGraph.GetNormal(ToVID);
			FVector3d NB = FaceGraph.GetNormal(SeedVID);
			double Dot = NA.Dot(NB);
			if (WeightingCoeffs.X > 0.001)
			{
				Dot = FMathd::Pow(Dot, WeightingCoeffs.X);
			}
			double W = FMathd::Clamp(1.0 - Dot * Dot, 0.0, 1.0);
			W = W * W * W;
			double Weight = FMathd::Clamp(W, 0.001, 1.0);
			return Weight * Distance;
		};
	}

	TArray<TMeshDijkstra<FMeshFaceDualGraph>::FSeedPoint> SuperPixelSeeds;
	for (int32 k = 0; k < SeedIndices.Num(); ++k)
	{
		SuperPixelSeeds.Add(TMeshDijkstra<FMeshFaceDualGraph>::FSeedPoint{ k, SeedIndices[k], 0.0 });
	}
	SuperPixels.ComputeToMaxDistance(SuperPixelSeeds, TNumericLimits<double>::Max());

	TArray<TArray<int32>> TriSets;
	TriSets.SetNum(SeedIndices.Num());
	TArray<int32> FailedSet;

	for (int32 tid : Mesh->TriangleIndicesItr())
	{
		int32 SeedID = SuperPixels.GetSeedExternalIDForPointSetID(tid);
		if (SeedID >= 0)
		{
			TriSets[SeedID].Add(tid);
		}
		else
		{
			FailedSet.Add(tid);
		}
	}


	for (int32 k = 0; k < TriSets.Num(); ++k)
	{
		if (TriSets[k].Num() > 0)
		{
			FoundPolygroups.Add(MoveTemp(TriSets[k]));
		}
	}
	
	if (FailedSet.Num() > 0)
	{
		FMeshConnectedComponents FailedComponents(Mesh);
		FailedComponents.FindConnectedTriangles(FailedSet);
		for (FMeshConnectedComponents::FComponent& Component : FailedComponents)
		{
			FoundPolygroups.Add(Component.Indices);
		}
	}

	if (bApplyPostProcessing)
	{
		PostProcessPolygroups(true);
	}
	if (bCopyToMesh)
	{
		CopyPolygroupsToMesh();
	}

	return (FoundPolygroups.Num() > 0);
}




void FPolygroupsGenerator::PostProcessPolygroups(bool bApplyMerging)
{
	if (bApplyMerging && MinGroupSize > 1)
	{
		OptimizePolygroups();
	}
}


void FPolygroupsGenerator::OptimizePolygroups()
{
	FMeshRegionGraph RegionGraph;
	RegionGraph.BuildFromTriangleSets(*Mesh, FoundPolygroups, [&](int32 SetIdx) { return SetIdx; });
	bool bMerged = RegionGraph.MergeSmallRegions(MinGroupSize-1, [&](int32 A, int32 B) { return RegionGraph.GetRegionTriCount(A) > RegionGraph.GetRegionTriCount(B); });
	bool bSwapped = RegionGraph.OptimizeBorders();
	if (bMerged || bSwapped)
	{
		FoundPolygroups.Reset();

		int32 N = RegionGraph.MaxRegionIndex();
		for (int32 k = 0; k < N; ++k)
		{
			if (RegionGraph.IsRegion(k))
			{
				const TArray<int32>& Tris = RegionGraph.GetRegionTris(k);
				FoundPolygroups.Add(Tris);
			}
		}
	}
}




void FPolygroupsGenerator::CopyPolygroupsToMesh()
{
	Mesh->EnableTriangleGroups(0);

	// set groups from Polygroups
	int NumPolygroups = FoundPolygroups.Num();

	// can be parallel for
	for (int PolyIdx = 0; PolyIdx < NumPolygroups; PolyIdx++)
	{
		const TArray<int>& Polygroup = FoundPolygroups[PolyIdx];
		int NumTriangles = Polygroup.Num();
		for (int k = 0; k < NumTriangles; ++k)
		{
			Mesh->SetTriangleGroup(Polygroup[k], (PolyIdx + 1));
		}
	}
}

void FPolygroupsGenerator::CopyPolygroupsToPolygroupSet(FPolygroupSet& Polygroups, FDynamicMesh3& TargetMesh)
{
	// set groups from Polygroups
	int NumPolygroups = FoundPolygroups.Num();

	// can be parallel for
	for (int PolyIdx = 0; PolyIdx < NumPolygroups; PolyIdx++)
	{
		const TArray<int>& Polygroup = FoundPolygroups[PolyIdx];
		int NumTriangles = Polygroup.Num();
		for (int k = 0; k < NumTriangles; ++k)
		{
			Polygroups.SetGroup(Polygroup[k], (PolyIdx + 1), TargetMesh);
		}
	}
}



bool FPolygroupsGenerator::FindPolygroupEdges()
{
	PolygroupEdges.Reset();

	for (int eid : Mesh->EdgeIndicesItr())
	{
		if (Mesh->IsGroupBoundaryEdge(eid))
		{
			PolygroupEdges.Add(eid);
		}
	}
	return (PolygroupEdges.Num() > 0);
}
