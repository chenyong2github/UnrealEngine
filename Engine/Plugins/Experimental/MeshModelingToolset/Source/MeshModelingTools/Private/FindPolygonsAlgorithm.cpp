// Copyright Epic Games, Inc. All Rights Reserved.

#include "FindPolygonsAlgorithm.h"
#include "DynamicMesh/MeshNormals.h"
#include "Selections/MeshConnectedComponents.h"
#include "Util/IndexUtil.h"
#include "Parameterization/MeshDijkstra.h"
#include "Parameterization/IncrementalMeshDijkstra.h"
#include "Parameterization/MeshRegionGraph.h"

#include "Curve/DynamicGraph3.h"



#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

FFindPolygonsAlgorithm::FFindPolygonsAlgorithm(FDynamicMesh3* MeshIn)
{
	Mesh = MeshIn;
}



bool FFindPolygonsAlgorithm::FindPolygonsFromUVIslands()
{
	const FDynamicMeshUVOverlay* UV = Mesh->Attributes()->GetUVLayer(0);

	FMeshConnectedComponents Components(Mesh);
	Components.FindConnectedTriangles([&UV](int32 TriIdx0, int32 TriIdx1)
	{
		return UV->AreTrianglesConnected(TriIdx0, TriIdx1);
	});

	int32 NumComponents = Components.Num();
	for (int32 ci = 0; ci < NumComponents; ++ci)
	{
		FoundPolygons.Add(Components.GetComponent(ci).Indices);
	}

	PostProcessPolygons(false);
	SetGroupsFromPolygons();

	return (FoundPolygons.Num() > 0);
}



bool FFindPolygonsAlgorithm::FindPolygonsFromConnectedTris()
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
		FoundPolygons.Add(Components.GetComponent(ci).Indices);
	}

	PostProcessPolygons(false);
	SetGroupsFromPolygons();

	return (FoundPolygons.Num() > 0);
}



bool FFindPolygonsAlgorithm::FindPolygonsFromFaceNormals(double DotTolerance)
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

		TArray<int> Polygon;
		Polygon.Add(TriID);
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
						Polygon.Add(NbrTris[j]);
						Stack.Add(NbrTris[j]);
						DoneTriangle[NbrTris[j]] = true;
					}
				}
			}
		}

		FoundPolygons.Add(Polygon);
	}

	PostProcessPolygons(true);
	SetGroupsFromPolygons();

	return (FoundPolygons.Num() > 0);
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
	static void MakeFaceDualGraphForMesh(FDynamicMesh3* Mesh, FMeshFaceDualGraph& FaceGraph)
	{
		for (int32 tid : Mesh->TriangleIndicesItr())
		{
			FVector3d Normal, Centroid;
			double Area;
			Mesh->GetTriInfo(tid, Normal, Area, Centroid);

			int32 newvid = FaceGraph.AppendVertex(Centroid, Normal, Area);
			check(newvid == tid);
		}

		for (int32 tid : Mesh->TriangleIndicesItr())
		{
			FIndex3i NbrT = Mesh->GetTriNeighbourTris(tid);
			for (int32 j = 0; j < 3; ++j)
			{
				if (Mesh->IsTriangle(NbrT[j]))
				{
					FaceGraph.AppendEdge(tid, NbrT[j]);
				}
			}
		}
	}

};






bool FFindPolygonsAlgorithm::FindPolygonsFromFurthestPointSampling(int32 NumPoints, EWeightingType WeightingType, FVector3d WeightingCoeffs)
{
	NumPoints = FMath::Min(NumPoints, Mesh->VertexCount());

	FMeshFaceDualGraph FaceGraph;
	FMeshFaceDualGraph::MakeFaceDualGraphForMesh(Mesh, FaceGraph);

	TIncrementalMeshDijkstra<FMeshFaceDualGraph> FurthestPoints(&FaceGraph);

	TArray<int32> SeedIndices;

	// need to add at least one seed point for each mesh connected component, so that all triangles are assigned a group
	// TODO: two seed points for components that have no boundary?
	FMeshConnectedComponents Components(Mesh);
	Components.FindConnectedTriangles();
	int32 NumConnected = Components.Num();
	for (int32 k = 0; k < NumConnected; ++k)
	{
		FIndex3i TriVerts = Mesh->GetTriangle(Components[k].Indices[0]);
		SeedIndices.Add(TriVerts.A);
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


	for (int32 k = 0; k < NumPoints; ++k)
	{
		if (TriSets[k].Num() > 0)
		{
			FoundPolygons.Add(MoveTemp(TriSets[k]));
		}
	}
	
	if (FailedSet.Num() > 0)
	{
		FMeshConnectedComponents FailedComponents(Mesh);
		FailedComponents.FindConnectedTriangles(FailedSet);
		for (FMeshConnectedComponents::FComponent& Component : FailedComponents)
		{
			FoundPolygons.Add(Component.Indices);
		}
	}

	PostProcessPolygons(true);
	SetGroupsFromPolygons();

	return (FoundPolygons.Num() > 0);
}




void FFindPolygonsAlgorithm::PostProcessPolygons(bool bApplyMerging)
{
	if (bApplyMerging && MinGroupSize > 1)
	{
		OptimizePolygons();
	}
}


void FFindPolygonsAlgorithm::OptimizePolygons()
{
	FMeshRegionGraph RegionGraph;
	RegionGraph.BuildFromTriangleSets(*Mesh, FoundPolygons, [&](int32 SetIdx) { return SetIdx; });
	bool bMerged = RegionGraph.MergeSmallRegions(MinGroupSize-1, [&](int32 A, int32 B) { return RegionGraph.GetRegionTriCount(A) > RegionGraph.GetRegionTriCount(B); });
	bool bSwapped = RegionGraph.OptimizeBorders();
	if (bMerged || bSwapped)
	{
		FoundPolygons.Reset();

		int32 N = RegionGraph.MaxRegionIndex();
		for (int32 k = 0; k < N; ++k)
		{
			if (RegionGraph.IsRegion(k))
			{
				const TArray<int32>& Tris = RegionGraph.GetRegionTris(k);
				FoundPolygons.Add(Tris);
			}
		}
	}
}




void FFindPolygonsAlgorithm::SetGroupsFromPolygons()
{
	Mesh->EnableTriangleGroups(0);

	// set groups from polygons
	int NumPolygons = FoundPolygons.Num();
	PolygonTags.SetNum(NumPolygons);
	PolygonNormals.SetNum(NumPolygons);
	// can be parallel for
	for (int PolyIdx = 0; PolyIdx < NumPolygons; PolyIdx++)
	{
		const TArray<int>& Polygon = FoundPolygons[PolyIdx];
		FVector3d AccumNormal(0, 0, 0);
		int NumTriangles = Polygon.Num();
		for (int k = 0; k < NumTriangles; ++k)
		{
			Mesh->SetTriangleGroup(Polygon[k], (PolyIdx + 1));
			AccumNormal += Mesh->GetTriArea(k) * Mesh->GetTriNormal(Polygon[k]);
		}
		PolygonTags[PolyIdx] = (PolyIdx + 1);

		// find a normal if the average failed
		UE::Geometry::Normalize(AccumNormal);
		int SubIdx = 0;
		while (AccumNormal.Length() < 0.9 && SubIdx < NumTriangles)
		{
			AccumNormal = Mesh->GetTriNormal(Polygon[SubIdx++]);
		}
		if (AccumNormal.Length() < 0.9)
		{
			AccumNormal = FVector3d::UnitY();
		}

		PolygonNormals[PolyIdx] = AccumNormal;
	}
}





bool FFindPolygonsAlgorithm::FindPolygonEdges()
{
	for (int eid : Mesh->EdgeIndicesItr())
	{
		if (Mesh->IsGroupBoundaryEdge(eid))
		{
			PolygonEdges.Add(eid);
		}
	}
	return (PolygonEdges.Num() > 0);
}
