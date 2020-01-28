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
	TSharedPtr<FDynamicMesh3> OriginalMesh;
	TSharedPtr<IndexMeshWithAcceleration> CombinedMeshTrees;

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

	EOcclusionCalculationMode InsideMode =
		EOcclusionCalculationMode::FastWindingNumber;


	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


