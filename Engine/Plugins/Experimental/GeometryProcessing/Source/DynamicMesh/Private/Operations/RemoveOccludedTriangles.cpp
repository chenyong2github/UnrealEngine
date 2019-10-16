// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp RemoveOccludedTriangles

#include "Operations/RemoveOccludedTriangles.h"

#include "DynamicMeshEditor.h"
#include "MeshNormals.h"

#include "MeshAdapter.h"

#include "Async/ParallelFor.h"
#include "Misc/ScopeLock.h"


template<typename OccluderTriangleMeshType>
bool TRemoveOccludedTriangles<OccluderTriangleMeshType>::Apply(FTransform3d MeshLocalToOccluderSpace, TMeshAABBTree3<OccluderTriangleMeshType>* Spatial, TFastWindingTree<OccluderTriangleMeshType>* FastWindingTree)
{
	if (Cancelled())
	{
		return false;
	}

	// ray directions
	TArray<FVector3d> RayDirs; int NR = 0;
	
	if (InsideMode == EOcclusionCalculationMode::SimpleOcclusionTest)
	{
		RayDirs.Add(FVector3d::UnitX()); RayDirs.Add(-FVector3d::UnitX());
		RayDirs.Add(FVector3d::UnitY()); RayDirs.Add(-FVector3d::UnitY());
		RayDirs.Add(FVector3d::UnitZ()); RayDirs.Add(-FVector3d::UnitZ());

		for (int AddRayIdx = 0; AddRayIdx < AddRandomRays; AddRayIdx++)
		{
			RayDirs.Add(FVector3d(FMath::VRand()));
		}
		NR = RayDirs.Num();
	}

	auto IsOccludedFWN = [this, &FastWindingTree](const FVector3d& Pt)
	{
		return FastWindingTree->FastWindingNumber(Pt) > WindingIsoValue;
	};
	auto IsOccludedSimple = [this, &Spatial, &RayDirs, &NR](const FVector3d& Pt)
	{
		FRay3d Ray;
		Ray.Origin = Pt;
		
		for (int RayIdx = 0; RayIdx < NR; ++RayIdx)
		{
			Ray.Direction = RayDirs[RayIdx];
			int HitTID = Spatial->FindNearestHitTriangle(Ray);
			if (HitTID == IndexConstants::InvalidID)
			{
				return false;
			}
		}
		return true;
	};

	TFunctionRef<bool(const FVector3d& Pt)> IsOccludedF =
			(InsideMode == EOcclusionCalculationMode::FastWindingNumber) ? 
		(TFunctionRef<bool(const FVector3d& Pt)>)IsOccludedFWN : 
		(TFunctionRef<bool(const FVector3d& Pt)>)IsOccludedSimple;

	bool bForceSingleThread = false;

	TArray<bool> VertexOccluded;
	if ( TriangleSamplingMethod == EOcclusionTriangleSampling::Vertices || TriangleSamplingMethod == EOcclusionTriangleSampling::VerticesAndCentroids )
	{
		VertexOccluded.SetNum(Mesh->MaxVertexID());

		// do not trust source mesh normals; safer to recompute
		FMeshNormals Normals(Mesh);
		Normals.ComputeVertexNormals();

		ParallelFor(Mesh->MaxVertexID(), [this, &Normals, &VertexOccluded, &IsOccludedF, &MeshLocalToOccluderSpace](int32 VID)
		{
			if (!Mesh->IsVertex(VID))
			{
				return;
			}
			FVector3d SamplePos = Mesh->GetVertex(VID);
			FVector3d Normal = Normals[VID];
			SamplePos += Normal * NormalOffset;
			VertexOccluded[VID] = IsOccludedF(MeshLocalToOccluderSpace.TransformPosition(SamplePos));
		}, bForceSingleThread);
	}
	if (Cancelled())
	{
		return false;
	}

	RemovedT.Empty();
	FCriticalSection RemoveTMutex;
	ParallelFor(Mesh->MaxTriangleID(), [this, &VertexOccluded, &IsOccludedF, &RemoveTMutex, &MeshLocalToOccluderSpace](int32 TID)
	{
		if (!Mesh->IsTriangle(TID))
		{
			return;
		}

		bool bInside = true;
		if (TriangleSamplingMethod == EOcclusionTriangleSampling::Vertices || TriangleSamplingMethod == EOcclusionTriangleSampling::VerticesAndCentroids)
		{
			FIndex3i Tri = Mesh->GetTriangle(TID);
			bInside = VertexOccluded[Tri.A] && VertexOccluded[Tri.B] && VertexOccluded[Tri.C];
		}
		if (bInside && (TriangleSamplingMethod == EOcclusionTriangleSampling::Centroids || TriangleSamplingMethod == EOcclusionTriangleSampling::VerticesAndCentroids))
		{
			FVector3d Centroid, Normal;
			double Area;
			Mesh->GetTriInfo(TID, Normal, Area, Centroid);

			FVector3d SamplePos = Centroid + Normal * NormalOffset;
			bInside = IsOccludedF(MeshLocalToOccluderSpace.TransformPosition(SamplePos));
		}
		if (bInside)
		{
			FScopeLock RemoveLock(&RemoveTMutex);
			RemovedT.Add(TID);
		}
	}, bForceSingleThread);


	if (Cancelled())
	{
		return false;
	}

	if (RemovedT.Num() > 0)
	{
		FDynamicMeshEditor Editor(Mesh);
		bool bOK = Editor.RemoveTriangles(RemovedT, true);
		if (!bOK)
		{
			bRemoveFailed = true;
			return false;
		}
		// TODO: do we want to consider if we have made the mesh non-manifold or do any cleanup?
	} 

	return true;
}
