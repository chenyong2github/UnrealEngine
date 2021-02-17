// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

#include "MeshAdapter.h"
#include "Operations/RemoveOccludedTriangles.h"

// simple struct to help construct big trees of all the occluders
struct MODELINGOPERATORS_API IndexMeshWithAcceleration
{
	TArray<int> Triangles;
	TArray<FVector3d> Vertices;
	TIndexMeshArrayAdapter<int, double, FVector3d> Mesh;
	TMeshAABBTree3<TIndexMeshArrayAdapter<int, double, FVector3d>> AABB;
	TFastWindingTree<TIndexMeshArrayAdapter<int, double, FVector3d>> FastWinding;

	IndexMeshWithAcceleration() : AABB(&Mesh, false), FastWinding(&AABB, false)
	{
		Mesh.SetSources(&Vertices, &Triangles);
	}

	void AddMesh(const FDynamicMesh3& MeshIn, FTransform3d Transform);

	void BuildAcceleration()
	{
		AABB.Build();
		FastWinding.Build();
	}
};


class MODELINGOPERATORS_API FRemoveOccludedTrianglesOp : public FDynamicMeshOperator
{
public:
	virtual ~FRemoveOccludedTrianglesOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;
	TSharedPtr<IndexMeshWithAcceleration, ESPMode::ThreadSafe> CombinedMeshTrees;

	TArray<FTransform3d> MeshTransforms;

	EOcclusionTriangleSampling TriangleSamplingMethod =
		EOcclusionTriangleSampling::Centroids;

	// we nudge points out by this amount to try to counteract numerical issues
	double NormalOffset = FMathd::ZeroTolerance;

	// use this as winding isovalue for WindingNumber mode
	double WindingIsoValue = 0.5;

	// random rays to add beyond +/- major axes, for raycast sampling
	int AddRandomRays = 0;

	// add triangle samples per triangle (in addition to TriangleSamplingMethod)
	int AddTriangleSamples = 0;

	// if true, *ignore* the combined selected occluder data in CombinedMeshTrees, and only consider self-occlusion of this mesh
	bool bOnlySelfOcclude = false;

	int ShrinkRemoval = 0;

	double MinAreaConnectedComponent = 0;

	int MinTriCountConnectedComponent = 0;

	// if true, we will set triangle group IDs for occluded triangles, rather than deleting the triangles
	bool bSetTriangleGroupInsteadOfRemoving = false;

	// name of the group layer to use if we are setting triangle groups instead of removing occluded triangles
	FName ActiveGroupLayer;

	// if true, the ActiveGroupLayer is the name of the "default" layer, so we'll use the built-in group IDs
	bool bActiveGroupLayerIsDefault = true;

	EOcclusionCalculationMode InsideMode =
		EOcclusionCalculationMode::FastWindingNumber;


	// outputs, used when bSetTriangleGroupInsteadOfRemoving is true
	int CreatedGroupID = -1;
	int CreatedGroupLayerIndex = -1;


	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


