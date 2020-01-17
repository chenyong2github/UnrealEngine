// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp RemoveOccludedTriangles

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "DynamicMesh3.h"
#include "Spatial/MeshAABBTree3.h"
#include "Spatial/FastWinding.h"

#include "Math/RandomStream.h"

#include "MeshAdapter.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"

#include "Async/ParallelFor.h"
#include "Misc/ScopeLock.h"


#include "Util/ProgressCancel.h"

enum class EOcclusionTriangleSampling
{
	Centroids,
	Vertices,
	VerticesAndCentroids
};

enum class EOcclusionCalculationMode
{
	FastWindingNumber,
	SimpleOcclusionTest
};

/**
 * Remove "occluded" triangles, i.e. triangles on the "inside" of the mesh(es).
 * This is a fuzzy definition, current implementation has a couple of options
 * including a winding number-based version and an ambient-occlusion-ish version,
 * where if face is occluded for all test rays, then we classify it as inside and remove it.
 */
template<typename OccluderTriangleMeshType>
class TRemoveOccludedTriangles
{
public:

	FDynamicMesh3* Mesh;

	TRemoveOccludedTriangles(FDynamicMesh3* Mesh) : Mesh(Mesh)
	{
	}
	virtual ~TRemoveOccludedTriangles() {}

	/**
	 * Remove the occluded triangles, considering the given occluder AABB tree (which may represent more geometry than a single mesh)
	 * See alternative versions below if you would like to not precompute acceleration structures; these are mainly provided so the N-mutual-occluding-objects case can run on each mesh separately without 
	 *
	 * @param LocalToWorld Transform to take the local mesh into the space of the occluders
	 * @param Spatial AABB trees for all occluders to consider
	 * @param FastWindingTrees Precomputed fast winding trees for occluder
	 * @return true on success
	 */
	virtual bool Apply(FTransform3d MeshLocalToOccluderSpace, TMeshAABBTree3<OccluderTriangleMeshType>* Spatial, TFastWindingTree<OccluderTriangleMeshType>* FastWindingTree)
	{
		if (Cancelled())
		{
			return false;
		}

		// ray directions
		TArray<FVector3d> RayDirs; int NR = 0;

		FRandomStream RaysRandomStream(2123123);
		if (InsideMode == EOcclusionCalculationMode::SimpleOcclusionTest)
		{
			RayDirs.Add(FVector3d::UnitX()); RayDirs.Add(-FVector3d::UnitX());
			RayDirs.Add(FVector3d::UnitY()); RayDirs.Add(-FVector3d::UnitY());
			RayDirs.Add(FVector3d::UnitZ()); RayDirs.Add(-FVector3d::UnitZ());

			for (int AddRayIdx = 0; AddRayIdx < AddRandomRays; AddRayIdx++)
			{
				RayDirs.Add(FVector3d(RaysRandomStream.VRand()));
			}
			NR = RayDirs.Num();
		}

		// triangle samples get their own random stream to make behavior slightly more predictable (e.g. moving the ray samples up shouldn't change all the triangle sample locations)
		FRandomStream TrisRandomStream(124233);
		TArray<FVector3d> TriangleBaryCoordSamples;
		for (int AddSampleIdx = 0; AddSampleIdx < AddTriangleSamples; AddSampleIdx++)
		{
			FVector3d BaryCoords(TrisRandomStream.FRand() * .999 + .001, TrisRandomStream.FRand() * .999 + .001, TrisRandomStream.FRand() * .999 + .001);
			BaryCoords /= (BaryCoords.X + BaryCoords.Y + BaryCoords.Z);
			TriangleBaryCoordSamples.Add(BaryCoords);
		}
		if (TriangleSamplingMethod == EOcclusionTriangleSampling::Centroids || TriangleSamplingMethod == EOcclusionTriangleSampling::VerticesAndCentroids)
		{
			TriangleBaryCoordSamples.Add(FVector3d(1 / 3.0, 1 / 3.0, 1 / 3.0));
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
		ParallelFor(Mesh->MaxTriangleID(), [this, &VertexOccluded, &IsOccludedF, &RemoveTMutex, &MeshLocalToOccluderSpace, &TriangleBaryCoordSamples](int32 TID)
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
			if (bInside && TriangleBaryCoordSamples.Num() > 0)
			{
				FVector3d Normal = Mesh->GetTriNormal(TID);
				FVector3d V0, V1, V2;
				Mesh->GetTriVertices(TID, V0, V1, V2);
				for (int32 SampleIdx = 0, NumSamples = TriangleBaryCoordSamples.Num(); bInside && SampleIdx < NumSamples; SampleIdx++)
				{
					FVector3d BaryCoords = TriangleBaryCoordSamples[SampleIdx];
					FVector3d SamplePos = V0 * BaryCoords.X + V1 * BaryCoords.Y + V2 * BaryCoords.Z + Normal * NormalOffset;
					bInside = IsOccludedF(MeshLocalToOccluderSpace.TransformPosition(SamplePos));
				}
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

	/**
	 * Remove the occluded triangles
	 *
	 * @param LocalToWorld Transform to take the local mesh into the space of the occluder geometry
	 * @param Occluder AABB tree of occluding geometry
	 * @return true on success
	 */
	virtual bool Apply(FTransform3d MeshLocalToOccluderSpace, TMeshAABBTree3<OccluderTriangleMeshType>* Occluder)
	{
		TFastWindingTree<OccluderTriangleMeshType> FastWindingTree(Occluder, InsideMode == EOcclusionCalculationMode::FastWindingNumber);
		return Apply(MeshLocalToOccluderSpace, Occluder, &FastWindingTree);
	}

	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot
	 */
	virtual EOperationValidationResult Validate()
	{
		// TODO: validate input
		return EOperationValidationResult::Ok;
	}

	//
	// Input settings
	//

	// how/where to sample triangles when testing for occlusion

	EOcclusionTriangleSampling TriangleSamplingMethod = EOcclusionTriangleSampling::Vertices;

	// we nudge points out by this amount to try to counteract numerical issues
	double NormalOffset = FMathd::ZeroTolerance;

	// use this as winding isovalue for WindingNumber mode
	double WindingIsoValue = 0.5;


	EOcclusionCalculationMode InsideMode = EOcclusionCalculationMode::FastWindingNumber;

	/** Number of additional ray directions to add to raycast-based occlusion checks, beyond the default +/- major axis directions */
	int AddRandomRays = 0;

	/** Number of additional samples to add per triangle */
	int AddTriangleSamples = 0;

	/**
	 * Set this to be able to cancel running operation
	 */
	FProgressCancel* Progress = nullptr;


	//
	// Outputs
	//

	// indices of removed triangles. will be empty if nothing removed
	TArray<int> RemovedT;

	// true if it wanted to remove triangles but the actual remove operation failed
	bool bRemoveFailed = false;


protected:
	/**
	 * if this returns true, abort computation. 
	 */
	virtual bool Cancelled()
	{
		return (Progress == nullptr) ? false : Progress->Cancelled();
	}

};

